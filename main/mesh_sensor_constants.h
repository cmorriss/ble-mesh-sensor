#include "host/ble_uuid.h"
#include "mesh_data_packet.h"
#include "mesh_log.h"

#ifndef MESH_SENSOR_CONSTANTS_H
#define MESH_SENSOR_CONSTANTS_H

/** GATT server. */
#define GATT_SVR_SVC_DATA_UUID  0x5000
#define GATT_CHR_W_DATA_UUID    0x5001
#define GATT_CHR_R_DATA_UUID    0x5002

static const ble_uuid16_t gatt_svr_svc_data_uuid = BLE_UUID16_INIT(GATT_SVR_SVC_DATA_UUID);
static const ble_uuid16_t gatt_chr_w_data_uuid = BLE_UUID16_INIT(GATT_CHR_W_DATA_UUID);
static const ble_uuid16_t gatt_chr_r_data_uuid = BLE_UUID16_INIT(GATT_CHR_R_DATA_UUID);
static const uint32_t firmware_version = 2;

/* Boot state */
#define BOOT_READ_AND_REPORT 1
#define BOOT_INSTALL_OTA_UPDATE 2
#define BOOT_VERIFY_OTA_UPDATE 3

/* NVS state keys */
#define BOOT_STATE_KEY "boot_state"
#define BATTERY_HV_STORE_KEY "battery_hv"
#define BATTERY_LV_STORE_KEY "battery_lv"
#define SENSOR_HV_STORE_KEY "sensor_hv"
#define SENSOR_LV_STORE_KEY "sensor_lv"
#define SLEEP_DURATION_STORE_KEY "sleep_duration"

#define PACKET_DECISION_FORWARD 1
#define PACKET_DECISION_PROCESS 2
#define PACKET_DECISION_TERMINATE 3

static const uint8_t std_ttl = 5;

// This increases the stack size used by the timer which was previously running out of space.
#define CONFIG_APPTRACE_ENABLE 0

#endif //MESH_SENSOR_CONSTANTS_H
