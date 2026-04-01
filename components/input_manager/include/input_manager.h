#pragma once

#include "driver/gpio.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Input types.
 */
typedef enum {
    INPUT_BUTTON,
    INPUT_ENCODER,
} input_type_t;

/**
 * Pin configuration for a button.
 */
typedef struct {
    gpio_num_t gpio;
} button_pin_t;

/**
 * Pin configuration for a rotary encoder.
 */
typedef struct {
    gpio_num_t gpio_a;   /**< Encoder channel A */
    gpio_num_t gpio_b;   /**< Encoder channel B */
    gpio_num_t gpio_btn; /**< Encoder push button (GPIO_NUM_NC if none) */
} encoder_pin_t;

/**
 * Descriptor for a single input device.
 */
typedef struct {
    uint8_t id;           /**< Logical input identifier */
    input_type_t type;    /**< INPUT_BUTTON or INPUT_ENCODER */
    union {
        button_pin_t button;
        encoder_pin_t encoder;
    } pin;
} input_descriptor_t;

/**
 * Initialize the input manager with the given descriptors.
 *
 * @param descriptors Array of input descriptors
 * @param count       Number of descriptors in the array
 */
void input_manager_init(const input_descriptor_t *descriptors, size_t count);

#ifdef __cplusplus
}
#endif
