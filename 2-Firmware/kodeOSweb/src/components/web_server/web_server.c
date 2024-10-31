#include "web_server.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "dirent.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "boot_config.h"
#include <errno.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "spi_flash_mmap.h"
#include "esp_vfs_fat.h"
#include <sys/stat.h>


#define SD_CMD 10
#define SD_CLK 12
#define SD_MOSI 11
#define SD_MISO 13

#define MAX_FILES 10

// Ajustar el tamaño del buffer al máximo permitido por DMA (por ejemplo, 64 KB)
#define BUFFER_SIZE (64 * 1024)  // Tamaño del buffer para leer y escribir (64 KB)

// Alinear los buffers al tamaño de palabra para mejorar eficiencia DMA
#define BUFFER_ALIGNMENT 4

// Definir SPI_FLASH_SEC_SIZE si no está definido
#ifndef SPI_FLASH_SEC_SIZE
#define SPI_FLASH_SEC_SIZE 4096
#endif

bool sd_card_mounted = false;

static const char *TAG = "web_server";
volatile int load_progress = 0;  // Variable global para almacenar el progreso de carga
SemaphoreHandle_t load_progress_mutex;  // Mutex para proteger load_progress

typedef struct {
    char file_path[512];
    char partition_label[32];
} copy_task_params_t;

// Prototipos de funciones
void copy_partition_task(void* params);
void copy_task(void* params);
void start_user_app_task(void* params);
esp_err_t run_get_handler(httpd_req_t *req);
esp_err_t save_get_handler(httpd_req_t *req);
void copy_partition_to_sd(const char* partition_label, const char* file_path);
void generate_button_html(char *html, size_t max_len, const char *file_name_no_ext, const char *file_path);
esp_err_t progress_get_handler(httpd_req_t *req);
void generate_bin_buttons_html(char *html, size_t max_len);
esp_err_t index_get_handler(httpd_req_t *req);
esp_err_t load_get_handler(httpd_req_t *req);
esp_err_t init_sd_card();
void start_web_server();

// Inicializar la tarjeta SD
esp_err_t init_sd_card() {
    ESP_LOGI(TAG, "Iniciando inicialización de la tarjeta SD en modo SPI...");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024  // Aumentar tamaño de unidad de asignación
    };

    sdmmc_card_t* card;
    const char mount_point[] = "/sdcard";

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI,
        .miso_io_num = SD_MISO,
        .sclk_io_num = SD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BUFFER_SIZE + 8
    };

    ESP_LOGI(TAG, "Inicializando el bus SPI...");
    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando el bus SPI: %s", esp_err_to_name(ret));
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CMD;
    slot_config.host_id = host.slot;

    ESP_LOGI(TAG, "Montando la tarjeta SD...");
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo montar el sistema de archivos: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Sistema de archivos SD montado correctamente.");
    sdmmc_card_print_info(stdout, card);
    return ESP_OK;
}


