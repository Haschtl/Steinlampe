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

## Status: provisioning, Proxy and Composition Data Get all confirmed on real hardware

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

**Flashed to real hardware and confirmed booting clean** (one of the
project's actual `desk_lamp` units, not a bare dev board). First flash
crashed exactly where expected - `bluetoothInit()`'s controller bring-up:

```
W (991) BTDM_INIT: esp_bt_controller_rom_mem_release already released, mode 2
E (992) BLE_INIT: controller init failed
ESP_ERROR_CHECK failed: esp_err_t 0x103 (ESP_ERR_INVALID_STATE) ... nimble_port_init()
```

Root cause: Arduino's own early startup releases the Classic-BT controller
memory before `nimble_port_init()` gets to use it, unless something forces
the linker to pull in `esp32-hal-bt.c` first (it has a strong,
non-weak `btInUse() -> true` on classic ESP32 that's supposed to prevent
exactly that release, but only takes effect if that translation unit is
actually linked in). Fixed with the exact same one-line workaround
NimBLE-Arduino's own `NimBLEDevice::init()` uses for the same reason -
call `btStarted()` (a plain status check, return value unused) at the top
of `bluetoothInit()`, purely to force that link.

Reflashed with the fix: **boots clean, no crash**, and the serial log shows
exactly the expected sequence - `"initializing mesh (NimBLE host)..."` ->
`"provisioning enabled, err=0"` -> NimBLE actively cycling between
`disc_mode=0` (connectable, PB-GATT) and `disc_mode=2` (non-connectable
beacon, PB-ADV) advertising, i.e. correctly advertising as an
unprovisioned Mesh device on both bearers.

**Full provisioning + Proxy + Composition Data Get roundtrip confirmed with
nRF Mesh (Android) against the real hardware.** Two more real bugs found and
fixed along the way, both only visible once an actual phone app was involved
(hand-checking against the headers never would have caught either):

1. **Missing Health Server self-test ID.** The Mesh spec requires a Health
   Server on the primary element (same as Config Server) - missed in the
   first pass, surfacing as repeated "No Health Server context provided"
   warnings. Fully zero-initializing `esp_ble_mesh_health_srv_t` (the
   obvious first fix) was itself a regression: `health_srv_init()`
   hard-requires at least one self-test ID, so that produced "No Health
   Test ID provided" / "Model init failed (err -22)", which cascaded into
   *every* model failing to init and broke provisioning outright (`err=22`
   instead of `err=0`). Fixed by giving it one dummy test ID - spec-valid
   without actually implementing a real self-test (see `main.cpp`'s
   `healthTestIds`).
2. **`gatt_proxy` left unset in the Config Server struct**, which defaults
   to disabled. This one only showed up *after* the Health Server fix, once
   provisioning itself started succeeding: the node would provision fine
   and respond to the very next request, but go permanently unreachable
   over Proxy maybe a minute later with `"No subnets to advertise on"` in
   the log, and simply stop appearing in nRF Mesh's "connect to proxy"
   scanner entirely. Root cause: Proxy advertising only runs unconditionally
   during the ~60s post-provisioning "Node Identity" window; after that it
   depends on the Config Server's `gatt_proxy` state, which Espressif's own
   example sets to `ESP_BLE_MESH_GATT_PROXY_ENABLED` and this spike had
   simply never set. Fixed by setting it explicitly.

One more thing worth documenting because it looks like a bug but isn't one:
right after provisioning completes, nRF Mesh's "Reading composition data"
step can fail with *"Not GATT Proxy Node is connected..."* even though
provisioning itself just succeeded over that same connection. Cause: Mesh
PB-GATT provisioning never does a standard BLE bond/pairing (the encryption
is inside the Mesh PDUs themselves, not the link layer), and our node
transitions from the Provisioning GATT service to the Proxy GATT service on
the *same* connection rather than disconnecting (confirmed in
`esp_ble_mesh`'s own `nimble_host/adapter.c`, which does send a standard
GATT "Service Changed" indication for this) - but without a bonded link,
phone BLE stacks often aren't subscribed to that indication and never
notice the service table changed. The fix is entirely app-side: manually
disconnect and reconnect to the node in nRF Mesh once it's provisioned: a
fresh connection does a clean GATT discovery from scratch and finds the
Proxy service correctly. Confirmed this resolves it and Composition Data
Get then succeeds, listing the Config Server, Health Server, Generic OnOff
Server and the Vendor Model exactly as composed in `main.cpp`.

One spike-only limitation worth knowing before testing: `CONFIG_BLE_MESH_SETTINGS`
(persisting provisioning state to flash) is deliberately left off, so the
node forgets it's provisioned on every reboot/reflash - re-provision each
time. A real product would turn this on.

## Decision gate: can NimBLE-Arduino coexist with esp_ble_mesh in one image?

**No - confirmed with a real build+flash test, not just source reading.**
This was the central open question before committing to any real-firmware
integration plan (see the plan doc): would `comms.cpp`/`midi_ble.cpp`/
`presence_ble.cpp`, which use NimBLE-Arduino's C++ wrapper, need porting to
the native ESP-IDF NimBLE API this spike uses, or could they stay as-is
with Mesh layered on top?

The hopeful theory going in: NimBLE-Arduino's own `NimBLEDevice::init()`
calls `nimble_port_init()`/`nimble_port_freertos_init()` internally - the
exact same calls this spike's own `bluetoothInit()` used before this test -
so maybe both libraries are just two independent sets of GATT services
sharing one already-running host, with no real conflict.

Tested directly: added `h2zero/NimBLE-Arduino` as a `lib_deps` alongside
the existing `esp_ble_mesh` setup, had `NimBLEDevice::init()` own host
bring-up (replacing this spike's manual calls, matching how comms.cpp does
it for real), added one throwaway `NimBLEServer`/characteristic standing in
for comms.cpp's real GATT service, and kept `esp_ble_mesh_init()` right
after. It compiled and linked with zero errors - but **crashed in a boot
loop on real hardware**, every single boot:

```
assert failed: ble_svc_gap_init ble_svc_gap.c:370 (rc == 0)
```

Root cause, confirmed by reading both call sites: `NimBLEServer.cpp:887-888`
(NimBLE-Arduino, triggered from `createServer()`) and `esp_ble_mesh`'s own
`core/nimble_host/adapter.c:2595-2596` (triggered from
`esp_ble_mesh_init()`) **both unconditionally call `ble_svc_gap_init()` and
`ble_svc_gatt_init()`** - the standard NimBLE helper services every app is
expected to register exactly once. Neither library checks whether the
other has already done it, and the second registration of the same
service fails its own internal assert. This isn't an ordering bug (swapping
which one runs first just moves the crash to the other caller) - it's a
hard assumption in both libraries that each is the sole owner of this
startup step, with no public flag on either side to skip it.

