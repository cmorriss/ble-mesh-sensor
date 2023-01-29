/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <host/ble_hs_adv.h>
#include <host/ble_gap.h>
#include <string.h>
#include <esp_log.h>
#include "host/ble_uuid.h"
#include "mesh_sensor.h"

static char addr_buf[6 * 3];

char *mesh_event_type_str(uint8_t type) {
    switch (type) {
        case 0:
            return "CONNECT";
        case 1:
            return "DISCONNECT";
        case 3:
            return "CONN_UPDATE";
        case 4:
            return "CONN_UPDATE_REQ";
        case 5:
            return "L2CAP_UPDATE_REQ";
        case 6:
            return "TERM_FAILURE";
        case 7:
            return "DISC";
        case 8:
            return "DISC_COMPLETE";
        case 9:
            return "ADV_COMPLETE";
        case 10:
            return "ENC_CHANGE";
        case 11:
            return "PASSKEY_ACTION";
        case 12:
            return "NOTIFY_RX";
        case 13:
            return "NOTIFY_TX";
        case 14:
            return "SUBSCRIBE";
        case 15:
            return "MTU";
        case 16:
            return "IDENTITY_RESOLVED";
        case 17:
            return "REPEAT_PAIRING";
        case 18:
            return "PHY_UPDATE_COMPLETE";
        case 19:
            return "EXT_DISC";
        case 20:
            return "PERIODIC_SYNC";
        case 21:
            return "PERIODIC_REPORT";
        case 22:
            return "PERIODIC_SYNC_LOST";
        case 23:
            return "SCAN_REQ_RCVD";
        default:
            return "UNKNOWN";
    }
}

char* mesh_replace_char(char* str, char find, char replace) {
    char *current_pos = strchr(str,find);
    while (current_pos) {
        *current_pos = replace;
        current_pos = strchr(current_pos + 1, find);
    }
    return str;
}

/**
 * Utility function to log an array of bytes.
 */
void mesh_print_bytes(const uint8_t *bytes, int len) {
    int i;

    for (i = 0; i < len; i++) {
        LOGI__("%s%02x", i != 0 ? ":" : "", bytes[i]);
    }
}

char *mesh_addr_str(const void *addr) {
    const uint8_t *u8p;

    u8p = addr;
    sprintf(addr_buf, "%02x:%02x:%02x:%02x:%02x:%02x",
            u8p[5], u8p[4], u8p[3], u8p[2], u8p[1], u8p[0]);

    return addr_buf;
}

void mesh_print_addr(const uint8_t *val) {
    LOGI__("%s", mesh_addr_str(val));
}

void mesh_print_ble_addr(const ble_addr_t *addr) {
    mesh_print_addr(addr->val);
}

void mesh_print_uuid(const ble_uuid_t *uuid) {
    char buf[BLE_UUID_STR_LEN];

    LOGI__("%s", ble_uuid_to_str(uuid, buf));
}

