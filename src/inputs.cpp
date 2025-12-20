#include <Arduino.h>

#include "settings.h"
#include "pinout.h"
#include "comms.h"
#include "quickmode.h"
#include "lamp_state.h"
#include "persistence.h"
#include "print.h"
#include "pattern.h"
#include "utils.h"
#include "sleepwake.h"

// Switch handling
#if ENABLE_SWITCH
bool switchRawState = false;
bool switchDebouncedState = false;
uint32_t switchLastDebounceMs = 0;
uint32_t lastSwitchOffMs = 0;
uint32_t lastSwitchOnMs = 0;
bool modeTapArmed = false;
#endif

#if ENABLE_POTI
static const float SECURE_POTI_LOW = 0.2f;  // normalized 0..1
static const float SECURE_POTI_HIGH = 0.8f; // normalized 0..1
#endif

uint32_t bootStartMs = 0;
uint8_t secureBootToggleCount = 0;
bool secureBootLatched = false;
bool secureBootWindowClosed = false;
bool startupHoldActive = false;

#if ENABLE_EXT_INPUT
bool extInputEnabled = false;
bool extInputAnalog = Settings::EXT_INPUT_ANALOG_DEFAULT;
float extInputAlpha = Settings::EXT_INPUT_ALPHA;
float extInputDelta = Settings::EXT_INPUT_DELTA;
float extInputFiltered = -1.0f;
float extInputLastApplied = -1.0f;
uint32_t extInputLastSampleMs = 0;
bool extInputLastDigital = false;
#endif

#if ENABLE_PUSH_BUTTON
bool pushEnabled = true;
bool pushRawState = false;
bool pushDebouncedState = false;
uint32_t pushLastDebounceMs = 0;
uint32_t pushPressMs = 0;
uint32_t pushLastReleaseMs = 0;
bool pushAwaitDouble = false;
bool pushHoldActive = false;
uint32_t pushHoldLastStepMs = 0;
#endif

// ---------- Schalter / Touch ----------
extern const int SWITCH_ACTIVE_LEVEL = LOW;
extern const uint32_t SWITCH_DEBOUNCE_MS = 35;
extern const uint32_t MODE_TAP_MAX_MS = 600; // max. Dauer für "kurz Aus" (Mode-Wechsel)
extern const uint32_t TOUCH_DOUBLE_MS = 500; // Touch-Doppeltipp Erkennung
extern const uint32_t SECURE_BOOT_WINDOW_MS = 1000;

// Touch-Schwellwerte-Defaults
extern const int TOUCH_DELTA_ON_DEFAULT = 12; // Counts relativ zur Baseline
extern const int TOUCH_DELTA_OFF_DEFAULT = 8; // Hysterese
extern const uint32_t TOUCH_SAMPLE_DT_MS = 25;
extern const uint32_t TOUCH_EVENT_DEBOUNCE_MS = 200;
extern const float DIM_RAMP_STEP = 0.005f;
extern const uint32_t DIM_RAMP_DT_MS = 80;
extern const float DIM_MIN = 0.02f;
extern const float DIM_MAX = 0.95f;

// Touch sensing state
int touchBaseline = 0;
bool touchActive = false;
uint32_t touchLastSampleMs = 0;
uint32_t touchStartMs = 0;
uint32_t touchLastRampMs = 0;
uint32_t lastTouchReleaseMs = 0;
uint32_t lastTouchChangeMs = 0;
bool dimRampUp = true;
bool brightnessChangedByTouch = false;
int touchDeltaOn = TOUCH_DELTA_ON_DEFAULT;
int touchDeltaOff = TOUCH_DELTA_OFF_DEFAULT;
bool touchDimEnabled = Settings::TOUCH_DIM_DEFAULT_ENABLED;
uint32_t touchHoldStartMs = Settings::TOUCH_HOLD_MS_DEFAULT;
float touchDimStep = Settings::TOUCH_DIM_STEP_DEFAULT;

