#pragma once

#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(TRAIN_EVENT);

typedef enum {
    /* WiFi AP events */
    TRAIN_EVT_STA_CONNECTED,
    TRAIN_EVT_STA_DISCONNECTED,

    /* Button events */
    TRAIN_EVT_BUTTON_PRESS,
    TRAIN_EVT_BUTTON_LONG_PRESS,
    TRAIN_EVT_BUTTON_RELEASE,

    /* Encoder events */
    TRAIN_EVT_ENCODER_CW,
    TRAIN_EVT_ENCODER_CCW,
    TRAIN_EVT_ENCODER_CLICK,

    /* MQTT events */
    TRAIN_EVT_MQTT_CLIENT_CONNECT,
    TRAIN_EVT_MQTT_CLIENT_DISCONNECT,
    TRAIN_EVT_MQTT_MESSAGE,
} train_event_id_t;

/**
 * Event data for input events (buttons and encoders).
 */
typedef struct {
    uint8_t input_id;   /**< Logical input identifier */
    int32_t value;      /**< Encoder delta or button state */
} input_event_t;

/**
 * Event data for MQTT message events.
 */
typedef struct {
    char topic[128];
    char payload[256];
    size_t payload_len;
} mqtt_message_event_t;

/**
 * Event data for WiFi station events.
 */
typedef struct {
    uint8_t mac[6];
} wifi_sta_event_t;

#ifdef __cplusplus
}
#endif
