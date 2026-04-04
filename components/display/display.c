#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "display.h"
#include "font8x8.h"

static const char *TAG = "display";

#define DISPLAY_H_RES 128
#define DISPLAY_V_RES 64
#define SSD1306_I2C_ADDR 0x3C
#define FONT_W 8
#define FONT_H 8

static esp_lcd_panel_handle_t s_panel = NULL;
static SemaphoreHandle_t s_display_mux = NULL;

static void display_blink_task(void *arg)
{
    bool on = false;
    while (1) {
        on = !on;
        /* Draw a 2x2 dot in the bottom right corner (126, 56) to (128, 64) 
         * SSD1306 page format: 0xC0 = bits 6 and 7 set (bottom 2 pixels of a page) */
        uint8_t data[2] = { on ? 0xC0 : 0x00, on ? 0xC0 : 0x00 };
        
        if (xSemaphoreTake(s_display_mux, pdMS_TO_TICKS(100)) == pdTRUE) {
            esp_lcd_panel_draw_bitmap(s_panel, 126, 56, 128, 64, data);
            xSemaphoreGive(s_display_mux);
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing SSD1306 128x64 I2C display");

    s_display_mux = xSemaphoreCreateMutex();
    if (s_display_mux == NULL) {
        return ESP_ERR_NO_MEM;
    }

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

    xTaskCreate(display_blink_task, "display_blink", 2048, NULL, 1, NULL);

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
    
    esp_err_t ret = ESP_FAIL;
    if (xSemaphoreTake(s_display_mux, pdMS_TO_TICKS(100)) == pdTRUE) {
        ret = esp_lcd_panel_draw_bitmap(s_panel, 0, 0, DISPLAY_H_RES, DISPLAY_V_RES, buf);
        xSemaphoreGive(s_display_mux);
    }
    return ret;
}

esp_err_t display_draw_bitmap(const uint8_t *bitmap)
{
    esp_err_t ret = ESP_FAIL;
    if (xSemaphoreTake(s_display_mux, pdMS_TO_TICKS(100)) == pdTRUE) {
        ret = esp_lcd_panel_draw_bitmap(s_panel, 0, 0, DISPLAY_H_RES, DISPLAY_V_RES, bitmap);
        xSemaphoreGive(s_display_mux);
    }
    return ret;
}

esp_err_t display_draw_text(int col, int row, const char *text)
{
    int x = col * FONT_W;
    int y = row * FONT_H;

    if (xSemaphoreTake(s_display_mux, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    for (const char *p = text; *p && x < DISPLAY_H_RES; p++, x += FONT_W) {
        char ch = *p;
        if (ch < 0x20 || ch > 0x7E) ch = '?';

        const uint8_t *glyph = font8x8[ch - 0x20];
        /* Each glyph column byte: LSB = top pixel row.
         * esp_lcd SSD1306 expects the same page format, so we can
         * write each character as an 8x8 block directly. */
        esp_err_t err = esp_lcd_panel_draw_bitmap(
            s_panel, x, y, x + FONT_W, y + FONT_H, glyph);
        if (err != ESP_OK) {
            xSemaphoreGive(s_display_mux);
            return err;
        }
    }
    xSemaphoreGive(s_display_mux);
    return ESP_OK;
}

esp_err_t display_draw_text_2x(int col, int row, const char *text)
{
    const int row_pitch = FONT_H * 2 + 8; /* 16px glyph + 8px gap */
    int x = col * FONT_W * 2;
    int y = row * row_pitch;

    if (xSemaphoreTake(s_display_mux, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    for (const char *p = text; *p && x < DISPLAY_H_RES; p++, x += FONT_W * 2) {
        char ch = *p;
        if (ch < 0x20 || ch > 0x7E) ch = '?';

        const uint8_t *glyph = font8x8[ch - 0x20];

        /* Scale 8x8 glyph to 16x16: each pixel becomes a 2x2 block.
         * SSD1306 page format: 16 bytes for top 8 rows, 16 for bottom 8. */
        uint8_t buf[32];

        for (int c = 0; c < 8; c++) {
            uint8_t orig = glyph[c];

            /* Stretch lower 4 bits → top page byte (8 bits) */
            uint8_t top = 0;
            for (int b = 0; b < 4; b++) {
                if (orig & (1 << b))
                    top |= (3 << (b * 2));
            }

            /* Stretch upper 4 bits → bottom page byte (8 bits) */
            uint8_t bot = 0;
            for (int b = 0; b < 4; b++) {
                if (orig & (1 << (b + 4)))
                    bot |= (3 << (b * 2));
            }

            /* Duplicate each column horizontally */
            buf[c * 2]         = top;
            buf[c * 2 + 1]     = top;
            buf[16 + c * 2]     = bot;
            buf[16 + c * 2 + 1] = bot;
        }

        esp_err_t err = esp_lcd_panel_draw_bitmap(
            s_panel, x, y, x + FONT_W * 2, y + FONT_H * 2, buf);
        if (err != ESP_OK) {
            xSemaphoreGive(s_display_mux);
            return err;
        }
    }
    xSemaphoreGive(s_display_mux);
    return ESP_OK;
}
