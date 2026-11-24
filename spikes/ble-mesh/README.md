# BLE Mesh feasibility spike

Throwaway, not part of the real quarzlampe firmware. Answers two questions:

1. Does NimBLE (not Bluedroid) + a BLE Mesh Node with a Generic OnOff
   Server model actually provision and respond on real hardware - both
   chips the main firmware targets (`upesy_wroom`/classic ESP32, same as
   `quarzlampe`/`quarzlampe_rd03(d)`/`desk_lamp`; and `on_air`'s ESP32-S3)?
2. Can a lamp receive its own existing text command syntax ("mode 3",
   "bri 50", ...) over Mesh too, via a Vendor Model, the same way it
   already does over the custom BLE/BT/USB GATT protocol in
   `comms.cpp`? That's what would let one lamp's switch/poti input - via
   Mesh group publish, configured once per lamp through a provisioner
   app - drive other lamps using our own command language, not just
   generic on/off, without building a from-scratch Mesh Configuration
   Client into our own app (a separate, bigger effort if we ever want
   the group/publish wiring itself done from our own GUI instead of a
   generic provisioner app's).

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

With all of that: **`pio run -e upesy_wroom` and `-e on_air` both
succeed** - `main.cpp` (Config Server + Generic OnOff Server + a Vendor
Model carrying raw text commands) compiled cleanly against the real
`esp_ble_mesh`/NimBLE API (not just checked by hand against headers
anymore) for both chips, linked, and produced `firmware.bin`.
`upesy_wroom`: RAM 12% (40KB/320KB), Flash 67% (700KB/1MB app partition).
`on_air` (ESP32-S3): RAM 10.5% (34KB/320KB), Flash 69% (720KB/1MB app
partition).

One real config gap caught before it would have wasted a hardware test:
the first `sdkconfig.defaults` only enabled `CONFIG_BLE_MESH_PB_ADV`
(advertising-bearer provisioning), reasoning it was "enough for a
single-node spike". That's wrong for testing with a phone - provisioner
apps (nRF Mesh et al.) provision over **PB-GATT** almost always, since
phone OS BLE stacks don't give apps the raw access to mesh advertising
PDUs PB-ADV provisioning needs on the provisioner's side. Without
`CONFIG_BLE_MESH_PB_GATT=y` (now added, alongside `CONFIG_BLE_MESH_PROXY=y`
for the phone's ongoing GATT connection after provisioning - AppKey
binding, model pub/sub config), this node would have been invisible to
nRF Mesh's provisioning flow despite advertising correctly. Both envs
rebuilt clean with the fix.

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
pio run -e upesy_wroom -t upload   # or -e on_air for the ESP32-S3
pio device monitor
```

`board = upesy_wroom` is a generic ESP32-WROOM-32 pin/flash definition,
not a requirement for that specific manufacturer's board - flashing this
onto any of the actual lamps (`quarzlampe`, `quarzlampe_rd03(d)`,
`desk_lamp`) works the same way, not just a bare dev board.

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
3. In the app, bind an AppKey to the node's two models (standard nRF Mesh
   flow: add the node to a group/network, create an AppKey if one doesn't
   exist, bind it to each model that should use it - the Generic OnOff
   Server, and the unnamed/unknown "Vendor Model" the app will likely show
   without a friendly name since it doesn't recognize our company
   ID/opcode).
4. Send a Generic OnOff Set (toggle) from the app - the serial monitor
   should print `"Generic OnOff Set -> 1"` / `"-> 0"`.
5. Exercising the Vendor Model specifically is the part most likely to
   need a different tool than nRF Mesh's own GUI - that app is built
   around model types it recognizes, and may not expose a "send this raw
   opcode/payload" control for an unknown vendor model. If it doesn't,
   either a second ESP32 running a small Vendor Model *client* sketch
   (sends `VENDOR_OP_CMD` with a test string to this node's address) or a
   lower-level tool (e.g. a Python mesh library, or nRF Mesh's "proxy
   filter"/raw-PDU features if your version has them) would be needed -
   untried, flag what you find.

If steps 2-4 (and ideally 5) work: the core premise holds, and it's worth
moving to the real decision gate (see the plan - does `comms.cpp` need
porting off NimBLE-Arduino onto the native API this spike uses, project
restructuring, which models to wire to the pattern engine, and whether the
Vendor Model route is the right one for lamp-to-lamp relaying vs. sticking
to generic models). If provisioning or either roundtrip never works even
after fixing compile errors, that's the answer too - report back what
specifically failed and where.
