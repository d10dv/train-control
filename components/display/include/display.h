#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize I2C bus, panel IO and SSD1306 display.
 * Must be called after esp_event_loop_create_default().
 */
esp_err_t display_init(void);

/**
 * Get the panel handle for direct drawing operations.
 */
esp_lcd_panel_handle_t display_get_panel(void);

/**
 * Clear the entire display (fill with black).
 */
esp_err_t display_clear(void);

/**
 * Draw a full-screen bitmap (128x64, 1-bit packed, 1024 bytes).
 * Each byte represents 8 vertical pixels (LSB = top).
 */
esp_err_t display_draw_bitmap(const uint8_t *bitmap);

/**
 * Draw a text string at the given character position.
 * @param col  character column (0–15 for 128px / 8px font)
 * @param row  character row   (0–7  for 64px  / 8px font)
 * @param text null-terminated string
 */
esp_err_t display_draw_text(int col, int row, const char *text);

#ifdef __cplusplus
}
#endif
