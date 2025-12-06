/*
  Quarzlampe – Pattern-Demo ohne Touch-Elektroden

  - spielt mehrere PWM-Muster nacheinander ab (Steinlampe-Demo)
  - globale Helligkeit sowie Musterwahl können später über beliebige Eingaben erfolgen
    (aktuell via Serieller Konsole / optional BLE / BT-Serial)
  - alter Metall-Kippschalter (GPIO32) schaltet EIN/AUS; kurzer Aus->Ein-Impuls blättert Modi
  - der Metallhebel wirkt zusätzlich als Touch-Elektrode (GPIO27/T7) zum Dimmen bei langem Berühren
*/

#include "lamp_config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

#include "comms.h"
#include "patterns.h"
#include "settings.h"
#include "utils.h"

#include <esp_sleep.h>
#include <esp_spp_api.h>
#include <esp_spp_api.h>

// ---------- Pins ----------
static const int PIN_PWM = 23;       // MOSFET-Gate
static const int PIN_SWITCH = 32;    // Kippschalter (digital)
static const int PIN_TOUCH_DIM = T7; // Touch-Elektrode am Metallschalter (GPIO27)

// ---------- LEDC ----------
static const int LEDC_CH = 0;
static const int LEDC_FREQ = 2000;
static const int LEDC_RES = 12;
static const int PWM_MAX = (1 << LEDC_RES) - 1;
static const float GAMMA = 2.2f;

// ---------- Schalter / Touch ----------
static const int SWITCH_ACTIVE_LEVEL = LOW;
static const uint32_t SWITCH_DEBOUNCE_MS = 35;
static const uint32_t MODE_TAP_MAX_MS = 600; // max. Dauer für "kurz Aus" (Mode-Wechsel)
static const uint32_t DOUBLE_TAP_MS = 600;   // schneller Doppel-Tipp -> Wake-Kick
static const uint32_t TOUCH_DOUBLE_MS = 500;  // Touch-Doppeltipp Erkennung

// Touch-Schwellwerte-Defaults
static const int TOUCH_DELTA_ON_DEFAULT = 6;  // Counts relativ zur Baseline
static const int TOUCH_DELTA_OFF_DEFAULT = 4; // Hysterese
static const uint32_t TOUCH_SAMPLE_DT_MS = 25;
static const uint32_t TOUCH_HOLD_START_MS = 500;
static const uint32_t TOUCH_EVENT_DEBOUNCE_MS = 200;
static const float DIM_RAMP_STEP = 0.02f;
static const uint32_t DIM_RAMP_DT_MS = 80;
static const float DIM_MIN = 0.10f;
static const float DIM_MAX = 0.95f;

// Vorwärtsdeklarationen
void handleCommand(String line);
void startWakeFade(uint32_t durationMs, bool announce = true);
void cancelWakeFade(bool announce = true);

// ---------- Persistenz ----------
Preferences prefs;
static const char *PREF_NS = "lamp";
static const char *PREF_KEY_B1000 = "b1000";
static const char *PREF_KEY_MODE = "mode";
static const char *PREF_KEY_AUTO = "auto";
static const char *PREF_KEY_THR_ON = "thr_on";
static const char *PREF_KEY_THR_OFF = "thr_off";
static const char *PREF_KEY_PRESENCE_EN = "pres_en";
static const char *PREF_KEY_PRESENCE_ADDR = "pres_addr";
static const char *PREF_KEY_RAMP_MS = "ramp_ms";
static const char *PREF_KEY_IDLE_OFF = "idle_off";
static const char *PREF_KEY_LS_EN = "ls_en";
static const char *PREF_KEY_CUSTOM = "cust";
static const char *PREF_KEY_CUSTOM_MS = "cust_ms";

// ---------- Zustand ----------
size_t currentPattern = 0;
uint32_t patternStartMs = 0;
float masterBrightness = Settings::DEFAULT_BRIGHTNESS; // 0..1
bool autoCycle = Settings::DEFAULT_AUTOCYCLE;
bool lampEnabled = false;
bool lampOffPending = false;
float lastLoggedBrightness = Settings::DEFAULT_BRIGHTNESS;

bool switchRawState = false;
bool switchDebouncedState = false;
uint32_t switchLastDebounceMs = 0;
uint32_t lastSwitchOffMs = 0;
uint32_t lastSwitchOnMs = 0;
bool modeTapArmed = false;

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

bool presenceEnabled = Settings::PRESENCE_DEFAULT_ENABLED;
String presenceAddr;
String lastBleAddr;
String lastBtAddr;
uint32_t rampDurationMs = Settings::DEFAULT_RAMP_MS;
uint32_t idleOffMs = Settings::DEFAULT_IDLE_OFF_MS;
uint32_t lastActivityMs = 0;
bool lightSensorEnabled = Settings::LIGHT_SENSOR_DEFAULT_ENABLED;
float lightFiltered = 0.0f;
uint32_t lastLightSampleMs = 0;
uint16_t lightMinRaw = 4095;
uint16_t lightMaxRaw = 0;

// Custom pattern editor
static const size_t CUSTOM_MAX = 32;
float customPattern[CUSTOM_MAX];
size_t customLen = 0;
uint32_t customStepMs = Settings::CUSTOM_STEP_MS_DEFAULT;

float patternCustom(uint32_t ms)
{
  if (customLen == 0)
    return 0.8f;
  uint32_t idx = (ms / customStepMs) % customLen;
  return clamp01(customPattern[idx]);
}

