#include <inttypes.h>
#include <esp_bt.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include <esp_sleep.h>
#include <esp_ota_ops.h>
#include "mesh_ota_update.h"
#include <esp_event_legacy.h>
#include <esp_wifi.h>
#include "freertos/event_groups.h"

/* AWS IoT */
#include <aws_iot_config.h>
#include "aws_iot_log.h"
#include "aws_iot_version.h"
#include "aws_iot_mqtt_client_interface.h"

/* BLE */
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "mesh_sensor.h"
#include "mesh_node.h"

/*
 * We add in an offset that's different for each sensor to ensure they can't accidentally overlap and never see each other.
 */
#define MAX_ADV_DURATION_IN_MS 30000
#define MAX_DSC_DURATION_IN_MS 30000
#define MAX_CONNECTION_DISCOVERY_DURATION_IN_MS 15000
#define MAX_TIME_AWAKE_IN_MS 60000

#define DEFAULT_SLEEP_TIME_SECONDS 60
#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define MAX_DOWNSTREAM_PEERS 0


static uint8_t own_addr[6] = {0};
static uint8_t own_addr_type;
static bool connection_discovery_stopped = false;

/**
 * Variables to hold stored state
 */
static uint8_t boot_state = 0;
static uint8_t peer_connections_total = 0;
static uint64_t timeToSleepInSeconds = DEFAULT_SLEEP_TIME_SECONDS;
static xTimerHandle forced_sleep_timer;
static xTimerHandle stop_connection_discovery_timer;
static bool ota_update_available = false;

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

void ble_store_config_init(void);

static void
meshsnsr_connect_if_interesting(const struct ble_gap_disc_desc *disc, const struct ble_hs_adv_fields *fields);

static int meshsnsr_gap_event(struct ble_gap_event *event, void *arg);

static void meshsnsr_on_disc_complete(const struct mesh_peer *peer, int status, void *arg);

static void meshsnsr_dsc(void);

static void meshsnsr_adv(void);

//static void meshsnsr_adv_or_dsc(void);
//
//static void meshsnsr_adv_or_dsc() {
//    if (rand() % 2 == 0) {
//        meshsnsr_dsc();
//    } else {
//        meshsnsr_adv();
//    }
//}


static void go_to_sleep() {
    uint64_t timeToSleep;

    LOGI("Going to sleep for %" PRId64 " seconds...", timeToSleepInSeconds);
    hibernate_sensor();
    timeToSleep = timeToSleepInSeconds * uS_TO_S_FACTOR;
    esp_sleep_enable_timer_wakeup(timeToSleep);
    esp_deep_sleep_start();
}

/**
 * Enables advertising with the following parameters:
 *     o General discoverable mode.
 *     o Undirected connectable mode.
 */
static void meshsnsr_adv() {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;

    const char *name;
    int rc;

    /**
     *  Set the advertisement data included in our advertisements:
     *     o Flags (indicates advertisement type and other general info).
     *     o Advertising tx power.
     *     o Device name.
     *     o 16-bit service UUIDs (alert notifications).
     */
    memset(&fields, 0, sizeof fields);

    /* Advertise two flags:
     *     o Discoverability in forthcoming advertisement (general)
     *     o BLE-only (BR/EDR unsupported).
     */
    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;

    /* Indicate that the TX power level field should be included; have the
     * stack fill this value automatically.  This is done by assigning the
     * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
     */
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *) name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    fields.uuids16 = (ble_uuid16_t[]) {
            BLE_UUID16_INIT(GATT_SVR_SVC_DATA_UUID)
    };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        LOGE("error setting advertisement data; rc=%d\n", rc);
        return;
    }

    /* Begin advertising. */
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    int32_t adv_period = (rand() % (MAX_ADV_DURATION_IN_MS - 1000)) + 1000;

    LOGI("Starting advertising for %d ms...", adv_period);
    rc = ble_gap_adv_start(own_addr_type, NULL, adv_period, &adv_params, meshsnsr_gap_event, NULL);

    if (rc != 0) {
        LOGE("error enabling advertisement; rc=%d\n", rc);
        return;
    }
}