#if ENABLE_POTI
float potiFiltered = 0.0f;
float potiLastApplied = -1.0f;
uint32_t lastPotiSampleMs = 0;
uint32_t potiSampleMs = Settings::POTI_SAMPLE_MS;
float potiAlpha = Settings::POTI_ALPHA;
float potiDeltaMin = Settings::POTI_DELTA_MIN;
float potiOffThreshold = Settings::POTI_OFF_THRESHOLD;
bool potiEnabled = true;
int potiLastRaw = -1;
float potiCalibMin = Settings::POTI_MIN_DEFAULT;
float potiCalibMax = Settings::POTI_MAX_DEFAULT;
bool potiInvert = Settings::POTI_INVERT_DEFAULT;
#endif

#if ENABLE_PUSH_BUTTON
uint32_t pushDebounceMs = Settings::PUSH_DEBOUNCE_MS;
uint32_t pushDoubleMs = Settings::PUSH_DOUBLE_MS;
uint32_t pushHoldMs = Settings::PUSH_HOLD_MS;
uint32_t pushStepMs = Settings::PUSH_BRI_STEP_MS;
float pushStep = Settings::PUSH_BRI_STEP;
#endif

/**
 * @brief Force lamp state to match current physical switch position.
 */
void syncLampToSwitch()
{
#if !ENABLE_SWITCH
    sendFeedback(F("[Sync] Switch disabled at build"));
    return;
#else
    setLampEnabled(switchDebouncedState, "sync switch");
    sendFeedback(String(F("[Sync] Lamp -> Switch ")) + (switchDebouncedState ? F("ON") : F("OFF")));
#endif
}


#if ENABLE_SWITCH
/**
 * @brief Read the current raw logic level of the mechanical switch.
 */
bool readSwitchRaw()
{
    return digitalRead(PIN_SWITCH) == SWITCH_ACTIVE_LEVEL;
}

/**
 * @brief Initializes switch debouncing state from current hardware level.
 */
void initSwitchState()
{
    switchRawState = readSwitchRaw();
    switchDebouncedState = switchRawState;
    // Apply switch state via setLampEnabled so ramps/guards are respected.
    setLampEnabled(switchDebouncedState, "init switch");
    lastSwitchOffMs = millis();
    lastSwitchOnMs = lampEnabled ? lastSwitchOffMs : 0;
    modeTapArmed = false;
}
/**
 * @brief Debounce the toggle switch and handle on/off plus mode tap detection.
 */
void updateSwitchLogic()
{
    uint32_t now = millis();
    bool raw = readSwitchRaw();
    if (raw != switchRawState)
    {
        switchRawState = raw;
        switchLastDebounceMs = now;
    }

    if ((now - switchLastDebounceMs) >= SWITCH_DEBOUNCE_MS && switchDebouncedState != switchRawState)
    {
        switchDebouncedState = switchRawState;
        if (switchDebouncedState)
        {
            uint32_t nowOn = now;
            // if (lastSwitchOnMs > 0 && (nowOn - lastSwitchOnMs) <= DOUBLE_TAP_MS)
            // {
            //   startWakeFade(Settings::DEFAULT_WAKE_MS / 6, true); // schneller Wake-Kick
            // }
            lastSwitchOnMs = nowOn;
            // Short off→on within MODE_TAP_MAX_MS: advance pattern
            if (modeTapArmed && (now - lastSwitchOffMs) <= MODE_TAP_MAX_MS)
            {
                size_t from = currentModeIndex;
                if (from >= quickModeCount())
                    from = 0;
                size_t next = nextQuickMode(from);
                applyQuickMode(next);
            }
            modeTapArmed = false;
            setLampEnabled(true, "switch on");
            sendFeedback(F("[Switch] ON"));
            confirmBtPairing("switch");
        }
        else
        {
            // Arm mode change if lamp was on before this off edge
            modeTapArmed = lampEnabled;
            lastSwitchOffMs = now;
            setLampEnabled(false, "switch off");
            sendFeedback(F("[Switch] OFF"));
            confirmBtPairing("switch");
        }
        saveSettings();
        lastActivityMs = now;
    }
}
#endif

/**
 * @brief During the secure-boot window, debounce the switch and count toggles without running full logic.
 */
