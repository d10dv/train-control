#pragma once

#include "input_manager.h"

/**
 * Initialize a single rotary encoder input.
 *
 * @param desc Pointer to the input descriptor (must be INPUT_ENCODER)
 */
void encoder_init(const input_descriptor_t *desc);
