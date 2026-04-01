#include "driver/gpio.h"
#include "esp_log.h"

#include "input_manager.h"
#include "button.h"
#include "encoder.h"
#include "debug_log.h"

static const char *TAG = "input_mgr";

void input_manager_init(const input_descriptor_t *descriptors, size_t count)
{
    /* Install GPIO ISR service (shared by buttons and encoders) */
    gpio_install_isr_service(0);

    for (size_t i = 0; i < count; i++) {
        const input_descriptor_t *desc = &descriptors[i];

        switch (desc->type) {
        case INPUT_BUTTON:
            button_init(desc);
            break;
        case INPUT_ENCODER:
            encoder_init(desc);
            break;
        default:
            DLOG_W(TAG, "Unknown input type %d for id %d", desc->type, desc->id);
            break;
        }
    }

    DLOG_I(TAG, "Input manager initialized with %d inputs", (int)count);
}