**Conclusion: real integration needs `comms.cpp`/`midi_ble.cpp`/
`presence_ble.cpp` ported off NimBLE-Arduino's C++ wrapper onto the native
ESP-IDF NimBLE API** (the same API this spike's Mesh node already uses) -
there's exactly one path into the shared host that way, so no double
registration is possible. The alternative (patching the vendored
NimBLE-Arduino or esp_ble_mesh source to add a guard) would work too but
means carrying a fork of third-party code across every future library
update - not worth it for what's otherwise a contained, mechanical port
(the command dispatch, pattern engine, and MSC logic in those files never
touch BLE APIs directly, so none of that needs to change).

The coexistence-test code itself was reverted, not kept (see git history on
this branch around the commit documenting this finding) - it was only ever
meant to answer this one question, and real hardware is left back on the
last known-good Mesh-only build.

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
   unprovisioned devices, and provision this one. **Confirmed working** -
   see Status above.
3. If "Reading composition data" fails right after provisioning, disconnect
   and reconnect to the node once more (see Status above for why) - then
   bind an AppKey to the node's models (standard nRF Mesh flow: add the
   node to a group/network, create an AppKey if one doesn't exist, bind it
   to each model that should use it - the Generic OnOff Server, and the
   unnamed/unknown "Vendor Model" the app will likely show without a
   friendly name since it doesn't recognize our company ID/opcode).
   **Composition Data Get confirmed working** - all four models (Config
   Server, Health Server, Generic OnOff Server, Vendor Model) show up
   correctly. AppKey binding not yet tried.
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
