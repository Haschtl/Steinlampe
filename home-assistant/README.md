# Home Assistant Integration for Quarzlampe (BLE)

Custom HACS component to control the ESP32 Quarzlampe controller over BLE text commands. The integration mirrors the web UI in `frontend/` (patterns/effects, brightness, ramps, presence, sensors, clap, custom patterns) and keeps the BLE transport encapsulated.

## Installation (HACS custom repo)

1. Copy `custom_components/quarzlampe` into your Home Assistant `config/custom_components/` folder, or add this repository as a custom HACS source and install the integration.
2. Restart Home Assistant.
3. Add the *Quarzlampe BLE* integration via *Settings → Devices & Services* and enter the lamp’s BLE MAC address (default name `Quarzlampe`). Discovery is filtered by the lamp service UUID `d94d86d7-1eaf-47a4-9d1e-7a90bf34e66b`.

## Entities

- `light.quarzlampe_lamp`: on/off, brightness, and full pattern list as effects (labels match `frontend/src/data/patterns.ts`).
- Switches: auto-cycle, touch dim, presence binding, ambient light sensor, clap detection.
- Numbers: brightness cap/min/max, ramp on/off, ramp ambient factor, idle-off minutes, pattern speed, pattern fade amount, PWM gamma.
- Sensors: pattern index/name, light raw, music level, touch delta, presence state.
- Buttons: next/previous pattern, status refresh, sync switch.

## Services (`quarzlampe.*`)

- `send_command`: send any raw CLI command over BLE.
- `wake`, `sleep`: wake/sleep fades (with optional mode/brightness/soft).
- `notify`: custom blink sequences (ms, optional fade & repeat).
- `morse`: blink arbitrary text.
- `presence_set`: bind/clear presence MAC (default binds the currently connected client).
- `custom_pattern`: upload custom pattern CSV and step time.
- `quick_modes`: configure quick tap list (mode numbers or profile slots).
- `config_import` / `config_export`: apply/collect cfg import/export strings.
- `demo`: start/stop demo cycling.

## BLE details (for debugging)

- Service UUID: `d94d86d7-1eaf-47a4-9d1e-7a90bf34e66b`
- Command characteristic (Write/WriteNR): `4bb5047d-0d8b-4c5e-81cd-6fb5c0d1d1f7` — send plain text commands terminated by `\n`.
- Status characteristic (Read/Notify): `c5ad78b6-9b77-4a96-9a42-8e6e9a40c123` — emits `STATUS|...` and `SENSORS|...` lines plus normal log lines.

The integration arms feedback by sending `status` on connect and then keeps the status snapshot fresh every ~30 seconds or after commands.

