#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the train controller.
 * Registers event handlers for input, button and MQTT events.
 * Must be called after event_bus, mqtt_broker, display and input_manager.
 */
void train_controller_init(void);

#ifdef __cplusplus
}
#endif
