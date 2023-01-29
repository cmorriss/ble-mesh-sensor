
#ifndef MESH_PEER_H
#define MESH_PEER_H

#include "mesh_data_packet.h"
#include "host/ble_gatt.h"
#include "nimble/ble.h"

/** Peer. */
struct mesh_peer_dsc {
    SLIST_ENTRY(mesh_peer_dsc) next;
    struct ble_gatt_dsc dsc;
};
SLIST_HEAD(peer_dsc_list, mesh_peer_dsc);

struct mesh_peer_chr {
    SLIST_ENTRY(mesh_peer_chr) next;
    struct ble_gatt_chr chr;

    struct peer_dsc_list dscs;
};
SLIST_HEAD(mesh_peer_chr_list, mesh_peer_chr);

struct mesh_peer_svc {
    SLIST_ENTRY(mesh_peer_svc) next;
    struct ble_gatt_svc svc;

    struct mesh_peer_chr_list chrs;
};
SLIST_HEAD(mesh_peer_svc_list, mesh_peer_svc);

struct mesh_peer_subs {
    SLIST_ENTRY(mesh_peer_subs) next;

    uint16_t *attr_handle;

    bool cur_notify;
};
SLIST_HEAD(peer_subs_list, mesh_peer_subs);

struct mesh_peer;
typedef void mesh_peer_disc_fn(const struct mesh_peer *peer, int status, void *arg);
typedef void mesh_peer_exec_fn(struct mesh_peer *peer, void *data);

struct mesh_peer {
    SLIST_ENTRY(mesh_peer) next;

    ble_addr_t *addr;

    uint16_t conn_handle;

    /** List of discovered GATT services. */
    struct mesh_peer_svc_list svcs;

    /** Keeps track of where we are in the service discovery process. */
    uint16_t disc_prev_chr_val;
    struct mesh_peer_svc *cur_svc;

    /** Callback that gets executed when service discovery completes. */
    mesh_peer_disc_fn *disc_cb;
    void *disc_cb_arg;
};

int
mesh_peer_disc_all(uint16_t conn_handle, mesh_peer_disc_fn *disc_cb,
                   void *disc_cb_arg);

const struct mesh_peer_dsc *
mesh_peer_dsc_find_uuid(const struct mesh_peer *peer, const ble_uuid_t *svc_uuid,
                        const ble_uuid_t *chr_uuid, const ble_uuid_t *dsc_uuid);

const struct mesh_peer_chr *
mesh_peer_chr_find_uuid(const struct mesh_peer *peer, const ble_uuid_t *svc_uuid,
                        const ble_uuid_t *chr_uuid);

const struct mesh_peer_svc *
mesh_peer_svc_find_uuid(const struct mesh_peer *peer, const ble_uuid_t *uuid);

int
mesh_peer_delete(uint16_t conn_handle);

int
mesh_peer_add(uint16_t conn_handle, const ble_addr_t *peer_addr);

int
mesh_peer_init(int max_peers, int max_svcs, int max_chrs, int max_dscs);

struct mesh_peer *
mesh_peer_find(uint16_t conn_handle);

struct mesh_peer *
mesh_peer_find_by_addr(const ble_addr_t *addr);

void
mesh_peer_exec_for_each(mesh_peer_exec_fn *exec_fn, void *data);

#endif //MESH_PEER_H
