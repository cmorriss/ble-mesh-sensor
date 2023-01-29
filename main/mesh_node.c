#include <assert.h>
#include <host/util/util.h>
#include "nvs_flash.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "mesh_data_packet.h"
#include "mesh_peer.h"
#include "mesh_node.h"
#include "mesh_misc.h"

#define MAX_PACKETS_AWAITING_RESPONSE 2
static mn_handle_packet_cb_fn *packet_handlers[NUM_PACKET_TYPES] = {NULL};
static uint8_t node_ble_addr[6];

static bool provisioning_requested = false;
static bool resend_packets = false;
static bool processed_packets[400];
static uint8_t idempotency_key_counter = 0;

static void *par_mem;
static struct os_mempool par_pool;
static SLIST_HEAD(, par) pars;

/**
 * Contains this nodes node id. We start out as a provisional node until we receive an assigned node id from the hub.
 */
static uint8_t our_node_id = PROVISIONAL_NODE_ID;
uint16_t dp_value_handle;

/**
 * Triggers resending a packet if a response has not been received.
 */
static xTimerHandle packet_resend_timer;

static int mn_receive_data(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt,
                           void *arg);

static void mn_process_packet(struct mesh_data_packet *packet);
static void mn_forward_packet(struct mesh_peer *peer, void *packet);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
        {
                /*** Service: Sensor Mesh */
                .type = BLE_GATT_SVC_TYPE_PRIMARY,
                .uuid = &gatt_svr_svc_data_uuid.u,
                .characteristics = (struct ble_gatt_chr_def[])
                        {{
                                 /*** Characteristic: write data to mesh. */
                                 .uuid = &gatt_chr_w_data_uuid.u,
                                 .access_cb = mn_receive_data,
                                 .flags = BLE_GATT_CHR_F_WRITE
                         },
                         {
                                 /*** Characteristic: notify data to hub. */
                                 .uuid = &gatt_chr_r_data_uuid.u,
                                 .access_cb = mn_receive_data,
                                 .val_handle = &dp_value_handle,
                                 .flags = BLE_GATT_CHR_F_NOTIFY
                         },
                         {
                                 0, /* No more characteristics in this service. */
                         }
                        }
        },
        {
                0, /* No more services. */
        },
};


uint8_t
mesh_node_get_node_id() {
    return our_node_id;
}

void
mesh_node_set_node_id(uint8_t node_id) {
    our_node_id = node_id;
}

uint8_t
mesh_node_next_idempotency_key() {
    return idempotency_key_counter++;
}

static void
mn_stop_resend_packets_timer() {
    if (xTimerStop(packet_resend_timer, 1000 / portTICK_PERIOD_MS ) == pdFAIL) {
        assert(false);
    }
}

static void
mn_start_resend_packets_timer() {
    LOGI("Running start resend packets timer");
    if (!xTimerIsTimerActive(packet_resend_timer)) {
        if (xTimerStart(packet_resend_timer, pdMS_TO_TICKS(PACKET_RESEND_CADENCE_IN_MS)) == pdFAIL) {
            LOGE("Resend packet timer failed to start!!");
            assert(false);
        }
    }
}

static void
mn_set_resend_flag(xTimerHandle ev) {
    resend_packets = true;
}

/**
 * This function checks for packets that are awaiting response and resends.
 */
void
mesh_node_resend_packets_if_needed()
{
    int total_pars = 0;
    struct par *par_to_resend;
    uint8_t next_idempotency_key;

    if (resend_packets) {
        resend_packets = false;
        LOGI("Resending packets...");
        SLIST_FOREACH(par_to_resend, &pars, next) {
            total_pars++;

            next_idempotency_key = mesh_node_next_idempotency_key();
            memcpy(&par_to_resend->packet->idempotency_key, &next_idempotency_key, SOB);
            mesh_peer_exec_for_each(mn_forward_packet, par_to_resend->packet);
        }

        if (total_pars == 0) {
            mn_stop_resend_packets_timer();
        }
    }
}

