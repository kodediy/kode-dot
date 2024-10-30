#include "wifi.h"
#include <string.h>       // Incluir para usar strlen
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_netif.h"

#define AP_SSID "kodeOSweb"
#define AP_PASS "unicornio"
#define AP_MAX_CONN 4

static const char *TAG = "wifi_ap";

void wifi_init() {
    // Crear la interfaz Wi-Fi en modo AP
    esp_netif_create_default_wifi_ap();

    // Configuración inicial de Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),  // Uso de strlen después de incluir <string.h>
            .password = AP_PASS,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };
    if (strlen(AP_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN; // Permite AP sin contraseña
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Punto de acceso Wi-Fi inicializado. SSID:%s, IP:192.168.4.1", AP_SSID);
}
