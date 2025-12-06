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
#include "lamp_state.h"
#include "patterns.h"
#include "settings.h"
#include "utils.h"

#if ENABLE_BLE
#include <BLEDevice.h>
#endif

#include <esp_sleep.h>
#include <esp_spp_api.h>
#include <esp_spp_api.h>

// ---------- Pins ----------
static const int PIN_PWM = 23;       // MOSFET-Gate
static const int PIN_SWITCH = 32;    // Kippschalter (digital)
static const int PIN_TOUCH_DIM = T7; // Touch-Elektrode am Metallschalter (GPIO27)

// ---------- Schalter / Touch ----------
static const int SWITCH_ACTIVE_LEVEL = LOW;
static const uint32_t SWITCH_DEBOUNCE_MS = 35;
static const uint32_t MODE_TAP_MAX_MS = 600; // max. Dauer für "kurz Aus" (Mode-Wechsel)
static const uint32_t TOUCH_DOUBLE_MS = 500;  // Touch-Doppeltipp Erkennung

// Touch-Schwellwerte-Defaults
static const int TOUCH_DELTA_ON_DEFAULT = 10; // Counts relativ zur Baseline
static const int TOUCH_DELTA_OFF_DEFAULT = 6; // Hysterese
static const uint32_t TOUCH_SAMPLE_DT_MS = 25;
static const uint32_t TOUCH_EVENT_DEBOUNCE_MS = 200;
static const float DIM_RAMP_STEP = 0.01f;
static const uint32_t DIM_RAMP_DT_MS = 80;
static const float DIM_MIN = 0.10f;
static const float DIM_MAX = 0.95f;

// Vorwärtsdeklarationen
void handleCommand(String line);
void startWakeFade(uint32_t durationMs, bool announce = true);
void cancelWakeFade(bool announce = true);
String getBLEAddress();

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
#if ENABLE_MUSIC_MODE
static const char *PREF_KEY_MUSIC_EN = "music_en";
#endif
static const char *PREF_KEY_TOUCH_DIM = "touch_dim";
static const char *PREF_KEY_LIGHT_GAIN = "light_gain";
static const char *PREF_KEY_BRI_MIN = "bri_min";
static const char *PREF_KEY_BRI_MAX = "bri_max";
static const char *PREF_KEY_PRES_GRACE = "pres_grace";
static const char *PREF_KEY_TOUCH_HOLD = "touch_hold";

// ---------- Zustand ----------
// Active pattern state (index and start time)
size_t currentPattern = 0;
uint32_t patternStartMs = 0;
// General flags/state
bool autoCycle = Settings::DEFAULT_AUTOCYCLE;

// Switch handling
bool switchRawState = false;
bool switchDebouncedState = false;
uint32_t switchLastDebounceMs = 0;
uint32_t lastSwitchOffMs = 0;
uint32_t lastSwitchOnMs = 0;
bool modeTapArmed = false;

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

// Presence tracking
bool presenceEnabled = Settings::PRESENCE_DEFAULT_ENABLED;
uint32_t presenceGraceMs = Settings::PRESENCE_GRACE_MS_DEFAULT;
uint32_t presenceGraceDeadline = 0;
bool presencePrevConnected = false;
bool presenceDetected = false;
String presenceAddr;
String lastBleAddr;
String lastBtAddr;
uint32_t lastPresenceSeenMs = 0;
uint32_t lastPresenceScanMs = 0;

// Ramping and timers
uint32_t idleOffMs = Settings::DEFAULT_IDLE_OFF_MS;
#if ENABLE_LIGHT_SENSOR
bool lightSensorEnabled = Settings::LIGHT_SENSOR_DEFAULT_ENABLED;
float lightFiltered = 0.0f;
uint32_t lastLightSampleMs = 0;
uint16_t lightMinRaw = 4095;
uint16_t lightMaxRaw = 0;
#endif
float lightGain = Settings::LIGHT_GAIN_DEFAULT;
#if ENABLE_MUSIC_MODE
bool musicEnabled = Settings::MUSIC_DEFAULT_ENABLED;
float musicFiltered = 0.0f;
uint32_t lastMusicSampleMs = 0;
#endif
bool bleWasConnected = false;
bool btWasConnected = false;

// Custom pattern editor storage
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

