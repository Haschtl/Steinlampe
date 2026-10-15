#include <Arduino.h>

#include "settings.h"
#include "pinout.h"
#include "inputs.h"
#include "comms.h"
#include "quickmode.h"
#include "lamp_state.h"
#include "persistence.h"
#include "print.h"
#include "pattern.h"
#include "utils.h"
#include "sleepwake.h"

bool switchDebouncedState = false;

#if ENABLE_BUTTONS
ButtonState buttonStates[BUTTON_COUNT];
#endif

#if ENABLE_POTI
static const float SECURE_POTI_LOW = 0.2f;  // normalized 0..1
static const float SECURE_POTI_HIGH = 0.8f; // normalized 0..1

static int readPotiMedian()
{
    static_assert(Settings::POTI_MEDIAN_SAMPLES > 0 && (Settings::POTI_MEDIAN_SAMPLES % 2) == 1,
                  "POTI_MEDIAN_SAMPLES must be a positive odd number");

    int samples[Settings::POTI_MEDIAN_SAMPLES];
    for (uint8_t i = 0; i < Settings::POTI_MEDIAN_SAMPLES; ++i)
    {
        samples[i] = analogRead(PIN_POTI);
        for (uint8_t j = i; j > 0 && samples[j] < samples[j - 1]; --j)
        {
            int tmp = samples[j];
            samples[j] = samples[j - 1];
            samples[j - 1] = tmp;
        }
    }
    return samples[Settings::POTI_MEDIAN_SAMPLES / 2];
}
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
#endif

// ---------- Schalter / Touch ----------
extern const uint32_t SWITCH_DEBOUNCE_MS = 35;
extern const uint32_t MODE_TAP_MAX_MS = 600; // max. Dauer für "kurz Aus" (Mode-Wechsel)
extern const uint32_t TOUCH_DOUBLE_MS = 500; // Touch-Doppeltipp Erkennung
extern const uint32_t SECURE_BOOT_WINDOW_MS = 1000;
// Number of switch toggles / poti full-swings within the boot window required to
// trigger a factory reset. Kept high so adjusting the knob right after power-on
// doesn't wipe settings (incl. ramp values) by accident.
static const int SECURE_BOOT_TOGGLES_REQUIRED = 5;

#if ENABLE_TOUCH_DIM
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
#endif

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
bool potiWasBelowOff = false;
PotiRole potiRole = (PotiRole)Settings::POTI_ROLE_DEFAULT;
static int potiLastQuickIdx = -1; // last mode applied in POTI_ROLE_MODE_SCAN, -1 = force reapply
#endif

#if ENABLE_PUSH_BUTTON
uint32_t pushDebounceMs = Settings::PUSH_DEBOUNCE_MS;
uint32_t pushDoubleMs = Settings::PUSH_DOUBLE_MS;
uint32_t pushHoldMs = Settings::PUSH_HOLD_MS;
uint32_t pushStepMs = Settings::PUSH_BRI_STEP_MS;
float pushStep = Settings::PUSH_BRI_STEP;
#endif

#if ENABLE_BUTTONS
static int firstPowerToggleIndex();
#endif

/**
 * @brief Force lamp state to match the first toggle/Power button's position.
 */
void syncLampToSwitch()
{
#if ENABLE_BUTTONS
    int idx = firstPowerToggleIndex();
    if (idx >= 0)
    {
        bool on = buttonStates[idx].debounced;
        switchDebouncedState = on;
        setLampEnabled(on, "sync switch");
        sendFeedback(String(F("[Sync] Lamp -> Switch ")) + (on ? F("ON") : F("OFF")));
        return;
    }
#endif
    sendFeedback(F("[Sync] Switch disabled at build"));
}

bool hardwareWantsOn()
{
#if ENABLE_BUTTONS
    int idx = firstPowerToggleIndex();
    if (idx >= 0)
        return buttonStates[idx].debounced;
#endif
#if ENABLE_POTI
    return potiFiltered > potiOffThreshold;
#else
    return true; // no hardware switch/poti compiled in - nothing to gate against
#endif
}

#if ENABLE_BUTTONS
/**
 * @brief Index of the first configured ROLE_POWER + BTN_TOGGLE button, or -1.
 */
static int firstPowerToggleIndex()
{
    for (size_t i = 0; i < BUTTON_COUNT; ++i)
        if (BUTTONS[i].type == BTN_TOGGLE && BUTTONS[i].role == ROLE_POWER)
            return (int)i;
    return -1;
}

const ButtonConfig *firstPowerToggleButton()
{
    int idx = firstPowerToggleIndex();
    return idx >= 0 ? &BUTTONS[idx] : nullptr;
}

