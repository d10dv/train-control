#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"

#include "wifi_ap.h"
#include "debug_log.h"
#include "event_bus.h"

static const char *TAG = "wifi_ap";

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), MACSTR, MAC2STR(event->mac));
        DLOG_I(TAG, "Station connected, MAC: %s, AID=%d", mac_str, event->aid);

        wifi_sta_event_t sta_evt;
        memcpy(sta_evt.mac, event->mac, 6);
        esp_event_post(TRAIN_EVENT, TRAIN_EVT_STA_CONNECTED,
                       &sta_evt, sizeof(sta_evt), portMAX_DELAY);

    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), MACSTR, MAC2STR(event->mac));
        DLOG_I(TAG, "Station disconnected, MAC: %s, AID=%d", mac_str, event->aid);

        wifi_sta_event_t sta_evt;
        memcpy(sta_evt.mac, event->mac, 6);
        esp_event_post(TRAIN_EVENT, TRAIN_EVT_STA_DISCONNECTED,
                       &sta_evt, sizeof(sta_evt), portMAX_DELAY);
    }
}

void wifi_ap_init(void)
{
    /* Create default AP netif */
    esp_netif_create_default_wifi_ap();

    /* Initialize WiFi with default config */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Register event handler */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    /* Configure AP */
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = CONFIG_WIFI_AP_SSID,
            .ssid_len = strlen(CONFIG_WIFI_AP_SSID),
            .channel = CONFIG_WIFI_AP_CHANNEL,
            .password = CONFIG_WIFI_AP_PASSWORD,
            .max_connection = CONFIG_WIFI_AP_MAX_CONNECTIONS,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = true,
            },
        },
    };

    /* Use open auth if no password */
    if (strlen(CONFIG_WIFI_AP_PASSWORD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    DLOG_I(TAG, "WiFi AP started. SSID: %s, Channel: %d",
           CONFIG_WIFI_AP_SSID, CONFIG_WIFI_AP_CHANNEL);
}