#if ENABLE_MUSIC_MODE
float patternMusic(uint32_t)
{
  // musicFiltered is normalized in updateMusicSensor to 0..1
  return clamp01(musicFiltered);
}
#endif

bool wakeFadeActive = false;
uint32_t wakeStartMs = 0;
uint32_t wakeDurationMs = 0;
float wakeTargetLevel = 0.8f;

bool sleepFadeActive = false;
uint32_t sleepStartMs = 0;
uint32_t sleepDurationMs = 0;
float sleepStartLevel = 0.0f;

// ---------- Hilfsfunktionen ----------
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
  cfg += F(" touch_dim=");
  cfg += touchDimEnabled ? F("on") : F("off");
  cfg += F(" touch_hold=");
  cfg += touchHoldStartMs;
  cfg += F(" light_gain=");
  cfg += String(lightGain, 2);
  cfg += F(" bri_min=");
  cfg += String(briMinUser, 3);
  cfg += F(" bri_max=");
  cfg += String(briMaxUser, 3);
  cfg += F(" pres_grace=");
  cfg += presenceGraceMs;
  sendFeedback(cfg);
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
  prefs.putUInt(PREF_KEY_TOUCH_HOLD, touchHoldStartMs);
  prefs.putBool(PREF_KEY_PRESENCE_EN, presenceEnabled);
  prefs.putString(PREF_KEY_PRESENCE_ADDR, presenceAddr);
  prefs.putUInt(PREF_KEY_RAMP_MS, rampDurationMs);
  prefs.putUInt(PREF_KEY_IDLE_OFF, idleOffMs);
#if ENABLE_LIGHT_SENSOR
  prefs.putBool(PREF_KEY_LS_EN, lightSensorEnabled);
  lightMinRaw = 4095;
  lightMaxRaw = 0;
#endif
  prefs.putUInt(PREF_KEY_CUSTOM_MS, customStepMs);
  prefs.putBytes(PREF_KEY_CUSTOM, customPattern, sizeof(float) * customLen);
#if ENABLE_MUSIC_MODE
  prefs.putBool(PREF_KEY_MUSIC_EN, musicEnabled);
#endif
  prefs.putBool(PREF_KEY_TOUCH_DIM, touchDimEnabled);
  prefs.putFloat(PREF_KEY_LIGHT_GAIN, lightGain);
  prefs.putFloat(PREF_KEY_BRI_MIN, briMinUser);
  prefs.putFloat(PREF_KEY_BRI_MAX, briMaxUser);
  prefs.putUInt(PREF_KEY_PRES_GRACE, presenceGraceMs);
  lastLoggedBrightness = masterBrightness;
  
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
  touchHoldStartMs = prefs.getUInt(PREF_KEY_TOUCH_HOLD, Settings::TOUCH_HOLD_MS_DEFAULT);
  if (touchHoldStartMs < 500)
    touchHoldStartMs = 500;
  else if (touchHoldStartMs > 5000)
    touchHoldStartMs = 5000;
  presenceEnabled = prefs.getBool(PREF_KEY_PRESENCE_EN, Settings::PRESENCE_DEFAULT_ENABLED);
  presenceAddr = prefs.getString(PREF_KEY_PRESENCE_ADDR, "");
  rampDurationMs = prefs.getUInt(PREF_KEY_RAMP_MS, Settings::DEFAULT_RAMP_MS);
  if (rampDurationMs < 50)
    rampDurationMs = Settings::DEFAULT_RAMP_MS;
  idleOffMs = prefs.getUInt(PREF_KEY_IDLE_OFF, Settings::DEFAULT_IDLE_OFF_MS);
#if ENABLE_LIGHT_SENSOR
  lightSensorEnabled = prefs.getBool(PREF_KEY_LS_EN, Settings::LIGHT_SENSOR_DEFAULT_ENABLED);
#endif
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
#if ENABLE_MUSIC_MODE
  musicEnabled = prefs.getBool(PREF_KEY_MUSIC_EN, Settings::MUSIC_DEFAULT_ENABLED);