bool should_process_adv(struct ble_gap_event *event) {
    struct ble_hs_adv_fields fields;
    char s[BLE_HS_ADV_MAX_SZ];
    int rc;

    if (event->type == BLE_GAP_EVENT_DISC) {
        rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                     event->disc.length_data);
        if (rc != 0) {
            return false;
        }

        if (fields.name != NULL) {
            assert(fields.name_len < sizeof s - 1);
            memcpy(s, fields.name, fields.name_len);
            s[fields.name_len] = '\0';
            return strncmp(LOG_NAME, s, fields.name_len) == 0;
        }
        return false;
    }
    return true;
}


/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that forms.
 * meshsnsr uses the same callback for all connections.
 *
 * @param event                 The type of event being signalled.
 * @param ctxt                  Various information pertaining to the event.
 * @param arg                   Application-specified argument; unused by
 *                                  meshsnsr.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int meshsnsr_gap_event(struct ble_gap_event *event, void *arg) {
    struct ble_gap_conn_desc desc;
    struct ble_hs_adv_fields fields;
    int rc;

    if (!should_process_adv(event)) {
        /* Ignore advertisements that aren't from another mesh sensor. */
        return 0;
    }

    LOGD("Received event %s\n", mesh_event_type_str(event->type));

    switch (event->type) {
        case BLE_GAP_EVENT_NOTIFY_TX:
            LOGI("Notification transmit event, status=%d, indication=%d", event->notify_tx.status,
                 event->notify_tx.indication);
            return 0;

        case BLE_GAP_EVENT_DISC:
            rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                         event->disc.length_data);
            if (rc != 0) {
                return 0;
            }

            /* An advertisement report was received during GAP discovery. */
            mesh_print_adv_fields(&fields);

            /* Try to connect to the advertiser if it looks interesting. */
            meshsnsr_connect_if_interesting(&event->disc, &fields);
            return 0;

        case BLE_GAP_EVENT_CONNECT:
            /* A new connection was established or a connection attempt failed. */
            LOGI("connection %s; status=%d ",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);
            if (event->connect.status == 0) {
                rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
                assert(rc == 0);
                mesh_print_conn_desc(&desc);

                /* Remember peer. */
                rc = mesh_peer_add(event->connect.conn_handle, &desc.peer_id_addr);
                if (rc != 0) {
                    if (rc == BLE_HS_EALREADY) {
                        LOGW("Peer was already added, not adding again.");
                    } else {
                        LOGE("Failed to add peer; rc=%d\n", rc);
                    }
                    return 0;
                }

                rc = mesh_peer_disc_all(event->connect.conn_handle,
                                        meshsnsr_on_disc_complete, NULL);
                if (rc != 0) {
                    LOGE("Failed to discover services; rc=%d\n", rc);
                    return 0;
                }
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            LOGI("disconnect; reason=%d ", event->disconnect.reason);
            mesh_print_conn_desc(&event->disconnect.conn);

            /* Forget about peer. */
            mesh_peer_delete(event->disconnect.conn.conn_handle);

            return 0;

        case BLE_GAP_EVENT_CONN_UPDATE:
            /* The central has updated the connection parameters. */
            LOGI("connection updated; status=%d ",
                 event->conn_update.status);
            rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
            assert(rc == 0);
            mesh_print_conn_desc(&desc);
            return 0;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            LOGI("advertise complete; reason=%d",
                 event->adv_complete.reason);
            mesh_node_resend_packets_if_needed();
            meshsnsr_dsc();
            return 0;

        case BLE_GAP_EVENT_DISC_COMPLETE:
            LOGI("discovery complete; reason=%d", event->disc_complete.reason);
            mesh_node_resend_packets_if_needed();
            return 0;

        case BLE_GAP_EVENT_REPEAT_PAIRING:
            /* We already have a bond with the peer, but it is attempting to
             * establish a new link.  Just throw away the old bond and accept the new link.
             */

            /* Delete the old bond. */
            rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
            assert(rc == 0);
            ble_store_util_delete_peer(&desc.peer_id_addr);

            /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
             * continue with the pairing operation.
             */
            return BLE_GAP_REPEAT_PAIRING_RETRY;

        case BLE_GAP_EVENT_SUBSCRIBE:
            LOGI("subscribe event from hub; cur_notify=%d\n value handle; "
                 "val_handle=%d\n",
                 event->subscribe.cur_notify, dp_value_handle);
            break;
    }

    return 0;
}

