#pragma once

#include <Arduino.h>

#include "lamp_config.h"

// ---------- Pins ----------

// MOSFET-Gate
#ifndef PIN_PWM
#define PIN_PWM 23
#endif

#ifndef ANALOG_OUT_PIN
#define ANALOG_OUT_PIN 25
#endif
static const int PIN_ANALOG = ANALOG_OUT_PIN; // DAC output (0-255)

#if ENABLE_ANALOG_OUTPUT
static const int PIN_OUTPUT = PIN_ANALOG;
#else
static const int PIN_OUTPUT = PIN_PWM;
#endif

#if ENABLE_SWITCH
#ifndef PIN_SWITCH
#define PIN_SWITCH 32 // Kippschalter (digital)
#endif
#endif
static const int PIN_TOUCH_DIM = T7; // Touch-Elektrode am Metallschalter (GPIO27)
#if ENABLE_POTI
static const int PIN_POTI = 39;
#endif
// Gated by ENABLE_BUTTON1 (not ENABLE_PUSH_BUTTON, which is only derived further
// down from the resolved slots - too late to guard this default pin with).
#if ENABLE_BUTTON1
static const int PIN_PUSHBTN = 34;
#endif

// ---------- Generic multi-button config ----------
// ButtonType/ButtonRole are the C++-side enums; the BUTTONn_TYPE/BUTTONn_ROLE
// macros below are plain ints (0/1/2/3) so they stay usable in #if arithmetic -
// see lamp_config.h for the slot-enable flags.
enum ButtonType : uint8_t { BTN_TOGGLE = 0, BTN_MOMENTARY = 1 };
enum ButtonRole : uint8_t { ROLE_POWER = 0, ROLE_MODE_NEXT = 1, ROLE_BRIGHTNESS_UP = 2, ROLE_BRIGHTNESS_DOWN = 3 };

// Which output channel a button/poti's action applies to. Inert today -
// there is only one output channel (see MULTI_CHANNEL_FEATURE_PLAN.md,
// §5.2/§5.5) - but present now so a future multi-channel build can read an
// already-configured target instead of needing to extend ButtonConfig (and
// every build_flags set that constructs one) again later. 0..N-1 addresses
// a specific channel; CHANNEL_TARGET_ALL broadcasts to all of them.
static const uint8_t CHANNEL_TARGET_ALL = 255;

struct ButtonConfig
{
    uint8_t pin;
    ButtonType type;
    ButtonRole role;
    bool activeLow;
    const char *label;
    uint8_t target; // channel index, or CHANNEL_TARGET_ALL
};

// Slot 0: defaults from the legacy toggle switch pin ("Kippschalter").
#if ENABLE_BUTTON0
#ifndef BUTTON0_PIN
#define BUTTON0_PIN PIN_SWITCH
#endif
#ifndef BUTTON0_TYPE
#define BUTTON0_TYPE 0 // toggle
#endif
#ifndef BUTTON0_ROLE
#define BUTTON0_ROLE 0 // power
#endif
#ifndef BUTTON0_ACTIVE_LOW
#define BUTTON0_ACTIVE_LOW 1
#endif
#ifndef BUTTON0_LABEL
#define BUTTON0_LABEL "switch"
#endif
#ifndef BUTTON0_TARGET
#define BUTTON0_TARGET CHANNEL_TARGET_ALL
#endif
#endif

// Slot 1: defaults from the legacy momentary push-button pin.
#if ENABLE_BUTTON1
#ifndef BUTTON1_PIN
#define BUTTON1_PIN PIN_PUSHBTN
#endif
#ifndef BUTTON1_TYPE
#define BUTTON1_TYPE 1 // momentary
#endif
#ifndef BUTTON1_ROLE
#define BUTTON1_ROLE 0 // power
#endif
#ifndef BUTTON1_ACTIVE_LOW
#define BUTTON1_ACTIVE_LOW 1
#endif
#ifndef BUTTON1_LABEL
#define BUTTON1_LABEL "push"
#endif
#ifndef BUTTON1_TARGET
#define BUTTON1_TARGET CHANNEL_TARGET_ALL
#endif
#endif

