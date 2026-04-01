#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_log.h"

#if CONFIG_INPUT_ENCODER_USE_PCNT
#include "driver/pulse_cnt.h"
#endif

#include "encoder.h"
#include "button.h"
#include "debug_log.h"
#include "event_bus.h"

static const char *TAG = "encoder";

typedef struct {
    uint8_t id;
    gpio_num_t gpio_a;
    gpio_num_t gpio_b;
    gpio_num_t gpio_btn;
#if CONFIG_INPUT_ENCODER_USE_PCNT
    pcnt_unit_handle_t pcnt_unit;
#endif
    int32_t last_count;
} encoder_state_t;

#define MAX_ENCODERS 4
static encoder_state_t s_encoders[MAX_ENCODERS];
static int s_encoder_count = 0;
static TaskHandle_t s_poll_task = NULL;

#if CONFIG_INPUT_ENCODER_USE_PCNT

static void encoder_init_pcnt(encoder_state_t *enc)
{
    /* Configure PCNT unit for quadrature decoding */
    pcnt_unit_config_t unit_config = {
        .high_limit = 32767,
        .low_limit = -32768,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &enc->pcnt_unit));

    /* Channel A: counts on A edges, direction from B level */
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = enc->gpio_a,
        .level_gpio_num = enc->gpio_b,
    };
    pcnt_channel_handle_t chan_a;
    ESP_ERROR_CHECK(pcnt_new_channel(enc->pcnt_unit, &chan_a_config, &chan_a));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan_a,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(chan_a,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    /* Channel B: counts on B edges, direction from A level */
    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = enc->gpio_b,
        .level_gpio_num = enc->gpio_a,
    };
    pcnt_channel_handle_t chan_b;
    ESP_ERROR_CHECK(pcnt_new_channel(enc->pcnt_unit, &chan_b_config, &chan_b));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan_b,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(chan_b,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    /* Enable and start */
    ESP_ERROR_CHECK(pcnt_unit_enable(enc->pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(enc->pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(enc->pcnt_unit));

    enc->last_count = 0;
}

static void encoder_poll_pcnt(encoder_state_t *enc)
{
    int count = 0;
    pcnt_unit_get_count(enc->pcnt_unit, &count);

    int32_t delta = count - enc->last_count;
    if (delta != 0) {
        enc->last_count = count;

        input_event_t evt = { .input_id = enc->id, .value = delta };
        train_event_id_t event_id = (delta > 0) ? TRAIN_EVT_ENCODER_CW : TRAIN_EVT_ENCODER_CCW;
        esp_event_post(TRAIN_EVENT, event_id, &evt, sizeof(evt), 0);
    }
}

#else /* GPIO ISR fallback */

static void encoder_init_gpio(encoder_state_t *enc)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << enc->gpio_a) | (1ULL << enc->gpio_b),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    enc->last_count = 0;
}

static void encoder_poll_gpio(encoder_state_t *enc)
{
    /* Simple polling-based quadrature decode */
    static uint8_t prev_state[MAX_ENCODERS] = {0};
    uint8_t a = gpio_get_level(enc->gpio_a);
    uint8_t b = gpio_get_level(enc->gpio_b);
    uint8_t state = (a << 1) | b;

    int idx = enc - s_encoders;
    uint8_t prev = prev_state[idx];

    /* State transition table for quadrature */
    static const int8_t transition[] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
    int8_t delta = transition[(prev << 2) | state];

    if (delta != 0) {
        input_event_t evt = { .input_id = enc->id, .value = delta };
        train_event_id_t event_id = (delta > 0) ? TRAIN_EVT_ENCODER_CW : TRAIN_EVT_ENCODER_CCW;
        esp_event_post(TRAIN_EVENT, event_id, &evt, sizeof(evt), 0);
    }

    prev_state[idx] = state;
}

#endif

/* Polling task for all encoders */
static void encoder_poll_task(void *arg)
{
    for (;;) {
        for (int i = 0; i < s_encoder_count; i++) {
#if CONFIG_INPUT_ENCODER_USE_PCNT
            encoder_poll_pcnt(&s_encoders[i]);
#else
            encoder_poll_gpio(&s_encoders[i]);
#endif
        }
        vTaskDelay(pdMS_TO_TICKS(5)); /* Poll at ~200 Hz */
    }
}

void encoder_init(const input_descriptor_t *desc)
{
    if (s_encoder_count >= MAX_ENCODERS) {
        DLOG_W(TAG, "Max encoders reached, ignoring encoder %d", desc->id);
        return;
    }

    encoder_state_t *enc = &s_encoders[s_encoder_count];
    enc->id = desc->id;
    enc->gpio_a = desc->pin.encoder.gpio_a;
    enc->gpio_b = desc->pin.encoder.gpio_b;
    enc->gpio_btn = desc->pin.encoder.gpio_btn;

#if CONFIG_INPUT_ENCODER_USE_PCNT
    encoder_init_pcnt(enc);
#else
    encoder_init_gpio(enc);
#endif

    /* Initialize encoder push button as a regular button if configured */
    if (enc->gpio_btn != GPIO_NUM_NC) {
        input_descriptor_t btn_desc = {
            .id = desc->id,  /* Same ID, differentiated by event type */
            .type = INPUT_BUTTON,
            .pin = { .button = { .gpio = enc->gpio_btn } },
        };
        button_init(&btn_desc);
    }

    s_encoder_count++;

    /* Start polling task if first encoder */
    if (s_encoder_count == 1 && s_poll_task == NULL) {
        xTaskCreate(encoder_poll_task, "enc_poll", 2048, NULL, 4, &s_poll_task);
    }

    DLOG_I(TAG, "Encoder %d initialized (A=%d, B=%d, BTN=%d)",
           enc->id, enc->gpio_a, enc->gpio_b, enc->gpio_btn);
}
