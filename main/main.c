#include "esp_log.h"
#include "esp_app_desc.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "debug_log.h"
#include "wifi_ap.h"
#include "mqtt_broker.h"
#include "input_manager.h"
#include "display.h"
#include "train_controller.h"

static const char *TAG = "main";

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

    /* 7. Initialize display */
    ESP_ERROR_CHECK(display_init());

    /* 8. Start input manager */
    /* TODO: define actual input descriptors for your hardware */
    static const input_descriptor_t inputs[] = {
        { .id = 0, .type = INPUT_BUTTON,  .pin = { .button  = { .gpio = GPIO_NUM_12 } } },
        { .id = 1, .type = INPUT_BUTTON,  .pin = { .button  = { .gpio = GPIO_NUM_14 } } },
        { .id = 2, .type = INPUT_ENCODER, .pin = { .encoder = { .gpio_a = GPIO_NUM_16, .gpio_b = GPIO_NUM_17, .gpio_btn = GPIO_NUM_21 } } },
    };
    input_manager_init(inputs, sizeof(inputs) / sizeof(inputs[0]));

    /* 9. Start train controller */
    train_controller_init();

    const esp_app_desc_t *app_desc = esp_app_get_description();
    ESP_LOGI(TAG, "Train Control v%s started (IDF %s)", app_desc->version, app_desc->idf_ver);
}