static bool readButtonRaw(size_t i)
{
    bool level = digitalRead(BUTTONS[i].pin) == HIGH;
    return BUTTONS[i].activeLow ? !level : level;
}

static void advanceQuickModeFromButton()
{
    size_t from = currentModeIndex;
    if (from >= quickModeCount())
        from = 0;
    size_t next = nextQuickMode(from);
    applyQuickMode(next);
}

void initButtonPins()
{
    for (size_t i = 0; i < BUTTON_COUNT; ++i)
        pinMode(BUTTONS[i].pin, BUTTONS[i].activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
}

/**
 * @brief Seed debounced state from current hardware levels and apply the boot
 * power state from the first toggle/Power button, if any.
 */
void initButtons()
{
    for (size_t i = 0; i < BUTTON_COUNT; ++i)
    {
        bool raw = readButtonRaw(i);
        buttonStates[i] = ButtonState();
        buttonStates[i].raw = raw;
        buttonStates[i].debounced = raw;
    }
    int idx = firstPowerToggleIndex();
    if (idx >= 0)
    {
        switchDebouncedState = buttonStates[idx].debounced;
        // Apply switch state via setLampEnabled so ramps/guards are respected.
        setLampEnabled(switchDebouncedState, "init switch");
        buttonStates[idx].lastOffMs = millis();
        buttonStates[idx].lastOnMs = lampEnabled ? buttonStates[idx].lastOffMs : 0;
        buttonStates[idx].modeTapArmed = false;
    }
    else
    {
        switchDebouncedState = false;
    }
}

/**
 * @brief Toggle-type button just became active (closed). Dispatches by role.
 */
static void onToggleActivated(size_t i)
{
    const ButtonConfig &cfg = BUTTONS[i];
    ButtonState &st = buttonStates[i];
    uint32_t now = millis();
    switch (cfg.role)
    {
    case ROLE_POWER:
        // Short off->on within MODE_TAP_MAX_MS: advance pattern
        if (st.modeTapArmed && (now - st.lastOffMs) <= MODE_TAP_MAX_MS)
            advanceQuickModeFromButton();
        st.modeTapArmed = false;
        st.lastOnMs = now;
        switchDebouncedState = true;
        setLampEnabled(true, "switch on");
        sendFeedback(String(F("[")) + cfg.label + F("] ON"));
        confirmBtPairing(cfg.label);
        break;
    case ROLE_MODE_NEXT:
        advanceQuickModeFromButton();
        break;
    case ROLE_BRIGHTNESS_UP:
    case ROLE_BRIGHTNESS_DOWN:
        st.holdLastStepMs = now; // (re)start the ramp-repeat timer
        break;
    }
    lastActivityMs = now;
}

/**
 * @brief Toggle-type button just became inactive (open). Dispatches by role.
 */
static void onToggleDeactivated(size_t i)
{
    const ButtonConfig &cfg = BUTTONS[i];
    ButtonState &st = buttonStates[i];
    uint32_t now = millis();
    if (cfg.role == ROLE_POWER)
    {
        // Arm mode change if lamp was on before this off edge
        st.modeTapArmed = lampEnabled;
        st.lastOffMs = now;
        switchDebouncedState = false;
        setLampEnabled(false, "switch off");
        sendFeedback(String(F("[")) + cfg.label + F("] OFF"));
        confirmBtPairing(cfg.label);
    }
    // Lamp on/off is not persisted; a mode-tap already saves via applyQuickMode(),
    // so avoid an extra NVS write on every toggle to reduce flash wear.
    lastActivityMs = now;
}

#if ENABLE_PUSH_BUTTON
static void momentaryPowerPress(size_t i)
{
    ButtonState &st = buttonStates[i];
    st.pressMs = millis();
    st.holdActive = false;
}

static void momentaryPowerRelease(size_t i)
{
    ButtonState &st = buttonStates[i];
    uint32_t now = millis();
    if (st.holdActive)
    {
        st.holdActive = false;
        return;
    }
    if (st.awaitDouble && (now - st.lastReleaseMs) <= pushDoubleMs)
    {
        st.awaitDouble = false;
        advanceQuickModeFromButton();
    }
    else
    {
        st.awaitDouble = true;
        st.lastReleaseMs = now;
    }
}

static void momentaryPowerTick(size_t i)
{
    const ButtonConfig &cfg = BUTTONS[i];
    ButtonState &st = buttonStates[i];
    uint32_t now = millis();
    if (st.awaitDouble && (now - st.lastReleaseMs) > pushDoubleMs)
    {
        st.awaitDouble = false;
        setLampEnabled(!lampEnabled, cfg.label);
    }
    if (st.debounced && !st.holdActive && (now - st.pressMs) >= pushHoldMs)
    {
        st.holdActive = true;
        st.holdLastStepMs = now;
        st.awaitDouble = false;
    }
    if (st.holdActive && st.debounced)
    {
        if (now - st.holdLastStepMs >= pushStepMs)
        {
            st.holdLastStepMs = now;
            float pct = masterBrightness * 100.0f + pushStep * 100.0f;
            if (pct > 100.0f)
                pct = briMinUser * 100.0f;
            setBrightnessPercent(pct, true);
        }
    }
    if (!st.debounced && st.holdActive)
        st.holdActive = false;
}

static void momentaryBrightnessPress(size_t i, float dir)
{
    ButtonState &st = buttonStates[i];
    st.pressMs = millis();
    st.holdActive = false;
    float pct = constrain(masterBrightness * 100.0f + dir * pushStep * 100.0f, briMinUser * 100.0f, 100.0f);
    setBrightnessPercent(pct, true);
}

static void momentaryBrightnessTick(size_t i, float dir)
{
    ButtonState &st = buttonStates[i];
    uint32_t now = millis();
    if (st.debounced && !st.holdActive && (now - st.pressMs) >= pushHoldMs)
    {
        st.holdActive = true;
        st.holdLastStepMs = now;
    }
    if (st.holdActive && st.debounced)
    {
        if (now - st.holdLastStepMs >= pushStepMs)
        {
            st.holdLastStepMs = now;
            float pct = constrain(masterBrightness * 100.0f + dir * pushStep * 100.0f, briMinUser * 100.0f, 100.0f);
            setBrightnessPercent(pct, true);
        }
    }
    if (!st.debounced && st.holdActive)
        st.holdActive = false;
}
#endif // ENABLE_PUSH_BUTTON

/**
 * @brief Debounce every configured button and dispatch its role's behavior.
 */
void updateButtons()
{
    uint32_t now = millis();
    for (size_t i = 0; i < BUTTON_COUNT; ++i)
    {
        const ButtonConfig &cfg = BUTTONS[i];
        ButtonState &st = buttonStates[i];
#if ENABLE_PUSH_BUTTON
        uint32_t debounceMs = (cfg.type == BTN_TOGGLE) ? SWITCH_DEBOUNCE_MS : pushDebounceMs;
#else
        uint32_t debounceMs = SWITCH_DEBOUNCE_MS;
#endif
        bool raw = readButtonRaw(i);
        if (raw != st.raw)
        {
            st.raw = raw;
            st.lastDebounceMs = now;
        }
        bool changed = false;
        if ((now - st.lastDebounceMs) >= debounceMs && st.debounced != st.raw)
        {
            st.debounced = st.raw;
            changed = true;
        }

        if (cfg.type == BTN_TOGGLE)
        {
            if (changed)
            {
                if (st.debounced)
                    onToggleActivated(i);
                else
                    onToggleDeactivated(i);
            }
#if ENABLE_PUSH_BUTTON
            if ((cfg.role == ROLE_BRIGHTNESS_UP || cfg.role == ROLE_BRIGHTNESS_DOWN) && st.debounced)
            {
                if (now - st.holdLastStepMs >= pushStepMs)
                {
                    st.holdLastStepMs = now;
                    float dir = (cfg.role == ROLE_BRIGHTNESS_UP) ? 1.0f : -1.0f;
                    float pct = constrain(masterBrightness * 100.0f + dir * pushStep * 100.0f, briMinUser * 100.0f, 100.0f);
                    setBrightnessPercent(pct, true);
                }
            }
#endif
        }
        else // BTN_MOMENTARY
        {
#if ENABLE_PUSH_BUTTON
            if (!pushEnabled)
                continue;
            if (changed)
            {
                if (st.debounced)
                {
                    if (cfg.role == ROLE_POWER)
                        momentaryPowerPress(i);
                    else if (cfg.role == ROLE_BRIGHTNESS_UP)
                        momentaryBrightnessPress(i, 1.0f);
                    else if (cfg.role == ROLE_BRIGHTNESS_DOWN)
                        momentaryBrightnessPress(i, -1.0f);
                    else
                        st.pressMs = now;
                }
                else
                {
                    if (cfg.role == ROLE_POWER)
                        momentaryPowerRelease(i);
                    else if (cfg.role == ROLE_MODE_NEXT)
                        advanceQuickModeFromButton();
                }
            }
            if (cfg.role == ROLE_POWER)
                momentaryPowerTick(i);
            else if (cfg.role == ROLE_BRIGHTNESS_UP)
                momentaryBrightnessTick(i, 1.0f);
            else if (cfg.role == ROLE_BRIGHTNESS_DOWN)
                momentaryBrightnessTick(i, -1.0f);
#endif
        }
    }
}
#endif // ENABLE_BUTTONS

/**
 * @brief During the secure-boot window, debounce the switch and count toggles without running full logic.
 */
void processStartupSwitch()
{
    if (secureBootWindowClosed)
        return;
#if ENABLE_BUTTONS
    {
        uint32_t now = millis();
        for (size_t i = 0; i < BUTTON_COUNT; ++i)
        {
            if (BUTTONS[i].type != BTN_TOGGLE)
                continue;
            ButtonState &st = buttonStates[i];
            bool raw = readButtonRaw(i);
            if (raw != st.raw)
            {
                st.raw = raw;
                st.lastDebounceMs = now;
            }
            if ((now - st.lastDebounceMs) >= SWITCH_DEBOUNCE_MS && st.debounced != st.raw)
            {
                st.debounced = st.raw;
                if (!secureBootLatched && !secureBootWindowClosed)
                {
                    secureBootToggleCount++;
                    if (secureBootToggleCount >= SECURE_BOOT_TOGGLES_REQUIRED)
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
                        sendFeedback(String(F("[SecureBoot] Toggle ")) + String(secureBootToggleCount) + F("/") + String(SECURE_BOOT_TOGGLES_REQUIRED));
                    }
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
                if (secureBootToggleCount >= SECURE_BOOT_TOGGLES_REQUIRED)
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
                    sendFeedback(String(F("[SecureBoot] Poti swing ")) + String(secureBootToggleCount) + F("/") + String(SECURE_BOOT_TOGGLES_REQUIRED));
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
#if !ENABLE_TOUCH_DIM
    sendFeedback(F("Touch disabled in build"));
#else
    long acc = 0;
    const int samples = 32;
    for (int i = 0; i < samples; ++i)
    {
        acc += touchRead(PIN_TOUCH_DIM);
        delay(5);
    }
    touchBaseline = (int)(acc / samples);
#endif
}

/**
 * @brief Guided calibration: measure baseline and touched delta to derive thresholds.
 */
void calibrateTouchGuided()
{
#if !ENABLE_TOUCH_DIM
    sendFeedback(F("Touch disabled in build"));
#else
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
#endif
}

/**
 * @brief Periodically sample the touch sensor to control long-press dimming.
 */
void updateTouchBrightness()
{
#if !ENABLE_TOUCH_DIM
    return;
#else
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
#endif
}


#if ENABLE_POTI
void updatePoti()
{
    if (!potiEnabled)
        return;
    uint32_t now = millis();
    if (now - lastPotiSampleMs < potiSampleMs)
        return;
    lastPotiSampleMs = now;

    int raw = readPotiMedian();
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

    bool nowBelow = (potiFiltered <= potiOffThreshold);
    bool crossedOff = (!potiWasBelowOff && nowBelow);
    bool crossedOn = (potiWasBelowOff && !nowBelow);
    if (!crossedOn && !crossedOff && potiLastApplied >= 0.0f && fabs(potiFiltered - potiLastApplied) < potiDeltaMin)
        return;

    confirmBtPairing("poti");
    if (nowBelow)
    {
        if (crossedOff && lampEnabled)
            setLampEnabled(false, "poti", false);
        // Force next sample to be applied once we rise above the threshold.
        potiLastApplied = -1.0f;
        potiWasBelowOff = true;
        potiLastQuickIdx = -1; // reapply mode-scan position on the next on-crossing
        return;
    }

    float target = potiFiltered;
    target = clamp01(target);

    if (crossedOn && !lampEnabled)
        setLampEnabled(true, "poti", true);
    potiWasBelowOff = nowBelow;
    potiLastApplied = potiFiltered;
    queueLiveState();

    if (potiRole == POTI_ROLE_MODE_SCAN)
    {
        size_t count = enabledQuickModeCount();
        if (count > 0)
        {
            size_t pos = (size_t)(target * (float)count);
            if (pos >= count)
                pos = count - 1;
            size_t idx = nthEnabledQuickMode(pos);
            if ((int)idx != potiLastQuickIdx)
            {
                potiLastQuickIdx = (int)idx;
                applyQuickMode(idx);
            }
        }
    }
    else
    {
        // Fast, non-persisted updates for smooth knob response (no flash wear).
        setBrightnessPercent(target * 100.0f, false, false, true);
    }
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
