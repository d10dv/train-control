#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "button.h"
#include "debug_log.h"
#include "event_bus.h"

static const char *TAG = "button";

typedef struct {
    uint8_t id;
    gpio_num_t gpio;
    bool active_low;
    TimerHandle_t debounce_timer;
    TimerHandle_t long_press_timer;
    bool pressed;
} button_state_t;

#define MAX_BUTTONS 16
static button_state_t s_buttons[MAX_BUTTONS];
static int s_button_count = 0;

static button_state_t *find_button_by_gpio(gpio_num_t gpio)
{
    for (int i = 0; i < s_button_count; i++) {
        if (s_buttons[i].gpio == gpio) {
            return &s_buttons[i];
        }
    }
    return NULL;
}

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    button_state_t *btn = (button_state_t *)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xTimerStartFromISR(btn->debounce_timer, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

static void debounce_timer_cb(TimerHandle_t timer)
{
    button_state_t *btn = (button_state_t *)pvTimerGetTimerID(timer);
    int level = gpio_get_level(btn->gpio);
    bool is_pressed = btn->active_low ? (level == 0) : (level == 1);

    if (is_pressed && !btn->pressed) {
        btn->pressed = true;
        DLOG_I(TAG, "Button %d pressed (GPIO %d)", btn->id, btn->gpio);

        input_event_t evt = { .input_id = btn->id, .value = 1 };
        esp_event_post(TRAIN_EVENT, TRAIN_EVT_BUTTON_PRESS,
                       &evt, sizeof(evt), 0);

        /* Start long-press timer */
        xTimerStart(btn->long_press_timer, 0);

    } else if (!is_pressed && btn->pressed) {
        btn->pressed = false;
        DLOG_I(TAG, "Button %d released (GPIO %d)", btn->id, btn->gpio);

        /* Stop long-press timer */
        xTimerStop(btn->long_press_timer, 0);

        input_event_t evt = { .input_id = btn->id, .value = 0 };
        esp_event_post(TRAIN_EVENT, TRAIN_EVT_BUTTON_RELEASE,
                       &evt, sizeof(evt), 0);
    }
}

static void long_press_timer_cb(TimerHandle_t timer)
{
    button_state_t *btn = (button_state_t *)pvTimerGetTimerID(timer);
    if (btn->pressed) {
        DLOG_I(TAG, "Long press on button %d", btn->id);

        input_event_t evt = { .input_id = btn->id, .value = 1 };
        esp_event_post(TRAIN_EVENT, TRAIN_EVT_BUTTON_LONG_PRESS,
                       &evt, sizeof(evt), 0);
    }
}

void button_init(const input_descriptor_t *desc)
{
    if (s_button_count >= MAX_BUTTONS) {
        DLOG_W(TAG, "Max buttons reached, ignoring button %d", desc->id);
        return;
    }

    button_state_t *btn = &s_buttons[s_button_count];
    btn->id = desc->id;
    btn->gpio = desc->pin.button.gpio;
    btn->active_low = desc->pin.button.active_low;
    btn->pressed = false;

    /* Configure GPIO as input with pull-up or pull-down */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << btn->gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = btn->active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = btn->active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io_conf);

    /* Create debounce timer */
    btn->debounce_timer = xTimerCreate(
        "btn_deb", pdMS_TO_TICKS(CONFIG_INPUT_DEBOUNCE_MS),
        pdFALSE, btn, debounce_timer_cb);

    /* Create long-press timer */
    btn->long_press_timer = xTimerCreate(
        "btn_lp", pdMS_TO_TICKS(CONFIG_INPUT_LONG_PRESS_MS),
        pdFALSE, btn, long_press_timer_cb);

    /* Install ISR handler */
    gpio_isr_handler_add(btn->gpio, gpio_isr_handler, btn);

    s_button_count++;

    DLOG_I(TAG, "Button %d initialized on GPIO %d", btn->id, btn->gpio);
}
