#include "mesh_ota_update.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "string.h"
#include "mesh_wifi_connect.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "mesh_log.h"
#include <sys/socket.h>
#include "esp_wifi.h"
#include "mesh_node.h"
#include "mesh_misc.h"

#define HASH_LEN 32

extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

static char ota_update_url[256];

esp_err_t _ota_http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            LOGI("HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            LOGI("HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            LOGI("HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            LOGI("HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            LOGI("HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            LOGI("HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            LOGI("HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

static char *get_ota_update_url(char *node_addr) {
    LOGI("Generating update URL...");

    sprintf(ota_update_url, "https://192.168.68.158:13800/%s/update.bin", node_addr);

    LOGI("Generated OTA update url: %s", ota_update_url);
    return ota_update_url;
}

void ota_update_task(void *pvParameter) {
    char *update_url;
    char *node_address;
    size_t node_address_len;
    nvs_handle_t my_handle;

    LOGI("Starting OTA update");

    esp_err_t err = nvs_open("io.morrissey", NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        LOGE("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
        assert(false);
    }

    nvs_get_str(my_handle, NODE_ADDRESS_KEY, NULL, &node_address_len);
    node_address = malloc(node_address_len);
    err = nvs_get_str(my_handle, NODE_ADDRESS_KEY, node_address, &node_address_len);
    if (err != ESP_OK) {
        LOGE("Error (%s) reading node address!", esp_err_to_name(err));
        assert(false);
    }
    nvs_close(my_handle);

    update_url = get_ota_update_url(node_address);
    esp_http_client_config_t config = {
            .url = update_url,
            .cert_pem = (char *) server_cert_pem_start,
            .timeout_ms = 20000,
            .skip_cert_common_name_check = true
    };

    esp_err_t ret = esp_https_ota(&config);
    if (ret != ESP_OK) {
        LOGE("Firmware upgrade failed. Restarting to try again...");
    } else {
        LOGI("Firmware upgrade succeeded. Setting boot state to verify ota update.");

        err = nvs_open("io.morrissey", NVS_READWRITE, &my_handle);
        if (err != ESP_OK) {
            LOGE("Error (%s) opening NVS handle for writing new boot state!\n", esp_err_to_name(err));
            assert(false);
        }

        err = nvs_set_u8(my_handle, BOOT_STATE_KEY, BOOT_VERIFY_OTA_UPDATE);
        if (err != ESP_OK) {
            LOGE("Error (%s) writing new boot state to verify OTA update!", esp_err_to_name(err));
            assert(false);
        }
        nvs_close(my_handle);
        LOGI("Restarting to load new firmware.");
    }
    esp_restart();
}

static void print_sha256(const uint8_t *image_hash, const char *label) {
    char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i) {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    LOGI("%s %s", label, hash_print);
}

static void get_sha256_of_partitions(void) {
    uint8_t sha_256[HASH_LEN] = {0};
    esp_partition_t partition;

    // get sha256 digest for bootloader
    partition.address = ESP_BOOTLOADER_OFFSET;
    partition.size = ESP_PARTITION_TABLE_OFFSET;
    partition.type = ESP_PARTITION_TYPE_APP;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for bootloader: ");

    // get sha256 digest for running partition
    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    print_sha256(sha_256, "SHA-256 for current firmware: ");
}

void install_ota_update() {

    get_sha256_of_partitions();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(mesh_wifi_connect());

    /* Ensure to disable any WiFi power save mode, this allows best throughput
     * and hence timings for overall OTA operation.
     */
    esp_wifi_set_ps(WIFI_PS_NONE);

    xTaskCreate(&ota_update_task, "ota_update_task", 8192, NULL, 5, NULL);
}