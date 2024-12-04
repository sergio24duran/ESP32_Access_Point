#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_http_server.h" // Incluimos la librería para el servidor web

#include "lwip/err.h"
#include "lwip/sys.h"

#define EXAMPLE_ESP_WIFI_SSID "MI_WIFI_SALON"
#define EXAMPLE_ESP_WIFI_CHANNEL 6
#define EXAMPLE_MAX_STA_CONN 5
#define LED_CONN_GPIO 18 // Cambiado al GPIO 18
#define LED_WEB_GPIO 14  // Nuevo LED controlado por botones

static const char *TAG = "wifi softAP";

static int device_count = 0;

// Control del estado del LED del botón
static int led_web_state = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);

        // Enciende el LED de conexión
        gpio_set_level(LED_CONN_GPIO, 1);

        // Espera 3 segundos (3000 ms)
        vTaskDelay(3000 / portTICK_PERIOD_MS);

        // Apaga el LED de conexión
        gpio_set_level(LED_CONN_GPIO, 0);

        // Incrementa el contador de dispositivos
        device_count++;
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d, reason=%d",
                 MAC2STR(event->mac), event->aid, event->reason);

        // Decrementa el contador de dispositivos
        device_count--;
    }
}

esp_err_t get_handler(httpd_req_t *req)
{
    const char *html_response = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Estado del ESP32</title>
    <style>
        body {
            font-size: 2em;
            text-align: center;
            font-family: Arial, sans-serif;
            background-color: #f4f4f9;
            color: #333;
        }
        h1 { font-size: 4em; }
        p { font-size: 2em; }
        button {
            font-size: 1em;
            padding: 20px 40px;
            margin: 10px;
            background-color: #007bff;
            color: white;
            border: none;
            border-radius: 10px;
            cursor: pointer;
        }
        button:hover { background-color: #0056b3; }
    </style>
</head>
<body>
    <h1>Estado del ESP32</h1>
    <p>Dispositivos conectados: %d</p>
    <button onclick="fetch('/led/on')">Encender LED</button>
    <button onclick="fetch('/led/off')">Apagar LED</button>
</body>
</html>
)rawliteral";

    char resp_buffer[2048];
    snprintf(resp_buffer, sizeof(resp_buffer), html_response, device_count);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp_buffer, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

esp_err_t led_on_handler(httpd_req_t *req)
{
    gpio_set_level(LED_WEB_GPIO, 1); // Enciende el LED
    led_web_state = 1;               // Actualiza el estado del LED
    httpd_resp_send(req, "LED encendido", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t led_off_handler(httpd_req_t *req)
{
    gpio_set_level(LED_WEB_GPIO, 0); // Apaga el LED
    led_web_state = 0;               // Actualiza el estado del LED
    httpd_resp_send(req, "LED apagado", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 2244;
    ESP_LOGI(TAG, "Iniciando el servidor HTTP...");

    // Inicia el servidor httpd
    if (httpd_start(&server, &config) == ESP_OK)
    {
        ESP_LOGI(TAG, "Servidor HTTP iniciado en puerto %d", config.server_port);

        // Página principal
        httpd_uri_t get_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &get_uri);

        // Control del LED: encender
        httpd_uri_t led_on_uri = {
            .uri = "/led/on",
            .method = HTTP_GET,
            .handler = led_on_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &led_on_uri);

        // Control del LED: apagar
        httpd_uri_t led_off_uri = {
            .uri = "/led/off",
            .method = HTTP_GET,
            .handler = led_off_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &led_off_uri);
    }
    else
    {
        ESP_LOGE(TAG, "Error iniciando el servidor HTTP!");
    }
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = "",
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_OPEN,
            .pmf_cfg = {
                .required = true,
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_CHANNEL);
}

void app_main(void)
{
    // Inicializa NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();

    // Configura los GPIOs
    gpio_reset_pin(LED_CONN_GPIO);
    gpio_set_direction(LED_CONN_GPIO, GPIO_MODE_OUTPUT);

    gpio_reset_pin(LED_WEB_GPIO);
    gpio_set_direction(LED_WEB_GPIO, GPIO_MODE_OUTPUT);

    // Inicia el servidor web
    start_webserver();
    ESP_LOGI(TAG, "Servidor HTTP iniciado correctamente");
}