/**
 * Called when service discovery of the specified peer has completed.
 */
static void meshsnsr_on_disc_complete(const struct mesh_peer *peer, int status, void *arg) {
    struct ble_gap_conn_desc desc;
    int rc;

    if (status != 0) {
        /* Service discovery failed.  Terminate the connection. */
        LOGE("Error: Service discovery failed; status=%d "
             "conn_handle=%d\n", status, peer->conn_handle);
        ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    } else {
        /* Service discovery has completed successfully.  Now we have a complete
         * list of services, characteristics, and descriptors that the peer
         * supports.
         */
        LOGI("Service discovery complete; status=%d "
             "conn_handle=%d\n", status, peer->conn_handle);

        rc = ble_gap_conn_find(peer->conn_handle, &desc);
        assert(rc == 0);
        mesh_print_conn_desc(&desc);

        mesh_node_connection_available();
    }

    meshsnsr_dsc();
}/**
 * @brief Default MQTT HOST URL is pulled from the aws_iot_config.h
 */
char HostAddress[255] = AWS_IOT_MQTT_HOST;

/**
 * @brief Default MQTT port is pulled from the aws_iot_config.h
 */
uint32_t port = AWS_IOT_MQTT_PORT;


static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            /* This is a workaround as ESP32 WiFi libs don't currently
               auto-reassociate. */
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void meshsnsr_on_reset(int reason) {
    LOGE("Resetting state; reason=%d\n", reason);
}

/**
 * Indicates whether we should try to connect to the sender of the specified
 * advertisement.  The function returns a positive result if the device
 * advertises connectability and support for the mesh data service.
 */
static int meshsnsr_should_connect(const struct ble_gap_disc_desc *disc) {
    struct ble_hs_adv_fields fields;
    int rc;
    int i;

    /* The device has to be advertising connectability. */
    if (disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
        disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) {
        return 0;
    }

    rc = ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data);
    if (rc != 0) {
        return rc;
    }

    if (mesh_peer_find_by_addr(&disc->addr) != NULL) {
        // We're already connected.
        return 0;
    }

    /* The device has to advertise our mesh data service
     */
    for (i = 0; i < fields.num_uuids16; i++) {
        if (ble_uuid_u16(&fields.uuids16[i].u) == GATT_SVR_SVC_DATA_UUID && mesh_peer_find_by_addr(&disc->addr) == NULL) {
            return 1;
        }
    }

    return 0;
}

/**
 * Connects to the sender of the specified advertisement of it looks
 * interesting.  A device is "interesting" if it advertises connectability and
 * support for the mesh data service.
 */
static void
meshsnsr_connect_if_interesting(const struct ble_gap_disc_desc *disc, const struct ble_hs_adv_fields *fields) {
    int rc;
    char s[BLE_HS_ADV_MAX_SZ];

    /* Don't do anything if we don't care about this advertiser. */
    if (!meshsnsr_should_connect(disc)) {
        return;
    }

    /* Scanning must be stopped before a connection can be initiated. */
    rc = ble_gap_disc_cancel();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        LOGE("Failed to cancel scan; rc=%d\n", rc);
    }

    /* Try to connect the the advertiser.  Allow 30 seconds (30000 ms) for
     * timeout.
     */
    memcpy(s, fields->name, fields->name_len);
    s[fields->name_len] = '\0';
    LOGI("Attempting connection to node %s\n", s);
    rc = ble_gap_connect(own_addr_type, &disc->addr, 30000, NULL,
                         meshsnsr_gap_event, NULL);

    if (rc != 0) {
        LOGE("Error: Failed to connect to device; addr_type=%d "
             "addr=%s; rc=%d\n",
             disc->addr.type, mesh_addr_str(disc->addr.val), rc);
    }
}


