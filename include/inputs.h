#pragma once

#include <Arduino.h>

#include "settings.h"


// Switch handling
#if ENABLE_SWITCH
extern bool switchRawState;
extern bool switchDebouncedState;
extern uint32_t switchLastDebounceMs;
extern uint32_t lastSwitchOffMs;
extern uint32_t lastSwitchOnMs;
extern bool modeTapArmed;
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
extern bool pushRawState;
extern bool pushDebouncedState;
extern uint32_t pushLastDebounceMs;
extern uint32_t pushPressMs;
extern uint32_t pushLastReleaseMs;
extern bool pushAwaitDouble;
extern bool pushHoldActive;
extern uint32_t pushHoldLastStepMs;
#endif

// ---------- Schalter / Touch ----------
extern const int SWITCH_ACTIVE_LEVEL;
extern const uint32_t SWITCH_DEBOUNCE_MS;
extern const uint32_t MODE_TAP_MAX_MS; // max. Dauer für "kurz Aus" (Mode-Wechsel)
extern const uint32_t TOUCH_DOUBLE_MS; // Touch-Doppeltipp Erkennung
extern const uint32_t SECURE_BOOT_WINDOW_MS;

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

#if ENABLE_POTI
extern float potiFiltered;
extern float potiLastApplied;
extern uint32_t lastPotiSampleMs;
extern uint32_t potiSampleMs;
extern float potiAlpha;
extern float potiDeltaMin;
extern float potiOffThreshold;
extern bool potiEnabled;
extern int potiLastRaw; // last ADC sample (0..4095)
#endif

#if ENABLE_PUSH_BUTTON
extern uint32_t pushDebounceMs;
extern uint32_t pushDoubleMs;
extern uint32_t pushHoldMs;
extern uint32_t pushStepMs;
extern float pushStep;
#endif

void syncLampToSwitch();

#if ENABLE_SWITCH
/**
 * @brief Read the current raw logic level of the mechanical switch.
 */
bool readSwitchRaw();

/**
 * @brief Initializes switch debouncing state from current hardware level.
 */
void initSwitchState();

/**
 * @brief Debounce the toggle switch and handle on/off plus mode tap detection.
 */
void updateSwitchLogic();
#endif

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


#if ENABLE_PUSH_BUTTON
void updatePushButton();
#endif

#if ENABLE_POTI
void updatePoti();
#endif

#if ENABLE_EXT_INPUT
void updateExternalInput();
#endif