void processStartupSwitch()
{
    if (secureBootWindowClosed)
        return;
#if ENABLE_SWITCH
    {
        uint32_t now = millis();
        bool raw = readSwitchRaw();
        if (raw != switchRawState)
        {
            switchRawState = raw;
            switchLastDebounceMs = now;
        }
        if ((now - switchLastDebounceMs) >= SWITCH_DEBOUNCE_MS && switchDebouncedState != switchRawState)
        {
            switchDebouncedState = switchRawState;
            if (!secureBootLatched && !secureBootWindowClosed)
            {
                secureBootToggleCount++;
                if (secureBootToggleCount >= 2)
                {
                    secureBootLatched = true;
                    secureBootWindowClosed = true;
                    startupHoldActive = false;
                    applyDefaultSettings(0.20f, false);
                    sendFeedback(F("[SecureBoot] Defaults applied (20% brightness)"));
                    printStatus();
                }
                else
                {
                    sendFeedback(String(F("[SecureBoot] Toggle ")) + String(secureBootToggleCount) + F("/2"));
                }
            }
        }
    }
#endif

#if ENABLE_POTI
    // Poti-based secure boot: count swings between low/high positions within the window
    static int lastZone = -1; // -1=unset, 0=low, 1=high
    static uint32_t lastToggleMs = 0;
    uint32_t now = millis();
    int raw = analogRead(PIN_POTI);
    float norm = clamp01((float)raw / 4095.0f);
    int zone = -1;
    if (norm <= SECURE_POTI_LOW)
        zone = 0;
    else if (norm >= SECURE_POTI_HIGH)
        zone = 1;

    if (zone >= 0)
    {
        if (lastZone == -1)
        {
            lastZone = zone; // establish baseline without counting
        }
        else if (zone != lastZone && (now - lastToggleMs) >= 60)
        {
            lastZone = zone;
            lastToggleMs = now;
            if (!secureBootLatched && !secureBootWindowClosed)
            {
                secureBootToggleCount++;
                if (secureBootToggleCount >= 2)
                {
                    secureBootLatched = true;
                    secureBootWindowClosed = true;
                    startupHoldActive = false;
                    applyDefaultSettings(0.20f, false);
                    sendFeedback(F("[SecureBoot] Defaults applied (20% brightness)"));
                    printStatus();
                }
                else
                {
                    sendFeedback(String(F("[SecureBoot] Poti swing ")) + String(secureBootToggleCount) + F("/2"));
                }
            }
        }
    }
#endif
}


/**
 * @brief Measure and store a fresh baseline value for the touch electrode.
 */
void calibrateTouchBaseline()
{
    long acc = 0;
    const int samples = 32;
    for (int i = 0; i < samples; ++i)
    {
        acc += touchRead(PIN_TOUCH_DIM);
        delay(5);
    }
    touchBaseline = (int)(acc / samples);
}

/**
 * @brief Guided calibration: measure baseline and touched delta to derive thresholds.
 */
void calibrateTouchGuided()
{
    sendFeedback(F("[Calib] Release electrode for 2s"));
    delay(200);
    uint32_t t0 = millis();
    long accBase = 0;
    int nBase = 0;
    while (millis() - t0 < 2000)
    {
        accBase += touchRead(PIN_TOUCH_DIM);
        nBase++;
        delay(40);
    }
    int baseAvg = (nBase > 0) ? (int)(accBase / nBase) : touchBaseline;

    sendFeedback(F("[Calib] Touch and hold for 2s"));
    delay(200);
    t0 = millis();
    long accTouch = 0;
    int nTouch = 0;
    int minTouch = 4095;
    int maxTouch = 0;
    while (millis() - t0 < 2000)
    {
        int raw = touchRead(PIN_TOUCH_DIM);
        accTouch += raw;
        nTouch++;
        if (raw < minTouch)
            minTouch = raw;
        if (raw > maxTouch)
            maxTouch = raw;
        delay(40);
    }
    int touchAvg = (nTouch > 0) ? (int)(accTouch / nTouch) : baseAvg;

    int delta = touchAvg - baseAvg;
    if (delta < 3)
        delta = 3;
    int newOn = delta * 6 / 10; // ~60% of observed delta
    if (newOn < 4)
        newOn = 4;
    if (newOn > 60)
        newOn = 60;
    int newOff = newOn * 6 / 10;
    if (newOff < 2)
        newOff = 2;
    if (newOff >= newOn)
        newOff = newOn - 1;

    touchBaseline = baseAvg;
    touchDeltaOn = newOn;
    touchDeltaOff = newOff;
    saveSettings();
    sendFeedback(String(F("[Calib] base=")) + String(baseAvg) + F(" touch=") + String(touchAvg) +
                 F(" delta=") + String(delta) + F(" thrOn=") + String(newOn) + F(" thrOff=") + String(newOff));
}

