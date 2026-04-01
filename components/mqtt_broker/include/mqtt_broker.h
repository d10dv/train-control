#pragma once

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize and start the MQTT broker.
 * Spawns a FreeRTOS task running the Mongoose event loop.
 */
void mqtt_broker_init(void);

/**
 * Publish a message from within the firmware (internal publish).
 * The message is delivered to all matching subscribers.
 *
 * @param topic   MQTT topic string
 * @param payload Payload data
 * @param len     Payload length in bytes
 */
void mqtt_broker_publish_internal(const char *topic, const void *payload, size_t len);

#ifdef __cplusplus
}
#endif
