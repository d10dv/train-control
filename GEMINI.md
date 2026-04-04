# Train Control Project

This project is an ESP32-based control system for model trains. It functions as a WiFi Access Point and an embedded MQTT broker, allowing it to act as a central controller for multiple trains (WiFi-enabled MQTT clients).

## Architecture & Technology Stack

- **Framework:** ESP-IDF v6.0 (C-based).
- **Network:** ESP32 SoftAP mode.
- **Messaging:** Embedded MQTT broker using [Mongoose](https://github.com/cesanta/mongoose).
- **Communication:** Decoupled event-driven architecture based on the ESP-IDF default event loop.
- **Hardware Interface:**
    - **Input:** Buttons and Rotary Encoders (using GPIO and PCNT peripherals).
    - **Display:** SSD1306 OLED (128x64) via I2C.
- **Core Components:**
    - `event_bus`: Defines the `TRAIN_EVENT` base and event IDs for system-wide communication.
    - `train_controller`: The central logic that maps input events to MQTT commands and manages train states.
    - `input_manager`: Handles low-level button and encoder logic.
    - `mqtt_broker`: Wraps Mongoose to provide an embedded broker.
    - `wifi_ap`: Manages WiFi SoftAP configuration.
    - `display`: Handles text rendering on the OLED.

## Building and Running

The project uses the standard ESP-IDF build system.

### Prerequisites
- ESP-IDF v6.0 installed and configured in your environment.

### Commands
- **Build:** `idf.py build`
- **Flash:** `idf.py flash`
- **Monitor:** `idf.py monitor`
- **Configure:** `idf.py menuconfig` (for WiFi, MQTT, Display, and Input settings)

## Development Conventions

### Event-Driven Communication
All modules should communicate through the `event_bus`. Avoid direct dependencies between components where possible.
- **Publishing:** Use `esp_event_post(TRAIN_EVENT, EVENT_ID, &data, sizeof(data), portMAX_DELAY)`.
- **Subscribing:** Use `esp_event_handler_register(TRAIN_EVENT, EVENT_ID, handler, arg)`.

### Hardware Mapping
Input pin assignments are defined in `main/main.c` within the `inputs` array. Configuration for I2C and other peripherals is available via Kconfig (`idf.py menuconfig`).

### MQTT Protocol
Refer to `PROTOCOL.md` for the full specification of topics and JSON payloads.
- **Commands:** `train/<id>/cmd`
- **Status:** `train/<id>/state` (retained)
- **Discovery:** `train/announce` (retained)

### Logging
Use the `DLOG_*` macros from the `debug_log` component or standard `ESP_LOG*` macros. `debug_log` provides structured logging that can optionally be published to MQTT.

## Key Files
- `main/main.c`: System initialization and input pin definitions.
- `components/event_bus/include/event_bus.h`: Central definition of all system events.
- `components/train_controller/train_controller.c`: Implementation of the control logic and MQTT command assembly.
- `PROTOCOL.md`: Detailed MQTT communication protocol.
- `sdkconfig.defaults`: Default project configurations.