/**
 * @brief Periodically sample the touch sensor to control long-press dimming.
 */
void updateTouchBrightness()
{
    uint32_t now = millis();
    if (now - touchLastSampleMs < TOUCH_SAMPLE_DT_MS)
        return;
    if (!touchDimEnabled)
        return;
    touchLastSampleMs = now;

    // Take a small moving average to reduce noise
    int raw = (touchRead(PIN_TOUCH_DIM) + touchRead(PIN_TOUCH_DIM) + touchRead(PIN_TOUCH_DIM)) / 3;
    int delta = touchBaseline - raw;
    int mag = abs(delta);

    if (!touchActive)
    {
        if (mag < touchDeltaOn)
            touchBaseline = (touchBaseline * 15 + raw) / 16;
        if (mag > touchDeltaOn && (now - lastTouchChangeMs) >= TOUCH_EVENT_DEBOUNCE_MS)
        {
            if (wakeFadeActive && wakeSoftCancel)
            {
                cancelWakeFade(true);
                setLampEnabled(false, "wake soft touch");
                touchActive = false;
                return;
            }
            touchActive = true;
            touchStartMs = now;
            touchLastRampMs = now;
            brightnessChangedByTouch = false;
            dimRampUp = (masterBrightness < 0.5f);
            lastActivityMs = now;
            sendFeedback(F("[Touch] detected"));
            if (lastTouchReleaseMs > 0 && (now - lastTouchReleaseMs) <= TOUCH_DOUBLE_MS)
            {
                sendFeedback(F("[Touch] double-tap"));
            }
            lastTouchChangeMs = now;
        }
        return;
    }

    if (mag < touchDeltaOff)
    {
        touchActive = false;
        touchBaseline = (touchBaseline * 7 + raw) / 8;
        if (brightnessChangedByTouch)
        {
            logBrightnessChange("touch");
            saveSettings();
            brightnessChangedByTouch = false;
        }
        if ((now - lastTouchChangeMs) >= TOUCH_EVENT_DEBOUNCE_MS)
        {
            sendFeedback(F("[Touch] release"));
            lastTouchReleaseMs = now;
            lastTouchChangeMs = now;
        }
        return;
    }

    if (!lampEnabled)
        return;

    // Long hold: ramp brightness up/down between DIM_MIN..DIM_MAX
    if ((now - touchStartMs) >= touchHoldStartMs && (now - touchLastRampMs) >= DIM_RAMP_DT_MS)
    {
        touchLastRampMs = now;
        lastActivityMs = now;
        float newLevel = masterBrightness + (dimRampUp ? touchDimStep : -touchDimStep);
        if (newLevel >= DIM_MAX)
        {
            newLevel = DIM_MAX;
            dimRampUp = false;
        }
        else if (newLevel <= DIM_MIN)
        {
            newLevel = DIM_MIN;
            dimRampUp = true;
        }
        masterBrightness = clamp01(newLevel);
        brightnessChangedByTouch = true;
        logBrightnessChange("touch hold");
    }
}