#endif
  touchDimEnabled = prefs.getBool(PREF_KEY_TOUCH_DIM, Settings::TOUCH_DIM_DEFAULT_ENABLED);
  lightGain = prefs.getFloat(PREF_KEY_LIGHT_GAIN, Settings::LIGHT_GAIN_DEFAULT);
  briMinUser = prefs.getFloat(PREF_KEY_BRI_MIN, Settings::BRI_MIN_DEFAULT);
  briMaxUser = prefs.getFloat(PREF_KEY_BRI_MAX, Settings::BRI_MAX_DEFAULT);
  presenceGraceMs = prefs.getUInt(PREF_KEY_PRES_GRACE, Settings::PRESENCE_GRACE_MS_DEFAULT);
#if ENABLE_LIGHT_SENSOR
  lastLoggedBrightness = masterBrightness;
  lightMinRaw = 4095;
  lightMaxRaw = 0;
#endif
}

/**
 * @brief Read the current raw logic level of the mechanical switch.
 */
bool readSwitchRaw()
{
  return digitalRead(PIN_SWITCH) == SWITCH_ACTIVE_LEVEL;
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
  int delta = touchBaseline - raw;
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
      // if (lastSwitchOnMs > 0 && (nowOn - lastSwitchOnMs) <= DOUBLE_TAP_MS)
      // {
      //   startWakeFade(Settings::DEFAULT_WAKE_MS / 6, true); // schneller Wake-Kick
      // }
      lastSwitchOnMs = nowOn;
      // Short off→on within MODE_TAP_MAX_MS: advance pattern
      if (modeTapArmed && (now - lastSwitchOffMs) <= MODE_TAP_MAX_MS)
      {
        setPattern((currentPattern + 1) % PATTERN_COUNT, true, true);
      }
      modeTapArmed = false;
      setLampEnabled(true, "switch on");
      sendFeedback(F("[Switch] ON"));
    }
    else
    {
      // Arm mode change if lamp was on before this off edge
      modeTapArmed = lampEnabled;
      lastSwitchOffMs = now;
      setLampEnabled(false, "switch off");
      sendFeedback(F("[Switch] OFF"));
    }
    saveSettings();
    lastActivityMs = now;
  }
}