void
mesh_node_disconnect(struct mesh_peer *peer, void *data) {
    LOGI("Terminating connection to peer with connection handle %d", peer->conn_handle);
    ble_gap_terminate(peer->conn_handle, BLE_ERR_RD_CONN_TERM_PWROFF);
    mesh_peer_delete(peer->conn_handle);
}

static int
mn_write_data_to_buf(struct os_mbuf *om, void *dst)
{
    uint16_t om_len;
    int rc;

    om_len = OS_MBUF_PKTLEN(om);

    LOGD("Writing data to os mbuf, om_len = %d", om_len);
    if (om_len < DATA_PACKET_MIN_SIZE || om_len > DATA_PACKET_MAX_SIZE) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    rc = ble_hs_mbuf_to_flat(om, dst, DATA_PACKET_MAX_SIZE, NULL);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0;
}

void
mn_print_packets_awaiting_response() {
    struct par *tmp_par;

    SLIST_FOREACH(tmp_par, &pars, next) {
        mdp_print_packet(tmp_par->packet);
    }
}

int
mn_add_packet_awaiting_response(struct mesh_data_packet *packet) {
    struct par *tmp_par;

    LOGD("Adding packet to those awaiting response, packet type is %d", packet->type);
    tmp_par = os_memblock_get(&par_pool);
    if (tmp_par == NULL) {
        /* out of memory */
        return BLE_HS_ENOMEM;
    }
    LOGI("Packet to be copied:");
    mdp_print_packet(packet);

    memset(tmp_par, 0, sizeof(struct par));

    tmp_par->packet = mdp_copy_packet(packet);
    SLIST_INSERT_HEAD(&pars, tmp_par, next);

    LOGI("Packet after head insertion:");
    mdp_print_packet(tmp_par->packet);

    mn_start_resend_packets_timer();

    return 0;
}

static struct par *
mn_find_par_by_type(uint8_t packet_type) {
    struct par *tmp_par;

    SLIST_FOREACH(tmp_par, &pars, next) {
        if (tmp_par->packet->type == packet_type) {
            return tmp_par;
        }
    }

    return NULL;
}

static int
mn_remove_packet_awaiting_response(uint8_t req_packet_type) {
    struct par *tmp_par;

    tmp_par = mn_find_par_by_type(req_packet_type);
    if (tmp_par == NULL) {
        return BLE_HS_ENOMEM;
    }

    SLIST_REMOVE(&pars, tmp_par, par, next);
    mdp_free(tmp_par->packet);
    return os_memblock_put(&par_pool, tmp_par);
}

void
mesh_node_packet_response_received(struct mesh_data_packet *packet) {
    LOGI("Received response for packet with type %d", packet->type);
    if (packet->type == PT_NODE_CONNECTED_RESP) {
        mn_remove_packet_awaiting_response(PT_NODE_CONNECTED);
    }
}

int
mn_packet_resender_init() {
    int rc;

    par_mem = malloc(
            OS_MEMPOOL_BYTES(MAX_PACKETS_AWAITING_RESPONSE, sizeof(struct par)));
    if (par_mem == NULL) {
        rc = BLE_HS_ENOMEM;
        goto err;
    }

    rc = os_mempool_init(&par_pool, MAX_PACKETS_AWAITING_RESPONSE,
                         sizeof(struct par), par_mem,
                         "par_pool");
    if (rc != 0) {
        rc = BLE_HS_EOS;
        goto err;
    }

    packet_resend_timer = xTimerCreate(
            "packet_resend_timer",
            pdMS_TO_TICKS(PACKET_RESEND_CADENCE_IN_MS),
            pdTRUE,
            (void *)0,
            mn_set_resend_flag
    );

err:
    free(par_mem);
    par_mem = NULL;
    return rc;
}

void
mesh_node_send_empty_packet(uint8_t packet_type, bool await_response) {
    struct mesh_data_packet *packet;

    packet = mdp_alloc(1);
    packet->type = packet_type;
    packet->source = mesh_node_get_node_id();
    packet->dest = HUB_NODE_ID;
    packet->ttl = std_ttl;
    packet->idempotency_key = mesh_node_next_idempotency_key();
    packet->data_length = 1;
    (*packet->data) = 0;

    mesh_node_send_packet(packet, await_response);
}

