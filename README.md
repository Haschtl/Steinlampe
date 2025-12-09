# Quarzlampe – ESP32 PWM Lamp & Home Assistant Integration

ESP32 firmware for a stone lamp with rich PWM patterns, touch/knob/switch control, and BLE/BT command channels. Includes a Web BLE UI (React) and a HACS-ready Home Assistant integration (BLE or BT-Serial transport).

## Quick Start

- **Firmware build:** `pio run -e upesy_wroom` (PlatformIO required; flags in `include/lamp_config.h`).
- **Web UI:** `cd frontend && npm install && npm run dev` (or `npm run build`).
- **Home Assistant:** Copy `custom_components/quarzlampe` (HACS) into your HA `config/custom_components/`, restart HA, add the *Quarzlampe* integration (choose BLE or BT-Serial).

## Hardware (ESP32 Devkit)

- MOSFET gate: GPIO23 (PWM)
- Toggle switch: GPIO32 → GND (debounced, tap-to-cycle)
- Capacitive touch: GPIO27/T7 via 1 MΩ to metal lever (optional 2–5 MΩ bleeder)
- Optional: Ambient light sensor GPIO35; Music/Audio sensor GPIO36/SVP
- Optional extras: Poti (ADC), Push button (GPIO34)

## Features (firmware)

- ~50 PWM patterns (Konstant, Atmung, Kerze, Lagerfeuer, KITT, Gewitter, Custom, Musik …)
- Wake/sleep fades, SOS, alert, notify, Morse
- Touch dimming, quick-list on switch taps, presence-based auto on/off
- Optional light sensor modulation, music-reactive patterns, clap control
- Pattern speed/fade/margins, gamma, ramp easing and durations
- Profiles (save/load 1–3), config export/import, factory reset
- Optional BLE/BT-MIDI RX mapping for brightness/mode and toggles

## Command Cheatsheet (BLE/BT/USB)

Send plain text lines (newline-terminated) over USB serial, BLE, or Classic BT SPP:

- Power & patterns: `on|off|toggle`, `mode <n>`, `next`, `prev`, `list`
- Brightness: `bri <0..100>`, `bri min <0..1>`, `bri max <0..1>`, `bri cap <0..100>`
- Ramps: `ramp on|off <ms>`, `ramp <ms>`, `ramp ease on|off <ease> [pow]`, `ramp ambient <0..5>`
- Pattern tuning: `pat scale <0.1-5>`, `pat fade on|off|amt <v>`, `pat margin <low> <high>`
- Sleep/Wake: `wake [soft] [mode=N] [bri=XX] <sec>`, `wake stop`, `sleep [min]`, `sleep stop`
- Auto/demo: `auto on|off`, `demo [seconds]`, `demo off`
- Sensors: `touchdim on|off`, `touch tune <on> <off>`, `light on|off/calib`, `light gain|alpha|clamp …`
- Music/clap: `music sens|smooth|auto on|off|thr <v>`, `clap on|off`, `clap thr <v>`, `clap cool <ms>`
- Custom/notify: `custom v1,v2,...`, `custom step <ms>`, `notify d1 d2 ... [fade=ms]`, `morse <text>`
- Presence: `presence on|off`, `presence set <MAC>|me`, `presence clear`, `presence grace <ms>`
- Profiles/quick: `profile save|load <1-3>`, `quick 1,5,7,...`
- Config: `cfg export`, `cfg import key=val ...`, `factory`, `status`, `help`

BLE UUIDs (default):
- Service: `d94d86d7-1eaf-47a4-9d1e-7a90bf34e66b`
- Command (Write/NR/Notify): `4bb5047d-0d8b-4c5e-81cd-6fb5c0d1d1f7`
- Status (Read/Notify): `c5ad78b6-9b77-4a96-9a42-8e6e9a40c123`

## Web BLE UI (React/Vite)

- Lives in `frontend/` (Tailwind, PWA-ready). `npm run dev` for local, `npm run build` for prod.
- GitHub Pages deployment via `.github/workflows/pages.yml` builds `frontend/dist` and publishes to `https://haschtl.github.io/<repo>/`.
- Use a Web-Bluetooth-capable browser (Chrome/Edge/Android) over HTTPS or `localhost`, click **Connect**, control lamp. BLE command coverage is being expanded.

## Home Assistant (HACS) Integration

- Paths: `custom_components/quarzlampe`, metadata `hacs.json`.
- Transport choice: BLE (enter lamp MAC) or BT-Serial (e.g., `/dev/rfcomm0`, 115200 baud).
- Entities:
  - `light.quarzlampe_lamp` with brightness and effect list (patterns).
  - Switches: auto-cycle, touch dim, presence, ambient light sensor, clap.
  - Numbers: brightness cap/min/max, ramp on/off, ambient factor, idle-off, pattern speed, pattern fade, PWM gamma.
  - Sensors: pattern index/name, light raw, music level, touch delta, presence state.
  - Buttons: next/prev pattern, status refresh, sync switch.
- Services (`quarzlampe.*`):
  - `send_command` (raw CLI over configured transport), `wake`, `sleep`, `notify`, `morse`,
    `presence_set`, `custom_pattern`, `quick_modes`, `config_import`, `config_export`, `demo`.
- Debug BLE details above; integration sends `status` on connect and refreshes ~30s or after commands.

## MIDI Mapping (optional BLE/BT-MIDI RX)

| Message               | Action                      |
|-----------------------|-----------------------------|
| CC 7                  | Master brightness 0–100 %   |
| CC 20                 | Pattern/mode 1–8 (scaled)   |
| Note 59 (B3)          | Toggle lamp                 |
| Note 60 (C4)          | Previous pattern            |
| Note 62 (D4)          | Next pattern                |
| Notes 70–77 (F#4..C5) | Select pattern/mode 1–8     |

## Build Notes

- PlatformIO env: `upesy_wroom`. Adjust pins/features in `include/lamp_config.h`.
- Default names/UUIDs and runtime defaults in `include/settings.h`.
- Classic BT SPP name from `Settings::BT_SERIAL_NAME` (default `Quarzlampe-SPP`).

