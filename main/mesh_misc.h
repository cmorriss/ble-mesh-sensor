#include <stdbool.h>
#include <host/ble_uuid.h>
#include <nimble/hci_common.h>
#include <host/ble_gatt.h>
#include <host/ble_hs_adv.h>
#include <host/ble_gap.h>

#ifndef MESH_MISC_H
#define MESH_MISC_H

struct ble_hs_adv_fields;
struct ble_gap_conn_desc;
struct ble_hs_cfg;
struct ble_gatt_register_ctxt;

char* mesh_replace_char(char* str, char find, char replace);
void mesh_print_bytes(const uint8_t *bytes, int len);
void mesh_print_ble_addr(const ble_addr_t *addr);
void mesh_print_addr(const uint8_t *val);
char* mesh_addr_str(const void *addr);
void mesh_print_uuid(const ble_uuid_t *uuid);
void mesh_print_conn_desc(const struct ble_gap_conn_desc *desc);
void mesh_print_adv_fields(const struct ble_hs_adv_fields *fields);
char* mesh_event_type_str(uint8_t type);
void mesh_print_mbuf(const struct os_mbuf *om);

#endif //MESH_MISC_H