bool wakeFadeActive = false;
uint32_t wakeStartMs = 0;
uint32_t wakeDurationMs = 0;
float wakeTargetLevel = 0.8f;

bool sleepFadeActive = false;
uint32_t sleepStartMs = 0;
uint32_t sleepDurationMs = 0;
float sleepStartLevel = 0.0f;

bool rampActive = false;
float rampStartLevel = 0.0f;
float rampTargetLevel = 0.0f;
uint32_t rampStartMs = 0;
uint32_t rampDurationActive = 0;

// ---------- Hilfsfunktionen ----------
/**
 * @brief Write a gamma-corrected PWM value to the LED driver.
 *
 * @param normalized Desired brightness in the range [0,1].
 */
void applyPwmLevel(float normalized)
{
  float level = clamp01(normalized);
  if (level <= 0.0f)
  {
    ledcWrite(LEDC_CH, 0);
    return;
  }
  float pwm = powf(level, GAMMA) * PWM_MAX;
  if (pwm < 0.0f)
    pwm = 0.0f;
  if (pwm > PWM_MAX)
    pwm = (float)PWM_MAX;
  uint32_t pwmValue = (uint32_t)(pwm + 0.5f);
  if (pwmValue > (uint32_t)PWM_MAX)
    pwmValue = (uint32_t)PWM_MAX;
  ledcWrite(LEDC_CH, pwmValue);
}

/**
 * @brief Log current brightness if changed since last log.
 */
void logBrightnessChange(const char *reason)
{
  if (fabs(masterBrightness - lastLoggedBrightness) < 0.001f)
    return;
  lastLoggedBrightness = masterBrightness;
  float perc = clamp01(masterBrightness) * 100.0f;
  String msg = String(F("[Brightness] ")) + String(perc, 1) + F(" %");
  if (reason && reason[0] != '\0')
  {
    msg += F(" (");
    msg += reason;
    msg += F(")");
  }
  sendFeedback(msg);
}

bool parseBool(const String &s, bool &out)
{
  if (s.equalsIgnoreCase(F("on")) || s.equalsIgnoreCase(F("true")) || s == "1")
  {
    out = true;
    return true;
  }
  if (s.equalsIgnoreCase(F("off")) || s.equalsIgnoreCase(F("false")) || s == "0")
  {
    out = false;
    return true;
  }
  return false;
}

void exportConfig()
{
  String cfg = F("cfg import ");
  cfg += F("bri=");
  cfg += String(masterBrightness, 3);
  cfg += F(" auto=");
  cfg += autoCycle ? F("on") : F("off");
  cfg += F(" touch_on=");
  cfg += touchDeltaOn;
  cfg += F(" touch_off=");
  cfg += touchDeltaOff;
  cfg += F(" ramp=");
  cfg += rampDurationMs;
  cfg += F(" idle=");
  cfg += idleOffMs / 60000;
  cfg += F(" presence_en=");
  cfg += presenceEnabled ? F("on") : F("off");
  cfg += F(" presence_addr=");
  cfg += presenceAddr;
  sendFeedback(cfg);
}


/**
 * @brief Start a smooth ramp towards target brightness over durationMs.
 */
void startBrightnessRamp(float target, uint32_t durationMs)
{
  rampStartLevel = masterBrightness;
  rampTargetLevel = clamp01(target);
  rampStartMs = millis();
  uint32_t dur = (durationMs > 0 ? durationMs : rampDurationMs);
  rampDurationActive = dur;
  rampActive = (dur > 0 && rampStartLevel != rampTargetLevel);
  if (!rampActive)
  {
    masterBrightness = rampTargetLevel;
    logBrightnessChange("instant");
  }
}

/**
 * @brief Update brightness ramp progress; should be called regularly.
 */
void updateBrightnessRamp()
{
  if (!rampActive)
    return;
  uint32_t now = millis();
  lastActivityMs = now;
  float t = rampDurationActive > 0 ? clamp01((float)(now - rampStartMs) / (float)rampDurationActive) : 1.0f;
  float eased = t * t * (3.0f - 2.0f * t);
  masterBrightness = rampStartLevel + (rampTargetLevel - rampStartLevel) * eased;
  if (t >= 1.0f)
  {
    masterBrightness = rampTargetLevel;
    rampActive = false;
    if (lampOffPending && masterBrightness <= 0.0f)
    {
      lampEnabled = false;
      lampOffPending = false;
      ledcWrite(LEDC_CH, 0);
    }
    logBrightnessChange("ramp");
  }
}

/**
 * @brief Persist current brightness, pattern and auto-cycle flag in NVS.
 */
void saveSettings()
{
  uint16_t b = (uint16_t)(clamp01(masterBrightness) * 1000.0f + 0.5f);
  prefs.putUShort(PREF_KEY_B1000, b);
  prefs.putUShort(PREF_KEY_MODE, (uint16_t)currentPattern);
  prefs.putBool(PREF_KEY_AUTO, autoCycle);
  prefs.putShort(PREF_KEY_THR_ON, (int16_t)touchDeltaOn);
  prefs.putShort(PREF_KEY_THR_OFF, (int16_t)touchDeltaOff);
  prefs.putBool(PREF_KEY_PRESENCE_EN, presenceEnabled);
  prefs.putString(PREF_KEY_PRESENCE_ADDR, presenceAddr);
  prefs.putUInt(PREF_KEY_RAMP_MS, rampDurationMs);
  prefs.putUInt(PREF_KEY_IDLE_OFF, idleOffMs);
  prefs.putBool(PREF_KEY_LS_EN, lightSensorEnabled);
  prefs.putUInt(PREF_KEY_CUSTOM_MS, customStepMs);
  prefs.putBytes(PREF_KEY_CUSTOM, customPattern, sizeof(float) * customLen);
  lastLoggedBrightness = masterBrightness;
  lightMinRaw = 4095;
  lightMaxRaw = 0;
}

