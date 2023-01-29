#include "mesh_data_packet.h"
#include <esp_log.h>
#include <memory.h>
#include "mesh_sensor_constants.h"
#include "mesh_node.h"
#include "mesh_misc.h"

static int allocated_packets = 0;

void mdp_pack(uint8_t *packed_buf, uint8_t *packed_data_len, uint8_t allocated_packed_data_len,
              struct mesh_data_packet *packet) {
    memcpy(packed_buf + DATA_PACKET_SRC_IDX, &packet->source, DATA_PACKET_SRC_SIZE);
    memcpy(packed_buf + DATA_PACKET_DST_IDX, &packet->dest, DATA_PACKET_DST_SIZE);
    memcpy(packed_buf + DATA_PACKET_TTL_IDX, &packet->ttl, DATA_PACKET_TTL_SIZE);
    memcpy(packed_buf + DATA_PACKET_IDEMPOTENCY_KEY_IDX, &packet->idempotency_key, DATA_PACKET_IDEMPOTENCY_KEY_SIZE);
    memcpy(packed_buf + DATA_PACKET_TYPE_IDX, &packet->type, DATA_PACKET_TYPE_SIZE);
    memcpy(packed_buf + DATA_PACKET_DATA_LEN_IDX, &packet->data_length, DATA_PACKET_DATA_LEN_SIZE);
    memcpy(packed_buf + DATA_PACKET_DATA_IDX, packet->data, packet->data_length);

    *packed_data_len = DATA_PACKET_DATA_IDX + packet->data_length;

    mdp_print_packed_packet(packed_buf, *packed_data_len, allocated_packed_data_len);
}

void mdp_unpack(uint8_t *packed_buf, struct mesh_data_packet *packet) {


    memcpy(&packet->source, packed_buf + DATA_PACKET_SRC_IDX, DATA_PACKET_SRC_SIZE);
    memcpy(&packet->dest, packed_buf + DATA_PACKET_DST_IDX, DATA_PACKET_DST_SIZE);
    memcpy(&packet->ttl, packed_buf + DATA_PACKET_TTL_IDX, DATA_PACKET_TTL_SIZE);
    memcpy(&packet->idempotency_key, packed_buf + DATA_PACKET_IDEMPOTENCY_KEY_IDX, DATA_PACKET_IDEMPOTENCY_KEY_SIZE);
    memcpy(&packet->type, packed_buf + DATA_PACKET_TYPE_IDX, DATA_PACKET_TYPE_SIZE);
    memcpy(&packet->data_length, packed_buf + DATA_PACKET_DATA_LEN_IDX, DATA_PACKET_DATA_LEN_SIZE);
    packet->data = (uint8_t *) malloc((packet->data_length) * SOB);
    memcpy(packet->data, packed_buf + DATA_PACKET_DATA_IDX, packet->data_length);

    mdp_print_packet(packet);
}

void mdp_print_packet(struct mesh_data_packet *packet) {
    LOGI_("\nMesh Data Packet Info: \n  source: 0x%02x\n  dest: 0x%02x\n  ttl: 0x%02x\n  idempotency key: 0x%02x\n  data type: 0x%02x\n  data length: %d\n  data: ",
          packet->source, packet->dest, packet->ttl, packet->idempotency_key, packet->type, packet->data_length);
    mesh_print_bytes(packet->data, packet->data_length);
    LOGI__("\n");
}

struct mesh_data_packet *mdp_copy_packet(struct mesh_data_packet *packet) {
    struct mesh_data_packet *tmp_packet;
    uint8_t *tmp_data;

    tmp_packet = mdp_alloc(packet->data_length);
    tmp_data = tmp_packet->data;
    *tmp_packet = *packet;
    tmp_packet->data = tmp_data;
    memcpy(tmp_packet->data, packet->data, packet->data_length);
    return tmp_packet;
}

void mdp_print_packed_packet(uint8_t *packed_packet, uint8_t size, uint8_t allocated_size) {
    assert(size <= allocated_size);
    LOGI_("Packed mesh data packet: ");
    mesh_print_bytes(packed_packet, size);
    LOGI__("\n");
}

void
mdp_free(struct mesh_data_packet *packet) {
    free(packet->data);
    packet->data = NULL;

    free(packet);
    packet = NULL;

    allocated_packets--;
    LOGI("**** Packet Freed, now %d packets unaccounted for. *****", allocated_packets);
}

struct mesh_data_packet *
mdp_alloc(size_t data_length) {
    struct mesh_data_packet *packet;

    packet = malloc(sizeof(struct mesh_data_packet));
    assert(packet != NULL);
    memset(packet, 0, sizeof(struct mesh_data_packet));

    packet->data = malloc(data_length);
    assert(packet->data != NULL);
    memset(packet->data, 0, data_length);

    allocated_packets++;
    LOGI("**** Packet Allocated, now %d packets unaccounted for. *****", allocated_packets);
    return packet;
}

int
mdp_cmp(struct mesh_data_packet *packet1, struct mesh_data_packet *packet2) {
    if (packet1->source == packet2->source && packet1->idempotency_key == packet2->idempotency_key)
        return 0;
    else
        return 1;
}