// Presence is handled via polling in loop()
void blePresenceUpdate(bool, const String &) {}

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
  int newOn = delta * 6 / 10;  // ~60% of observed delta
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

  int raw = touchRead(PIN_TOUCH_DIM);
  int delta = touchBaseline - raw;
  int mag = abs(delta);

  if (!touchActive)
  {
    // touchBaseline = (touchBaseline * 15 + raw) / 16;
    if (mag > touchDeltaOn && (now - lastTouchChangeMs) >= TOUCH_EVENT_DEBOUNCE_MS)
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

  if (mag < touchDeltaOff)
  {
    touchActive = false;
    // touchBaseline = (touchBaseline * 7 + raw) / 8;
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
  String payload;
  String line1 = String(F("Pattern ")) + String(currentPattern + 1) + F("/") + String(PATTERN_COUNT) + F(" '") +
                 PATTERNS[currentPattern].name + F("' | AutoCycle=") + (autoCycle ? F("ON") : F("OFF"));
  if (wakeFadeActive)
    line1 += F(" | Wake");
  if (sleepFadeActive)
    line1 += F(" | Sleep");
  sendFeedback(line1);
  payload += line1 + '\n';

  String line2 = String(F("Lamp=")) + (lampEnabled ? F("ON") : F("OFF")) + F(" | Switch=") +
                 (switchDebouncedState ? F("ON") : F("OFF")) + F(" | Brightness=") +
                 String(masterBrightness * 100.0f, 1) + F("%");
  sendFeedback(line2);
  payload += line2 + '\n';

  String line3 = F("Ramp=");
  line3 += String(rampDurationMs);
  line3 += F("ms | IdleOff=");
  if (idleOffMs == 0)
    line3 += F("off");
  else
  {
    line3 += String(idleOffMs / 60000);
    line3 += F("m");
  }
  line3 += F(" | TouchDim=");
  line3 += touchDimEnabled ? F("ON") : F("OFF");
  sendFeedback(line3);
  payload += line3 + '\n';

  String line4 = F("Presence=");
  if (presenceEnabled)
  {
    line4 += F("ON (");
    line4 += (presenceAddr.isEmpty() ? F("no device") : presenceAddr);
    line4 += F(")");
  }
  else
  {
    line4 += F("OFF");
  }
  sendFeedback(line4);
  payload += line4 + '\n';

#if ENABLE_BLE
  String line4b = F("Device=");
  line4b += getBLEAddress();
  line4b += F(" | Service=");
  line4b += Settings::BLE_SERVICE_UUID;
  line4b += F(" | Cmd=");
  line4b += Settings::BLE_COMMAND_CHAR_UUID;
  line4b += F(" | Status=");
  line4b += Settings::BLE_STATUS_CHAR_UUID;
  sendFeedback(line4b);
  payload += line4b + '\n';
#endif

  String customLine = String(F("[Custom] len=")) + String(customLen) + F(" stepMs=") + String(customStepMs);
  sendFeedback(customLine);
  payload += customLine + '\n';

  int raw = touchRead(PIN_TOUCH_DIM);
  int delta = raw - touchBaseline;
  int mag = abs(delta);
  String touchLine = String(F("[Touch] base=")) + String(touchBaseline) + F(" raw=") + String(raw) +
                     F(" delta=") + String(delta) + F(" |mag=") + String(mag) + F(" thrOn=") + String(touchDeltaOn) +
                     F(" thrOff=") + String(touchDeltaOff) + F(" active=") + (touchActive ? F("1") : F("0"));
  sendFeedback(touchLine);
  payload += touchLine + '\n';

#if ENABLE_LIGHT_SENSOR
  String lightLine = F("[Light] ");
  if (lightSensorEnabled)
  {
    lightLine += String(F("raw=")) + String((int)lightFiltered) + F(" min=") + String((int)lightMinRaw) +
                 F(" max=") + String((int)lightMaxRaw);
  }
  else
  {
    lightLine += F("N/A");
  }
  sendFeedback(lightLine);
  payload += lightLine + '\n';
#else
  String lightLine = F("[Light] N/A");
  sendFeedback(lightLine);
  payload += lightLine + '\n';
#endif

#if ENABLE_MUSIC_MODE
  String musicLine = String(F("[Music] ")) + (musicEnabled ? F("ON") : F("OFF"));
  sendFeedback(musicLine);
  payload += musicLine + '\n';
#else
  String musicLine = F("[Music] N/A");
  sendFeedback(musicLine);
  payload += musicLine + '\n';
#endif

  updateBleStatus(payload);
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
    else if (key == "touch_hold")
    {
      uint32_t v = val.toInt();
      if (v >= 500 && v <= 5000)
        touchHoldStartMs = v;
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
    else if (key == "touch_dim")
    {
      bool v;
      if (parseBool(val, v))
        touchDimEnabled = v;
    }
    else if (key == "light_gain")
    {
      lightGain = val.toFloat();
      if (lightGain < 0.1f)
        lightGain = 0.1f;
      if (lightGain > 5.0f)
        lightGain = 5.0f;
    }
    else if (key == "bri_min")
    {
      briMinUser = clamp01(val.toFloat());
    }
    else if (key == "bri_max")
    {
      briMaxUser = clamp01(val.toFloat());
    }
    else if (key == "pres_grace")
    {
      uint32_t v = val.toInt();
      if (v < 0)
        v = 0;
      presenceGraceMs = v;
    }
  }
  saveSettings();
  sendFeedback(F("[Config] Imported"));
  if (briMaxUser < briMinUser)
    briMaxUser = briMinUser;
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
      "  bri min/max <0..1>- Min/Max-Level setzen",
      "  wake [Sekunden]   - sanfter Weckfade starten (Default 180s)",
      "  wake stop         - Weckfade abbrechen",
      "  sleep [Minuten]   - Sleep-Fade auf 0, Default 15min",
      "  sleep stop        - Sleep-Fade abbrechen",
      "  ramp <ms>         - Ramp-Dauer für Helligkeit setzen",
      "  idleoff <Min>     - Auto-Off nach X Minuten (0=aus)",
      "  touch tune <on> <off> - Touch-Schwellen setzen",
      "  touch hold <ms>   - Hold-Start 500..5000 ms",
      "  touchdim on/off   - Touch-Dimmen aktivieren/deaktivieren",
      "  presence on|off   - Auto-Off wenn Gerät weg",
      "  presence set <addr>/clear - Gerät binden oder löschen",
      "  presence grace <ms> - Verzögerung vor Auto-Off",
      "  custom v1,v2,...   - Custom-Pattern setzen (0..1)",
      "  custom step <ms>   - Schrittzeit Custom-Pattern",
      "  light gain <f>     - Verstärkung Lichtsensor",
      "  music on|off       - Music-Mode (ADC) aktivieren",
      "  calibrate touch    - Geführte Touch-Kalibrierung",
      "  calibrate         - Touch-Baseline neu messen",
      "  touch             - aktuellen Touch-Rohwert anzeigen",
      "  status            - aktuellen Zustand anzeigen",
      "  factory           - Reset aller Settings",
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
  if (lower == "calibrate touch")
  {
    calibrateTouchGuided();
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
  if (lower.startsWith("touch hold"))
  {
    uint32_t v = line.substring(line.indexOf("hold") + 4).toInt();
    if (v >= 500 && v <= 5000)
    {
      touchHoldStartMs = v;
      saveSettings();
      sendFeedback(String(F("[Touch] hold ms=")) + String(v));
    }
    else
    {
      sendFeedback(F("Usage: touch hold <500-5000>"));
    }
    return;
  }
  if (lower == "touchdim on")
  {
    touchDimEnabled = true;
    saveSettings();
    sendFeedback(F("[TouchDim] Enabled"));
    return;
  }
  if (lower == "touchdim off")
  {
    touchDimEnabled = false;
    saveSettings();
    sendFeedback(F("[TouchDim] Disabled"));
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
  if (lower.startsWith("bri min"))
  {
    float v = line.substring(7).toFloat();
    v = clamp01(v);
    briMinUser = v;
    if (briMaxUser < briMinUser)
      briMaxUser = briMinUser;
    saveSettings();
    sendFeedback(String(F("[Bri] min=")) + String(v, 3));
    return;
  }
  if (lower.startsWith("bri max"))
  {
    float v = line.substring(7).toFloat();
    v = clamp01(v);
    if (v < briMinUser)
      v = briMinUser;
    briMaxUser = v;
    saveSettings();
    sendFeedback(String(F("[Bri] max=")) + String(v, 3));
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
    else if (arg.startsWith("gain"))
    {
      float g = arg.substring(4).toFloat();
      if (g < 0.1f)
        g = 0.1f;
      if (g > 5.0f)
        g = 5.0f;
      lightGain = g;
      saveSettings();
      sendFeedback(String(F("[Light] gain=")) + String(g, 2));
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
#if ENABLE_MUSIC_MODE
  if (lower.startsWith("music"))
  {
    String arg = line.substring(5);
    arg.trim();
    arg.toLowerCase();
    if (arg == "on")
    {
      musicEnabled = true;
      saveSettings();
      sendFeedback(F("[Music] Enabled"));
    }
    else if (arg == "off")
    {
      musicEnabled = false;
      saveSettings();
      sendFeedback(F("[Music] Disabled"));
    }
    else
    {
      sendFeedback(String(F("[Music] level=")) + String(musicFiltered, 3) + F(" en=") + (musicEnabled ? F("1") : F("0")));
    }
    return;
  }
#endif
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
    else if (arg.startsWith("grace"))
    {
      uint32_t v = line.substring(line.indexOf("grace") + 5).toInt();
      presenceGraceMs = v;
      saveSettings();
      sendFeedback(String(F("[Presence] Grace ")) + String(v) + F(" ms"));
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
  if (lower == "factory")
  {
    prefs.begin(PREF_NS, false);
    prefs.clear();
    prefs.end();
    // reset runtime defaults
    masterBrightness = Settings::DEFAULT_BRIGHTNESS;
    autoCycle = Settings::DEFAULT_AUTOCYCLE;
    touchDeltaOn = TOUCH_DELTA_ON_DEFAULT;
    touchDeltaOff = TOUCH_DELTA_OFF_DEFAULT;
    touchDimEnabled = Settings::TOUCH_DIM_DEFAULT_ENABLED;
    touchHoldStartMs = Settings::TOUCH_HOLD_MS_DEFAULT;
    presenceEnabled = Settings::PRESENCE_DEFAULT_ENABLED;
    presenceGraceMs = Settings::PRESENCE_GRACE_MS_DEFAULT;
    presenceAddr = "";
    rampDurationMs = Settings::DEFAULT_RAMP_MS;
    idleOffMs = Settings::DEFAULT_IDLE_OFF_MS;
    briMinUser = Settings::BRI_MIN_DEFAULT;
    briMaxUser = Settings::BRI_MAX_DEFAULT;
    customLen = 0;
    customStepMs = Settings::CUSTOM_STEP_MS_DEFAULT;
#if ENABLE_LIGHT_SENSOR
    lightSensorEnabled = Settings::LIGHT_SENSOR_DEFAULT_ENABLED;
#endif
    lightGain = Settings::LIGHT_GAIN_DEFAULT;
#if ENABLE_MUSIC_MODE
    musicEnabled = Settings::MUSIC_DEFAULT_ENABLED;
#endif
    saveSettings();
    sendFeedback(F("[Factory] Settings cleared"));
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

// Active scanning disabled: rely on explicit connections/pings
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

  // Presence polling (no active scan)
  bool anyClient = btHasClient();
  if (bleActive())
  {
    anyClient = true;
    if (lastBleAddr.length() == 0)
      lastBleAddr = getLastBleAddr();
  }
  if (presenceEnabled)
  {
    bool wasDetected = presenceDetected;
    bool detected = anyClient;
    if (presenceAddr.isEmpty())
    {
      if (lastBleAddr.length() > 0)
        presenceAddr = lastBleAddr;
      else if (lastBtAddr.length() > 0)
        presenceAddr = lastBtAddr;
    }

    if (lastPresenceSeenMs > 0 && (millis() - lastPresenceSeenMs) <= presenceGraceMs)
      detected = true;

    if (detected)
    {
      presenceGraceDeadline = 0;
      presencePrevConnected = true;
      if (!presenceDetected)
        sendFeedback(F("[Presence] detected (client match)"));
      presenceDetected = true;
      lastPresenceSeenMs = millis();
      if (switchDebouncedState && !lampEnabled)
      {
        setLampEnabled(true, "presence connect");
        sendFeedback(F("[Presence] Detected -> Lamp ON"));
      }
    }
    else if (presenceGraceDeadline == 0 && presenceAddr.length() > 0 &&
             (presencePrevConnected || lastPresenceSeenMs > 0))
    {
      presenceGraceDeadline = millis() + presenceGraceMs;
      sendFeedback(String(F("[Presence] No client -> pending OFF in ")) + String(presenceGraceMs) + F("ms"));
      presenceDetected = false;
    }
    else if (!detected && wasDetected)
    {
      presenceDetected = false;
      sendFeedback(F("[Presence] no client detected"));
    }
  }

  if (presenceGraceDeadline > 0 && millis() >= presenceGraceDeadline)
  {
    presenceGraceDeadline = 0;
    if (lampEnabled)
    {
      setLampEnabled(false, "presence grace");
      sendFeedback(F("[Presence] Grace timeout -> Lamp OFF"));
      return;
    }
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
  float combined = lampEnabled ? relative * masterBrightness * ambientScale * outputScale : 0.0f;
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
  {
    ambientScale = 1.0f;
    return;
  }
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
  // Map ambient reading to a dimming factor; smooth via low-pass
  float target = clamp01((0.2f + 0.8f * norm) * lightGain);
  ambientScale = 0.85f * ambientScale + 0.15f * target;
#endif
}

#if ENABLE_MUSIC_MODE
void updateMusicSensor()
{
  if (!musicEnabled)
    return;
  uint32_t now = millis();
  if (now - lastMusicSampleMs < Settings::MUSIC_SAMPLE_MS)
    return;
  lastMusicSampleMs = now;
  int raw = analogRead(Settings::MUSIC_PIN);
  // crude envelope: high-pass-ish by subtracting filtered baseline
  static float env = 0.0f;
  float val = (float)raw / 4095.0f;
  env = Settings::MUSIC_ALPHA * val + (1.0f - Settings::MUSIC_ALPHA) * env;
  musicFiltered = clamp01(env);
}
#endif

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
#if ENABLE_MUSIC_MODE
  updateMusicSensor();
#endif
  maybeLightSleep();
  delay(10);
}
