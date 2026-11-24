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

## Status: configures clean, blocked on a broken local SCons install

A real build was attempted (on a sandboxed CI-like machine, not real dev
hardware). Progress, in order:

1. `framework = arduino, espidf` initially failed with "Missing Arduino
   framework directory" - this platform's manifest wants arduino-esp32
   3.3.5 specifically, but an unrelated, already-cached dev snapshot
   (3.20017.241212) got resolved instead. Fixed by pinning the exact
   3.3.5 tarball via `platform_packages` in `platformio.ini` (see its
   comment) - that dev snapshot, for reference, turned out separately to
   require `esp_adc_cal`, an IDF component with no managed-component
   replacement, so it wouldn't have worked anyway.
2. The IDF Component Manager then correctly auto-resolved 27 dependencies
   (arduino-esp32 3.3.5's own requirements) once `src/idf_component.yml`
   declared `espressif/mdns` - arduino-esp32 requires mDNS unconditionally
   even though this sketch never touches WiFi; recent ESP-IDF extracted
   mdns out of the core component set, so it needs explicit declaration
   here to be fetched from the registry.
3. CMake configuration then failed on `esp32-arduino requires
   CONFIG_FREERTOS_HZ=1000 (currently 100)` - added to
   `sdkconfig.defaults` (now there, with a comment).
4. With that, **CMake configuration completes successfully** - every
   dependency (including `CONFIG_BT_NIMBLE_ENABLED` + BLE Mesh) resolved
   and the build graph is built. It then reaches the actual SCons build
   invocation and fails there with `ModuleNotFoundError: No module named
   'SCons.Tool.FortranCommon'`. Dug further: this is a genuine upstream
   packaging bug, not a local environment problem in the usual sense -
   `pio run` always re-fetches its own pinned `tool-scons@4.40801.0`/
   `@4.41101.0` from the `pioarduino` registry
   (`github.com/pioarduino/registry`) for the actual build step,
   regardless of what's otherwise installed; that specific distributed
   package is missing `SCons/Tool/FortranCommon.py`. Confirmed a *correct*
   SCons install exists and works fine (manually `pip install scons` into
   PlatformIO's ESP-IDF-specific Python venv, `FortranCommon` imports
   there without issue) - `pio run` just doesn't use it for this step, it
   keeps reaching for its own broken copy. Not fixable by installing
   things locally; would need either a different/patched `tool-scons`
   release from that registry, or forcing PlatformIO to use a system
   SCons instead (untried - out of scope for a spike instead of the
   actual question this was meant to answer).

**`src/main.cpp` itself was never actually reached by the compiler** - the
failure above happens before compiling any `.cpp` file. Its API usage was
checked by hand against the real `esp_ble_mesh`/NimBLE headers (every
struct field, macro and enum value used was verified against
`framework-arduinoespressif32-libs@5.5.0`), but that's not the same as a
real compile. The part most likely to need fixing first once it does
compile: `bluetoothInit()`'s `nimble_port_init()` /
`nimble_port_freertos_init()` controller/host bring-up sequence - that
specific API has shifted across ESP-IDF versions historically, more than
the Mesh/model APIs have.

On a normal development machine (a real PlatformIO install, not this
sandboxed one) steps 1-4 above should already be baked in via the files in
this directory - you're most likely starting from "`pio run` actually
invokes the C++ compiler on `main.cpp`", not from scratch.

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