// Función para copiar el archivo a la partición con actualización de progreso
void copy_sd_to_partition(const char* file_path, const char* partition_label) {
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_OTA_0,
        partition_label);

    if (partition == NULL) {
        ESP_LOGE(TAG, "No se encontró la partición con la etiqueta: %s", partition_label);
        return;
    }

    // Log para verificar la ruta que se intenta abrir
    ESP_LOGI(TAG, "Intentando abrir el archivo en la ruta: %s", file_path);

    FILE* f = fopen(file_path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Error al abrir el archivo para leer desde la SD: %s", strerror(errno));
        return;
    }

    // Obtener el tamaño del archivo utilizando fseek y ftell
    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size > partition->size) {
        ESP_LOGE(TAG, "El tamaño del archivo (%zu bytes) es mayor que el tamaño de la partición (%" PRIu32 " bytes)", file_size, partition->size);
        fclose(f);
        return;
    }

    // Alinear el buffer al tamaño de palabra y asegurarse de que sea compatible con DMA
    uint8_t* buffer = (uint8_t*)heap_caps_aligned_alloc(BUFFER_ALIGNMENT, BUFFER_SIZE, MALLOC_CAP_DMA);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Error al asignar memoria para el buffer");
        fclose(f);
        return;
    }

    size_t bytes_remaining = file_size;
    size_t bytes_to_write;
    size_t total_bytes_written = 0;
    uint32_t offset = 0;

    xSemaphoreTake(load_progress_mutex, portMAX_DELAY);
    load_progress = 0;
    xSemaphoreGive(load_progress_mutex);

    ESP_LOGI(TAG, "Borrando la partición antes de escribir...");
    // Borrar solo los sectores necesarios
    size_t erase_size = (file_size + SPI_FLASH_SEC_SIZE - 1) & (~(SPI_FLASH_SEC_SIZE - 1));
    esp_err_t ret = esp_partition_erase_range(partition, 0, erase_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al borrar la partición: %s", esp_err_to_name(ret));
        free(buffer);
        fclose(f);
        return;
    }

    ESP_LOGI(TAG, "Iniciando la copia del archivo a la partición...");

    while (bytes_remaining > 0) {
        bytes_to_write = (bytes_remaining > BUFFER_SIZE) ? BUFFER_SIZE : bytes_remaining;
        size_t read = fread(buffer, 1, bytes_to_write, f);
        if (read != bytes_to_write) {
            ESP_LOGE(TAG, "Error al leer del archivo");
            free(buffer);
            fclose(f);
            return;
        }

        ret = esp_partition_write(partition, offset, buffer, bytes_to_write);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error al escribir en la partición: %s", esp_err_to_name(ret));
            free(buffer);
            fclose(f);
            return;
        }

        bytes_remaining -= bytes_to_write;
        offset += bytes_to_write;
        total_bytes_written += bytes_to_write;

        int progress = (int)((total_bytes_written * 100) / file_size);
        xSemaphoreTake(load_progress_mutex, portMAX_DELAY);
        load_progress = progress;
        xSemaphoreGive(load_progress_mutex);

        // Mostrar progreso cada 10%
        if (progress % 10 == 0 && progress != 0) {
            ESP_LOGI(TAG, "Progreso: %d%%", progress);
        }
    }

    free(buffer);
    fclose(f);

    ESP_LOGI(TAG, "Running APP");

    xSemaphoreTake(load_progress_mutex, portMAX_DELAY);
    load_progress = 100;  // Marca el progreso como completo al final
    xSemaphoreGive(load_progress_mutex);
}