/**
 * Initiates the GAP general discovery procedure.
 */
static void meshsnsr_dsc(void) {
    struct ble_gap_disc_params disc_params;
    int rc;

    if (connection_discovery_stopped) {
        return;
    }

    /* Tell the controller to filter duplicates; we don't want to process
     * repeated advertisements from the same device.
     */
    disc_params.filter_duplicates = 1;

    /**
     * We are going to actively scan to let the sensors know we've read their data and they can shut down.
     */
    disc_params.passive = 0;

    /* Use defaults for the rest of the parameters. */
    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    int32_t dsc_period = (rand() % (MAX_DSC_DURATION_IN_MS - 1000)) + 1000;

    LOGI("Starting discovery for %d ms...", dsc_period);
    rc = ble_gap_disc(own_addr_type, dsc_period, &disc_params, meshsnsr_gap_event, NULL);
    if (rc != 0) {
        LOGE("Error initiating GAP discovery procedure; rc=%d\n",
             rc);
    }
}

static void
meshsnsr_proc_data_request(struct mesh_data_packet *request_packet) {
    struct mesh_data_packet *data_packet;
    uint32_t data_value;

    LOGI("Processing data request for packet type 0x%02x.", request_packet->type);

    data_packet = mdp_alloc(sizeof(uint32_t));
    data_packet->source = mesh_node_get_node_id();
    data_packet->dest = HUB_NODE_ID;
    data_packet->idempotency_key = mesh_node_next_idempotency_key();
    data_packet->ttl = std_ttl;
    data_packet->data_length = sizeof(uint32_t);

    switch (request_packet->type) {
        case PT_REQ_MOISTURE_VOLTAGE:
            LOGI("Reading soil moisture voltage and loading in data request_packet.");
            data_value = read_soil_moisture_voltage();
            data_packet->type = PT_RESP_MOISTURE_VOLTAGE;
            break;
        case PT_REQ_MOISTURE_PCT:
            LOGI("Reading soil moisture percent and loading in data request packet");
            data_value = convert_moisture_voltage_to_pct(read_soil_moisture_voltage());
            data_packet->type = PT_RESP_MOISTURE_PCT;
            break;
        case PT_REQ_BATTERY_PCT:
            LOGI("Reading remaining percent and loading in data request_packet.");
            data_value = read_battery_remaining_percent();
            data_packet->type = PT_RESP_BATTERY_PCT;
            break;
        case PT_REQ_BATTERY_VOLTAGE:
            LOGI("Reading battery voltage and loading in data request_packet.");
            data_value = read_battery_voltage();
            data_packet->type = PT_RESP_BATTERY_VOLTAGE;
            break;
        default:
            LOGE("Received a data request for an unknown packet type, 0x%02x. Ignoring.", request_packet->type);
            return;
    }

    memcpy(data_packet->data, &data_value, sizeof(uint32_t));

    mesh_node_send_packet(data_packet, false);
}