#if ENABLE_PUSH_BUTTON
void updatePushButton()
{
    if (!pushEnabled)
        return;
    uint32_t now = millis();
    bool raw = digitalRead(PIN_PUSHBTN) == PUSH_ACTIVE_LEVEL;
    if (raw != pushRawState)
    {
        pushRawState = raw;
        pushLastDebounceMs = now;
    }
    if ((now - pushLastDebounceMs) >= pushDebounceMs && pushDebouncedState != pushRawState)
    {
        pushDebouncedState = pushRawState;
        if (pushDebouncedState)
        {
            pushPressMs = now;
            pushHoldActive = false;
        }
        else
        {
            if (pushHoldActive)
            {
                pushHoldActive = false;
            }
            else
            {
                if (pushAwaitDouble && (now - pushLastReleaseMs) <= pushDoubleMs)
                {
                    pushAwaitDouble = false;
                    size_t from = currentModeIndex;
                    if (from >= quickModeCount())
                        from = 0;
                    size_t next = nextQuickMode(from);
                    applyQuickMode(next);
                }
                else
                {
                    pushAwaitDouble = true;
                    pushLastReleaseMs = now;
                }
            }
        }
    }

    // finalize single click if waiting too long
    if (pushAwaitDouble && (now - pushLastReleaseMs) > pushDoubleMs)
    {
        pushAwaitDouble = false;
        setLampEnabled(!lampEnabled, "pushbtn");
    }

    // hold brightness adjustment
    if (pushDebouncedState && !pushHoldActive && (now - pushPressMs) >= pushHoldMs)
    {
        pushHoldActive = true;
        pushHoldLastStepMs = now;
        pushAwaitDouble = false;
    }

    if (pushHoldActive && pushDebouncedState)
    {
        if (now - pushHoldLastStepMs >= pushStepMs)
        {
            pushHoldLastStepMs = now;
            float pct = masterBrightness * 100.0f + pushStep * 100.0f;
            if (pct > 100.0f)
                pct = briMinUser * 100.0f;
            setBrightnessPercent(pct, true);
        }
    }
    if (!pushDebouncedState && pushHoldActive)
    {
        pushHoldActive = false;
    }
}
#endif

#if ENABLE_POTI
void updatePoti()
{
    if (!potiEnabled)
        return;
    uint32_t now = millis();
    if (now - lastPotiSampleMs < potiSampleMs)
        return;
    lastPotiSampleMs = now;

    int raw = analogRead(PIN_POTI);
    potiLastRaw = raw;
    float levelRaw = (float)raw / 4095.0f;
    float minCal = potiCalibMin;
    float maxCal = potiCalibMax;
    float span = maxCal - minCal;
    if (span < 0.05f) // avoid division noise; fallback to raw scale
    {
        minCal = 0.0f;
        maxCal = 1.0f;
        span = 1.0f;
    }
    float level = clamp01((levelRaw - minCal) / span);
    if (potiInvert)
        level = 1.0f - level;
    potiFiltered = potiAlpha * level + (1.0f - potiAlpha) * potiFiltered;

    if (potiLastApplied >= 0.0f && fabs(potiFiltered - potiLastApplied) < potiDeltaMin)
        return;

    potiLastApplied = potiFiltered;
    confirmBtPairing("poti");
    if (potiFiltered <= potiOffThreshold)
    {
        if (lampEnabled)
            setLampEnabled(false, "poti");
        return;
    }

    float target = potiFiltered;
    target = clamp01(target);

    if (!lampEnabled)
        setLampEnabled(true, "poti");
    setBrightnessPercent(target * 100.0f, true);
}
#endif

#if ENABLE_EXT_INPUT
void updateExternalInput()
{
    if (!extInputEnabled)
        return;
    uint32_t now = millis();
    if (extInputAnalog)
    {
        if (now - extInputLastSampleMs < Settings::EXT_INPUT_SAMPLE_MS)
            return;
        extInputLastSampleMs = now;
        int raw = analogRead(Settings::EXT_INPUT_PIN);
        float norm = (float)raw / 4095.0f;
        if (norm < 0.0f)
            norm = 0.0f;
        if (norm > 1.0f)
            norm = 1.0f;
        float a = clamp01(extInputAlpha);
        if (extInputFiltered < 0.0f)
            extInputFiltered = norm;
        extInputFiltered = extInputFiltered + (norm - extInputFiltered) * a;
        if (extInputLastApplied < 0.0f || fabsf(extInputFiltered - extInputLastApplied) >= extInputDelta)
        {
            extInputLastApplied = extInputFiltered;
            setLampEnabled(true, "ext-analog");
            setBrightnessPercent(extInputFiltered * 100.0f, false, false);
            logBrightnessChange("ext analog");
        }
    }
    else
    {
        bool level = digitalRead(Settings::EXT_INPUT_PIN) == HIGH;
        if (level != extInputLastDigital)
        {
            extInputLastDigital = level;
            bool active = Settings::EXT_INPUT_ACTIVE_LOW ? !level : level;
            setLampEnabled(active, "ext-digital");
            sendFeedback(String(F("[Ext] Digital ")) + (active ? F("ON") : F("OFF")));
        }
    }
}
#endif
