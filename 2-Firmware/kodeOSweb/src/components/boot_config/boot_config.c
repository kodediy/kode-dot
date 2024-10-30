#include "boot_config.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"

#define NVS_NAMESPACE "boot_config"
#define NVS_KEY "temporary_boot"
static const char *TAG = "BOOT_CONFIG";

void set_temporary_boot_flag(bool value) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs_handle, NVS_KEY, value ? 1 : 0);
        nvs_commit(nvs_handle);  // Asegurarse de que los cambios se guarden
        nvs_close(nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set NVS flag: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    }
}

bool get_temporary_boot_flag(void) {
    nvs_handle_t nvs_handle;
    uint8_t value = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        err = nvs_get_u8(nvs_handle, NVS_KEY, &value);
        nvs_close(nvs_handle);
        if (err == ESP_OK) {
            return value == 1;
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to get NVS flag: %s", esp_err_to_name(err));
        }
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    }
    return false;
}

void clear_temporary_boot_flag(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_erase_key(nvs_handle, NVS_KEY);
        nvs_commit(nvs_handle);  // Asegurarse de que los cambios se guarden
        nvs_close(nvs_handle);
    } else {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    }
}