/**
 * @brief Restore persisted brightness/pattern settings from NVS.
 */
void loadSettings()
{
  prefs.begin(PREF_NS, false);
  uint16_t b = prefs.getUShort(PREF_KEY_B1000, (uint16_t)(Settings::DEFAULT_BRIGHTNESS * 1000.0f));
  masterBrightness = clamp01(b / 1000.0f);
  uint16_t idx = prefs.getUShort(PREF_KEY_MODE, 0);
  if (idx >= PATTERN_COUNT)
    idx = 0;
  currentPattern = idx;
  autoCycle = prefs.getBool(PREF_KEY_AUTO, Settings::DEFAULT_AUTOCYCLE);
  touchDeltaOn = prefs.getShort(PREF_KEY_THR_ON, TOUCH_DELTA_ON_DEFAULT);
  touchDeltaOff = prefs.getShort(PREF_KEY_THR_OFF, TOUCH_DELTA_OFF_DEFAULT);
  if (touchDeltaOn < 1)
    touchDeltaOn = TOUCH_DELTA_ON_DEFAULT;
  if (touchDeltaOff < 1 || touchDeltaOff >= touchDeltaOn)
    touchDeltaOff = TOUCH_DELTA_OFF_DEFAULT;
  presenceEnabled = prefs.getBool(PREF_KEY_PRESENCE_EN, Settings::PRESENCE_DEFAULT_ENABLED);
  presenceAddr = prefs.getString(PREF_KEY_PRESENCE_ADDR, "");
  rampDurationMs = prefs.getUInt(PREF_KEY_RAMP_MS, Settings::DEFAULT_RAMP_MS);
  if (rampDurationMs < 50)
    rampDurationMs = Settings::DEFAULT_RAMP_MS;
  idleOffMs = prefs.getUInt(PREF_KEY_IDLE_OFF, Settings::DEFAULT_IDLE_OFF_MS);
  lightSensorEnabled = prefs.getBool(PREF_KEY_LS_EN, Settings::LIGHT_SENSOR_DEFAULT_ENABLED);
  customStepMs = prefs.getUInt(PREF_KEY_CUSTOM_MS, Settings::CUSTOM_STEP_MS_DEFAULT);
  if (customStepMs < 100)
    customStepMs = Settings::CUSTOM_STEP_MS_DEFAULT;
  size_t maxFloats = CUSTOM_MAX;
  size_t readBytes = prefs.getBytesLength(PREF_KEY_CUSTOM);
  if (readBytes > 0 && readBytes <= sizeof(float) * CUSTOM_MAX)
  {
    customLen = readBytes / sizeof(float);
    prefs.getBytes(PREF_KEY_CUSTOM, customPattern, readBytes);
  }
  else
  {
    customLen = 0;
  }
  lastLoggedBrightness = masterBrightness;
  lightMinRaw = 4095;
  lightMaxRaw = 0;
}

/**
 * @brief Read the current raw logic level of the mechanical switch.
 */
bool readSwitchRaw()
{
  return digitalRead(PIN_SWITCH) == SWITCH_ACTIVE_LEVEL;
}

/**
 * @brief Enable or disable the lamp output (keeps brightness state).
 */
void logLampState(const char *reason)
{
  String msg = String(F("[Lamp] ")) + (lampEnabled ? F("ON") : F("OFF"));
  if (reason && reason[0] != '\0')
  {
    msg += F(" (");
    msg += reason;
    msg += F(")");
  }
  sendFeedback(msg);
}

void setLampEnabled(bool enable, const char *reason = nullptr)
{
  if (lampEnabled == enable)
    return;
  lastActivityMs = millis();
  if (enable)
  {
    lampEnabled = true;
    lampOffPending = false;
    startBrightnessRamp(masterBrightness <= 0.0f ? Settings::DEFAULT_BRIGHTNESS : masterBrightness, rampDurationMs);
    logLampState(reason);
  }
  else
  {
    lampOffPending = true;
    startBrightnessRamp(0.0f, rampDurationMs);
    cancelWakeFade(false);
    logLampState(reason);
  }
}

/**
 * @brief Print averaged touch sensor data for calibration purposes.
 */
void printTouchDebug()
{
  long acc = 0;
  const int samples = 10;
  for (int i = 0; i < samples; ++i)
  {
    acc += touchRead(PIN_TOUCH_DIM);
    delay(5);
  }
  int raw = (int)(acc / samples);
  int delta = raw - touchBaseline;
  sendFeedback(String(F("[Touch] raw=")) + String(raw) + F(" baseline=") + String(touchBaseline) +
               F(" delta=") + String(delta) + F(" thrOn=") + String(touchDeltaOn) + F(" thrOff=") + String(touchDeltaOff));
}

/**
 * @brief Initializes switch debouncing state from current hardware level.
 */
void initSwitchState()
{
  switchRawState = readSwitchRaw();
  switchDebouncedState = switchRawState;
  lampEnabled = switchDebouncedState;
  if (!lampEnabled)
    ledcWrite(LEDC_CH, 0);
  lastSwitchOffMs = millis();
  lastSwitchOnMs = lampEnabled ? lastSwitchOffMs : 0;
  modeTapArmed = false;
}