void mesh_print_adv_fields(const struct ble_hs_adv_fields *fields) {
    char s[BLE_HS_ADV_MAX_SZ];
    const uint8_t *u8p;
    int i;

    if (fields->flags != 0) {
        LOGI__("    flags=0x%02x\n", fields->flags);
    }

    if (fields->uuids16 != NULL) {
        LOGI__("    uuids16(%scomplete)=",
                 fields->uuids16_is_complete ? "" : "in");
        for (i = 0; i < fields->num_uuids16; i++) {
            mesh_print_uuid(&fields->uuids16[i].u);
            LOGI__(" ");
        }
        LOGI__("\n");
    }

    if (fields->uuids32 != NULL) {
        LOGI__("    uuids32(%scomplete)=",
                 fields->uuids32_is_complete ? "" : "in");
        for (i = 0; i < fields->num_uuids32; i++) {
            mesh_print_uuid(&fields->uuids32[i].u);
            LOGI(" ");
        }
        LOGI__("\n");
    }

    if (fields->uuids128 != NULL) {
        LOGI__("    uuids128(%scomplete)=",
                 fields->uuids128_is_complete ? "" : "in");
        for (i = 0; i < fields->num_uuids128; i++) {
            mesh_print_uuid(&fields->uuids128[i].u);
            LOGI(" ");
        }
        LOGI__("\n");
    }

    if (fields->name != NULL) {
        assert(fields->name_len < sizeof s - 1);
        memcpy(s, fields->name, fields->name_len);
        s[fields->name_len] = '\0';
        LOGI__("    name(%scomplete)=%s\n",
                 fields->name_is_complete ? "" : "in", s);
    }

    if (fields->tx_pwr_lvl_is_present) {
        LOGI__("    tx_pwr_lvl=%d\n", fields->tx_pwr_lvl);
    }

    if (fields->slave_itvl_range != NULL) {
        LOGI__("    slave_itvl_range=");
        mesh_print_bytes(fields->slave_itvl_range, BLE_HS_ADV_SLAVE_ITVL_RANGE_LEN);
        LOGI__("\n");
    }

    if (fields->svc_data_uuid16 != NULL) {
        LOGI__("    svc_data_uuid16=");
        mesh_print_bytes(fields->svc_data_uuid16, fields->svc_data_uuid16_len);
        LOGI__("\n");
    }

    if (fields->public_tgt_addr != NULL) {
        LOGI__("    public_tgt_addr=");
        u8p = fields->public_tgt_addr;
        for (i = 0; i < fields->num_public_tgt_addrs; i++) {
            LOGI("public_tgt_addr=%s ", mesh_addr_str(u8p));
            u8p += BLE_HS_ADV_PUBLIC_TGT_ADDR_ENTRY_LEN;
        }
        LOGI__("\n");
    }

    if (fields->appearance_is_present) {
        LOGI__("    appearance=0x%04x\n", fields->appearance);
    }

    if (fields->adv_itvl_is_present) {
        LOGI__("    adv_itvl=0x%04x\n", fields->adv_itvl);
    }

    if (fields->svc_data_uuid32 != NULL) {
        LOGI__("    svc_data_uuid32=");
        mesh_print_bytes(fields->svc_data_uuid32, fields->svc_data_uuid32_len);
        LOGI__("\n");
    }

    if (fields->svc_data_uuid128 != NULL) {
        LOGI__("    svc_data_uuid128=");
        mesh_print_bytes(fields->svc_data_uuid128, fields->svc_data_uuid128_len);
        LOGI__("\n");
    }

    if (fields->uri != NULL) {
        LOGI__("    uri=");
        mesh_print_bytes(fields->uri, fields->uri_len);
        LOGI__("\n");
    }

    if (fields->mfg_data != NULL) {
        LOGI__("    mfg_data=");
        mesh_print_bytes(fields->mfg_data, fields->mfg_data_len);
        LOGI__("\n");
    }
}

/**
 * Logs information about a connection to the console.
 */
void
mesh_print_conn_desc(const struct ble_gap_conn_desc *desc)
{
    LOGI("handle=%d our_ota_addr_type=%d our_ota_addr=%s ",
         desc->conn_handle, desc->our_ota_addr.type,
         mesh_addr_str(desc->our_ota_addr.val));
    LOGI("our_id_addr_type=%d our_id_addr=%s ",
         desc->our_id_addr.type, mesh_addr_str(desc->our_id_addr.val));
    LOGI("peer_ota_addr_type=%d peer_ota_addr=%s ",
         desc->peer_ota_addr.type, mesh_addr_str(desc->peer_ota_addr.val));
    LOGI("peer_id_addr_type=%d peer_id_addr=%s ",
         desc->peer_id_addr.type, mesh_addr_str(desc->peer_id_addr.val));
    LOGI("role=%d", desc->role);
    LOGI("conn_itvl=%d conn_latency=%d supervision_timeout=%d "
                       "encrypted=%d authenticated=%d bonded=%d",
                desc->conn_itvl, desc->conn_latency,
                desc->supervision_timeout,
                desc->sec_state.encrypted,
                desc->sec_state.authenticated,
                desc->sec_state.bonded);
}

void
mesh_print_mbuf(const struct os_mbuf *om)
{
    int colon, i;

    colon = 0;
    while (om != NULL) {
        if (colon) {
            LOGI__(":");
        } else {
            colon = 1;
        }
        for (i = 0; i < om->om_len; i++) {
            LOGI__("%s0x%02x", i != 0 ? ":" : "", om->om_data[i]);
        }
        om = SLIST_NEXT(om, om_next);
    }
    if (colon) LOGI__("\n");
}
