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

## Status: unbuilt

`src/main.cpp` was written against the real `esp_ble_mesh`/NimBLE headers
(every struct field, macro and enum value used was checked against
`framework-arduinoespressif32-libs@5.5.0`'s actual headers) but **never
compiled** - there was no ESP-IDF toolchain available on the machine that
wrote this. Expect real compile errors on the first build; fixing those is
exactly what this spike is for, not a sign something went wrong.

The part most likely to need fixing first: `bluetoothInit()`'s
`nimble_port_init()` / `nimble_port_freertos_init()` controller/host
bring-up sequence. That specific API has shifted across ESP-IDF versions
historically, more than the Mesh/model APIs have.

## Building

```sh
cd spikes/ble-mesh
pio run -e upesy_wroom -t upload
pio device monitor
```

First build downloads the full ESP-IDF toolchain (a separate, multi-GB
`framework-espidf` package - nothing like the lightweight
`framework-arduinoespressif32-libs` the main firmware uses) - expect that to
take a while.

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
