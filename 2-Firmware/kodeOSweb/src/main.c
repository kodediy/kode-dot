#include "web_server.h"
#include "wifi.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "boot_config.h"  

#define TAG "APP_MAIN"

void app_main() {
    // Inicializar NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Comprobar si el arranque es temporal
    if (get_temporary_boot_flag()) {
        ESP_LOGI(TAG, "Detección de arranque temporal. Restaurando partición factory.");

        // Restaurar la partición factory
        const esp_partition_t *factory_partition = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);

        if (factory_partition != NULL) {
            esp_err_t err = esp_ota_set_boot_partition(factory_partition);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Partición de arranque restaurada a la factory.");
                clear_temporary_boot_flag();
                ESP_LOGI(TAG, "Reiniciando para aplicar los cambios...");
                esp_restart();  // Reiniciar el dispositivo para aplicar el cambio
            } else {
                ESP_LOGE(TAG, "Error al restaurar la partición de fábrica: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGE(TAG, "No se encontró la partición factory.");
        }
    }

    // Inicializar la pila de red y el event loop predeterminado
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Configurar el ESP32 como punto de acceso Wi-Fi
    wifi_init();

    // Arrancar el servidor web
    start_web_server();
}
