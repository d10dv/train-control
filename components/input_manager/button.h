#pragma once

#include "input_manager.h"

/**
 * Initialize a single button input.
 *
 * @param desc Pointer to the input descriptor (must be INPUT_BUTTON)
 */
void button_init(const input_descriptor_t *desc);