// Función para copiar la partición a la SD
void copy_partition_to_sd(const char* partition_label, const char* file_path) {
    ESP_LOGI(TAG, "Iniciando copia de la partición '%s' al archivo '%s'", partition_label, file_path);

    // Buscar la partición con la etiqueta especificada
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_OTA_0,
        partition_label);

    if (partition == NULL) {
        ESP_LOGE(TAG, "No se encontró la partición con la etiqueta: %s", partition_label);
        return;
    } else {
        ESP_LOGI(TAG, "Partición encontrada: tamaño = %" PRIu32 " bytes", partition->size);
    }

    // Intentar abrir el archivo en la SD para escribir (crearlo si no existe)
    ESP_LOGI(TAG, "Intentando abrir el archivo para escribir: %s", file_path);
    FILE* f = fopen(file_path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Error al abrir el archivo para escribir en la SD: %s", strerror(errno));
        return;
    } else {
        ESP_LOGI(TAG, "Archivo abierto correctamente para escritura.");
    }

    // Alinear el buffer al tamaño de palabra y asegurarse de que sea compatible con DMA
    uint8_t* buffer = (uint8_t*)heap_caps_aligned_alloc(BUFFER_ALIGNMENT, BUFFER_SIZE, MALLOC_CAP_DMA);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Error al asignar memoria para el buffer");
        fclose(f);
        return;
    }

    size_t bytes_remaining = partition->size;
    size_t bytes_to_read;
    size_t total_bytes_read = 0;
    uint32_t offset = 0;

    // Inicializar el progreso de carga
    xSemaphoreTake(load_progress_mutex, portMAX_DELAY);
    load_progress = 0;
    xSemaphoreGive(load_progress_mutex);

    ESP_LOGI(TAG, "Iniciando lectura de la partición y escritura en el archivo.");

    while (bytes_remaining > 0) {
        bytes_to_read = (bytes_remaining > BUFFER_SIZE) ? BUFFER_SIZE : bytes_remaining;

        // Leer de la partición
        esp_err_t ret = esp_partition_read(partition, offset, buffer, bytes_to_read);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Error al leer la partición en offset %" PRIu32 ": %s", offset, esp_err_to_name(ret));
            break;
        }

        // Escribir en el archivo
        size_t written = fwrite(buffer, 1, bytes_to_read, f);
        if (written != bytes_to_read) {
            ESP_LOGE(TAG, "Error al escribir en el archivo: %s", strerror(errno));
            break;
        }

        // Actualizar el estado de la copia
        bytes_remaining -= bytes_to_read;
        offset += bytes_to_read;
        total_bytes_read += bytes_to_read;

        int progress = (int)((total_bytes_read * 100) / partition->size);
        xSemaphoreTake(load_progress_mutex, portMAX_DELAY);
        load_progress = progress;
        xSemaphoreGive(load_progress_mutex);

        // Mostrar progreso cada 10%
        if (progress % 10 == 0 && progress != 0) {
            ESP_LOGI(TAG, "Progreso de copia: %d%%", progress);
        }
    }

    // Finalizar la copia
    xSemaphoreTake(load_progress_mutex, portMAX_DELAY);
    load_progress = 100;  // Marca el progreso como completo al final
    xSemaphoreGive(load_progress_mutex);

    // Liberar recursos
    free(buffer);
    fclose(f);

    ESP_LOGI(TAG, "Copia completada exitosamente.");
}

// Tarea para copiar la partición a la SD
void copy_partition_task(void* params) {
    copy_task_params_t* task_params = (copy_task_params_t*) params;
    copy_partition_to_sd(task_params->partition_label, task_params->file_path);
    free(task_params);
    vTaskDelete(NULL);
}

// Función de la tarea de copia desde SD a partición
void copy_task(void* params) {
    copy_task_params_t* task_params = (copy_task_params_t*) params;
    copy_sd_to_partition(task_params->file_path, task_params->partition_label);
    free(task_params);

    // Después de copiar, cambiar a la partición user_app
    ESP_LOGI(TAG, "Copia completada. Cambiando a la partición user_app.");

    const esp_partition_t *user_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "user_app");

    if (user_partition != NULL) {
        esp_err_t err = esp_ota_set_boot_partition(user_partition);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Partición de arranque establecida a user_app.");

            // Marcar el arranque como temporal
            set_temporary_boot_flag(true);

            ESP_LOGI(TAG, "Reiniciando al user_app...");
            esp_restart(); // Reiniciar el dispositivo
        } else {
            ESP_LOGE(TAG, "Error al establecer la partición de arranque: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "No se encontró la partición user_app.");
    }

    vTaskDelete(NULL);
}

// Manejador para mostrar el progreso en la web
esp_err_t progress_get_handler(httpd_req_t *req) {
    char progress_str[32];
    int progress_value;

    xSemaphoreTake(load_progress_mutex, portMAX_DELAY);
    progress_value = load_progress;
    xSemaphoreGive(load_progress_mutex);

    if (progress_value >= 100) {
        strcpy(progress_str, "Finished");
        xSemaphoreTake(load_progress_mutex, portMAX_DELAY);
        load_progress = 0;  // Reinicia el progreso después de completado
        xSemaphoreGive(load_progress_mutex);
    } else {
        snprintf(progress_str, sizeof(progress_str), "Loading: %d%%", progress_value);
    }

    httpd_resp_sendstr(req, progress_str);
    return ESP_OK;
}

// Genera un botón HTML para un archivo, sin la extensión .bin
void generate_button_html(char *html, size_t max_len, const char *file_name_no_ext, const char *file_path) {
    char button[256];
    snprintf(button, sizeof(button),
             "<button class='app-button' onclick=\"startLoad('%s')\">%s</button><br/>",
             file_path, file_name_no_ext);
    strncat(html, button, max_len - strlen(html) - 1);
}

