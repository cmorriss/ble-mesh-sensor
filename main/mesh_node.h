#include "mesh_sensor_constants.h"
#include "mesh_peer.h"
#include "host/ble_hs.h"

#ifndef MESH_NODE_H
#define MESH_NODE_H

#define MAX_PACKETS 50
#define PACKET_RESEND_CADENCE_IN_MS 10000

/* Reserved node ids */
#define HUB_NODE_ID 0
#define PROVISIONAL_NODE_ID 1

#define BT_ADDRESS_SIZE 6

struct par {
    SLIST_ENTRY(par) next;

    /** List of discovered GATT services. */
    struct mesh_data_packet *packet;
};

extern uint16_t dp_value_handle;

typedef void mn_handle_packet_cb_fn(struct mesh_data_packet *packet);

int
mesh_node_init();

void
mesh_node_connection_available();

void
mesh_node_disconnect(struct mesh_peer *peer, void *data);

void
mesh_node_send_packet(struct mesh_data_packet *packet, bool await_response);

void
mesh_node_resend_packets_if_needed();

uint8_t
mesh_node_next_idempotency_key();

void
mesh_node_send_empty_packet(uint8_t packet_type, bool await_response);

void
mesh_node_packet_response_received(struct mesh_data_packet *packet);

uint8_t
mesh_node_get_node_id();

uint8_t *
mesh_node_get_node_addr();

void
mesh_node_set_node_id(uint8_t node_id);

void
mesh_node_gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);

void
mesh_node_register_packet_handler(uint8_t packet_type, mn_handle_packet_cb_fn *handler);

#endif //MESH_NODE_H
