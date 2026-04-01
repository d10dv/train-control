#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize and start WiFi in Access Point mode.
 * Configures SSID, password, and channel from Kconfig.
 * Blocks until the AP is started.
 */
void wifi_ap_init(void);

#ifdef __cplusplus
}
#endif
