#pragma once

#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the debug logging subsystem.
 */
void debug_log_init(void);

/**
 * Structured logging macros.
 * These wrap esp_log with additional structured formatting when enabled.
 */
#if CONFIG_DLOG_ENABLE_STRUCTURED
    #define DLOG_E(tag, fmt, ...) ESP_LOGE(tag, "[ERR ] " fmt, ##__VA_ARGS__)
    #define DLOG_W(tag, fmt, ...) ESP_LOGW(tag, "[WARN] " fmt, ##__VA_ARGS__)
    #define DLOG_I(tag, fmt, ...) ESP_LOGI(tag, "[INFO] " fmt, ##__VA_ARGS__)
    #define DLOG_D(tag, fmt, ...) ESP_LOGD(tag, "[DBG ] " fmt, ##__VA_ARGS__)
    #define DLOG_V(tag, fmt, ...) ESP_LOGV(tag, "[VERB] " fmt, ##__VA_ARGS__)
#else
    #define DLOG_E(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
    #define DLOG_W(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
    #define DLOG_I(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
    #define DLOG_D(tag, fmt, ...) ESP_LOGD(tag, fmt, ##__VA_ARGS__)
    #define DLOG_V(tag, fmt, ...) ESP_LOGV(tag, fmt, ##__VA_ARGS__)
#endif

#ifdef __cplusplus
}
#endif