// Manejador para guardar la partición en la SD
esp_err_t save_get_handler(httpd_req_t *req) {
    char query[128];
    char project_name[64] = {0};

    // Obtiene la cadena de consulta completa y extrae el parámetro "file"
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "file", project_name, sizeof(project_name)) == ESP_OK) {
            char project_folder[512];
            char file_path[512];

            // Construir la ruta del directorio del proyecto
            strncpy(project_folder, "/sdcard/", sizeof(project_folder) - 1);
            strncat(project_folder, project_name, sizeof(project_folder) - strlen(project_folder) - 1);

            // Crear la carpeta del proyecto si no existe
            if (mkdir(project_folder, 0775) != 0 && errno != EEXIST) {
                ESP_LOGE(TAG, "Error al crear la carpeta del proyecto: %s", strerror(errno));
                httpd_resp_send_500(req);
                return ESP_FAIL;
            }

            // Construir la ruta completa para el archivo .bin dentro de la carpeta del proyecto
            strncpy(file_path, project_folder, sizeof(file_path) - 1);
            strncat(file_path, "/", sizeof(file_path) - strlen(file_path) - 1);
            strncat(file_path, project_name, sizeof(file_path) - strlen(file_path) - 1);
            strncat(file_path, ".bin", sizeof(file_path) - strlen(file_path) - 1);

            // Alocar parámetros para la tarea
            copy_task_params_t* task_params = malloc(sizeof(copy_task_params_t));
            if (task_params == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for task parameters");
                httpd_resp_send_500(req);
                return ESP_FAIL;
            }
            strncpy(task_params->file_path, file_path, sizeof(task_params->file_path) - 1);
            strncpy(task_params->partition_label, "user_app", sizeof(task_params->partition_label) - 1);

            // Crear la tarea para copiar la partición a la SD
            if (xTaskCreate(copy_partition_task, "copy_partition_task", 8192, task_params, tskIDLE_PRIORITY + 4, NULL) != pdPASS) {
                ESP_LOGE(TAG, "Failed to create copy_partition_task");
                free(task_params);
                httpd_resp_send_500(req);
                return ESP_FAIL;
            }
        } else {
            ESP_LOGE(TAG, "Parámetro 'file' no encontrado en la solicitud");
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'file' parameter");
            return ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "Error al obtener la cadena de consulta de la solicitud");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to get query string");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "");  // Respuesta vacía para evitar recarga
    return ESP_OK;
}

