#pragma once

#include <Arduino.h>

// Runtime-configurable mapping from an input gesture (N-tap, N-swing,
// long-press, double-long) to a dispatchable action - lets the frontend UI
// freely remap what a switch/poti/momentary-button gesture does, instead of
// only the build-time ButtonRole behavior in pinout.h. inputs.cpp detects
// gestures and calls dispatchGestureOrDefault(); when no binding exists for
// a given (input, gesture) key, the caller's own hardcoded legacy behavior
// runs instead - see inputs.cpp call sites for what each default is.

enum GestureAction : uint8_t
{
    ACTION_NONE = 0,
    ACTION_QUICK_CYCLE,
    ACTION_TOGGLE,
    ACTION_ON,
    ACTION_OFF,
    ACTION_SET_BRIGHTNESS,      // param1 = percent*10
    ACTION_MESH_SET_ON,         // param1 = mesh addr
    ACTION_MESH_SET_OFF,        // param1 = mesh addr
    ACTION_MESH_SET_BRIGHTNESS, // param1 = mesh addr, param2 = percent*10
};

enum GestureInputKind : uint8_t
{
    GIN_SWITCH = 0,
    GIN_POTI = 1,
    GIN_BUTTON = 2,
};

enum GestureKindId : uint8_t
{
    GK_N_TAP = 0,
    GK_N_SWING = 1,
    GK_LONG_PRESS = 2,
    GK_DOUBLE_LONG = 3,
};

// Switch/button: state before the gesture's first edge. Poti: which zone the
// swing started in (GS_OFF = started low, GS_ON = started high) - "turned
// up-down" vs. "down-up" is just which of these two.
enum GestureStartState : uint8_t
{
    GS_EITHER = 0,
    GS_ON = 1,
    GS_OFF = 2,
};

struct GestureBinding
{
    uint8_t inputKind;  // GestureInputKind
    uint8_t inputIndex; // BUTTONS[] slot index; ignored (0) for GIN_POTI
    uint8_t gestureKind; // GestureKindId
    uint8_t count;      // N for GK_N_TAP/GK_N_SWING, else 0
    uint8_t startState; // GestureStartState
    uint8_t action;     // GestureAction
    uint16_t param1;
    uint16_t param2;
};

static const uint8_t MAX_GESTURE_BINDINGS = 24;
extern GestureBinding gestureBindings[MAX_GESTURE_BINDINGS];
extern uint8_t gestureBindingCount;

/**
 * @brief Look up a binding matching the given gesture key. startState is only
 * compared when the binding itself isn't GS_EITHER, so a binding recorded as
 * "either" matches any start state.
 */
const GestureBinding *findGestureBinding(uint8_t inputKind, uint8_t inputIndex,
                                          uint8_t gestureKind, uint8_t count, uint8_t startState);

/**
 * @brief Run the action a binding points to (quick-cycle/toggle/on/off/
 * brightness/mesh send).
 */
void dispatchGestureAction(const GestureBinding &b);

/**
 * @brief No bindings configured by default - every gesture falls back to its
 * caller's own legacy behavior until the user configures one.
 */
void resetGestureBindingsToDefault();

String gestureBindingsToCsv();

/**
 * @brief Parse a semicolon-separated binding list (see gestures.cpp's own
 * comment for the exact grammar) into gestureBindings[]/gestureBindingCount.
 * Replaces the current table entirely. Returns false (table left untouched)
 * on any malformed entry.
 */
bool parseGestureCsv(const String &csv);
