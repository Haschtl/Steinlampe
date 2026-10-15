#pragma once

#include <Arduino.h>

#include "settings.h"


// Mirrors the first ROLE_POWER+BTN_TOGGLE button's debounced level (false if
// none configured). Kept as an unconditional global since other subsystems
// (idle-off gating in main.cpp, music auto-lamp in microphone.cpp, status in
// print.cpp) read it regardless of which boards compile in a toggle switch.
extern bool switchDebouncedState;

#if ENABLE_BUTTONS
// Per-button runtime state, parallel to the compile-time BUTTONS[] config array
// (see pinout.h). Holds both toggle-debounce bookkeeping and momentary
// press/release/hold bookkeeping - whichever fields a given button's type/role
// actually use.
struct ButtonState
{
    bool raw = false;
    bool debounced = false;
    uint32_t lastDebounceMs = 0;
    uint32_t pressMs = 0;      // momentary: last press edge
    uint32_t lastReleaseMs = 0; // momentary: last release edge
    bool awaitDouble = false;   // momentary ROLE_POWER
    bool holdActive = false;    // momentary: currently in hold-repeat
    uint32_t holdLastStepMs = 0;
    bool modeTapArmed = false;  // toggle ROLE_POWER
    uint32_t lastOnMs = 0;      // toggle ROLE_POWER
    uint32_t lastOffMs = 0;     // toggle ROLE_POWER
};
extern ButtonState buttonStates[];

/**
 * @brief Configure pin modes for all configured buttons. Call early in setup(),
 * before loadSettings()/output init.
 */
void initButtonPins();

/**
 * @brief Seed debounced state from current hardware levels and apply the boot
 * power state from the first toggle/Power button, if any. Call late in setup(),
 * after output + settings are ready.
 */
void initButtons();

/**
 * @brief Debounce every configured button and dispatch its role's behavior.
 */
void updateButtons();

/**
 * @brief The first configured ROLE_POWER + BTN_TOGGLE button, or nullptr if none.
 * Used for light-sleep ext0 wakeup (single-pin hardware limit) as well as
 * hardwareWantsOn()/syncLampToSwitch().
 */
const ButtonConfig *firstPowerToggleButton();
#endif

// Secure-boot window state (used by switch or poti)
extern uint32_t bootStartMs;
extern uint8_t secureBootToggleCount;
extern bool secureBootLatched;
extern bool secureBootWindowClosed;
extern bool startupHoldActive;

#if ENABLE_EXT_INPUT
extern bool extInputEnabled;
extern bool extInputAnalog;
extern float extInputAlpha;
extern float extInputDelta;
extern float extInputFiltered;
extern float extInputLastApplied;
extern uint32_t extInputLastSampleMs;
extern bool extInputLastDigital;
#endif

#if ENABLE_PUSH_BUTTON
extern bool pushEnabled;
#endif

// ---------- Schalter / Touch ----------
extern const uint32_t SWITCH_DEBOUNCE_MS;
extern const uint32_t MODE_TAP_MAX_MS; // max. Dauer für "kurz Aus" (Mode-Wechsel)
extern const uint32_t TOUCH_DOUBLE_MS; // Touch-Doppeltipp Erkennung
extern const uint32_t SECURE_BOOT_WINDOW_MS;

#if ENABLE_TOUCH_DIM
// Touch-Schwellwerte-Defaults
extern const int TOUCH_DELTA_ON_DEFAULT; // Counts relativ zur Baseline
extern const int TOUCH_DELTA_OFF_DEFAULT; // Hysterese
extern const uint32_t TOUCH_SAMPLE_DT_MS;
extern const uint32_t TOUCH_EVENT_DEBOUNCE_MS;
extern const float DIM_RAMP_STEP;
extern const uint32_t DIM_RAMP_DT_MS;
extern const float DIM_MIN;
extern const float DIM_MAX;

// Touch sensing state
extern int touchBaseline;
extern bool touchActive;
extern uint32_t touchLastSampleMs;
extern uint32_t touchStartMs;
extern uint32_t touchLastRampMs;
extern uint32_t lastTouchReleaseMs;
extern uint32_t lastTouchChangeMs;
extern bool dimRampUp;
extern bool brightnessChangedByTouch;
extern int touchDeltaOn;
extern int touchDeltaOff;
extern bool touchDimEnabled;
extern uint32_t touchHoldStartMs;
extern float touchDimStep;
#endif

#if ENABLE_POTI
// What the poti's position controls while the lamp is on (independent of the
// on/off threshold crossing, which always applies regardless of role).
enum PotiRole : uint8_t
{
    POTI_ROLE_BRIGHTNESS = 0, // today's behavior: position -> brightness
    POTI_ROLE_MODE_SCAN = 1,  // position -> index into the quick-cycle-enabled modes
};

extern float potiFiltered;
extern float potiLastApplied;
extern uint32_t lastPotiSampleMs;
extern uint32_t potiSampleMs;
extern float potiAlpha;
extern float potiDeltaMin;
extern float potiOffThreshold;
extern bool potiEnabled;
extern int potiLastRaw; // last ADC sample (0..4095)
extern float potiCalibMin;
extern float potiCalibMax;
extern bool potiInvert;
extern PotiRole potiRole;
/// Inert today - see CHANNEL_TARGET_ALL in pinout.h.
extern uint8_t potiTarget;
#endif

#if ENABLE_PUSH_BUTTON
extern uint32_t pushDebounceMs;
extern uint32_t pushDoubleMs;
extern uint32_t pushHoldMs;
extern uint32_t pushStepMs;
extern float pushStep;
#endif

void syncLampToSwitch();

/**
 * @brief Whether the physical switch/poti currently implies "lamp should be on".
 * True if ENABLE_SWITCH and the switch is on, or ENABLE_POTI and the poti is above its
 * off threshold, or true unconditionally if neither hardware input is compiled in (nothing
 * to gate against). Used by presence-driven auto on/off features to decide whether they may
 * override the hardware's current intent.
 */
bool hardwareWantsOn();

/**
 * @brief During the secure-boot window, look for the unlock gesture (switch toggles or poti swings).
 */
void processStartupSwitch();

/**
 * @brief Measure and store a fresh baseline value for the touch electrode.
 */
void calibrateTouchBaseline();

/**
 * @brief Guided calibration: measure baseline and touched delta to derive thresholds.
 */
void calibrateTouchGuided();

/**
 * @brief Periodically sample the touch sensor to control long-press dimming.
 */
void updateTouchBrightness();

#if ENABLE_POTI
void updatePoti();
#endif

#if ENABLE_EXT_INPUT
void updateExternalInput();
#endif