// Manejador para la ruta /run
esp_err_t run_get_handler(httpd_req_t *req) {
    // Crear una tarea para cambiar a la partición user_app
    if (xTaskCreate(start_user_app_task, "start_user_app_task", 4096, NULL, tskIDLE_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create start_user_app_task");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_sendstr(req, "");  // Respuesta vacía
    return ESP_OK;
}

// Tarea para iniciar la aplicación de usuario
void start_user_app_task(void* params) {
    ESP_LOGI(TAG, "Cambiando a la partición user_app.");

    const esp_partition_t *user_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "user_app");

    if (user_partition != NULL) {
        esp_err_t err = esp_ota_set_boot_partition(user_partition);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Partición de arranque establecida a user_app.");

            // Marcar el arranque como temporal
            set_temporary_boot_flag(true);

            ESP_LOGI(TAG, "Reiniciando al user_app...");
            esp_restart(); // Reiniciar el dispositivo
        } else {
            ESP_LOGE(TAG, "Error al establecer la partición de arranque: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "No se encontró la partición user_app.");
    }

    vTaskDelete(NULL);
}

// Genera botones HTML para los archivos .bin en la SD
void generate_bin_buttons_html(char *html, size_t max_len) {
    DIR *dir = opendir("/sdcard");
    if (!dir) {
        snprintf(html, max_len, "<p>Error al abrir la tarjeta SD.</p>");
        return;
    }

    int file_count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL && file_count < MAX_FILES) {
        if (entry->d_type == DT_DIR) {
            // Comprobar si el directorio contiene un archivo .bin con el mismo nombre
            char project_path[512] = "/sdcard/";
            char project_button_path[512] = "/sdcard/";

            // Añadir el nombre de la carpeta al `project_path`
            strncat(project_path, entry->d_name, sizeof(project_path) - strlen(project_path) - 1);

            // Añadir el nombre de la carpeta y el archivo `.bin` a `project_button_path`
            strncat(project_button_path, entry->d_name, sizeof(project_button_path) - strlen(project_button_path) - 1);
            strncat(project_button_path, "/", sizeof(project_button_path) - strlen(project_button_path) - 1);
            strncat(project_button_path, entry->d_name, sizeof(project_button_path) - strlen(project_button_path) - 1);
            strncat(project_button_path, ".bin", sizeof(project_button_path) - strlen(project_button_path) - 1);

            DIR *project_dir = opendir(project_path);
            if (!project_dir) {
                ESP_LOGE(TAG, "No se pudo abrir la carpeta del proyecto: %s", project_path);
                continue;
            }

            struct dirent *project_entry;
            bool valid_project = false;

            // Verificar que la carpeta tenga un archivo .bin con el mismo nombre que la carpeta
            while ((project_entry = readdir(project_dir)) != NULL) {
                if (project_entry->d_type == DT_REG) {
                    const char *ext = strrchr(project_entry->d_name, '.');
                    if (ext && strcasecmp(ext, ".bin") == 0) {
                        if (strncmp(entry->d_name, project_entry->d_name, ext - project_entry->d_name) == 0) {
                            valid_project = true;
                            break;
                        }
                    }
                }
            }
            closedir(project_dir);

            if (valid_project) {
                ESP_LOGI(TAG, "Proyecto válido encontrado: %s", entry->d_name);

                // Generar botón para el proyecto válido
                generate_button_html(html, max_len, entry->d_name, project_button_path);
                file_count++;
            }
        }
    }
    closedir(dir);
}

// Manejador para la página principal
esp_err_t index_get_handler(httpd_req_t *req) {
    char html[4096] = "<!DOCTYPE html><html><head>"
                      "<style>"
                      "body { display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; font-family: Arial, sans-serif; }"
                      ".container { display: flex; flex-direction: column; align-items: center; width: 100%; max-width: 300px; }"
                      "h1 { text-align: center; font-size: 2em; margin-bottom: 20px; }"
                      "button { width: 100%; padding: 20px; font-size: 1.5em; margin: 10px 0; border-radius: 8px; color: white; border: none; cursor: pointer; }"
                      "button:disabled { background-color: #A9A9A9; cursor: not-allowed; }"  // Botón deshabilitado en gris
                      ".save-run-button { background-color: #4CAF50; }"  // Botones Save y Run en verde
                      ".save-run-button:hover { background-color: #45a049; }"  // Hover verde oscuro para Save y Run
                      ".app-button { background-color: #007BFF; }"  // Botones de aplicaciones en azul
                      ".app-button:hover { background-color: #0069d9; }"  // Hover azul oscuro para aplicaciones
                      "#progress { font-size: 1.2em; margin-top: 20px; color: #555; }"
                      "</style>"
                      "</head><body>"
                      "<div class='container'>";

    strcat(html, "<h1>kodeOS</h1>");

    // Mensaje de error si la SD no está montada
    if (!sd_card_mounted) {
        strcat(html, "<p style='color:red;'>Error: No se pudo montar la tarjeta SD.</p>");
    }

    // Botón Save (verde) - solo activo si la SD está montada
    if (sd_card_mounted) {
        strcat(html, "<button class='save-run-button' onclick=\"startSave()\">Save UserAPP</button><br/>");
    } else {
        strcat(html, "<button class='save-run-button' disabled>Save UserAPP</button><br/>");
    }

    // Botón Run (verde) - siempre activo
    strcat(html, "<button class='save-run-button' onclick=\"startRun()\">Run UserAPP</button><br/>");

    // Generar los botones para los archivos .bin si la SD está montada
    if (sd_card_mounted) {
        generate_bin_buttons_html(html, sizeof(html));
    }

    strcat(html, "<div id='progress'>Waiting</div>");
    strcat(html, "</div><script>"
                 "function updateProgress() {"
                 "  fetch('/progress').then(response => response.text()).then(data => {"
                 "    document.getElementById('progress').textContent = data;"
                 "    if (data !== 'Finished') { setTimeout(updateProgress, 1000); }"
                 "    else { location.reload(); }"
                 "  });"
                 "}"
                 "function startLoad(filePath) {"
                 "  if(confirm('Perform load operation?')) {"
                 "    document.getElementById('progress').textContent = 'Loading...';"
                 "    fetch(`/load?file=${filePath}`).then(() => { updateProgress(); });"
                 "  }"
                 "}"
                 "function startSave() {"
                 "  var fileName = prompt('Introduce file name:');"
                 "  if (fileName !== null && fileName.trim() !== '') {"
                 "    document.getElementById('progress').textContent = 'Saving...';"
                 "    fetch(`/save?file=${encodeURIComponent(fileName.trim())}`).then(() => { updateProgress(); });"
                 "  }"
                 "}"
                 "function startRun() {"
                 "  if(confirm('Run UserAPP?')) {"
                 "    document.getElementById('progress').textContent = 'Running...';"
                 "    fetch('/run').then(() => { updateProgress(); });"
                 "  }"
                 "}"
                 "</script></body></html>");

    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

// Manejador para cargar el archivo seleccionado
esp_err_t load_get_handler(httpd_req_t *req) {
    char query[128];
    char file[64] = {0};

    // Obtiene la cadena de consulta completa
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "file", file, sizeof(file)) == ESP_OK) {
            char file_path[512];
            snprintf(file_path, sizeof(file_path), "%s", file);

            // Alocar parámetros para la tarea
            copy_task_params_t* task_params = malloc(sizeof(copy_task_params_t));
            if (task_params == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for task parameters");
                httpd_resp_send_500(req);
                return ESP_FAIL;
            }
            strcpy(task_params->file_path, file_path);
            strcpy(task_params->partition_label, "user_app");

            // Crear la tarea
            if (xTaskCreate(copy_task, "copy_task", 8192, task_params, tskIDLE_PRIORITY + 4, NULL) != pdPASS) {
                ESP_LOGE(TAG, "Failed to create copy_task");
                free(task_params);
                httpd_resp_send_500(req);
                return ESP_FAIL;
            }
        }
    }
    httpd_resp_sendstr(req, "");  // Respuesta vacía para evitar recarga
    return ESP_OK;
}