void
mesh_node_send_packet(struct mesh_data_packet *packet, bool await_response) {
    mdp_print_packet(packet);
    mesh_peer_exec_for_each(mn_forward_packet, (void *) packet);

    if (await_response) {
        mn_add_packet_awaiting_response(packet);
    }
    mdp_free(packet);
    mn_print_packets_awaiting_response();
}

uint8_t *
mesh_node_get_node_addr() {
    uint8_t addr_type;
    int rc;

    ble_hs_id_infer_auto(0, &addr_type);
    ble_hs_id_copy_addr(addr_type, node_ble_addr, NULL);

    return node_ble_addr;
}

void
mesh_node_connection_available() {
    if (!provisioning_requested) {
        provisioning_requested = true;

        struct mesh_data_packet *connected_packet;
        uint8_t *node_addr = mesh_node_get_node_addr();

        LOGI_("Connected to a peer, sending connected data packet, own addr: ");
        mesh_print_addr(node_addr);
        LOGI__("\n");

        connected_packet = mdp_alloc(BT_ADDRESS_SIZE);
        connected_packet->type = PT_NODE_CONNECTED;
        connected_packet->source = our_node_id;
        connected_packet->dest = HUB_NODE_ID;
        connected_packet->idempotency_key = mesh_node_next_idempotency_key();
        connected_packet->ttl = std_ttl;
        connected_packet->data_length = BT_ADDRESS_SIZE;
        memcpy(connected_packet->data, node_addr, BT_ADDRESS_SIZE);

        mesh_node_send_packet(connected_packet, true);
    }
}

static bool
mn_packet_processed(struct mesh_data_packet *packet) {
    return processed_packets[packet->idempotency_key];
}

int
mn_packet_next_step(struct mesh_data_packet *packet) {
    uint8_t *my_address;

    if (packet->dest == our_node_id) {
        if (packet->type == (PT_NODE_CONNECTED_RESP)) {
            // We have a connected response, but it may not be for us. We need to check the address in data
            // matches our address. If it does, we need to process it.
            my_address = mesh_node_get_node_addr();
            if (memcmp(my_address, packet->data, BT_ADDRESS_SIZE) == 0) {
                return PACKET_DECISION_PROCESS;
            } else {
                // Node connected response was not meant for us. Forward it on.
                return PACKET_DECISION_FORWARD;
            }
        } else if (mn_packet_processed(packet)) {
            // If we've already processed the packet, ignore it.
            return PACKET_DECISION_TERMINATE;
        } else {

            return PACKET_DECISION_PROCESS;
        }
    } else if (packet->type == PT_GO_TO_SLEEP) {
        return PACKET_DECISION_PROCESS;
    } else if (packet->ttl == 0) {
        return PACKET_DECISION_TERMINATE;
    } else {
        return PACKET_DECISION_FORWARD;
    }
}

static int
mn_receive_data(uint16_t conn_handle, uint16_t attr_handle,
                struct ble_gatt_access_ctxt *ctxt,
                void *arg) {
    const ble_uuid_t *uuid;
    uint8_t packed_data[DATA_PACKET_MAX_SIZE] = {0};
    struct mesh_data_packet data_packet;
    int rc = 0;

    uuid = ctxt->chr->uuid;
    if (ble_uuid_cmp(uuid, &gatt_chr_w_data_uuid.u) == 0) {
        assert(ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR);
        rc = mn_write_data_to_buf(ctxt->om, (void *) packed_data);
        if (rc != 0) {
            LOGE("Error while writing data from om buffer to packed data buffer; rc=%d", rc);
            return rc;
        }

        mdp_unpack(packed_data, &data_packet);
        mdp_print_packet(&data_packet);

        switch(mn_packet_next_step(&data_packet)) {
            case PACKET_DECISION_FORWARD:
                LOGD("Forwarding packet...");
                // Decrement ttl so that the packet will eventually stop flooding the network.
                data_packet.ttl -= 1;
                mesh_peer_exec_for_each(mn_forward_packet, &data_packet);
                break;
            case PACKET_DECISION_PROCESS:
                LOGD("Processing packet...");
                mn_process_packet(&data_packet);
                break;
            case PACKET_DECISION_TERMINATE:
                LOGD("Terminating packet.");
                // Do nothing as the packet stops here without being processed.
                break;
        }
        mesh_node_resend_packets_if_needed();
        return rc;

    }
    return rc;
}

