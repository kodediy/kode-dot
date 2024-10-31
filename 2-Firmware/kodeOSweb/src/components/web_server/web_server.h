#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"

// Inicializa la tarjeta SD y monta el sistema de archivos
esp_err_t init_sd_card();

// Genera el HTML de un botón para un archivo específico
void generate_button_html(char *html, size_t max_len, const char *file_name_no_ext, const char *file_path);

// Genera botones HTML para todos los archivos .bin en la tarjeta SD
void generate_bin_buttons_html(char *html, size_t max_len);

// Inicia el servidor web y configura el manejador de solicitudes HTTP
void start_web_server();

#endif // WEB_SERVER_H