void start_web_server() {
    load_progress_mutex = xSemaphoreCreateMutex();
    if (load_progress_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create load_progress mutex");
        return;
    }

    ESP_LOGI(TAG, "Intentando montar la tarjeta SD...");
    if (init_sd_card() == ESP_OK) {
        sd_card_mounted = true;
        ESP_LOGI(TAG, "Tarjeta SD montada correctamente.");
    } else {
        sd_card_mounted = false;
        ESP_LOGW(TAG, "Error al montar la tarjeta SD. 'sd_card_mounted' es false.");
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 4;
    config.stack_size = 10240;  // Aumentar a 10 KB

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Servidor web iniciado.");

        // Registrar los manejadores de URI
        httpd_uri_t uri_index = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = index_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_index);

        httpd_uri_t uri_load = {
            .uri = "/load",
            .method = HTTP_GET,
            .handler = load_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_load);

        httpd_uri_t uri_progress = {
            .uri = "/progress",
            .method = HTTP_GET,
            .handler = progress_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_progress);

        httpd_uri_t uri_save = {
            .uri = "/save",
            .method = HTTP_GET,
            .handler = save_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_save);

        httpd_uri_t uri_run = {
            .uri = "/run",
            .method = HTTP_GET,
            .handler = run_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &uri_run);

        ESP_LOGI(TAG, "Manejadores de URI registrados correctamente.");
    } else {
        ESP_LOGE(TAG, "Error al iniciar el servidor web");
    }
}
