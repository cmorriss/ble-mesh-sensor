#ifndef MESH_DATA_PACKET_H
#define MESH_DATA_PACKET_H

#include <stdint-gcc.h>
#include <stddef.h>

/* Size of byte */
#define SOB sizeof(uint8_t)

#define DATA_PACKET_SRC_SIZE SOB
#define DATA_PACKET_DST_SIZE SOB
#define DATA_PACKET_TTL_SIZE SOB
#define DATA_PACKET_IDEMPOTENCY_KEY_SIZE SOB
#define DATA_PACKET_TYPE_SIZE SOB
#define DATA_PACKET_DATA_LEN_SIZE SOB
#define DATA_PACKET_MAX_DATA_SIZE (14 * SOB)

#define DATA_PACKET_SRC_IDX         0
#define DATA_PACKET_DST_IDX         (DATA_PACKET_SRC_IDX + DATA_PACKET_SRC_SIZE)
#define DATA_PACKET_TTL_IDX         (DATA_PACKET_DST_IDX + DATA_PACKET_DST_SIZE)
#define DATA_PACKET_IDEMPOTENCY_KEY_IDX (DATA_PACKET_TTL_IDX + DATA_PACKET_TTL_SIZE)
#define DATA_PACKET_TYPE_IDX        (DATA_PACKET_IDEMPOTENCY_KEY_IDX + DATA_PACKET_IDEMPOTENCY_KEY_SIZE)
#define DATA_PACKET_DATA_LEN_IDX    (DATA_PACKET_TYPE_IDX + DATA_PACKET_TYPE_SIZE)
#define DATA_PACKET_DATA_IDX        (DATA_PACKET_DATA_LEN_IDX + DATA_PACKET_DATA_LEN_SIZE)

#define DATA_PACKET_MIN_SIZE (DATA_PACKET_DATA_IDX + SOB)
#define DATA_PACKET_MAX_SIZE (DATA_PACKET_MIN_SIZE + DATA_PACKET_MAX_DATA_SIZE)

/* Packet types */

/* Base packet types */
#define PT_NODE_CONNECTED 1
#define PT_NODE_CONNECTED_RESP 2
#define PT_OTA_UPDATE_AVAILABLE 3
#define PT_OTA_UPDATE_AVAILABLE_RESP 4
#define PT_GO_TO_SLEEP 5

/* Data request types */
#define PT_REQ_BATTERY_PCT 10
#define PT_RESP_BATTERY_PCT 11
#define PT_REQ_BATTERY_VOLTAGE 12
#define PT_RESP_BATTERY_VOLTAGE 13
#define PT_REQ_MOISTURE_PCT 14
#define PT_RESP_MOISTURE_PCT 15
#define PT_REQ_MOISTURE_VOLTAGE 16
#define PT_RESP_MOISTURE_VOLTAGE 17

/* Configuration update types */
#define PT_UPDATE_SENSOR_HV 30
#define PT_ACK_SENSOR_HV 31
#define PT_UPDATE_SENSOR_LV 32
#define PT_ACK_SENSOR_LV 33
#define PT_UPDATE_BATTERY_HV 34
#define PT_ACK_BATTERY_HV 35
#define PT_UPDATE_BATTERY_LV 36
#define PT_ACK_BATTERY_LV 37
#define PT_UPDATE_SLEEP_DURATION 38
#define PT_ACK_SLEEP_DURATION 39
#define NUM_PACKET_TYPES 40

struct mesh_data_packet {
    uint8_t source;
    uint8_t dest;
    uint8_t ttl;
    uint8_t idempotency_key;
    uint8_t type;
    uint8_t data_length;
    uint8_t *data;
};


/* Data packet distribution */
void mdp_pack(uint8_t *packed_buf, uint8_t *packed_data_len, uint8_t allocated_packed_data_len, struct mesh_data_packet *packet);
void mdp_unpack(uint8_t *packed_buf, struct mesh_data_packet *packet);
void mdp_print_packet(struct mesh_data_packet *packet);
void mdp_print_packed_packet(uint8_t *packed_packet, uint8_t packed_packet_len, uint8_t allocated_packed_packet_len);
void mdp_free(struct mesh_data_packet *packet);
struct mesh_data_packet *mdp_alloc(size_t data_length);
struct mesh_data_packet *mdp_copy_packet(struct mesh_data_packet *packet);
int mdp_cmp(struct mesh_data_packet *packet1, struct mesh_data_packet *packet2);

#endif //MESH_DATA_PACKET_H