// Slots 2-3: free extras, no sane pin default - must be supplied by the env.
#if ENABLE_BUTTON2
#ifndef BUTTON2_TYPE
#define BUTTON2_TYPE 1 // momentary
#endif
#ifndef BUTTON2_ROLE
#define BUTTON2_ROLE 1 // mode-next
#endif
#ifndef BUTTON2_ACTIVE_LOW
#define BUTTON2_ACTIVE_LOW 1
#endif
#ifndef BUTTON2_LABEL
#define BUTTON2_LABEL "btn2"
#endif
#ifndef BUTTON2_TARGET
#define BUTTON2_TARGET CHANNEL_TARGET_ALL
#endif
#endif

#if ENABLE_BUTTON3
#ifndef BUTTON3_TYPE
#define BUTTON3_TYPE 1 // momentary
#endif
#ifndef BUTTON3_ROLE
#define BUTTON3_ROLE 1 // mode-next
#endif
#ifndef BUTTON3_ACTIVE_LOW
#define BUTTON3_ACTIVE_LOW 1
#endif
#ifndef BUTTON3_LABEL
#define BUTTON3_LABEL "btn3"
#endif
#ifndef BUTTON3_TARGET
#define BUTTON3_TARGET CHANNEL_TARGET_ALL
#endif
#endif

#if ENABLE_BUTTON0 || ENABLE_BUTTON1 || ENABLE_BUTTON2 || ENABLE_BUTTON3
#define ENABLE_BUTTONS 1
#else
#define ENABLE_BUTTONS 0
#endif

// Auto-derive ENABLE_PUSH_BUTTON (gates the shared momentary/ramp tunables -
// pushDebounceMs, pushHoldMs, pushStepMs, pushStep - in settings.h/persistence.cpp/
// command.cpp/print.cpp/inputs.cpp) from whether any configured slot either is
// momentary, or uses a brightness-ramp role (which reuses pushStepMs/pushStep for
// its ramp speed even on a toggle-type button) - no need to set it by hand.
#define BUTTON_NEEDS_PUSH_TUNABLES(type, role) ((type) == 1 || (role) == 2 || (role) == 3)
#if (ENABLE_BUTTON0 && BUTTON_NEEDS_PUSH_TUNABLES(BUTTON0_TYPE, BUTTON0_ROLE)) || \
    (ENABLE_BUTTON1 && BUTTON_NEEDS_PUSH_TUNABLES(BUTTON1_TYPE, BUTTON1_ROLE)) || \
    (ENABLE_BUTTON2 && BUTTON_NEEDS_PUSH_TUNABLES(BUTTON2_TYPE, BUTTON2_ROLE)) || \
    (ENABLE_BUTTON3 && BUTTON_NEEDS_PUSH_TUNABLES(BUTTON3_TYPE, BUTTON3_ROLE))
#undef ENABLE_PUSH_BUTTON
#define ENABLE_PUSH_BUTTON 1
#endif
#undef BUTTON_NEEDS_PUSH_TUNABLES

#if ENABLE_BUTTONS
static const ButtonConfig BUTTONS[] = {
#if ENABLE_BUTTON0
    {(uint8_t)BUTTON0_PIN, (ButtonType)BUTTON0_TYPE, (ButtonRole)BUTTON0_ROLE, (bool)BUTTON0_ACTIVE_LOW, BUTTON0_LABEL, (uint8_t)BUTTON0_TARGET},
#endif
#if ENABLE_BUTTON1
    {(uint8_t)BUTTON1_PIN, (ButtonType)BUTTON1_TYPE, (ButtonRole)BUTTON1_ROLE, (bool)BUTTON1_ACTIVE_LOW, BUTTON1_LABEL, (uint8_t)BUTTON1_TARGET},
#endif
#if ENABLE_BUTTON2
    {(uint8_t)BUTTON2_PIN, (ButtonType)BUTTON2_TYPE, (ButtonRole)BUTTON2_ROLE, (bool)BUTTON2_ACTIVE_LOW, BUTTON2_LABEL, (uint8_t)BUTTON2_TARGET},
#endif
#if ENABLE_BUTTON3
    {(uint8_t)BUTTON3_PIN, (ButtonType)BUTTON3_TYPE, (ButtonRole)BUTTON3_ROLE, (bool)BUTTON3_ACTIVE_LOW, BUTTON3_LABEL, (uint8_t)BUTTON3_TARGET},
#endif
};
static const size_t BUTTON_COUNT = sizeof(BUTTONS) / sizeof(BUTTONS[0]);
#else
static const ButtonConfig *const BUTTONS = nullptr;
static const size_t BUTTON_COUNT = 0;
#endif
