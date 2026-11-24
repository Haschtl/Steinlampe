# BLE Mesh feasibility spike

Throwaway, not part of the real quarzlampe firmware. Answers one question:
does NimBLE (not Bluedroid) + a BLE Mesh Node with a Generic OnOff Server
model actually provision and respond on real `upesy_wroom` (classic ESP32)
hardware - the same chip `quarzlampe`/`desk_lamp` run on?

Background/why this exists at all: see the ble-mesh plan this was scaffolded
from. Short version - the official precompiled `framework = arduino`-only
distribution doesn't ship the native NimBLE host for the classic ESP32 chip,
only Bluedroid; this uses the mixed `arduino, espidf` PlatformIO framework
instead, which builds the real ESP-IDF from source against the
`sdkconfig.defaults` here, where `CONFIG_BT_NIMBLE_ENABLED` for the classic
ESP32 target is a combination `esp_ble_mesh`'s own source code supports -
just not one Espressif also ships precompiled for the fast path.

## Status: builds and links successfully

A real build was run (on the actual dev machine this project lives on).
Fixed along the way, in order:

1. `framework = arduino, espidf` initially failed with "Missing Arduino
   framework directory", then with a missing `esp_adc_cal` IDF component
   that has no managed-component replacement - both because unqualified
   `platform = espressif32` resolved to the third-party "pioarduino" fork
   on this machine (version string "55.3.35", packages from
   `github.com/pioarduino/registry`), which had an unrelated, already-cached
   arduino-esp32 dev snapshot (3.20017.241212) lying around that doesn't
   match what that platform's own manifest actually wants (3.3.5).
2. Pinning the exact 3.3.5 tarball got further, but then hit a second,
   genuinely reproducible bug in that same "pioarduino" fork: its
   `tool-scons` package would install correctly (confirmed via
   PlatformIO's own `ToolPackageManager` API - present on disk, correct
   module included) and then get silently deleted again by something in
   that fork's own build orchestration before the final SCons-driven link
   step ran, every single time, regardless of which `pio` binary/venv ran
   the build. **Fixed by switching to the official `platformio/espressif32`
   platform instead** (`platform = platformio/espressif32@^8.0.0` in
   `platformio.ini`) - that whole class of failure disappeared outright.
3. The IDF Component Manager then correctly auto-resolved 27 dependencies
   (arduino-esp32 3.3.5's own requirements) once `src/idf_component.yml`
   declared `espressif/mdns` - arduino-esp32 requires mDNS unconditionally
   even though this sketch never touches WiFi, and recent ESP-IDF extracted
   mdns out of the core component set.
4. CMake configuration then failed on `esp32-arduino requires
   CONFIG_FREERTOS_HZ=1000 (currently 100)`, then on a linker error
   (`undefined reference to 'app_main'`) once it got past that - both are
   `sdkconfig.defaults` entries arduino-esp32 itself requires
   (`CONFIG_FREERTOS_HZ=1000`, `CONFIG_AUTOSTART_ARDUINO=y` - the latter is
   what generates the `app_main()` that calls into Arduino's `setup()`/
   `loop()`; both are in `sdkconfig.defaults` now, with comments).

With all of that: **`pio run -e upesy_wroom` succeeds** - `main.cpp`
compiled cleanly against the real `esp_ble_mesh`/NimBLE API (not just
checked by hand against headers anymore), linked, and produced
`firmware.bin`. RAM 12% (39KB/320KB), Flash 67% (698KB/1MB app partition).

Not yet verified: flashing to a real board and the actual provisioning/
OnOff roundtrip (see Testing below) - that needs real hardware in hand.
The part most likely to still need attention if something's off at that
point: `bluetoothInit()`'s `nimble_port_init()` /
`nimble_port_freertos_init()` controller/host bring-up sequence, and the
fixed ~200ms delay before `bleMeshStart()` papering over a real
init-ordering dependency - both noted in `main.cpp`'s comments as the
least-tested part of the actual Mesh/NimBLE logic, as opposed to the
build plumbing above.

## Building

```sh
cd spikes/ble-mesh
pio run -e upesy_wroom -t upload
pio device monitor
```

First build downloads the full ESP-IDF toolchain (a separate, multi-GB
`framework-espidf` package, plus ~27 managed components arduino-esp32
itself needs - nothing like the lightweight
`framework-arduinoespressif32-libs` the main firmware uses) - expect that to
take a while the first time.

## Testing once it builds and flashes

1. Watch the serial monitor for `"BLE Mesh spike booting"` then `"mesh stack
   initialized"` and `"provisioning enabled"`.
2. Open a BLE Mesh provisioner app (e.g. nRF Mesh on Android/iOS), scan for
   unprovisioned devices, and provision this one.
3. In the app, bind an AppKey to the node's Generic OnOff Server model
   (standard nRF Mesh flow: add the node to a group/network, create an
   AppKey if one doesn't exist, bind it to the model).
4. Send a Generic OnOff Set (toggle) from the app - the serial monitor
   should print `"Generic OnOff Set -> 1"` / `"-> 0"`.

If step 2-4 work: the core premise holds, and it's worth moving to the real
decision gate (see the plan - does `comms.cpp` need porting off
NimBLE-Arduino onto the native API this spike uses, project restructuring,
which models to wire to the pattern engine). If provisioning or the OnOff
roundtrip never works even after fixing compile errors, that's the answer
too - report back what specifically failed and where.
