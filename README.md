# Train Control

ESP32-based control system. The microcontroller acts as a WiFi access point and MQTT broker for external devices. Supports button and rotary encoder input with event publishing over MQTT.

## Features

- **WiFi Access Point** — ESP32 runs as a soft AP, external devices connect directly to it
- **MQTT Broker** — embedded broker built on [Mongoose](https://github.com/cesanta/mongoose), port 1883
- **Input Handling** — buttons (press/long-press/release) and rotary encoders (CW/CCW/click) with hardware quadrature decoding (PCNT)
- **Structured Logging** — debug output over UART with optional publishing to an MQTT topic

## Architecture

```
┌─────────────────────────────────────────────┐
│                  main.c                      │
│           (boot + input→MQTT bridge)         │
└──────┬────────┬────────┬────────┬───────────┘
       │        │        │        │
  ┌────▼──┐ ┌──▼───┐ ┌──▼────┐ ┌─▼──────────┐
  │wifi_ap│ │mqtt_  │ │input_ │ │debug_log   │
  │       │ │broker │ │manager│ │            │
  └───┬───┘ └──┬───┘ └──┬────┘ └────────────┘
      │        │        │
      └────────┴────────┘
               │
        ┌──────▼──────┐
        │  event_bus  │
        │(TRAIN_EVENT)│
        └─────────────┘
```

Modules communicate through the event bus (`event_bus`) built on the ESP-IDF event loop. There are no direct dependencies between modules.

## Components

| Component | Description |
|-----------|-------------|
| `event_bus` | Event bus — declares `TRAIN_EVENT` base, event IDs, and data structures |
| `debug_log` | Structured logging macros on top of `esp_log` |
| `wifi_ap` | WiFi access point with Kconfig settings |
| `mqtt_broker` | MQTT broker (Mongoose) with subscription table and wildcard matching |
| `input_manager` | Button and encoder handling, delegates to `button.c` and `encoder.c` |
| `mongoose` | Vendored Mongoose networking library v7.20 |

## Requirements

- ESP-IDF v6.0
- ESP32

## Build and Flash

```bash
# Set up ESP-IDF environment
export IDF_PATH=/path/to/esp-idf
source $IDF_PATH/export.sh

# Set target (once)
idf.py set-target esp32

# Build
idf.py build

# Flash and open serial monitor
idf.py flash monitor
```

## Configuration

All settings are available via `idf.py menuconfig`:

### WiFi Access Point
| Parameter | Default | Description |
|-----------|---------|-------------|
| `WIFI_AP_SSID` | TrainControl | Network name |
| `WIFI_AP_PASSWORD` | train1234 | Password (WPA2) |
| `WIFI_AP_CHANNEL` | 6 | WiFi channel |
| `WIFI_AP_MAX_CONNECTIONS` | 4 | Max connected stations |

### MQTT Broker
| Parameter | Default | Description |
|-----------|---------|-------------|
| `MQTT_BROKER_PORT` | 1883 | TCP listen port |
| `MQTT_BROKER_MAX_CLIENTS` | 8 | Max concurrent clients |
| `MQTT_BROKER_MAX_SUBSCRIPTIONS` | 32 | Max total subscriptions |

### Input Manager
| Parameter | Default | Description |
|-----------|---------|-------------|
| `INPUT_DEBOUNCE_MS` | 50 | Button debounce time (ms) |
| `INPUT_LONG_PRESS_MS` | 1000 | Long press threshold (ms) |
| `INPUT_ENCODER_USE_PCNT` | y | Use hardware PCNT peripheral |

## Input Configuration

Button and encoder pin assignments are defined in `main/main.c`:

```c
static const input_descriptor_t inputs[] = {
    { .id = 0, .type = INPUT_BUTTON,  .pin = { .button  = { .gpio = GPIO_NUM_12 } } },
    { .id = 1, .type = INPUT_BUTTON,  .pin = { .button  = { .gpio = GPIO_NUM_14 } } },
    { .id = 2, .type = INPUT_ENCODER, .pin = { .encoder = {
        .gpio_a = GPIO_NUM_25, .gpio_b = GPIO_NUM_26, .gpio_btn = GPIO_NUM_27
    } } },
};
input_manager_init(inputs, sizeof(inputs) / sizeof(inputs[0]));
```

## MQTT Topics

Input events are automatically published to MQTT:

| Topic | Payload | Event |
|-------|---------|-------|
| `train/input/button/<id>` | `press` | Button pressed |
| `train/input/button/<id>` | `long_press` | Button long-pressed |
| `train/input/button/<id>` | `release` | Button released |
| `train/input/encoder/<id>` | `cw:<delta>` | Encoder rotated clockwise |
| `train/input/encoder/<id>` | `ccw:<delta>` | Encoder rotated counter-clockwise |
| `train/input/encoder/<id>` | `click` | Encoder button clicked |

## Verification

```bash
# Connect to WiFi "TrainControl" (password: train1234)

# Subscribe to all topics
mosquitto_sub -h 192.168.4.1 -t "train/#"

# Publish a test message
mosquitto_pub -h 192.168.4.1 -t "train/test" -m "hello"
```

## Project Structure

```
train-control/
├── CMakeLists.txt
├── sdkconfig.defaults
├── README.md
├── main/
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild
│   └── main.c
└── components/
    ├── event_bus/
    ├── debug_log/
    ├── wifi_ap/
    ├── mqtt_broker/
    ├── input_manager/
    └── mongoose/
```
