#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_app_desc.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "debug_log.h"
#include "event_bus.h"
#include "wifi_ap.h"
#include "mqtt_broker.h"
#include "input_manager.h"

static const char *TAG = "main";

/* ─── Input → MQTT bridge ─── */

static void input_to_mqtt_handler(void *arg, esp_event_base_t event_base,
                                  int32_t event_id, void *event_data)
{
    input_event_t *evt = (input_event_t *)event_data;
    char topic[64];
    char payload[32];

    switch (event_id) {
    case TRAIN_EVT_BUTTON_PRESS:
        snprintf(topic, sizeof(topic), "train/input/button/%d", evt->input_id);
        snprintf(payload, sizeof(payload), "press");
        break;
    case TRAIN_EVT_BUTTON_LONG_PRESS:
        snprintf(topic, sizeof(topic), "train/input/button/%d", evt->input_id);
        snprintf(payload, sizeof(payload), "long_press");
        break;
    case TRAIN_EVT_BUTTON_RELEASE:
        snprintf(topic, sizeof(topic), "train/input/button/%d", evt->input_id);
        snprintf(payload, sizeof(payload), "release");
        break;
    case TRAIN_EVT_ENCODER_CW:
        snprintf(topic, sizeof(topic), "train/input/encoder/%d", evt->input_id);
        snprintf(payload, sizeof(payload), "cw:%ld", (long)evt->value);
        break;
    case TRAIN_EVT_ENCODER_CCW:
        snprintf(topic, sizeof(topic), "train/input/encoder/%d", evt->input_id);
        snprintf(payload, sizeof(payload), "ccw:%ld", (long)evt->value);
        break;
    case TRAIN_EVT_ENCODER_CLICK:
        snprintf(topic, sizeof(topic), "train/input/encoder/%d", evt->input_id);
        snprintf(payload, sizeof(payload), "click");
        break;
    default:
        return;
    }

    mqtt_broker_publish_internal(topic, payload, strlen(payload));
}

void app_main(void)
{
    /* 1. Initialize NVS — required for WiFi */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 2. Create default event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 3. Initialize TCP/IP stack */
    ESP_ERROR_CHECK(esp_netif_init());

    /* 4. Start logging subsystem */
    debug_log_init();

    /* 5. Start WiFi Access Point */
    wifi_ap_init();

    /* 6. Start MQTT broker */
    mqtt_broker_init();

    /* 7. Start input manager */
    /* TODO: define actual input descriptors for your hardware */
    static const input_descriptor_t inputs[] = {
        { .id = 0, .type = INPUT_BUTTON, .pin = { .button = { .gpio = GPIO_NUM_12 } } },
        { .id = 1, .type = INPUT_BUTTON, .pin = { .button = { .gpio = GPIO_NUM_14 } } },
        { .id = 2, .type = INPUT_ENCODER, .pin = { .encoder = { .gpio_a = GPIO_NUM_25, .gpio_b = GPIO_NUM_26, .gpio_btn = GPIO_NUM_27 } } },
    };
    input_manager_init(inputs, sizeof(inputs) / sizeof(inputs[0]));

    /* 8. Register input → MQTT bridge */
    esp_event_handler_register(TRAIN_EVENT, TRAIN_EVT_BUTTON_PRESS, input_to_mqtt_handler, NULL);
    esp_event_handler_register(TRAIN_EVENT, TRAIN_EVT_BUTTON_LONG_PRESS, input_to_mqtt_handler, NULL);
    esp_event_handler_register(TRAIN_EVENT, TRAIN_EVT_BUTTON_RELEASE, input_to_mqtt_handler, NULL);
    esp_event_handler_register(TRAIN_EVENT, TRAIN_EVT_ENCODER_CW, input_to_mqtt_handler, NULL);
    esp_event_handler_register(TRAIN_EVENT, TRAIN_EVT_ENCODER_CCW, input_to_mqtt_handler, NULL);
    esp_event_handler_register(TRAIN_EVENT, TRAIN_EVT_ENCODER_CLICK, input_to_mqtt_handler, NULL);

    const esp_app_desc_t *app_desc = esp_app_get_description();
    ESP_LOGI(TAG, "Train Control v%s started (IDF %s)", app_desc->version, app_desc->idf_ver);
}
