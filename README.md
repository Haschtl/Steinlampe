# Quarzlampe PWM Demo

This firmware drives a stone-lamp via ESP32 PWM patterns. It offers switch control, capacitive dimming, and optional BLE/BT interfaces.

## Hardware Overview

```
                +------------------------+
                |        ESP32 Devkit    |
                |                        |
                |   (GPIO23) PWM  ----> MOSFET Gate -> LED Driver 24V
                |   (GPIO32) Switch ----> old toggle (to GND)
                |   (GPIO27/T7) Touch --> metal lever (via 1MΩ)   
                |                        |
                |   VIN/USB / GND ------> supply + reference
                +------------------------+

Old toggle switch: one pole to GPIO32, other pole to GND (internal pull-up).
Touch: lever isolated from switch contact, wired via 1MΩ to GPIO27; optional 2-5MΩ bleeder to GND.
.

```

## Features

- Pattern sequencer (Konstant, Atmung, Pulsierend, Funkeln, Kerze, Lagerfeuer, Stufen, Zwinkern)
- Wake fade triggered via BLE/serial `wake <seconds>`
- Classic BT serial + BLE command channel (configurable via `ENABLE_*` flags).
  For Android automations you can use the [Tasker BLE Writer](https://github.com/Haschtl/Tasker-Ble-Writer) profile to send commands like `wake 180`.
- Physical switch: on/off + tap-to-cycle; capacitive hold-to-dim

### Configuration

- Compile-time flags live in `include/lamp_config.h` (`ENABLE_BLE`, `ENABLE_BT_SERIAL`).
- User-facing defaults (names, wake duration, brightness) are centralized in `include/settings.h`.

## Command Reference

All commands can be sent via USB serial, BLE, or classic BT serial:

| Command             | Description                                                |
|---------------------|------------------------------------------------------------|
| `list`              | Print all available patterns                               |
| `mode <n>`          | Immediately switch to pattern number `n`                   |
| `next` / `prev`     | Cycle to the next or previous pattern                      |
| `on` / `off` / `toggle` | Switch lamp on/off like the hardware toggle               |
| `auto on\|off`       | Enable/disable automatic pattern cycling                   |
| `bri <0..100>`      | Set master brightness in percent                           |
| `wake <seconds>`    | Start a sunrise fade over the specified duration           |
| `wake stop`         | Abort an active wake fade                                  |
| `sleep [minutes]`   | Fade down to off over given minutes (default 15)           |
| `sleep stop`        | Abort an active sleep fade                                 |
| `touch tune <on> <off>` | Adjust touch thresholds (on>off>0)                       |
| `touch`             | Print raw touch readings for threshold calibration         |
| `calibrate`         | Re-measure touch baseline                                  |
| `presence on|off`   | Enable/disable auto-off when registered device disconnects |
| `presence set <MAC>`/`presence set me`/`presence clear` | Bind connected device or explicit MAC / clear |
| `status`            | Show current pattern, brightness and wake/auto state       |
| `help`              | Display the quick command overview                         |

Tasks or mobile workflows (e.g., Tasker) can simply send these ASCII commands.

## Build

```sh
pio run -e upesy_wroom
```

*Note: PlatformIO must be installed locally. In this environment `pio` is not available.*