void
meshsnsr_proc_config_update(struct mesh_data_packet *packet) {
    uint32_t new_value;
    nvs_handle_t my_handle;
    char *store_key;
    uint8_t ack_pt;

    assert(packet->data_length == sizeof(uint32_t));
    memcpy(&new_value, packet->data, sizeof(uint32_t));

    switch (packet->type) {
        case PT_UPDATE_BATTERY_HV:
            store_key = BATTERY_HV_STORE_KEY;
            ack_pt = PT_ACK_BATTERY_HV;
            break;
        case PT_UPDATE_BATTERY_LV:
            store_key = BATTERY_LV_STORE_KEY;
            ack_pt = PT_ACK_BATTERY_LV;
            break;
        case PT_UPDATE_SENSOR_HV:
            store_key = SENSOR_HV_STORE_KEY;
            ack_pt = PT_ACK_SENSOR_HV;
            break;
        case PT_UPDATE_SENSOR_LV:
            store_key = SENSOR_LV_STORE_KEY;
            ack_pt = PT_ACK_SENSOR_LV;
            break;
        case PT_UPDATE_SLEEP_DURATION:
            store_key = SLEEP_DURATION_STORE_KEY;
            ack_pt = PT_ACK_SLEEP_DURATION;
            timeToSleepInSeconds = new_value;
            break;
        default:
            LOGW("Received unknown packet type in config update: 0x%02x", packet->type);
            return;
    }
    LOGD("Received config update packet. store_key=%s, ack_pt=%d, new_value=%d", store_key, ack_pt, new_value);

    esp_err_t err = nvs_open("io.morrissey", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        LOGE("Error (%s) opening NVS handle for config update!\n", esp_err_to_name(err));
        return;
    }
    err = nvs_set_u32(my_handle, store_key, new_value);
    if (err != 0) {
        LOGI("Error (%s) storing config update with key %s and value %d!\n", esp_err_to_name(err), store_key,
             new_value);
    }

    nvs_close(my_handle);

    mesh_node_send_empty_packet(ack_pt, false);
}

void
meshsnsr_proc_ota_update_available(struct mesh_data_packet *packet) {
    nvs_handle_t my_handle;
    struct mesh_data_packet *resp_packet;
    char *my_node_addr_str;

    assert(packet->data_length == sizeof(uint32_t));

    resp_packet = mdp_alloc(sizeof(uint32_t));
    resp_packet->type = PT_OTA_UPDATE_AVAILABLE_RESP;
    resp_packet->source = mesh_node_get_node_id();
    resp_packet->dest = HUB_NODE_ID;
    resp_packet->ttl = std_ttl;
    resp_packet->idempotency_key = mesh_node_next_idempotency_key();
    resp_packet->data_length = sizeof(uint32_t);

    if (*((uint32_t *) packet->data) > firmware_version) {
        LOGI("Received ota update available message. Storing for use during startup.");
        esp_err_t err = nvs_open("io.morrissey", NVS_READWRITE, &my_handle);
        if (err != ESP_OK) {
            LOGE("Error (%s) opening NVS handle for config update!\n", esp_err_to_name(err));
            return;
        }
        err = nvs_set_u8(my_handle, BOOT_STATE_KEY, BOOT_INSTALL_OTA_UPDATE);
        if (err != 0) {
            LOGE("Error (%s) storing config update indicating that an ota update is available!\n",
                 esp_err_to_name(err));
        }

        my_node_addr_str = mesh_addr_str(mesh_node_get_node_addr());
        mesh_replace_char(my_node_addr_str, ':', '-');
        err = nvs_set_str(my_handle, NODE_ADDRESS_KEY, my_node_addr_str);
        if (err != 0) {
            LOGE("Error (%s) storing node address!",
                 esp_err_to_name(err));
        }

        nvs_close(my_handle);
        ota_update_available = true;
        /* 0 indicates the update will be downloaded and installed */
        (*((uint32_t *) resp_packet->data)) = 0;
    } else {
        LOGI("Received ota update available, but current version is already at the available version.");
        /* When we don't need the update, we just send our current version. Can help with debugging. */
        (*((uint32_t *) resp_packet->data)) = firmware_version;
    }

    mesh_node_send_packet(resp_packet, false);
}

void
meshsnsr_proc_go_to_sleep(struct mesh_data_packet *packet) {
    struct mesh_data_packet *forward_packet;

    forward_packet = mdp_copy_packet(packet);

    // Resend the go to sleep packet before going to sleep.
    mesh_node_send_packet(forward_packet, false);

    forward_packet = mdp_copy_packet(packet);

    // Send it twice just to be sure.
    mesh_node_send_packet(forward_packet, false);

    // Now disconnect from all peers.
    mesh_peer_exec_for_each(mesh_node_disconnect, NULL);

    if (ota_update_available) {
        esp_restart();
    } else {
        go_to_sleep();
    }
}

