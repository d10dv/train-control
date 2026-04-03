#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

#include "display.h"
#include "font8x8.h"

static const char *TAG = "display";

#define DISPLAY_H_RES 128
#define DISPLAY_V_RES 64
#define SSD1306_I2C_ADDR 0x3C
#define FONT_W 8
#define FONT_H 8

static esp_lcd_panel_handle_t s_panel = NULL;

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing SSD1306 128x64 I2C display");

    /* --- I2C bus --- */
    i2c_master_bus_handle_t bus_handle = NULL;
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,
        .sda_io_num = CONFIG_DISPLAY_PIN_SDA,
        .scl_io_num = CONFIG_DISPLAY_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(
        i2c_new_master_bus(&bus_cfg, &bus_handle),
        TAG, "I2C bus init failed");

    /* --- Panel IO (I2C) --- */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_i2c_config_t io_cfg = {
        .scl_speed_hz = CONFIG_DISPLAY_I2C_FREQ_HZ,
        .dev_addr = SSD1306_I2C_ADDR,
        .control_phase_bytes = 1,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_bit_offset = 6,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i2c(bus_handle, &io_cfg, &io_handle),
        TAG, "Panel IO init failed");

    /* --- Panel driver (SSD1306) --- */
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .bits_per_pixel = 1,
        .reset_gpio_num = CONFIG_DISPLAY_PIN_RST,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_ssd1306(io_handle, &panel_cfg, &s_panel),
        TAG, "SSD1306 panel init failed");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "Panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel),  TAG, "Panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "Display on failed");

    display_clear();

    ESP_LOGI(TAG, "SSD1306 display ready (%dx%d)", DISPLAY_H_RES, DISPLAY_V_RES);
    return ESP_OK;
}

esp_lcd_panel_handle_t display_get_panel(void)
{
    return s_panel;
}

esp_err_t display_clear(void)
{
    uint8_t buf[DISPLAY_H_RES * DISPLAY_V_RES / 8];
    memset(buf, 0, sizeof(buf));
    return esp_lcd_panel_draw_bitmap(s_panel, 0, 0, DISPLAY_H_RES, DISPLAY_V_RES, buf);
}

esp_err_t display_draw_bitmap(const uint8_t *bitmap)
{
    return esp_lcd_panel_draw_bitmap(s_panel, 0, 0, DISPLAY_H_RES, DISPLAY_V_RES, bitmap);
}

esp_err_t display_draw_text(int col, int row, const char *text)
{
    int x = col * FONT_W;
    int y = row * FONT_H;

    for (const char *p = text; *p && x < DISPLAY_H_RES; p++, x += FONT_W) {
        char ch = *p;
        if (ch < 0x20 || ch > 0x7E) ch = '?';

        const uint8_t *glyph = font8x8[ch - 0x20];
        /* Each glyph column byte: LSB = top pixel row.
         * esp_lcd SSD1306 expects the same page format, so we can
         * write each character as an 8x8 block directly. */
        esp_err_t err = esp_lcd_panel_draw_bitmap(
            s_panel, x, y, x + FONT_W, y + FONT_H, glyph);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}