static int
mn_on_forward_packet(uint16_t conn_handle,
                     const struct ble_gatt_error *error,
                     struct ble_gatt_attr *attr,
                     void *arg) {
    return 0;
}

void
mn_forward_packet(struct mesh_peer *peer, void *packet) {
    struct mesh_data_packet *data_packet;
    uint8_t *packed_data;
    uint8_t packed_data_len;
    const struct mesh_peer_chr *chr;
    struct os_mbuf *om;
    int rc;

    data_packet = (struct mesh_data_packet *)packet;

    packed_data = calloc(DATA_PACKET_MAX_SIZE, SOB);

    mdp_pack(packed_data, &packed_data_len, DATA_PACKET_MAX_SIZE, data_packet);
    LOGD("Packed data length when forwarding is %d, conn handle is %d", packed_data_len, peer->conn_handle);

    chr = mesh_peer_chr_find_uuid(peer,
                                  BLE_UUID16_DECLARE(GATT_SVR_SVC_DATA_UUID),
                                  BLE_UUID16_DECLARE(GATT_CHR_W_DATA_UUID));
    // All nodes have the data write characteristic. Only the hub does not, so we send the data through notification.
    if (chr == NULL) {
        om = ble_hs_mbuf_from_flat(packed_data, packed_data_len);
        rc = ble_gattc_notify_custom(peer->conn_handle, dp_value_handle, om);
        if (rc != 0) { LOGE("Error sending notification to hub, rc=%d", rc); }
    } else {
        rc = ble_gattc_write_flat(peer->conn_handle, chr->chr.val_handle,
                                  packed_data, packed_data_len, mn_on_forward_packet, NULL);
        if (rc != 0) {
            LOGE("Error: Failed to write characteristic; rc=%d\n", rc);
        }
    }
    free(packed_data);
}

void
mn_process_packet(struct mesh_data_packet *packet) {
    mn_handle_packet_cb_fn *cb;

    LOGI("Received packet for processing\n");

    processed_packets[packet->idempotency_key] = true;

    cb = packet_handlers[packet->type];

    if (cb) {
        cb(packet);
    } else if (packet->type) {
        LOGW("Received packet for processing with no registered handler; pt=%d", packet->type);
    }
}

void write_update_url_and_reset(const char *url) {
    nvs_handle_t my_handle;
    LOGI("Opening NVS for writing url %s\n", url);

    esp_err_t err = nvs_open("io.morrissey", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        LOGI("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        LOGI("Updating update url in NVS ... ");
        err = nvs_set_str(my_handle, "update_url", url);
        LOGI("Update url update complete. rc=%d", err);

        LOGI("Committing updates in NVS ... ");
        err = nvs_commit(my_handle);
        LOGI("Committing update in NVS complete. rc=%d", err);
        nvs_close(my_handle);

    }
}

void
mesh_node_gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
        case BLE_GATT_REGISTER_OP_SVC:
            LOGD("registered service %s with handle=%d\n",
                     ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                     ctxt->svc.handle);
            break;

        case BLE_GATT_REGISTER_OP_CHR:
            LOGD("registering characteristic %s with "
                                  "def_handle=%d val_handle=%d\n",
                     ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                     ctxt->chr.def_handle,
                     ctxt->chr.val_handle);
            break;

        case BLE_GATT_REGISTER_OP_DSC:
            LOGD("registering descriptor %s with handle=%d\n",
                     ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                     ctxt->dsc.handle);
            break;

        default:
            assert(0);
    }
}

void
mesh_node_register_packet_handler(uint8_t packet_type, mn_handle_packet_cb_fn *handler) {
    packet_handlers[packet_type] = handler;
}

int
mesh_node_init() {
    int rc;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    rc = mn_packet_resender_init();
    if (rc != 0) {
        return rc;
    }

    return 0;
}
