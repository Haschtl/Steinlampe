# Quarzlampe

**An ESP32-powered lamp with ~60 PWM patterns, BLE/BT/USB control, a web app, a cross-platform desktop automation daemon, and a Home Assistant integration — all in one repo.**

[![CI - Build Firmware](https://github.com/Haschtl/Steinlampe/actions/workflows/ci.yml/badge.svg)](https://github.com/Haschtl/Steinlampe/actions/workflows/ci.yml)
[![CI - Build Desktop App](https://github.com/Haschtl/Steinlampe/actions/workflows/desktop-build.yml/badge.svg)](https://github.com/Haschtl/Steinlampe/actions/workflows/desktop-build.yml)
[![Deploy GitHub Pages](https://github.com/Haschtl/Steinlampe/actions/workflows/pages.yml/badge.svg)](https://github.com/Haschtl/Steinlampe/actions/workflows/pages.yml)
[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg)](LICENSE)

---

## What's in this repo

A single lamp, controlled from five directions, all kept in sync:

| Where | What |
| --- | --- |
| **Firmware** (`src/`, `include/`) | Arduino/ESP32 firmware: patterns, ramps, sensors, BLE/BT-Serial/USB command channels, OTA. |
| **Web app** (`frontend/`) | A React + Web Bluetooth control UI, deployed to GitHub Pages. Works in any Web-Bluetooth-capable browser (Chrome/Edge/Android) — **not Safari/iOS**, which doesn't implement Web Bluetooth at all. |
| **Desktop app** (`desktop/`) | A cross-platform tray daemon + companion console, automating the lamp from your computer: "turn on while my mic is active," "flash on a Teams notification," scheduled scenes, hotkeys, and more. |
| **Home Assistant integration** (`custom_components/quarzlampe/`) | A HACS-installable integration exposing the lamp as light/switch/sensor/number entities, talking to it over BLE or BT-Serial. |
| **Hardware** (`hardware/`) | KiCad schematic/PCB for the driver board. |

If you only want to turn the lamp on from your phone, start with the [web app](#web-app). If you want your desktop to control it automatically, see the [desktop app](#desktop-app). If you want it in Home Assistant, see [that section](#home-assistant-integration).

## Table of contents

- [Hardware variants](#hardware-variants)
- [Firmware](#firmware)
- [Web app](#web-app)
- [Desktop app](#desktop-app)
- [Home Assistant integration](#home-assistant-integration)
- [MIDI input mapping](#midi-input-mapping-optional)
- [Known limitations](#known-limitations)
- [Development](#development)
- [License](#license)

## Hardware variants

The firmware is one codebase, built differently per board via PlatformIO environments (`platformio.ini`):

| Environment | Chip | Notable hardware | Enabled features |
| --- | --- | --- | --- |
| `quarzlampe` | ESP32-WROOM | switch, touch lever | light sensor, music mode, touch dim |
| `quarzlampe_rd03` | ESP32-WROOM | + RD-03 mmWave radar | single-target presence/distance |
| `quarzlampe_rd03d` | ESP32-WROOM | + RD-03D mmWave radar | multi-target presence/distance |
| `desk_lamp` | ESP32-WROOM | external analog/digital input, potentiometer | no switch, inverted PWM |
| `on_air` | ESP32-S3 | switch only, no Bluetooth Classic radio | BLE-only (S3 has no BT Classic) |

> The RD-03/RD-03D radar protocol is implemented against community-documented framing and is **not yet verified against real hardware** from this repo's own development — see `src/presence_radar.cpp`'s doc comments before relying on it.

Core wiring (ESP32 Devkit, `quarzlampe` env):

- MOSFET gate: GPIO23 (PWM)
- Toggle switch: GPIO32 → GND (debounced, tap-to-cycle patterns)
- Capacitive touch: GPIO27/T7 via 1 MΩ to the metal lever (optional 2–5 MΩ bleeder)
- Optional: ambient light sensor (GPIO35), music/audio sensor (GPIO36/SVP), potentiometer, push button (GPIO34)

Other variants remap pins — see each environment's `build_flags` in `platformio.ini` and `include/pinout.h`.

## Firmware

### Build

```sh
pio run -e quarzlampe          # or quarzlampe_rd03 / quarzlampe_rd03d / desk_lamp / on_air
pio run -e quarzlampe -t upload
```

Requires [PlatformIO](https://platformio.org/). Feature flags live in `include/lamp_config.h`; runtime defaults (names, BLE UUIDs, thresholds) in `include/settings.h`.

Partition table: default is `min_spiffs.csv` (two OTA slots, small SPIFFS). If you don't need OTA and want more app headroom plus larger NVS, set `board_build.partitions = partitions/quarzlampe_no_ota_large_nvs.csv` (~3.4 MB app space, 64 KB NVS, no OTA slot).

### Features

- ~60 PWM patterns (constant, breathing, candle, campfire, KITT, thunderstorm, custom, music-reactive, and a family of "broken fixture" effects: flicker, fluorescent-starter, CRT-pop, loose-contact, brownout, strobe, …) plus 20+ ramp-easing curves.
- Wake/sleep fades, SOS, Morse code, configurable notification blink patterns.
- Touch dimming, switch tap-to-cycle through a configurable "quick list," BLE-proximity and (on radar variants) mmWave-distance based auto on/off.
- Ambient-light-driven brightness scaling, music-reactive patterns and clap detection, pattern speed/fade/margins, PWM gamma correction.
- A DSP filter chain (IIR smoothing, soft-clip, tremolo, sparkle, compressor, envelope follower, delay) layered on top of any pattern.
- Six save/load profile slots, config export/import, factory reset (also triggerable via 5 switch toggles or poti swings within 1s of boot, without needing a command channel).
- BLE-MIDI / BT-MIDI receive for brightness/mode control from an external MIDI controller.
- OTA firmware updates over BLE/BT-Serial/USB (chunked, MD5-verified).

### Command reference (BLE / BT-Serial / USB)

Plain newline-terminated text commands, identical over all three transports:

| Category | Commands |
| --- | --- |
| Power & patterns | `on` / `off` / `toggle`, `mode <n>`, `next`, `prev`, `list` |
| Brightness | `bri <0-100>`, `bri min <0-1>`, `bri max <0-1>` |
| Ramps | `ramp <ms>`, `ramp on\|off <ms>`, `ramp ease on\|off <curve> [power]`, `ramp ambient <0-5>` |
| Pattern tuning | `pat scale <0.1-5>`, `pat fade on\|off\|amt <v>`, `pat margin <low> <high>`, `pat reactive on\|off\|list\|clear\|<n> on\|off` |
| Sleep/Wake | `wake [soft] [mode=N] [bri=XX] <sec>`, `wake stop`, `sleep [min]`, `sleep stop` |
| Auto/Demo | `auto on\|off`, `demo [sec]`, `demo off` |
| Sensors | `touchdim on\|off`, `touch tune <on> <off>`, `light on\|off\|calib\|gain\|alpha\|clamp …` |
| Music/clap | `music sens\|smooth\|auto\|thr <v>`, `clap on\|off\|thr <v>\|cool <ms>\|train` |
| Filters | `filter iir\|clip\|trem\|spark\|comp\|env\|delay <on/off> [params…]` |
| Custom/notify | `custom v1,v2,...`, `custom step <ms>`, `notify d1 d2 ... [fade=ms]`, `morse <text>` |
| Presence | `presence_ble on\|off`, `presence_ble add\|del\|set <MAC>\|clear`, `presence_ble grace <ms>` |
| Radar (RD-03 variants) | `radar on\|off`, `radar dim\|motion\|offdist <params>`, `radar debug on\|off` |
| Profiles/quick | `profile save\|load\|show <1-6>`, `quick <csv>\|default` |
| Trust/pairing | `trust list\|ble\|bt add/del <MAC>` |
| OTA | `ota begin <size> <md5> <version>`, `ota chunk <base64>`, `ota end`, `ota abort`, `ota status` |
| Config | `cfg export`, `cfg import key=val ...`, `factory`, `status`, `status raw`, `help` |

BT-Serial pairing from an unrecognized device requires a physical confirmation within ~20s (toggle the switch or move the potentiometer) before it's trusted.

BLE UUIDs (default, see `include/settings.h`):

- Service: `d94d86d7-1eaf-47a4-9d1e-7a90bf34e66b`
- Command (Write / Write-No-Response): `4bb5047d-0d8b-4c5e-81cd-6fb5c0d1d1f7`
- Status (Read / Notify): `c5ad78b6-9b77-4a96-9a42-8e6e9a40c123`

## Web app

React + Vite + Tailwind, in `frontend/`. Connects directly to the lamp over Web Bluetooth — no backend, no account, nothing in between.

```sh
cd frontend
pnpm install
pnpm run dev      # local dev server
pnpm run build    # production build -> frontend/dist
```

Auto-deployed to GitHub Pages on push to `main` (`.github/workflows/pages.yml`), published at `https://haschtl.github.io/Steinlampe/`.

**Browser support:** anything with Web Bluetooth over HTTPS or `localhost` — Chrome, Edge, Chrome for Android. **Safari and every iOS browser are unsupported**, since Apple has never implemented the Web Bluetooth API; there is currently no way to control the lamp from an iPhone through this app.

## Desktop app

A Rust workspace (`desktop/`) with two pieces:

- **`quarzlampe-tray`** — an always-running, windowless tray daemon. Owns the BLE connection(s), evaluates automation rules, and shows live per-lamp status in the tray icon.
- **`quarzlampe-console`** — an ordinary window, opened on demand from the tray icon, for manual control and editing rules. Talks to the daemon over a local loopback IPC connection; never touches BLE directly.

### Automation rules

A rule is `when <trigger(s)> → do <action>, on <lamp(s)>`. A rule can have multiple triggers (fires on *any* of them):

| Trigger | Fires when… | Platform support |
| --- | --- | --- |
| Mic active / inactive | the system microphone starts/stops being used | Linux (verified), Windows/macOS (implemented, unverified) |
| Camera active / inactive | the webcam starts/stops being used | Linux (verified), Windows/macOS (implemented, unverified) |
| Daemon starts | on every daemon boot | all |
| Before sleep / before shutdown | the OS is about to suspend/shut down | all (native on every platform) |
| Screen locks / unlocks | the session is locked/unlocked | all (native on every platform) |
| A notification appears | a desktop notification arrives, optionally filtered by sender app | Linux only — no supported cross-platform API exists for this |
| An app starts | a process with a given name launches | all |
| A hotkey is pressed | a configured global keyboard shortcut fires | all except Wayland (no global-hotkey API there) |
| At a specific time | daily, at a configured HH:MM | all |
| A webhook fires | an HTTP `GET` hits the daemon's local, token-protected endpoint | all |

Actions: turn on/off, set brightness/mode, load a profile, play a notify/Morse pattern, start a wake fade.

A **webhook trigger** gets a fixed local URL (`http://127.0.0.1:47622/webhook/<id>?token=<token>`, shown in the console) that any local tool — a Stream Deck "HTTP Request" action, a shell script, Home Assistant's `rest_command` — can call to fire the rule. Loopback-only; the port stays fixed across restarts so the URL never needs updating.

A tray menu toggle lets you pause all automations at once without disabling each rule individually.

### Install

- **Linux**: an AppImage (built by `desktop-build.yml`), or build from source (below).
- **Windows**: an MSI installer; the two executables are signed free of charge via the [SignPath Foundation](https://signpath.io/) for open-source projects (see `desktop/CODE_SIGNING_POLICY.md`).
- **macOS**: a DMG.

Grab the latest build from the [Actions](https://github.com/Haschtl/Steinlampe/actions/workflows/desktop-build.yml) artifacts, or a tagged [Release](https://github.com/Haschtl/Steinlampe/releases) once one exists.

### Build from source

```sh
cd desktop
cargo build --release --workspace
# binaries: target/release/quarzlampe-tray[.exe], target/release/quarzlampe-console[.exe]
```

Requires a Rust toolchain. On Linux, the tray icon uses the KSNI (StatusNotifierItem) D-Bus backend — no GTK/libappindicator system packages needed.

## Home Assistant integration

- Install via HACS: add this repository as a custom repository (type: *Integration*), then install **Quarzlampe**. Files live in `custom_components/quarzlampe/`.
- The config flow scans for nearby lamps advertising the Quarzlampe BLE service UUID, so you rarely need to type a MAC address by hand.
- **Transport**: BLE (preferred on Home Assistant OS) or BT-Serial (e.g. `/dev/rfcomm0` at 115200 baud).
  - Classic Bluetooth is usually *not* preconfigured on HA OS/Container. BT-Serial needs a supported USB BT dongle, an `rfcomm` binding (e.g. via the SSH add-on: `rfcomm bind 0 <mac> 1`), and that device file passed into the container (udev rule + add-on config). Without that, BT-Serial fails with "No such file or directory: /dev/rfcomm0".
- **Entities**: `light.quarzlampe_lamp` (brightness + pattern as effect list); switches for auto-cycle, touch dim, presence, ambient light, clap; numbers for brightness min/max, ramp durations, ambient factor, idle-off, pattern speed/fade, PWM gamma; sensors for pattern index/name, light level, music level, touch delta, presence state, BLE availability; buttons for next/prev pattern, status refresh, sync-to-switch.
- **Services** (`quarzlampe.*`): `send_command` (raw CLI passthrough), `wake`, `sleep`, `notify`, `morse`, `presence_set`, `custom_pattern`, `quick_modes`, `config_import`, `config_export`, `demo`.

## MIDI input mapping (optional)

Receive-only, over BLE-MIDI or BT-MIDI:

| Message | Action |
| --- | --- |
| CC 7 | Master brightness, 0–100% |
| CC 20 | Pattern/mode 1–8 (scaled) |
| Note 59 (B3) | Toggle lamp |
| Note 60 (C4) | Previous pattern |
| Note 62 (D4) | Next pattern |
| Notes 70–77 (F#4–C5) | Select pattern/mode 1–8 |

## Known limitations

- **BLE has no pairing-confirmation gate.** BT-Serial requires a physical confirmation (switch toggle or poti move) before accepting commands from an unrecognized device (`btPairPending` in `src/comms.cpp`). BLE has no equivalent — an unknown BLE client connecting within the boot grace window can send commands immediately. Most relevant for BLE-only hardware like `on_air` (ESP32-S3, no Classic BT radio).
- **The BLE radio never sleeps.** BT-Serial has a boot/idle timeout (`maybeSleepBtSerial`) that powers down the Classic BT radio after inactivity; BLE has no equivalent and stays advertising/connectable indefinitely. Intentional for now, but worth knowing if power draw matters for your build.
- **The desktop app's Windows/macOS event-loop integration (tray clicks, hotkeys, sleep/shutdown/screen-lock triggers) is new and compiles in CI but is not yet confirmed working on real hardware** — only Linux has been exercised live. If something doesn't fire as expected on Windows/macOS, please open an issue.
- **No iOS control path.** The web app needs Web Bluetooth, which no iOS browser supports; Home Assistant or the desktop app are the only current options if your phone is an iPhone.

## Development

- **Firmware**: PlatformIO, see [Firmware](#firmware) above. `src/`/`include/` only, no Arduino IDE project files.
- **Desktop app**: a Cargo workspace (`desktop/core`, `desktop/app`, `desktop/console`) — see `desktop/PLAN.md` for the architecture history and design decisions behind the daemon/console split.
- **Frontend**: pnpm workspace, Vite + React + Tailwind, see [Web app](#web-app) above.
- **Home Assistant integration**: a standard HACS custom component, Python, in `custom_components/quarzlampe/`.
- CI (`.github/workflows/`) builds firmware for every environment, the desktop app for Linux/Windows/macOS, and deploys the web app + firmware OTA bundles to GitHub Pages — all on every push to `main`.

## License

GPL-3.0-or-later — see [LICENSE](LICENSE). The desktop app's Windows binaries are signed free of charge under a [SignPath](https://signpath.io/) open-source code-signing certificate for this project; see [desktop/CODE_SIGNING_POLICY.md](desktop/CODE_SIGNING_POLICY.md).