void
meshsnsr_proc_node_connected_resp(struct mesh_data_packet *packet) {
    mesh_node_set_node_id(*(packet->data + BT_ADDRESS_SIZE));
    LOGI("Received assigned node id: %d", mesh_node_get_node_id());

    mesh_node_packet_response_received(packet);
}

static void meshsnsr_on_sync(void) {
    int rc;

    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        LOGE("error determining address type; rc=%d", rc);
        return;
    }

    /* Store our address for easy access. */
    rc = ble_hs_id_copy_addr(own_addr_type, own_addr, NULL);
    if (rc != 0) {
        LOGE("error determining our address; rc=%d", rc);
        return;
    }

    /* Printing ADDR */
    mesh_print_addr(own_addr);

    /* register the callbacks for specific packet types. */
    mesh_node_register_packet_handler(PT_REQ_BATTERY_VOLTAGE, meshsnsr_proc_data_request);
    mesh_node_register_packet_handler(PT_REQ_BATTERY_PCT, meshsnsr_proc_data_request);
    mesh_node_register_packet_handler(PT_REQ_MOISTURE_VOLTAGE, meshsnsr_proc_data_request);
    mesh_node_register_packet_handler(PT_REQ_MOISTURE_PCT, meshsnsr_proc_data_request);
    mesh_node_register_packet_handler(PT_REQ_MOISTURE_VOLTAGE, meshsnsr_proc_data_request);
    mesh_node_register_packet_handler(PT_NODE_CONNECTED_RESP, meshsnsr_proc_node_connected_resp);
    mesh_node_register_packet_handler(PT_UPDATE_BATTERY_HV, meshsnsr_proc_config_update);
    mesh_node_register_packet_handler(PT_UPDATE_BATTERY_LV, meshsnsr_proc_config_update);
    mesh_node_register_packet_handler(PT_UPDATE_SENSOR_HV, meshsnsr_proc_config_update);
    mesh_node_register_packet_handler(PT_UPDATE_SENSOR_LV, meshsnsr_proc_config_update);
    mesh_node_register_packet_handler(PT_UPDATE_SLEEP_DURATION, meshsnsr_proc_config_update);
    mesh_node_register_packet_handler(PT_OTA_UPDATE_AVAILABLE, meshsnsr_proc_ota_update_available);
    mesh_node_register_packet_handler(PT_GO_TO_SLEEP, meshsnsr_proc_go_to_sleep);

    meshsnsr_adv();
}

static void
forced_sleep(xTimerHandle ev) {
    go_to_sleep();
}

static void
start_sleep_timer() {
    if (!xTimerIsTimerActive(forced_sleep_timer)) {
        if (xTimerStart(forced_sleep_timer, 0) == pdFAIL) {
            LOGE("Unable to start forced sleep timer!!");
            assert(false);
        }
    }
}

static void
start_stop_connection_discovery_timer() {
    if (!xTimerIsTimerActive(stop_connection_discovery_timer)) {
        if (xTimerStart(stop_connection_discovery_timer, 0) == pdFAIL) {
            LOGE("Unable to start connection discovery timer!!");
            assert(false);
        }
    }
}

void meshsnsr_host_task(void *param) {
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    nimble_port_freertos_deinit();
}

static bool
handle_load_err(esp_err_t err) {
    if (err != 0) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            LOGE("Error (%s) reading!\n", esp_err_to_name(err));
        }
        return false;
    } else {
        return true;
    }
}

