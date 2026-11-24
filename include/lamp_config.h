#pragma once

/**
 * @file lamp_config.h
 * @brief Compile-time feature toggles for the lamp firmware.
 */

#ifndef ENABLE_BLE
#define ENABLE_BLE 1
#endif

#ifndef ENABLE_BT_SERIAL
#define ENABLE_BT_SERIAL 1
#endif

// NimBLE (ENABLE_BLE's implementation since the migration off the Bluedroid
// BLE stack) frees the Classic-BT (BR/EDR) controller memory on init
// (NimBLEDevice::init() -> esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT)
// on the original ESP32) and brings the controller up BLE-only. That makes it
// mutually exclusive with BluetoothSerial/SPP on the same chip - there's no
// partial/degraded combination, one of the two just never starts. Fail the
// build instead of leaving that to a silent runtime surprise; pick one via
// build_flags (-DENABLE_BLE=0 or -DENABLE_BT_SERIAL=0).
#if ENABLE_BLE && ENABLE_BT_SERIAL
#error "ENABLE_BLE and ENABLE_BT_SERIAL cannot both be 1: NimBLE frees the Classic-BT controller memory on init. Set one of them to 0."
#endif

#ifndef ENABLE_LIGHT_SENSOR
#define ENABLE_LIGHT_SENSOR 0
#endif

#ifndef ENABLE_MUSIC_MODE
#define ENABLE_MUSIC_MODE 0
#endif

#ifndef ENABLE_BLE_MIDI
#define ENABLE_BLE_MIDI 0
#endif

#ifndef ENABLE_BT_MIDI
#define ENABLE_BT_MIDI 0
#endif

#ifndef ENABLE_SWITCH
#define ENABLE_SWITCH 1
#endif

#ifndef ENABLE_POTI
#define ENABLE_POTI 0
#endif

#ifndef ENABLE_PUSH_BUTTON
#define ENABLE_PUSH_BUTTON 0
#endif

// ---------- Generic multi-button slots (see pinout.h for ButtonConfig) ----------
// Up to 4 independent physical buttons, each freely a latching toggle switch or a
// momentary push button, with its own role. Slot 0 defaults from the legacy
// ENABLE_SWITCH/PIN_SWITCH flags so existing boards need no changes; slots 1-3 are
// opt-in extras (set e.g. -DENABLE_BUTTON1=1 -DBUTTON1_PIN=27 -DBUTTON1_TYPE=1
// -DBUTTON1_ROLE=1 for a momentary mode-next button on GPIO27). ENABLE_PUSH_BUTTON
// is auto-derived in pinout.h from whether any configured slot is momentary - it
// no longer needs to be set by hand, it just keeps gating the shared momentary
// tunables (pushDebounceMs etc.) in settings.h/persistence.cpp/command.cpp.
#ifndef ENABLE_BUTTON0
#define ENABLE_BUTTON0 ENABLE_SWITCH
#endif
#ifndef ENABLE_BUTTON1
#define ENABLE_BUTTON1 0
#endif
#ifndef ENABLE_BUTTON2
#define ENABLE_BUTTON2 0
#endif
#ifndef ENABLE_BUTTON3
#define ENABLE_BUTTON3 0
#endif

#ifndef ENABLE_EXT_INPUT
#define ENABLE_EXT_INPUT 0
#endif

#ifndef ENABLE_ANALOG_OUTPUT
#define ENABLE_ANALOG_OUTPUT 0
#endif

#ifndef ENABLE_RD03
#define ENABLE_RD03 0
#endif

#ifndef ENABLE_OTA
#define ENABLE_OTA 1
#endif

#ifndef PWM_INVERT_OUTPUT
#define PWM_INVERT_OUTPUT 0
#endif
#ifndef DEBUG_BRIGHTNESS_LOG
#define DEBUG_BRIGHTNESS_LOG 0
#endif

#if ENABLE_POTI||ENABLE_SWITCH
#ifndef ENABLE_BT_PAIRING
#define ENABLE_BT_PAIRING 1
#endif
#endif