/**
 * @brief Log the currently selected pattern and its index.
 */
void announcePattern()
{
  sendFeedback(String(F("[Mode] ")) + String(currentPattern + 1) + F("/") + String(PATTERN_COUNT) + F(" - ") + PATTERNS[currentPattern].name);
}

/**
 * @brief Change the active pattern and optionally announce/persist it.
 */
void setPattern(size_t index, bool announce = true, bool persist = false)
{
  if (index >= PATTERN_COUNT)
  {
    index = 0;
  }
  currentPattern = index;
  patternStartMs = millis();
  if (announce)
    announcePattern();
  if (persist)
    saveSettings();
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
      if (lastSwitchOnMs > 0 && (nowOn - lastSwitchOnMs) <= DOUBLE_TAP_MS)
      {
        startWakeFade(Settings::DEFAULT_WAKE_MS / 6, true); // schneller Wake-Kick
      }
      lastSwitchOnMs = nowOn;
      if (modeTapArmed && (now - lastSwitchOffMs) <= MODE_TAP_MAX_MS)
      {
        setPattern((currentPattern + 1) % PATTERN_COUNT, true, true);
      }
      modeTapArmed = false;
      setLampEnabled(true, "switch on");
    }
    else
    {
      modeTapArmed = lampEnabled;
      lastSwitchOffMs = now;
      setLampEnabled(false, "switch off");
    }
    saveSettings();
    lastActivityMs = now;
  }
}

/**
 * @brief Handle presence events from BLE connections (auto-off when device leaves).
 */
void blePresenceUpdate(bool connected, const String &addr)
{
  if (connected)
  {
    if (addr.length() > 0)
      lastBleAddr = addr;
    // if (presenceAddr.isEmpty() && addr.length() > 0)
    if (addr.length() > 0)
    {
      presenceAddr = addr;
      saveSettings();
      sendFeedback(String(F("[Presence] Registered ")) + presenceAddr);
    }
      if (presenceEnabled)
      {
        if (presenceAddr.isEmpty() || addr.isEmpty() || addr == presenceAddr)
        {
          if (switchDebouncedState)
          {
            setLampEnabled(true, "presence connect");
            sendFeedback(F("[Presence] Device connected -> Lamp ON"));
          }
        }
      }
    return;
  }
  // disconnect
  if (!presenceEnabled)
    return;
  if (!presenceAddr.isEmpty())
  {
    if (addr.length() == 0 || addr == presenceAddr)
    {
      setLampEnabled(false, "presence disconnect");
      sendFeedback(String(F("[Presence] Device left: ")) + presenceAddr + F(" -> Lamp OFF"));
    }
  }
}

/**
 * @brief Handle Classic BT SPP events for presence tracking.
 */
void sppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
  if (event == ESP_SPP_SRV_OPEN_EVT)
  {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             param->srv_open.rem_bda[0], param->srv_open.rem_bda[1], param->srv_open.rem_bda[2],
             param->srv_open.rem_bda[3], param->srv_open.rem_bda[4], param->srv_open.rem_bda[5]);
    lastBtAddr = String(buf);
    presenceAddr = lastBtAddr;
    saveSettings();
    sendFeedback(String(F("[Presence] Registered via BT: ")) + presenceAddr);
    if (presenceEnabled && switchDebouncedState)
    {
      setLampEnabled(true, "BT presence connect");
      sendFeedback(F("[Presence] BT connect -> Lamp ON"));
    }
  }
  else if (event == ESP_SPP_CLOSE_EVT)
  {
    if (!presenceEnabled)
      return;
    if (!presenceAddr.isEmpty() && (lastBtAddr.isEmpty() || lastBtAddr == presenceAddr))
    {
      setLampEnabled(false, "BT presence disconnect");
      sendFeedback(String(F("[Presence] BT disconnect: ")) + presenceAddr + F(" -> Lamp OFF"));
    }
  }
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
 * @brief Periodically sample the touch sensor to control long-press dimming.
 */
