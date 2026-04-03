# Train Control MQTT Protocol

## Topology

```
ESP32 (WiFi AP + MQTT Broker)
  +-- Physical controls (buttons, encoder)
  |         | event_bus
  +-- train_command component -- forms commands
  |         | mqtt_broker_publish_internal()
  +-- MQTT Broker ----> Train (WiFi MQTT client)
                   <--- (status)
```

## Topic Hierarchy

| Topic | Direction | Description |
|---|---|---|
| `train/<id>/cmd` | Controller -> Train | Control commands |
| `train/<id>/state` | Train -> Controller | Current state (retained) |
| `train/<id>/ping` | Controller -> Train | Status request |
| `train/<id>/pong` | Train -> Controller | Ping response |
| `train/announce` | Train -> Controller | Train announces connection |

`<id>` -- train identifier (e.g. `loco1`), allowing multi-train control.

## Payload Format -- JSON

Single `cmd` topic with `action` field instead of multiple subtopics -- simpler subscription, less code on the train side.

### Commands (Controller -> Train)

#### Set Speed

```json
{"action":"speed","value":75}
```

`value`: 0--100 (% of max speed). 0 = stop.

#### Smooth Stop

```json
{"action":"stop"}
```

Train decelerates to stop with current inertia settings.

#### Emergency Stop

```json
{"action":"estop"}
```

Immediate motor cutoff. Resets speed to 0.

#### Direction

```json
{"action":"direction","value":"forward"}
```

`value`: `"forward"` | `"reverse"`. Only applied when speed is 0 (safety).

#### Lights

```json
{"action":"light","target":"head","value":true}
```

`target`: `"head"` | `"tail"` | `"cabin"` | `"all"`.
`value`: `true` / `false`.

#### Horn

```json
{"action":"horn","value":"short"}
```

`value`: `"short"` | `"long"` | `"off"`.

#### Configuration

```json
{"action":"config","param":"accel_rate","value":50}
```

Parameters: `accel_rate` (acceleration), `decel_rate` (deceleration), `max_speed` (speed limit).

### State (Train -> Controller)

Published to `train/<id>/state` with **retained** flag so the controller sees current state immediately upon connection:

```json
{
  "speed": 75,
  "direction": "forward",
  "lights": {"head": true, "tail": false, "cabin": false},
  "battery": 82,
  "error": null,
  "uptime": 3600
}
```

On error:

```json
{
  "speed": 0,
  "direction": "forward",
  "lights": {"head": true, "tail": false, "cabin": false},
  "battery": 15,
  "error": "low_battery",
  "uptime": 7200
}
```

### Discovery (Train -> Controller)

On connection the train publishes to `train/announce` (retained):

```json
{"id": "loco1", "model": "steam", "fw": "1.0.3"}
```

LWT (Last Will and Testament) on the same topic:

```json
{"id": "loco1", "model": "steam", "fw": "1.0.3", "online": false}
```

## Physical Input Mapping

| Input | Event | Command |
|---|---|---|
| Encoder rotate CW | `TRAIN_EVT_ENCODER_CW` | `speed` +5 (increment) |
| Encoder rotate CCW | `TRAIN_EVT_ENCODER_CCW` | `speed` -5 (decrement) |
| Encoder click | `TRAIN_EVT_ENCODER_CLICK` | `direction` toggle |
| Button 0 press | `TRAIN_EVT_BUTTON_PRESS` id=0 | `stop` |
| Button 0 long press | `TRAIN_EVT_BUTTON_LONG_PRESS` id=0 | `estop` |
| Button 1 press | `TRAIN_EVT_BUTTON_PRESS` id=1 | `horn` short |
| Button 1 long press | `TRAIN_EVT_BUTTON_LONG_PRESS` id=1 | `light` toggle all |