void
read_stored_state() {
    nvs_handle_t my_handle;
    uint32_t config_value;

    esp_err_t err = nvs_open("io.morrissey", NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        LOGE("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        return;
    }

    // value will default to 0, if not set yet in NVS
    err = nvs_get_u8(my_handle, BOOT_STATE_KEY, &boot_state);
    handle_load_err(err);

    err = nvs_get_u32(my_handle, BATTERY_HV_STORE_KEY, &config_value);
    if (handle_load_err(err)) {
        LOGI("Loaded battery hv value %d", config_value);
        set_battery_high_voltage(config_value);
    }

    err = nvs_get_u32(my_handle, BATTERY_LV_STORE_KEY, &config_value);
    if (handle_load_err(err)) {
        LOGI("Loaded battery lv value %d", config_value);
        set_battery_low_voltage(config_value);
    }

    err = nvs_get_u32(my_handle, SENSOR_HV_STORE_KEY, &config_value);
    if (handle_load_err(err)) {
        LOGI("Loaded sensor hv value %d", config_value);
        set_sensor_high_voltage(config_value);
    }

    err = nvs_get_u32(my_handle, SENSOR_LV_STORE_KEY, &config_value);
    if (handle_load_err(err)) {
        LOGI("Loaded sensor lv value %d", config_value);
        set_sensor_low_voltage(config_value);
    }

    err = nvs_get_u32(my_handle, SLEEP_DURATION_STORE_KEY, &config_value);
    if (handle_load_err(err)) {
        LOGI("Loaded sleep duration value %d", config_value);
        memcpy(&timeToSleepInSeconds, &config_value, sizeof(uint32_t));
    }

    nvs_close(my_handle);
}

static void
verify_ota_update() {
    nvs_handle_t my_handle;

    LOGI("Verifying ota update and setting boot state to read and report.");
    /* Make sure that if this was an OTA update, it is not rolled back since we made it this far. */
    esp_ota_mark_app_valid_cancel_rollback();

    esp_err_t err = nvs_open("io.morrissey", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        LOGE("Error (%s) opening NVS handle for setting boot state to read and report!\n", esp_err_to_name(err));
        return;
    }
    err = nvs_set_u8(my_handle, BOOT_STATE_KEY, BOOT_READ_AND_REPORT);
    if (err != 0) {
        LOGE("Error (%s) storing config update indicating that an ota update is available!\n", esp_err_to_name(err));
    }

    nvs_close(my_handle);
}

static void
stop_connection_discovery() {
    connection_discovery_stopped = true;
}

void init_mesh() {
    int rc;

    ESP_ERROR_CHECK(esp_nimble_hci_and_controller_init());

    nimble_port_init();

    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);

    /* Initialize the NimBLE host configuration. */
    ble_hs_cfg.reset_cb = meshsnsr_on_reset;
    ble_hs_cfg.sync_cb = meshsnsr_on_sync;
    ble_hs_cfg.gatts_register_cb = mesh_node_gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_hs_cfg.sm_io_cap = CONFIG_EXAMPLE_IO_TYPE;
    ble_hs_cfg.sm_sc = 0;

    rc = mesh_node_init();
    assert(rc == 0);

    rc = mesh_peer_init(MYNEWT_VAL(BLE_MAX_CONNECTIONS), 64, 64, 64);
    assert(rc == 0);

    /* Set the default device name. */
    rc = ble_svc_gap_device_name_set(LOG_NAME);
    assert(rc == 0);

    /* XXX Need to have template for store */
    ble_store_config_init();

    if (boot_state == BOOT_VERIFY_OTA_UPDATE) {
        verify_ota_update();
    }

    nimble_port_freertos_init(meshsnsr_host_task);
}

void
app_main(void) {
    LOGI("Starting main, firmware version is %d\n", firmware_version);
    ESP_LOGI("NimBLE", "Initiating tag cache for NimBLE\n");

    /* Initialize NVS — it is used to store PHY calibration data and whether an update is available*/
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    read_stored_state();

    if (boot_state == BOOT_INSTALL_OTA_UPDATE) {
        install_ota_update();
    } else {
        forced_sleep_timer = xTimerCreate(
                "forced_sleep_timer",
                pdMS_TO_TICKS(MAX_TIME_AWAKE_IN_MS),
                pdFALSE,
                (void *) 0,
                forced_sleep
        );

        stop_connection_discovery_timer = xTimerCreate(
                "stop_connection_discovery",
                pdMS_TO_TICKS(MAX_CONNECTION_DISCOVERY_DURATION_IN_MS),
                pdFALSE,
                (void *) 0,
                stop_connection_discovery
        );

        start_sleep_timer();
        start_stop_connection_discovery_timer();

        init_mesh();
    }
}