void updateTouchBrightness()
{
  uint32_t now = millis();
  if (now - touchLastSampleMs < TOUCH_SAMPLE_DT_MS)
    return;
  touchLastSampleMs = now;

  int raw = touchRead(PIN_TOUCH_DIM);
  int delta = raw - touchBaseline;

  if (!touchActive)
  {
    touchBaseline = (touchBaseline * 15 + raw) / 16;
    if (delta > touchDeltaOn && (now - lastTouchChangeMs) >= TOUCH_EVENT_DEBOUNCE_MS)
    {
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

  if (delta < touchDeltaOff)
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

  if ((now - touchStartMs) >= TOUCH_HOLD_START_MS && (now - touchLastRampMs) >= DIM_RAMP_DT_MS)
  {
    touchLastRampMs = now;
    lastActivityMs = now;
    float newLevel = masterBrightness + (dimRampUp ? DIM_RAMP_STEP : -DIM_RAMP_STEP);
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
/**
 * @brief Start a sunrise-style wake fade over the given duration.
 */
void startWakeFade(uint32_t durationMs, bool announce)
{
  if (durationMs < 5000)
    durationMs = 5000;
  wakeDurationMs = durationMs;
  wakeStartMs = millis();
  wakeTargetLevel = clamp01(fmax(masterBrightness, Settings::WAKE_MIN_TARGET));
  setLampEnabled(true);
  wakeFadeActive = true;
  if (announce)
  {
    sendFeedback(String(F("[Wake] Starte Fade über ")) + String(durationMs / 1000.0f, 1) + F(" Sekunden."));
  }
}

/**
 * @brief Abort any active wake fade animation.
 */
void cancelWakeFade(bool announce)
{
  if (!wakeFadeActive)
    return;
  wakeFadeActive = false;
  if (announce)
  {
    sendFeedback(F("[Wake] Abgebrochen."));
  }
}

/**
 * @brief Start a sleep fade down to zero over the given duration.
 */
void startSleepFade(uint32_t durationMs)
{
  if (durationMs < 5000)
    durationMs = 5000;
  sleepStartLevel = masterBrightness;
  sleepDurationMs = durationMs;
  sleepStartMs = millis();
  sleepFadeActive = true;
  setLampEnabled(true);
  sendFeedback(String(F("[Sleep] Fade ueber ")) + String(durationMs / 1000) + F("s"));
}

/**
 * @brief Cancel an active sleep fade.
 */
void cancelSleepFade()
{
  sleepFadeActive = false;
}

/**
 * @brief Set the master brightness in percent, optionally persisting/announcing.
 */
void setBrightnessPercent(float percent, bool persist = false, bool announce = true)
{
  startBrightnessRamp(clamp01(percent / 100.0f), rampDurationMs);
  if (announce)
  {
    logBrightnessChange("cmd bri");
  }
  if (persist)
    saveSettings();
}

/**
 * @brief Print current mode, brightness and wake/auto state.
 */
void printStatus()
{
  String line = String(F("Pattern: ")) + String(currentPattern + 1) + F(" '") + PATTERNS[currentPattern].name +
                F("'  Brightness ") + String(masterBrightness * 100.0f, 1) + F("%  AutoCycle ") +
                (autoCycle ? F("ON") : F("OFF")) + F("  Lamp ") + (lampEnabled ? F("ON") : F("OFF"));
  if (wakeFadeActive)
    line += F("  Wake ACTIVE");
  if (sleepFadeActive)
    line += F("  Sleep ACTIVE");
  if (presenceEnabled)
  {
    line += F("  Presence ON (");
    line += (presenceAddr.isEmpty() ? F("no device") : presenceAddr);
    line += F(")");
  }
  line += F("  Ramp=");
  line += String(rampDurationMs);
  line += F("ms");
  line += F("  IdleOff=");
  if (idleOffMs == 0)
    line += F("off");
  else
    line += String(idleOffMs / 60000);
  sendFeedback(line);

  int raw = touchRead(PIN_TOUCH_DIM);
  int delta = raw - touchBaseline;
  String touchLine = String(F("[Touch] base=")) + String(touchBaseline) + F(" raw=") + String(raw) +
                     F(" delta=") + String(delta) + F(" thrOn=") + String(touchDeltaOn) +
                     F(" thrOff=") + String(touchDeltaOff) + F(" active=") + (touchActive ? F("1") : F("0"));
  sendFeedback(touchLine);
  updateBleStatus(line);
#if ENABLE_LIGHT_SENSOR
  if (lightSensorEnabled)
  {
    sendFeedback(String(F("[Light] raw=")) + String((int)lightFiltered) + F(" min=") + String((int)lightMinRaw) +
                 F(" max=") + String((int)lightMaxRaw));
  }
#endif
}

void importConfig(const String &args)
{
  // Format: key=value whitespace separated (e.g., ramp=400 idle=0 touch_on=8 touch_off=5 presence_en=on)
  int idx = 0;
  String rest = args;
  rest.trim();
  while (rest.length() > 0)
  {
    int spacePos = rest.indexOf(' ');
    String token;
    if (spacePos >= 0)
    {
      token = rest.substring(0, spacePos);
      rest = rest.substring(spacePos + 1);
      rest.trim();
    }
    else
    {
      token = rest;
      rest = "";
    }
    int eqPos = token.indexOf('=');
    if (eqPos <= 0)
      continue;
    String key = token.substring(0, eqPos);
    String val = token.substring(eqPos + 1);
    key.toLowerCase();
    val.trim();
    if (key == "ramp")
    {
      uint32_t v = val.toInt();
      if (v >= 50 && v <= 10000)
        rampDurationMs = v;
    }
    else if (key == "idle")
    {
      int minutes = val.toInt();
      if (minutes < 0)
        minutes = 0;
      idleOffMs = minutes == 0 ? 0 : (uint32_t)minutes * 60000U;
    }
    else if (key == "touch_on")
    {
      int v = val.toInt();
      if (v > 0)
        touchDeltaOn = v;
    }
    else if (key == "touch_off")
    {
      int v = val.toInt();
      if (v > 0)
        touchDeltaOff = v;
    }
    else if (key == "bri")
    {
      float v = val.toFloat();
      masterBrightness = clamp01(v);
      lastLoggedBrightness = masterBrightness;
    }
    else if (key == "auto")
    {
      bool v;
      if (parseBool(val, v))
        autoCycle = v;
    }
    else if (key == "presence_en")
    {
      bool v;
      if (parseBool(val, v))
        presenceEnabled = v;
    }
    else if (key == "presence_addr")
    {
      presenceAddr = val;
    }
  }
  saveSettings();
  sendFeedback(F("[Config] Imported"));
  printStatus();
}

/**
 * @brief Print available serial/BLE command usage.
 */
void printHelp()
{
  const char *lines[] = {
      "Serien-Kommandos:",
      "  list              - verfügbare Muster",
      "  mode <1..N>       - bestimmtes Muster wählen",
      "  next / prev       - weiter oder zurück",
      "  on / off / toggle - Lampe schalten",
      "  auto on|off       - automatisches Durchschalten",
      "  bri <0..100>      - globale Helligkeit in %",
      "  wake [Sekunden]   - sanfter Weckfade starten (Default 180s)",
      "  wake stop         - Weckfade abbrechen",
      "  sleep [Minuten]   - Sleep-Fade auf 0, Default 15min",
      "  sleep stop        - Sleep-Fade abbrechen",
      "  ramp <ms>         - Ramp-Dauer für Helligkeit setzen",
      "  idleoff <Min>     - Auto-Off nach X Minuten (0=aus)",
      "  touch tune <on> <off> - Touch-Schwellen setzen",
      "  presence on|off   - Auto-Off wenn Gerät weg",
      "  presence set <addr>/clear - Gerät binden oder löschen",
      "  custom v1,v2,...   - Custom-Pattern setzen (0..1)",
      "  custom step <ms>   - Schrittzeit Custom-Pattern",
      "  calibrate         - Touch-Baseline neu messen",
      "  touch             - aktuellen Touch-Rohwert anzeigen",
      "  status            - aktuellen Zustand anzeigen",
      "  help              - diese Übersicht",
  };
  for (auto l : lines)
    sendFeedback(String(l));
}

/**
 * @brief List all available patterns and their indices.
 */
void listPatterns()
{
  for (size_t i = 0; i < PATTERN_COUNT; ++i)
  {
    sendFeedback(String(i + 1) + F(": ") + PATTERNS[i].name);
  }
}

/**
 * @brief Parse and execute a command string from any input channel.
 */
void handleCommand(String line)
{
  line.trim();
  if (line.isEmpty())
    return;

  lastActivityMs = millis();

  String lower = line;
  lower.toLowerCase();

  if (lower == "help")
  {
    printHelp();
    return;
  }
  if (lower == "list")
  {
    listPatterns();
    return;
  }
  if (lower == "status")
  {
    printStatus();
    return;
  }
  if (lower == "on")
  {
    setLampEnabled(true, "cmd on");
    saveSettings();
    printStatus();
    return;
  }
  if (lower == "off")
  {
    setLampEnabled(false, "cmd off");
    saveSettings();
    printStatus();
    return;
  }
  if (lower == "toggle")
  {
    setLampEnabled(!lampEnabled, "cmd toggle");
    saveSettings();
    printStatus();
    return;
  }
  if (lower == "touch")
  {
    printTouchDebug();
    return;
  }
  if (lower.startsWith("touch tune"))
  {
    String args = line.substring(10);
    args.trim();
    int on = 0, off = 0;
    if (sscanf(args.c_str(), "%d %d", &on, &off) == 2 && on > 0 && off > 0 && off < on)
    {
      touchDeltaOn = on;
      touchDeltaOff = off;
      saveSettings();
      sendFeedback(String(F("[Touch] tune on=")) + String(on) + F(" off=") + String(off));
    }
    else
    {
      sendFeedback(F("Usage: touch tune <on> <off> (on>off>0)"));
    }
    return;
  }
  if (lower.startsWith("custom"))
  {
    String args = line.substring(6);
    args.trim();
    if (args.startsWith("step"))
    {
      uint32_t v = args.substring(4).toInt();
      if (v >= 100 && v <= 5000)
      {
        customStepMs = v;
        saveSettings();
        sendFeedback(String(F("[Custom] step ms=")) + String(v));
      }
      else
      {
        sendFeedback(F("Usage: custom step <100-5000>"));
      }
      return;
    }
    // parse CSV of floats 0..1
    size_t count = 0;
    float vals[CUSTOM_MAX];
    while (args.length() > 0 && count < CUSTOM_MAX)
    {
      int comma = args.indexOf(',');
      String token;
      if (comma >= 0)
      {
        token = args.substring(0, comma);
        args = args.substring(comma + 1);
      }
      else
      {
        token = args;
        args = "";
      }
      token.trim();
      if (token.length() == 0)
        continue;
      float v = token.toFloat();
      if (v < 0.0f)
        v = 0.0f;
      if (v > 1.0f)
        v = 1.0f;
      vals[count++] = v;
    }
    if (count > 0)
    {
      customLen = count;
      for (size_t i = 0; i < count; ++i)
        customPattern[i] = vals[i];
      saveSettings();
      sendFeedback(String(F("[Custom] Stored " )) + String(count) + F(" values"));
    }
    else
    {
      sendFeedback(F("Usage: custom v1,v2,... or custom step <ms>"));
    }
    return;
  }
  if (lower == "next")
  {
    size_t next = (currentPattern + 1) % PATTERN_COUNT;
    setPattern(next, true, true);
    return;
  }
  if (lower == "prev")
  {
    size_t prev = (currentPattern + PATTERN_COUNT - 1) % PATTERN_COUNT;
    setPattern(prev, true, true);
    return;
  }
  if (lower.startsWith("mode"))
  {
    long idx = line.substring(4).toInt();
    if (idx >= 1 && (size_t)idx <= PATTERN_COUNT)
    {
      setPattern((size_t)idx - 1, true, true);
    }
    else
    {
      sendFeedback(F("Ungültiger Mode."));
    }
    return;
  }
  if (lower.startsWith("bri"))
  {
    float value = line.substring(3).toFloat();
    if (value < 0.0f)
      value = 0.0f;
    if (value > 100.0f)
      value = 100.0f;
    setBrightnessPercent(value, true);
    return;
  }
  if (lower.startsWith("auto"))
  {
    String arg = line.substring(4);
    arg.trim();
    arg.toLowerCase();
    if (arg == "on")
      autoCycle = true;
    else if (arg == "off")
      autoCycle = false;
    else
      sendFeedback(F("auto on|off"));
    saveSettings();
    printStatus();
    return;
  }
  if (lower.startsWith("ramp"))
  {
    String arg = line.substring(4);
    arg.trim();
    uint32_t val = arg.toInt();
    if (val >= 50 && val <= 10000)
    {
      rampDurationMs = val;
      saveSettings();
      sendFeedback(String(F("[Ramp] Duration set to ")) + String(val) + F(" ms"));
    }
    else
    {
      sendFeedback(F("Usage: ramp <50-10000 ms>"));
    }
    return;
  }
  if (lower.startsWith("idleoff"))
  {
    String arg = line.substring(7);
    arg.trim();
    int minutes = arg.toInt();
    if (minutes < 0)
      minutes = 0;
    idleOffMs = (minutes == 0) ? 0 : (uint32_t)minutes * 60000U;
    saveSettings();
    if (idleOffMs == 0)
      sendFeedback(F("[IdleOff] Disabled"));
    else
      sendFeedback(String(F("[IdleOff] ")) + String(minutes) + F(" min"));
    return;
  }
  if (lower.startsWith("light"))
  {
#if ENABLE_LIGHT_SENSOR
    String arg = line.substring(5);
    arg.trim();
    arg.toLowerCase();
    if (arg == "on")
    {
      lightSensorEnabled = true;
      saveSettings();
      sendFeedback(F("[Light] Enabled"));
    }
    else if (arg == "off")
    {
      lightSensorEnabled = false;
      saveSettings();
      sendFeedback(F("[Light] Disabled"));
    }
    else if (arg == "calib")
    {
      int raw = analogRead(Settings::LIGHT_PIN);
      lightFiltered = raw;
      lightMinRaw = raw;
      lightMaxRaw = raw;
      sendFeedback(String(F("[Light] Calibrated raw=")) + String(raw));
    }
    else
    {
      sendFeedback(String(F("[Light] raw=")) + String((int)lightFiltered) + F(" en=") + (lightSensorEnabled ? F("1") : F("0")));
    }
#else
    sendFeedback(F("[Light] Sensor disabled at build (ENABLE_LIGHT_SENSOR=0)"));
#endif
    return;
  }
  if (lower.startsWith("wake"))
  {
    String arg = line.substring(4);
    arg.trim();
    arg.toLowerCase();
    if (arg == "stop" || arg == "cancel")
    {
      cancelWakeFade(true);
    }
    else
    {
      uint32_t durationMs = Settings::DEFAULT_WAKE_MS;
      if (arg.length() > 0)
      {
        float seconds = line.substring(4).toFloat();
        if (seconds > 0.0f)
          durationMs = (uint32_t)(seconds * 1000.0f);
      }
      startWakeFade(durationMs, true);
    }
    return;
  }
  if (lower.startsWith("sleep"))
  {
    String arg = line.substring(5);
    arg.trim();
    arg.toLowerCase();
    if (arg == "stop" || arg == "cancel")
    {
      cancelSleepFade();
      sendFeedback(F("[Sleep] Abgebrochen."));
    }
    else
    {
      uint32_t durMs = Settings::DEFAULT_SLEEP_MS;
      if (arg.length() > 0)
      {
        float minutes = line.substring(5).toFloat();
        if (minutes > 0.0f)
          durMs = (uint32_t)(minutes * 60000.0f);
      }
      startSleepFade(durMs);
    }
    return;
  }
  if (lower.startsWith("presence"))
  {
    String arg = line.substring(8);
    arg.trim();
    arg.toLowerCase();
    if (arg == "on")
    {
      presenceEnabled = true;
      saveSettings();
      sendFeedback(F("[Presence] Enabled"));
    }
    else if (arg == "off")
    {
      presenceEnabled = false;
      saveSettings();
      sendFeedback(F("[Presence] Disabled"));
    }
    else if (arg.startsWith("set"))
    {
      String addr = line.substring(12);
      addr.trim();
      if (addr.isEmpty() || addr == "me")
      {
        if (lastBleAddr.length() > 0)
        {
          presenceAddr = lastBleAddr;
          saveSettings();
          sendFeedback(String(F("[Presence] Set to connected device ")) + presenceAddr);
        }
        else
        {
          sendFeedback(F("[Presence] Kein aktives BLE-Geraet gefunden."));
        }
      }
      else if (addr.length() >= 11)
      {
        presenceAddr = addr;
        saveSettings();
        sendFeedback(String(F("[Presence] Set to ")) + presenceAddr);
      }
      else
      {
        sendFeedback(F("Usage: presence set <MAC>"));
      }
    }
    else if (arg == "clear")
    {
      presenceAddr = "";
      saveSettings();
      sendFeedback(F("[Presence] Cleared"));
    }
    else
    {
      sendFeedback(String(F("[Presence] ")) + (presenceEnabled ? F("ON ") : F("OFF ")) +
                   F("dev=") + (presenceAddr.isEmpty() ? F("none") : presenceAddr));
    }
    return;
  }
  if (lower.startsWith("cfg"))
  {
    String arg = line.substring(3);
    arg.trim();
    if (arg.startsWith("export"))
    {
      exportConfig();
    }
    else if (arg.startsWith("import"))
    {
      String payload = line.substring(line.indexOf("import") + 6);
      importConfig(payload);
    }
    else
    {
      sendFeedback(F("cfg export | cfg import key=val ..."));
    }
    return;
  }
  if (lower == "calibrate")
  {
    calibrateTouchBaseline();
    sendFeedback(F("[Touch] Baseline neu kalibriert."));
    return;
  }

  sendFeedback(F("Unbekanntes Kommando. 'help' tippen."));
}

/**
 * @brief Drive the active animation (wake fade or pattern) and auto-cycle.
 */
void updatePatternEngine()
{
  uint32_t now = millis();
  if (wakeFadeActive)
  {
    lastActivityMs = now;
    if (!lampEnabled)
    {
      wakeFadeActive = false;
      return;
    }
    uint32_t elapsedWake = now - wakeStartMs;
    float progress = (wakeDurationMs > 0) ? clamp01((float)elapsedWake / (float)wakeDurationMs) : 1.0f;
    float eased = progress * progress * (3.0f - 2.0f * progress);
    float level = clamp01(Settings::WAKE_START_LEVEL + (wakeTargetLevel - Settings::WAKE_START_LEVEL) * eased);
    applyPwmLevel(level);
    if (progress >= 1.0f)
    {
      wakeFadeActive = false;
      patternStartMs = now;
      sendFeedback(F("[Wake] Fade abgeschlossen."));
    }
    return;
  }

  // idle-off timer
  if (idleOffMs > 0 && lampEnabled && !rampActive)
  {
    if (now - lastActivityMs >= idleOffMs)
    {
      setLampEnabled(false, "idleoff");
      sendFeedback(F("[IdleOff] Timer -> Lamp OFF"));
    }
  }

  if (sleepFadeActive)
  {
    lastActivityMs = now;
    uint32_t elapsedSleep = now - sleepStartMs;
    float progress = sleepDurationMs > 0 ? clamp01((float)elapsedSleep / (float)sleepDurationMs) : 1.0f;
    float level = sleepStartLevel * (1.0f - progress);
    applyPwmLevel(level);
    if (progress >= 1.0f)
    {
      sleepFadeActive = false;
      setLampEnabled(false, "sleep done");
      sendFeedback(F("[Sleep] Fade abgeschlossen."));
    }
    return;
  }

  const Pattern &p = PATTERNS[currentPattern];
  uint32_t elapsed = now - patternStartMs;
  float relative = clamp01(p.evaluate(elapsed));
  float combined = lampEnabled ? relative * masterBrightness : 0.0f;
  applyPwmLevel(combined);

  if (autoCycle && p.durationMs > 0 && elapsed >= p.durationMs)
  {
    setPattern((currentPattern + 1) % PATTERN_COUNT, true, false);
  }
}

void updateLightSensor()
{
#if ENABLE_LIGHT_SENSOR
  if (!lightSensorEnabled)
    return;
  uint32_t now = millis();
  if (now - lastLightSampleMs < Settings::LIGHT_SAMPLE_MS)
    return;
  lastLightSampleMs = now;
  int raw = analogRead(Settings::LIGHT_PIN);
  lightFiltered = (1.0f - Settings::LIGHT_ALPHA) * lightFiltered + Settings::LIGHT_ALPHA * (float)raw;
  if (raw < (int)lightMinRaw)
    lightMinRaw = raw;
  if (raw > (int)lightMaxRaw)
    lightMaxRaw = raw;

  if (!lampEnabled || touchActive || wakeFadeActive || sleepFadeActive)
    return;
  int range = (int)lightMaxRaw - (int)lightMinRaw;
  if (range < 20)
    return;
  float norm = ((float)lightFiltered - (float)lightMinRaw) / (float)range;
  norm = clamp01(norm);
  float target = clamp01(0.2f + 0.8f * norm);
  startBrightnessRamp(target, rampDurationMs);
#endif
}

/**
 * @brief Enter light sleep briefly when lamp is idle to save power.
 */
void maybeLightSleep()
{
#if !ENABLE_BLE
  if (lampEnabled || wakeFadeActive || sleepFadeActive || rampActive)
    return;
  esp_sleep_enable_timer_wakeup(200000); // 200 ms
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_SWITCH, SWITCH_ACTIVE_LEVEL == LOW ? 0 : 1);
  esp_light_sleep_start();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0);
#endif
}

// ---------- Setup / Loop ----------
/**
 * @brief Arduino setup hook: configure IO, restore state, start comms.
 */
void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("Quarzlampe PWM-Demo"));

  pinMode(PIN_SWITCH, INPUT_PULLUP);
  initSwitchState();
  calibrateTouchBaseline();

#if ENABLE_LIGHT_SENSOR
  analogReadResolution(12);
  analogSetPinAttenuation(Settings::LIGHT_PIN, ADC_11db);
  pinMode(Settings::LIGHT_PIN, INPUT);
#endif

  loadSettings();
  ledcSetup(LEDC_CH, LEDC_FREQ, LEDC_RES);
  ledcAttachPin(PIN_PWM, LEDC_CH);
  patternStartMs = millis();

  announcePattern();
  printHelp();
  printStatus();
  setupCommunications();
  updatePatternEngine();
}

/**
 * @brief Arduino loop hook: process inputs and refresh PWM output.
 */
void loop()
{
  pollCommunications();
  updateSwitchLogic();
  updateTouchBrightness();
  updateBrightnessRamp();
  updatePatternEngine();
  updateLightSensor();
  maybeLightSleep();
  delay(10);
}
