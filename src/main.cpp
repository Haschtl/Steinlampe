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
#include <vector>
#include <ctype.h>

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

// ---------- Pins ----------
static const int PIN_PWM = 23;       // MOSFET-Gate
#if ENABLE_SWITCH
static const int PIN_SWITCH = 32;    // Kippschalter (digital)
#endif
static const int PIN_TOUCH_DIM = T7; // Touch-Elektrode am Metallschalter (GPIO27)
#if ENABLE_POTI
static const int PIN_POTI = Settings::POTI_PIN;
#endif
#if ENABLE_PUSH_BUTTON
static const int PIN_PUSHBTN = Settings::PUSH_BUTTON_PIN;
static const int PUSH_ACTIVE_LEVEL = LOW;
#endif

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
static const float DIM_RAMP_STEP = 0.005f;
static const uint32_t DIM_RAMP_DT_MS = 80;
static const float DIM_MIN = 0.02f;
static const float DIM_MAX = 0.95f;

// Vorwärtsdeklarationen
void handleCommand(String line);
void executeClapCommand(uint8_t count);
void startWakeFade(uint32_t durationMs, bool announce = true, bool softCancel = false, float targetOverride = -1.0f);
void cancelWakeFade(bool announce = true);
String getBLEAddress();
void syncLampToSwitch();
void startDemo(uint32_t dwellMs);
void stopDemo();
void saveSettings();
void importConfig(const String &args);
void setPattern(size_t index, bool announce = true, bool persist = false);
void printStatus();
void printStatusStructured();

// ---------- Persistenz ----------
Preferences prefs;
static const char *PREF_NS = "lamp";
static const char *PREF_KEY_B1000 = "b1000";
static const char *PREF_KEY_MODE = "mode";
static const char *PREF_KEY_AUTO = "auto";
static const char *PREF_KEY_THR_ON = "thr_on";
static const char *PREF_KEY_THR_OFF = "thr_off";
static const char *PREF_KEY_BRI_CAP = "bri_cap";
static const char *PREF_KEY_PRESENCE_EN = "pres_en";
static const char *PREF_KEY_PRESENCE_ADDR = "pres_addr";
static const char *PREF_KEY_RAMP_MS = "ramp_ms";
static const char *PREF_KEY_RAMP_ON_MS = "ramp_on_ms";
static const char *PREF_KEY_RAMP_OFF_MS = "ramp_off_ms";
static const char *PREF_KEY_IDLE_OFF = "idle_off";
static const char *PREF_KEY_LS_EN = "ls_en";
static const char *PREF_KEY_CUSTOM = "cust";
static const char *PREF_KEY_CUSTOM_MS = "cust_ms";
#if ENABLE_MUSIC_MODE
static const char *PREF_KEY_MUSIC_EN = "music_en";
static const char *PREF_KEY_CLAP_EN = "clap_en";
static const char *PREF_KEY_CLAP_THR = "clap_thr";
static const char *PREF_KEY_CLAP_COOL = "clap_cl";
static const char *PREF_KEY_CLAP_CMD1 = "clap_c1";
static const char *PREF_KEY_CLAP_CMD2 = "clap_c2";
static const char *PREF_KEY_CLAP_CMD3 = "clap_c3";
static const char *PREF_KEY_MUSIC_AUTOLAMP = "mus_auto";
static const char *PREF_KEY_MUSIC_AUTOTHR = "mus_thr";
static const char *PREF_KEY_MUSIC_MODE = "mus_mode";
#endif
static const char *PREF_KEY_TOUCH_DIM = "touch_dim";
static const char *PREF_KEY_LIGHT_GAIN = "light_gain";
static const char *PREF_KEY_BRI_MIN = "bri_min";
static const char *PREF_KEY_BRI_MAX = "bri_max";
static const char *PREF_KEY_PRES_GRACE = "pres_grace";
static const char *PREF_KEY_TOUCH_HOLD = "touch_hold";
static const char *PREF_KEY_PAT_SCALE = "pat_scale";
static const char *PREF_KEY_QUICK_MASK = "qmask";
static const char *PREF_KEY_QUICK_MASK_HI = "qmask_hi";
static const char *PREF_KEY_PAT_FADE = "pat_fade";
static const char *PREF_KEY_PAT_FADE_AMT = "pat_fade_amt";
static const char *PREF_KEY_RAMP_EASE_ON = "ramp_e_on";
static const char *PREF_KEY_RAMP_EASE_OFF = "ramp_e_off";
static const char *PREF_KEY_RAMP_POW_ON = "ramp_p_on";
static const char *PREF_KEY_RAMP_POW_OFF = "ramp_p_off";
static const char *PREF_KEY_LCLAMP_MIN = "lcl_min";
static const char *PREF_KEY_LCLAMP_MAX = "lcl_max";
static const char *PREF_KEY_LIGHT_ALPHA = "light_a";
static const char *PREF_KEY_MUSIC_GAIN = "mus_gain";
#if ENABLE_POTI
static const char *PREF_KEY_POTI_EN = "poti_en";
static const char *PREF_KEY_POTI_ALPHA = "poti_a";
static const char *PREF_KEY_POTI_DELTA = "poti_d";
static const char *PREF_KEY_POTI_OFF = "poti_off";
static const char *PREF_KEY_POTI_SAMPLE = "poti_s";
#endif
#if ENABLE_PUSH_BUTTON
static const char *PREF_KEY_PUSH_EN = "push_en";
static const char *PREF_KEY_PUSH_DB = "push_db";
static const char *PREF_KEY_PUSH_DBL = "push_dbl";
static const char *PREF_KEY_PUSH_HOLD = "push_hold";
static const char *PREF_KEY_PUSH_STEP_MS = "push_s_ms";
static const char *PREF_KEY_PUSH_STEP = "push_step";
#endif
static const char *PREF_KEY_PROFILE_BASE = "profile"; // profile slots profile1..profileN
static const uint8_t PROFILE_SLOTS = 3;
static const char *PREF_KEY_PWM_GAMMA = "pwm_g";

// ---------- Zustand ----------
// Active pattern state (index and start time)
size_t currentPattern = 0;
uint32_t patternStartMs = 0;
// General flags/state
bool autoCycle = Settings::DEFAULT_AUTOCYCLE;
float patternSpeedScale = 1.0f;
uint64_t quickMask = 0; // bitmask of modes used for quick switch tap cycling (supports up to 64 entries)
size_t currentModeIndex = 0; // tracks current mode (patterns + profile slots)
bool patternFadeEnabled = false;
float patternFadeStrength = 1.0f; // multiplier for smoothing duration (1.0 = rampDurationMs)
float patternFilteredLevel = 0.0f;
uint32_t patternFilterLastMs = 0;
// SOS shortcut snapshot
bool sosModeActive = false;
float sosPrevBrightness = Settings::DEFAULT_BRIGHTNESS;
size_t sosPrevPattern = 0;
bool sosPrevAutoCycle = false;
bool sosPrevLampOn = false;
// Demo cycling
bool demoActive = false;
uint32_t demoDwellMs = 6000;
std::vector<uint8_t> demoList;
size_t demoIndex = 0;
uint32_t demoLastSwitchMs = 0;

// Switch handling
#if ENABLE_SWITCH
bool switchRawState = false;
bool switchDebouncedState = false;
uint32_t switchLastDebounceMs = 0;
uint32_t lastSwitchOffMs = 0;
uint32_t lastSwitchOnMs = 0;
bool modeTapArmed = false;
#endif

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
// Notify blink
std::vector<uint32_t> notifySeq = {120, 60, 120, 200};
uint8_t notifyIdx = 0;
uint32_t notifyStageStartMs = 0;
bool notifyInvert = false;
bool notifyRestoreLamp = false;
bool notifyPrevLampOn = false;
bool notifyActive = false;
uint32_t notifyFadeMs = 0;

// Ramping and timers
uint32_t idleOffMs = Settings::DEFAULT_IDLE_OFF_MS;
#if ENABLE_LIGHT_SENSOR
bool lightSensorEnabled = Settings::LIGHT_SENSOR_DEFAULT_ENABLED;
float lightFiltered = 0.0f;
uint32_t lastLightSampleMs = 0;
uint16_t lightMinRaw = 4095;
uint16_t lightMaxRaw = 0;
float lightAlpha = Settings::LIGHT_ALPHA;
#endif
float lightGain = Settings::LIGHT_GAIN_DEFAULT;
float lightClampMin = Settings::LIGHT_CLAMP_MIN_DEFAULT;
float lightClampMax = Settings::LIGHT_CLAMP_MAX_DEFAULT;

#if ENABLE_MUSIC_MODE
bool musicEnabled = Settings::MUSIC_DEFAULT_ENABLED;
float musicFiltered = 0.0f;
float musicRawLevel = 0.0f;
uint32_t lastMusicSampleMs = 0;
float musicGain = Settings::MUSIC_GAIN_DEFAULT;
bool musicAutoLamp = false;
float musicAutoThr = 0.4f;
uint8_t musicMode = 0; // 0=direct,1=beat
float musicModScale = 1.0f;
float musicBeatEnv = 0.0f;
float musicBeatIntervalMs = 600.0f;
uint32_t musicLastBeatMs = 0;
bool clapEnabled = Settings::CLAP_DEFAULT_ENABLED;
float clapThreshold = Settings::CLAP_THRESHOLD_DEFAULT;
uint32_t clapCooldownMs = Settings::CLAP_COOLDOWN_MS_DEFAULT;
uint32_t clapLastMs = 0;
bool clapAbove = false;
String clapCmd1 = F("");
String clapCmd2 = F("toggle");
String clapCmd3 = F("");
uint8_t clapCount = 0;
uint32_t clapWindowStartMs = 0;
constexpr uint32_t CLAP_WINDOW_MS = 1200;
bool clapTraining = false;
uint32_t clapTrainLastLog = 0;
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
#endif

#if ENABLE_PUSH_BUTTON
uint32_t pushDebounceMs = Settings::PUSH_DEBOUNCE_MS;
uint32_t pushDoubleMs = Settings::PUSH_DOUBLE_MS;
uint32_t pushHoldMs = Settings::PUSH_HOLD_MS;
uint32_t pushStepMs = Settings::PUSH_BRI_STEP_MS;
float pushStep = Settings::PUSH_BRI_STEP;
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
bool wakeSoftCancel = false;

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

uint8_t easeFromString(const String &s)
{
  String l = s;
  l.toLowerCase();
  if (l == "linear")
    return 0;
  if (l == "ease")
    return 1;
  if (l == "ease-in" || l == "easein")
    return 2;
  if (l == "ease-out" || l == "easeout")
    return 3;
  if (l == "ease-in-out" || l == "easeinout")
    return 4;
  if (l == "flash")
    return 5;
  if (l == "wave")
    return 6;
  if (l == "blink")
    return 7;
  return 1;
}

String easeToString(uint8_t t)
{
  switch (t)
  {
  case 0:
    return F("linear");
  case 2:
    return F("ease-in");
  case 3:
    return F("ease-out");
  case 4:
    return F("ease-in-out");
  case 5:
    return F("flash");
  case 6:
    return F("wave");
  case 7:
    return F("blink");
  case 1:
  default:
    return F("ease");
  }
}

/**
 * @brief Build a profile string (cfg import style) without presence/touch/quick.
 */
String buildProfileString()
{
  String cfg;
  cfg += F("mode=");
  cfg += String(currentPattern + 1);
  cfg += F(" bri=");
  cfg += String(masterBrightness, 3);
  cfg += F(" auto=");
  cfg += autoCycle ? F("on") : F("off");
  cfg += F(" pat_scale=");
  cfg += String(patternSpeedScale, 2);
  cfg += F(" ramp=");
  cfg += rampDurationMs;
  cfg += F(" ramp_on_ms=");
  cfg += rampOnDurationMs;
  cfg += F(" ramp_off_ms=");
  cfg += rampOffDurationMs;
  cfg += F(" pat_fade=");
  cfg += patternFadeEnabled ? F("on") : F("off");
  cfg += F(" pat_fade_amt=");
  cfg += String(patternFadeStrength, 2);
  cfg += F(" bri_cap=");
  cfg += String(brightnessCap, 3);
  cfg += F(" ramp_on_ease=");
  cfg += easeToString(rampEaseOnType);
  cfg += F(" ramp_off_ease=");
  cfg += easeToString(rampEaseOffType);
  cfg += F(" ramp_on_pow=");
  cfg += String(rampEaseOnPower, 2);
  cfg += F(" ramp_off_pow=");
  cfg += String(rampEaseOffPower, 2);
  cfg += F(" bri_min=");
  cfg += String(briMinUser, 3);
  cfg += F(" bri_max=");
  cfg += String(briMaxUser, 3);
  cfg += F(" bri_cap=");
  cfg += String(brightnessCap, 3);
  cfg += F(" pwm_gamma=");
  cfg += String(outputGamma, 2);
#if ENABLE_LIGHT_SENSOR
  cfg += F(" light_gain=");
  cfg += String(lightGain, 2);
  cfg += F(" light_min=");
  cfg += String(lightClampMin, 2);
  cfg += F(" light_max=");
  cfg += String(lightClampMax, 2);
  cfg += F(" light_alpha=");
  cfg += String(lightAlpha, 3);
  cfg += F(" light=");
  cfg += lightSensorEnabled ? F("on") : F("off");
#endif
#if ENABLE_MUSIC_MODE
  cfg += F(" music=");
  cfg += musicEnabled ? F("on") : F("off");
  cfg += F(" music_gain=");
  cfg += String(musicGain, 2);
  cfg += F(" clap=");
  cfg += clapEnabled ? F("on") : F("off");
  cfg += F(" clap_thr=");
  cfg += String(clapThreshold, 2);
  cfg += F(" clap_cool=");
  cfg += String(clapCooldownMs);
#endif
  return cfg;
}

/**
 * @brief Default profiles for slots 1..3 (used if slot is empty).
 */
String defaultProfileString(uint8_t slot)
{
  String cfg;
  switch (slot)
  {
  case 1: // A: full brightness, constant
    cfg = F("mode=1 bri=1.0 auto=off pat_scale=1 pat_fade=on pat_fade_amt=0.01 ramp_on_ease=ease-out ramp_off_ease=ease-out ramp_on_pow=7 ramp_off_pow=2 ramp_on_ms=320 ramp_off_ms=600 bri_min=0.05 bri_max=0.95");
    break;
  case 2: // B: half brightness, constant
    cfg = F("mode=1 bri=0.5 auto=off pat_scale=1 pat_fade=on pat_fade_amt=0.01 ramp_on_ease=ease-out ramp_off_ease=ease-out ramp_on_pow=7 ramp_off_pow=2 ramp_on_ms=320 ramp_off_ms=600 bri_min=0.05 bri_max=0.95");
    break;
  case 3: // C: half brightness, pulsierend
    cfg = F("mode=5 bri=0.5 auto=off pat_scale=1 pat_fade=on pat_fade_amt=0.01 ramp_on_ease=ease-out ramp_off_ease=ease-out ramp_on_pow=7 ramp_off_pow=2 ramp_on_ms=320 ramp_off_ms=600 bri_min=0.05 bri_max=0.95");
    break;
  default:
    break;
  }
  return cfg;
}

size_t quickModeCount()
{
  return PATTERN_COUNT + PROFILE_SLOTS;
}

/**
 * @brief Apply a profile string (same keys as buildProfileString).
 */
void applyProfileString(const String &cfg)
{
  // reuse import parser but limited keys
  importConfig(cfg);
  // set pattern if present
  int mpos = cfg.indexOf("mode=");
  if (mpos >= 0)
  {
    int end = cfg.indexOf(' ', mpos);
    String v = (end >= 0) ? cfg.substring(mpos + 5, end) : cfg.substring(mpos + 5);
    int idx = v.toInt();
    if (idx >= 1 && (size_t)idx <= PATTERN_COUNT)
      setPattern((size_t)idx - 1, false, true);
  }
  saveSettings();
}

bool loadProfileSlot(uint8_t slot, bool announce = true)
{
  if (slot < 1 || slot > PROFILE_SLOTS)
    return false;
  String key = String(PREF_KEY_PROFILE_BASE) + String(slot);
  String cfg = prefs.getString(key.c_str(), "");
  if (cfg.length() == 0)
  {
    cfg = defaultProfileString(slot);
    if (cfg.length() > 0)
      prefs.putString(key.c_str(), cfg);
  }
  if (cfg.length() == 0)
  {
    if (announce)
      sendFeedback(F("[Profile] Slot empty"));
    return false;
  }
  applyProfileString(cfg);
  currentModeIndex = PATTERN_COUNT + (slot - 1);
  if (announce)
  {
    sendFeedback(String(F("[Profile] Loaded slot ")) + String(slot));
    printStatus();
  }
  return true;
}

/**
 * @brief Build default quick-cycle mask: the three profile slots by default.
 */
uint64_t computeDefaultQuickMask()
{
  // Default: the three profile slots (mode indices PATTERN_COUNT .. PATTERN_COUNT+2)
  uint64_t mask = 0;
  for (size_t slot = 0; slot < PROFILE_SLOTS && slot < 3; ++slot)
  {
    size_t idx = PATTERN_COUNT + slot;
    if (idx < 64)
      mask |= (1ULL << idx);
  }
  // Fallback to the first profile bit if nothing set (should not happen)
  if (mask == 0 && PROFILE_SLOTS > 0 && PATTERN_COUNT < 64)
    mask = (1ULL << PATTERN_COUNT);
  return mask;
}

/**
 * @brief Clamp the quick-mode mask to available patterns and ensure non-empty.
 */
void sanitizeQuickMask()
{
  size_t total = quickModeCount();
  uint64_t limitMask = (total >= 64) ? 0xFFFFFFFFFFFFFFFFULL : ((1ULL << total) - 1ULL);
  quickMask &= limitMask;
  if (quickMask == 0)
    quickMask = computeDefaultQuickMask() & limitMask;
}

bool isQuickEnabled(size_t idx)
{
  size_t total = quickModeCount();
  return idx < total && idx < 64 && (quickMask & (1ULL << idx));
}

size_t nextQuickMode(size_t from)
{
  size_t total = quickModeCount();
  if (total == 0)
    return 0;
  for (size_t step = 1; step <= total; ++step)
  {
    size_t idx = (from + step) % total;
    if (isQuickEnabled(idx))
      return idx;
  }
  return from;
}

void applyQuickMode(size_t idx)
{
  if (idx < PATTERN_COUNT)
  {
    setPattern(idx, true, true);
  }
  else
  {
    uint8_t slot = (uint8_t)(idx - PATTERN_COUNT + 1);
    if (!loadProfileSlot(slot, true))
    {
      sendFeedback(F("[Quick] Profile slot empty"));
    }
  }
}

/**
 * @brief Convert quick-mask to comma-separated 1-based indices.
 */
String quickMaskToCsv()
{
  String out;
  size_t total = quickModeCount();
  for (size_t i = 0; i < total; ++i)
  {
    if (isQuickEnabled(i))
    {
      if (!out.isEmpty())
        out += ',';
      out += String(i + 1);
    }
  }
  return out.isEmpty() ? String(F("none")) : out;
}

/**
 * @brief Build and start a demo cycle through quick-enabled modes with fixed dwell.
 */
void startDemo(uint32_t dwellMs)
{
  demoList.clear();
  size_t total = quickModeCount();
  for (size_t i = 0; i < total; ++i)
  {
    if (isQuickEnabled(i))
      demoList.push_back((uint8_t)i);
  }
  if (demoList.empty())
  {
    demoActive = false;
    sendFeedback(F("[Demo] Quick list empty"));
    return;
  }
  demoDwellMs = dwellMs;
  if (demoDwellMs < 500)
    demoDwellMs = 500;
  if (demoDwellMs > 600000)
    demoDwellMs = 600000;
  demoIndex = 0;
  demoLastSwitchMs = millis();
  demoActive = true;
  applyQuickMode(demoList[demoIndex]);
  sendFeedback(String(F("[Demo] Start dwell=")) + String(demoDwellMs) + F("ms list=") + quickMaskToCsv());
}

void stopDemo()
{
  demoActive = false;
  sendFeedback(F("[Demo] Stopped"));
}

bool parseQuickCsv(const String &csv, uint64_t &outMask)
{
  String tmp = csv;
  tmp.replace(',', ' ');
  outMask = 0;
  size_t total = quickModeCount();
  int start = 0;
  while (start < tmp.length())
  {
    while (start < tmp.length() && isspace(tmp[start]))
      start++;
    if (start >= tmp.length())
      break;
    int end = start;
    while (end < tmp.length() && !isspace(tmp[end]))
      end++;
    String tok = tmp.substring(start, end);
    int idx = tok.toInt();
    if (idx >= 1 && idx <= (int)total && idx <= 64)
      outMask |= (1ULL << (idx - 1));
    start = end + 1;
  }
  return outMask != 0;
}

void exportConfig()
{
  String cfg = F("cfg import ");
  cfg += F("bri=");
  cfg += String(masterBrightness, 3);
  cfg += F(" auto=");
  cfg += autoCycle ? F("on") : F("off");
  cfg += F(" pat_scale=");
  cfg += String(patternSpeedScale, 2);
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
  cfg += F(" light_min=");
  cfg += String(lightClampMin, 2);
  cfg += F(" light_max=");
  cfg += String(lightClampMax, 2);
  cfg += F(" bri_min=");
  cfg += String(briMinUser, 3);
  cfg += F(" bri_max=");
  cfg += String(briMaxUser, 3);
  cfg += F(" pres_grace=");
  cfg += presenceGraceMs;
  cfg += F(" pat_fade=");
  cfg += patternFadeEnabled ? F("on") : F("off");
  cfg += F(" pat_fade_amt=");
  cfg += String(patternFadeStrength, 2);
  cfg += F(" ramp_on_ease=");
  cfg += easeToString(rampEaseOnType);
  cfg += F(" ramp_off_ease=");
  cfg += easeToString(rampEaseOffType);
  cfg += F(" ramp_on_pow=");
  cfg += String(rampEaseOnPower, 2);
  cfg += F(" ramp_off_pow=");
  cfg += String(rampEaseOffPower, 2);
  cfg += F(" quick=");
  cfg += quickMaskToCsv();
#if ENABLE_MUSIC_MODE
  cfg += F(" music_gain=");
  cfg += String(musicGain, 2);
#endif
  if (notifyActive)
    cfg += F(" notify=active");
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
  prefs.putFloat(PREF_KEY_PAT_SCALE, patternSpeedScale);
  prefs.putShort(PREF_KEY_THR_ON, (int16_t)touchDeltaOn);
  prefs.putShort(PREF_KEY_THR_OFF, (int16_t)touchDeltaOff);
  prefs.putUInt(PREF_KEY_TOUCH_HOLD, touchHoldStartMs);
  prefs.putFloat(PREF_KEY_PAT_SCALE, patternSpeedScale);
  prefs.putBool(PREF_KEY_PRESENCE_EN, presenceEnabled);
  prefs.putString(PREF_KEY_PRESENCE_ADDR, presenceAddr);
  prefs.putFloat(PREF_KEY_BRI_CAP, brightnessCap);
  prefs.putUInt(PREF_KEY_RAMP_MS, rampDurationMs);
  prefs.putUInt(PREF_KEY_RAMP_ON_MS, rampOnDurationMs);
  prefs.putUInt(PREF_KEY_RAMP_OFF_MS, rampOffDurationMs);
  prefs.putUInt(PREF_KEY_IDLE_OFF, idleOffMs);
  prefs.putUChar(PREF_KEY_RAMP_EASE_ON, rampEaseOnType);
  prefs.putUChar(PREF_KEY_RAMP_EASE_OFF, rampEaseOffType);
  prefs.putFloat(PREF_KEY_RAMP_POW_ON, rampEaseOnPower);
  prefs.putFloat(PREF_KEY_RAMP_POW_OFF, rampEaseOffPower);
#if ENABLE_LIGHT_SENSOR
  prefs.putBool(PREF_KEY_LS_EN, lightSensorEnabled);
  lightMinRaw = 4095;
  lightMaxRaw = 0;
  prefs.putFloat(PREF_KEY_LCLAMP_MIN, lightClampMin);
  prefs.putFloat(PREF_KEY_LCLAMP_MAX, lightClampMax);
  prefs.putFloat(PREF_KEY_LIGHT_ALPHA, lightAlpha);
#endif
  prefs.putUInt(PREF_KEY_CUSTOM_MS, customStepMs);
  prefs.putBytes(PREF_KEY_CUSTOM, customPattern, sizeof(float) * customLen);
#if ENABLE_MUSIC_MODE
  prefs.putBool(PREF_KEY_MUSIC_EN, musicEnabled);
  prefs.putFloat(PREF_KEY_MUSIC_GAIN, musicGain);
  prefs.putBool(PREF_KEY_MUSIC_AUTOLAMP, musicAutoLamp);
  prefs.putFloat(PREF_KEY_MUSIC_AUTOTHR, musicAutoThr);
  prefs.putUChar(PREF_KEY_MUSIC_MODE, musicMode);
  prefs.putBool(PREF_KEY_CLAP_EN, clapEnabled);
  prefs.putFloat(PREF_KEY_CLAP_THR, clapThreshold);
  prefs.putUInt(PREF_KEY_CLAP_COOL, clapCooldownMs);
  prefs.putString(PREF_KEY_CLAP_CMD1, clapCmd1);
  prefs.putString(PREF_KEY_CLAP_CMD2, clapCmd2);
  prefs.putString(PREF_KEY_CLAP_CMD3, clapCmd3);
#endif
#if ENABLE_POTI
  prefs.putBool(PREF_KEY_POTI_EN, potiEnabled);
  prefs.putFloat(PREF_KEY_POTI_ALPHA, potiAlpha);
  prefs.putFloat(PREF_KEY_POTI_DELTA, potiDeltaMin);
  prefs.putFloat(PREF_KEY_POTI_OFF, potiOffThreshold);
  prefs.putUInt(PREF_KEY_POTI_SAMPLE, potiSampleMs);
#endif
#if ENABLE_PUSH_BUTTON
  prefs.putBool(PREF_KEY_PUSH_EN, pushEnabled);
  prefs.putUInt(PREF_KEY_PUSH_DB, pushDebounceMs);
  prefs.putUInt(PREF_KEY_PUSH_DBL, pushDoubleMs);
  prefs.putUInt(PREF_KEY_PUSH_HOLD, pushHoldMs);
  prefs.putUInt(PREF_KEY_PUSH_STEP_MS, pushStepMs);
  prefs.putFloat(PREF_KEY_PUSH_STEP, pushStep);
#endif
  prefs.putBool(PREF_KEY_TOUCH_DIM, touchDimEnabled);
  prefs.putFloat(PREF_KEY_LIGHT_GAIN, lightGain);
  prefs.putFloat(PREF_KEY_BRI_MIN, briMinUser);
  prefs.putFloat(PREF_KEY_BRI_MAX, briMaxUser);
  prefs.putUInt(PREF_KEY_PRES_GRACE, presenceGraceMs);
  prefs.putBool(PREF_KEY_PAT_FADE, patternFadeEnabled);
  prefs.putFloat(PREF_KEY_PAT_FADE_AMT, patternFadeStrength);
  prefs.putUInt(PREF_KEY_QUICK_MASK, (uint32_t)(quickMask & 0xFFFFFFFFULL));
  prefs.putUInt(PREF_KEY_QUICK_MASK_HI, (uint32_t)(quickMask >> 32));
  prefs.putFloat(PREF_KEY_PWM_GAMMA, outputGamma);
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
  if (strcmp(PATTERNS[currentPattern].name, "SOS") == 0)
    currentPattern = 0;
  autoCycle = prefs.getBool(PREF_KEY_AUTO, Settings::DEFAULT_AUTOCYCLE);
  patternSpeedScale = prefs.getFloat(PREF_KEY_PAT_SCALE, 1.0f);
  if (patternSpeedScale < 0.1f)
    patternSpeedScale = 0.1f;
  else if (patternSpeedScale > 5.0f)
    patternSpeedScale = 5.0f;
  touchDeltaOn = prefs.getShort(PREF_KEY_THR_ON, TOUCH_DELTA_ON_DEFAULT);
  touchDeltaOff = prefs.getShort(PREF_KEY_THR_OFF, TOUCH_DELTA_OFF_DEFAULT);
  if (touchDeltaOn < 1)
    touchDeltaOn = TOUCH_DELTA_ON_DEFAULT;
  if (touchDeltaOff < 1 || touchDeltaOff >= touchDeltaOn)
    touchDeltaOff = TOUCH_DELTA_OFF_DEFAULT;
  patternSpeedScale = prefs.getFloat(PREF_KEY_PAT_SCALE, 1.0f);
  if (patternSpeedScale < 0.1f)
    patternSpeedScale = 0.1f;
  else if (patternSpeedScale > 5.0f)
    patternSpeedScale = 5.0f;
  touchHoldStartMs = prefs.getUInt(PREF_KEY_TOUCH_HOLD, Settings::TOUCH_HOLD_MS_DEFAULT);
  if (touchHoldStartMs < 500)
    touchHoldStartMs = 500;
  else if (touchHoldStartMs > 5000)
    touchHoldStartMs = 5000;
  {
    uint64_t lo = prefs.getUInt(PREF_KEY_QUICK_MASK, (uint32_t)(computeDefaultQuickMask() & 0xFFFFFFFFULL));
    uint64_t hi = prefs.getUInt(PREF_KEY_QUICK_MASK_HI, 0);
    quickMask = (hi << 32) | lo;
  }
  sanitizeQuickMask();
  patternFadeEnabled = prefs.getBool(PREF_KEY_PAT_FADE, false);
  patternFadeStrength = prefs.getFloat(PREF_KEY_PAT_FADE_AMT, 1.0f);
  if (patternFadeStrength < 0.01f)
    patternFadeStrength = 0.01f;
  if (patternFadeStrength > 10.0f)
    patternFadeStrength = 10.0f;
  presenceEnabled = prefs.getBool(PREF_KEY_PRESENCE_EN, Settings::PRESENCE_DEFAULT_ENABLED);
  presenceAddr = prefs.getString(PREF_KEY_PRESENCE_ADDR, "");
  brightnessCap = prefs.getFloat(PREF_KEY_BRI_CAP, Settings::BRI_CAP_DEFAULT);
  if (brightnessCap < briMinUser)
    brightnessCap = briMinUser;
  if (brightnessCap > 1.0f)
    brightnessCap = 1.0f;
  rampDurationMs = prefs.getUInt(PREF_KEY_RAMP_MS, Settings::DEFAULT_RAMP_MS);
  if (rampDurationMs < 50)
    rampDurationMs = Settings::DEFAULT_RAMP_MS;
  rampOnDurationMs = prefs.getUInt(PREF_KEY_RAMP_ON_MS, Settings::DEFAULT_RAMP_ON_MS);
  rampOffDurationMs = prefs.getUInt(PREF_KEY_RAMP_OFF_MS, Settings::DEFAULT_RAMP_OFF_MS);
  if (rampOnDurationMs < 50)
    rampOnDurationMs = Settings::DEFAULT_RAMP_ON_MS;
  if (rampOffDurationMs < 50)
    rampOffDurationMs = Settings::DEFAULT_RAMP_OFF_MS;
  idleOffMs = prefs.getUInt(PREF_KEY_IDLE_OFF, Settings::DEFAULT_IDLE_OFF_MS);
  rampEaseOnType = prefs.getUChar(PREF_KEY_RAMP_EASE_ON, Settings::DEFAULT_RAMP_EASE_ON);
  rampEaseOffType = prefs.getUChar(PREF_KEY_RAMP_EASE_OFF, Settings::DEFAULT_RAMP_EASE_OFF);
  rampEaseOnPower = prefs.getFloat(PREF_KEY_RAMP_POW_ON, Settings::DEFAULT_RAMP_POW_ON);
  rampEaseOffPower = prefs.getFloat(PREF_KEY_RAMP_POW_OFF, Settings::DEFAULT_RAMP_POW_OFF);
  if (rampEaseOnType > 5)
    rampEaseOnType = Settings::DEFAULT_RAMP_EASE_ON;
  if (rampEaseOffType > 5)
    rampEaseOffType = Settings::DEFAULT_RAMP_EASE_OFF;
  if (rampEaseOnPower < 0.01f)
    rampEaseOnPower = Settings::DEFAULT_RAMP_POW_ON;
  if (rampEaseOffPower < 0.01f)
    rampEaseOffPower = Settings::DEFAULT_RAMP_POW_OFF;
  if (rampEaseOnPower > 10.0f)
    rampEaseOnPower = 10.0f;
  if (rampEaseOffPower > 10.0f)
    rampEaseOffPower = 10.0f;
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
    // Default custom pattern: gentle wave / pulse
    const float defVals[] = {0.1f, 0.3f, 0.6f, 0.9f, 0.6f, 0.3f};
    customLen = sizeof(defVals) / sizeof(defVals[0]);
    for (size_t i = 0; i < customLen && i < CUSTOM_MAX; ++i)
      customPattern[i] = defVals[i];
    customStepMs = 600;
  }
#if ENABLE_MUSIC_MODE
  musicEnabled = prefs.getBool(PREF_KEY_MUSIC_EN, Settings::MUSIC_DEFAULT_ENABLED);
  musicGain = prefs.getFloat(PREF_KEY_MUSIC_GAIN, Settings::MUSIC_GAIN_DEFAULT);
  if (musicGain < 0.1f) musicGain = 0.1f;
  if (musicGain > 5.0f) musicGain = 5.0f;
  musicAutoLamp = prefs.getBool(PREF_KEY_MUSIC_AUTOLAMP, false);
  musicAutoThr = prefs.getFloat(PREF_KEY_MUSIC_AUTOTHR, 0.4f);
  if (musicAutoThr < 0.05f) musicAutoThr = 0.05f;
  if (musicAutoThr > 1.5f) musicAutoThr = 1.5f;
  musicMode = prefs.getUChar(PREF_KEY_MUSIC_MODE, 0);
  if (musicMode > 1) musicMode = 0;
  clapEnabled = prefs.getBool(PREF_KEY_CLAP_EN, Settings::CLAP_DEFAULT_ENABLED);
  clapThreshold = prefs.getFloat(PREF_KEY_CLAP_THR, Settings::CLAP_THRESHOLD_DEFAULT);
  clapCooldownMs = prefs.getUInt(PREF_KEY_CLAP_COOL, Settings::CLAP_COOLDOWN_MS_DEFAULT);
  clapCmd1 = prefs.getString(PREF_KEY_CLAP_CMD1, clapCmd1);
  clapCmd2 = prefs.getString(PREF_KEY_CLAP_CMD2, clapCmd2);
  clapCmd3 = prefs.getString(PREF_KEY_CLAP_CMD3, clapCmd3);
  if (clapThreshold < 0.05f) clapThreshold = 0.05f;
  if (clapThreshold > 1.5f) clapThreshold = 1.5f;
  if (clapCooldownMs < 200) clapCooldownMs = 200;
#endif
#if ENABLE_POTI
  potiEnabled = prefs.getBool(PREF_KEY_POTI_EN, true);
  potiAlpha = prefs.getFloat(PREF_KEY_POTI_ALPHA, Settings::POTI_ALPHA);
  if (potiAlpha < 0.01f) potiAlpha = 0.01f;
  if (potiAlpha > 1.0f) potiAlpha = 1.0f;
  potiDeltaMin = prefs.getFloat(PREF_KEY_POTI_DELTA, Settings::POTI_DELTA_MIN);
  if (potiDeltaMin < 0.001f) potiDeltaMin = 0.001f;
  if (potiDeltaMin > 0.5f) potiDeltaMin = 0.5f;
  potiOffThreshold = prefs.getFloat(PREF_KEY_POTI_OFF, Settings::POTI_OFF_THRESHOLD);
  if (potiOffThreshold < 0.0f) potiOffThreshold = 0.0f;
  if (potiOffThreshold > 0.5f) potiOffThreshold = 0.5f;
  potiSampleMs = prefs.getUInt(PREF_KEY_POTI_SAMPLE, Settings::POTI_SAMPLE_MS);
  if (potiSampleMs < 10) potiSampleMs = 10;
  if (potiSampleMs > 2000) potiSampleMs = 2000;
#endif
#if ENABLE_PUSH_BUTTON
  pushEnabled = prefs.getBool(PREF_KEY_PUSH_EN, true);
  pushDebounceMs = prefs.getUInt(PREF_KEY_PUSH_DB, Settings::PUSH_DEBOUNCE_MS);
  if (pushDebounceMs < 5) pushDebounceMs = 5;
  if (pushDebounceMs > 500) pushDebounceMs = 500;
  pushDoubleMs = prefs.getUInt(PREF_KEY_PUSH_DBL, Settings::PUSH_DOUBLE_MS);
  if (pushDoubleMs < 100) pushDoubleMs = 100;
  if (pushDoubleMs > 5000) pushDoubleMs = 5000;
  pushHoldMs = prefs.getUInt(PREF_KEY_PUSH_HOLD, Settings::PUSH_HOLD_MS);
  if (pushHoldMs < 200) pushHoldMs = 200;
  if (pushHoldMs > 6000) pushHoldMs = 6000;
  pushStepMs = prefs.getUInt(PREF_KEY_PUSH_STEP_MS, Settings::PUSH_BRI_STEP_MS);
  if (pushStepMs < 50) pushStepMs = 50;
  if (pushStepMs > 2000) pushStepMs = 2000;
  pushStep = prefs.getFloat(PREF_KEY_PUSH_STEP, Settings::PUSH_BRI_STEP);
  if (pushStep < 0.005f) pushStep = 0.005f;
  if (pushStep > 0.5f) pushStep = 0.5f;
#endif
  outputGamma = prefs.getFloat(PREF_KEY_PWM_GAMMA, Settings::PWM_GAMMA_DEFAULT);
  if (outputGamma < 0.5f || outputGamma > 4.0f)
    outputGamma = Settings::PWM_GAMMA_DEFAULT;
  touchDimEnabled = prefs.getBool(PREF_KEY_TOUCH_DIM, Settings::TOUCH_DIM_DEFAULT_ENABLED);
#if ENABLE_LIGHT_SENSOR
  lightGain = prefs.getFloat(PREF_KEY_LIGHT_GAIN, Settings::LIGHT_GAIN_DEFAULT);
  lightClampMin = prefs.getFloat(PREF_KEY_LCLAMP_MIN, Settings::LIGHT_CLAMP_MIN_DEFAULT);
  lightClampMax = prefs.getFloat(PREF_KEY_LCLAMP_MAX, Settings::LIGHT_CLAMP_MAX_DEFAULT);
  lightAlpha = prefs.getFloat(PREF_KEY_LIGHT_ALPHA, Settings::LIGHT_ALPHA);
  if (lightAlpha < 0.001f) lightAlpha = 0.001f;
  if (lightAlpha > 0.8f) lightAlpha = 0.8f;
  if (lightClampMin < 0.0f) lightClampMin = 0.0f;
  if (lightClampMax > 1.5f) lightClampMax = 1.0f;
  if (lightClampMin >= lightClampMax) {
    lightClampMin = Settings::LIGHT_CLAMP_MIN_DEFAULT;
    lightClampMax = Settings::LIGHT_CLAMP_MAX_DEFAULT;
  }
#endif
  briMinUser = prefs.getFloat(PREF_KEY_BRI_MIN, Settings::BRI_MIN_DEFAULT);
  briMaxUser = prefs.getFloat(PREF_KEY_BRI_MAX, Settings::BRI_MAX_DEFAULT);
  presenceGraceMs = prefs.getUInt(PREF_KEY_PRES_GRACE, Settings::PRESENCE_GRACE_MS_DEFAULT);
#if ENABLE_LIGHT_SENSOR
  lastLoggedBrightness = masterBrightness;
  lightMinRaw = 4095;
  lightMaxRaw = 0;
#endif
  currentModeIndex = currentPattern;
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
 * @brief Find the index of a pattern by name (case-insensitive), -1 if missing.
 */
int findPatternIndexByName(const char *name)
{
  if (!name)
    return -1;
  for (size_t i = 0; i < PATTERN_COUNT; ++i)
  {
    if (String(PATTERNS[i].name).equalsIgnoreCase(name))
      return (int)i;
  }
  return -1;
}

/**
 * @brief Change the active pattern and optionally announce/persist it.
 */
void setPattern(size_t index, bool announce, bool persist)
{
  if (index >= PATTERN_COUNT)
  {
    index = 0;
  }
  currentPattern = index;
  currentModeIndex = index;
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
        size_t from = currentModeIndex;
        if (from >= quickModeCount())
          from = 0;
        size_t next = nextQuickMode(from);
        applyQuickMode(next);
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
#endif

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
void startWakeFade(uint32_t durationMs, bool announce, bool softCancel, float targetOverride)
{
  if (durationMs < 5000)
    durationMs = 5000;
  wakeDurationMs = durationMs;
  wakeStartMs = millis();
  if (targetOverride >= 0.0f)
    wakeTargetLevel = clamp01(targetOverride);
  else
    wakeTargetLevel = clamp01(fmax(masterBrightness, Settings::WAKE_MIN_TARGET));
  wakeSoftCancel = softCancel;
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
  wakeSoftCancel = false;
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
  float cap = brightnessCap;
  if (cap < briMinUser) cap = briMinUser;
  if (cap > 1.0f) cap = 1.0f;
  float target = clamp01(percent / 100.0f);
  if (target > cap) target = cap;
  uint32_t dur = (target >= masterBrightness) ? rampOnDurationMs : rampOffDurationMs;
  startBrightnessRamp(target, dur);
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
                 PATTERNS[currentPattern].name + F("' | AutoCycle=") + (autoCycle ? F("ON") : F("OFF")) +
                 F(" | Speed=") + String(patternSpeedScale, 2);
  if (wakeFadeActive)
    line1 += F(" | Wake");
  if (sleepFadeActive)
    line1 += F(" | Sleep");
  sendFeedback(line1);
  payload += line1 + '\n';

  String quickLine = String(F("[Quick] ")) + quickMaskToCsv();
  sendFeedback(quickLine);
  payload += quickLine + '\n';

  String line2 = String(F("Lamp=")) + (lampEnabled ? F("ON") : F("OFF")) + F(" | Brightness=") +
                 String(masterBrightness * 100.0f, 1) + F("% | Cap=") + String(brightnessCap * 100.0f, 1) + F("%");
#if ENABLE_SWITCH
  line2 += F(" | Switch=");
  line2 += (switchDebouncedState ? F("ON") : F("OFF"));
#endif
  sendFeedback(line2);
  payload += line2 + '\n';

#if ENABLE_MUSIC_MODE
  String clapLine = String(F("[Clap] ")) + (clapEnabled ? F("ON") : F("OFF")) + F(" thr=") + String(clapThreshold, 2) + F(" cool=") + String(clapCooldownMs);
  sendFeedback(clapLine);
  payload += clapLine + '\n';
#endif

  String line3 = F("Ramp=");
  line3 += String(rampOnDurationMs);
  line3 += F("ms (on) / ");
  line3 += String(rampOffDurationMs);
  line3 += F("ms (off) | IdleOff=");
  if (idleOffMs == 0)
    line3 += F("off");
  else
  {
    line3 += String(idleOffMs / 60000);
    line3 += F("m");
  }
  line3 += F(" | TouchDim=");
  line3 += touchDimEnabled ? F("ON") : F("OFF");
  line3 += F(" | PatFade=");
  if (patternFadeEnabled)
  {
    line3 += String(F("ON(")) + String(patternFadeStrength, 2) + F("x)");
  }
  else
  {
    line3 += F("OFF");
  }
  line3 += F(" | RampOn=");
  line3 += easeToString(rampEaseOnType);
  line3 += F("(");
  line3 += String(rampEaseOnPower, 2);
  line3 += F(")");
  line3 += F(" | RampOff=");
  line3 += easeToString(rampEaseOffType);
  line3 += F("(");
  line3 += String(rampEaseOffPower, 2);
  line3 += F(")");
  line3 += F(" | PWM=");
  line3 += String(outputGamma, 2);
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

  if (demoActive)
  {
    String demoLine = String(F("[Demo] dwell=")) + String(demoDwellMs) + F("ms list=") + quickMaskToCsv();
    sendFeedback(demoLine);
    payload += demoLine + '\n';
  }

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
                 F(" max=") + String((int)lightMaxRaw) + F(" alpha=") + String(lightAlpha, 3);
  }
  else
  {
    lightLine += F("off");
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
#if ENABLE_POTI
  String potiLine = String(F("[Poti] ")) + (potiEnabled ? F("ON") : F("OFF")) + F(" a=") + String(potiAlpha, 2) +
                    F(" d=") + String(potiDeltaMin, 3) + F(" off=") + String(potiOffThreshold, 3) +
                    F(" smpl=") + String(potiSampleMs) + F("ms");
#else
  String potiLine = F("[Poti] N/A");
#endif
  sendFeedback(potiLine);
  payload += potiLine + '\n';

#if ENABLE_PUSH_BUTTON
  String pushLine = String(F("[Push] ")) + (pushEnabled ? F("ON") : F("OFF")) + F(" db=") + String(pushDebounceMs) +
                    F(" dbl=") + String(pushDoubleMs) + F(" hold=") + String(pushHoldMs) +
                    F(" step=") + String(pushStep * 100.0f, 1) + F("%/") + String(pushStepMs) + F("ms");
#else
  String pushLine = F("[Push] N/A");
#endif
  sendFeedback(pushLine);
  payload += pushLine + '\n';

  updateBleStatus(payload);
  printStatusStructured();
}

/**
 * @brief Emit structured sensor snapshot for machine parsing.
 */
void printSensorsStructured()
{
  String line = F("SENSORS|");
  line += F("touch_base=");
  line += String(touchBaseline);
  int raw = touchRead(PIN_TOUCH_DIM);
  line += F("|touch_raw=");
  line += String(raw);
  line += F("|touch_delta=");
  line += String(touchBaseline - raw);
  line += F("|touch_active=");
  line += touchActive ? F("1") : F("0");
#if ENABLE_LIGHT_SENSOR
  line += F("|light_raw=");
  line += String((int)lightFiltered);
  line += F("|light_min=");
  line += String((int)lightMinRaw);
  line += F("|light_max=");
  line += String((int)lightMaxRaw);
#else
  line += F("|light_raw=N/A");
#endif
#if ENABLE_MUSIC_MODE
  line += F("|music_env=");
  line += String(musicFiltered, 3);
  line += F("|music_auto=");
  line += musicAutoLamp ? F("ON") : F("OFF");
  line += F("|music_thr=");
  line += String(musicAutoThr, 2);
  line += F("|music_mode=");
  line += (musicMode == 1) ? F("beat") : F("direct");
  line += F("|music_mod=");
  line += String(musicModScale, 3);
#else
  line += F("|music_env=N/A");
#endif
  sendFeedback(line);
}
/**
 * @brief Emit a single structured status line for easier parsing (key=value pairs).
 */
void printStatusStructured()
{
  String line = F("STATUS|");
  line += F("pattern=");
  line += String(currentPattern + 1);
  line += F("|pattern_total=");
  line += String(PATTERN_COUNT);
  line += F("|pattern_name=");
  line += PATTERNS[currentPattern].name;
  line += F("|pat_ms=");
  line += String(millis() - patternStartMs);
  line += F("|auto=");
  line += autoCycle ? F("1") : F("0");
  line += F("|bri=");
  line += String(masterBrightness * 100.0f, 1);
  line += F("|cap=");
  line += String(brightnessCap * 100.0f, 1);
  line += F("|lamp=");
  line += lampEnabled ? F("ON") : F("OFF");
#if ENABLE_SWITCH
  line += F("|switch=");
  line += switchDebouncedState ? F("ON") : F("OFF");
#else
  line += F("|switch=N/A");
#endif
  line += F("|touch_dim=");
  line += touchDimEnabled ? F("1") : F("0");
  line += F("|ramp_on_ms=");
  line += String(rampOnDurationMs);
  line += F("|ramp_off_ms=");
  line += String(rampOffDurationMs);
  line += F("|ramp_on_ease=");
  line += easeToString(rampEaseOnType);
  line += F("|ramp_off_ease=");
  line += easeToString(rampEaseOffType);
  line += F("|ramp_on_pow=");
  line += String(rampEaseOnPower, 2);
  line += F("|ramp_off_pow=");
  line += String(rampEaseOffPower, 2);
  line += F("|idle_min=");
  line += idleOffMs == 0 ? F("0") : String(idleOffMs / 60000);
  line += F("|pat_speed=");
  line += String(patternSpeedScale, 2);
  line += F("|pat_fade=");
  line += patternFadeEnabled ? String(patternFadeStrength, 2) : F("off");
  line += F("|quick=");
  line += quickMaskToCsv();
  line += F("|presence=");
  line += presenceEnabled ? (presenceAddr.isEmpty() ? F("ON(no-dev)") : presenceAddr) : F("OFF");
  line += F("|custom_len=");
  line += String(customLen);
  line += F("|custom_step_ms=");
  line += String(customStepMs);
  line += F("|demo=");
  line += demoActive ? F("ON") : F("OFF");
  line += F("|gamma=");
  line += String(outputGamma, 2);
  line += F("|bri_min=");
  line += String(briMinUser * 100.0f, 1);
  line += F("|bri_max=");
  line += String(briMaxUser * 100.0f, 1);
#if ENABLE_LIGHT_SENSOR
  line += F("|light=");
  line += lightSensorEnabled ? F("ON") : F("OFF");
  line += F("|light_gain=");
  line += String(lightGain, 2);
  line += F("|light_min=");
  line += String(lightClampMin, 2);
  line += F("|light_max=");
  line += String(lightClampMax, 2);
  line += F("|light_alpha=");
  line += String(lightAlpha, 3);
  line += F("|light_raw=");
  line += String((int)lightFiltered);
  line += F("|light_raw_min=");
  line += String((int)lightMinRaw);
  line += F("|light_raw_max=");
  line += String((int)lightMaxRaw);
#else
  line += F("|light=N/A");
#endif
#if ENABLE_MUSIC_MODE
  line += F("|music=");
  line += musicEnabled ? F("ON") : F("OFF");
  line += F("|music_gain=");
  line += String(musicGain, 2);
  line += F("|music_auto=");
  line += musicAutoLamp ? F("ON") : F("OFF");
  line += F("|music_thr=");
  line += String(musicAutoThr, 2);
  line += F("|music_mode=");
  line += (musicMode == 1 ? F("beat") : F("direct"));
  line += F("|music_mod=");
  line += String(musicModScale, 3);
  line += F("|music_env=");
  line += String(musicFiltered, 3);
  line += F("|music_level=");
  line += String(musicRawLevel, 3);
  line += F("|clap=");
  line += clapEnabled ? F("ON") : F("OFF");
  line += F("|clap_thr=");
  line += String(clapThreshold, 2);
  line += F("|clap_cool=");
  line += String(clapCooldownMs);
#else
  line += F("|music=N/A|clap=N/A");
#endif
#if ENABLE_POTI
  line += F("|poti=");
  line += potiEnabled ? F("ON") : F("OFF");
  line += F("|poti_alpha=");
  line += String(potiAlpha, 2);
  line += F("|poti_delta=");
  line += String(potiDeltaMin, 3);
  line += F("|poti_off=");
  line += String(potiOffThreshold, 3);
  line += F("|poti_sample=");
  line += String(potiSampleMs);
#else
  line += F("|poti=N/A");
#endif
#if ENABLE_PUSH_BUTTON
  line += F("|push=");
  line += pushEnabled ? F("ON") : F("OFF");
  line += F("|push_db=");
  line += String(pushDebounceMs);
  line += F("|push_dbl=");
  line += String(pushDoubleMs);
  line += F("|push_hold=");
  line += String(pushHoldMs);
  line += F("|push_step_ms=");
  line += String(pushStepMs);
  line += F("|push_step=");
  line += String(pushStep, 3);
#else
  line += F("|push=N/A");
#endif
  sendFeedback(line);
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
      if (v >= 50 && v <= 10000) {
        rampDurationMs = v;
        rampOnDurationMs = v;
        rampOffDurationMs = v;
      }
    }
    else if (key == "ramp_on_ms")
    {
      uint32_t v = val.toInt();
      if (v >= 50 && v <= 10000)
        rampOnDurationMs = v;
    }
    else if (key == "ramp_off_ms")
    {
      uint32_t v = val.toInt();
      if (v >= 50 && v <= 10000)
        rampOffDurationMs = v;
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
    else if (key == "pat_scale")
    {
      float v = val.toFloat();
      if (v >= 0.1f && v <= 5.0f)
        patternSpeedScale = v;
    }
    else if (key == "pat_fade")
    {
      bool v;
      if (parseBool(val, v))
        patternFadeEnabled = v;
    }
    else if (key == "pat_fade_amt")
    {
      float v = val.toFloat();
      if (v >= 0.01f && v <= 10.0f)
        patternFadeStrength = v;
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
#if ENABLE_LIGHT_SENSOR
      lightGain = val.toFloat();
      if (lightGain < 0.1f)
        lightGain = 0.1f;
      if (lightGain > 5.0f)
        lightGain = 5.0f;
#endif
    }
    else if (key == "light_alpha")
    {
#if ENABLE_LIGHT_SENSOR
      float v = val.toFloat();
      if (v < 0.001f)
        v = 0.001f;
      if (v > 0.8f)
        v = 0.8f;
      lightAlpha = v;
#endif
    }
    else if (key == "light_min")
    {
#if ENABLE_LIGHT_SENSOR
      float v = val.toFloat();
      if (v >= 0.0f && v <= 1.0f)
        lightClampMin = v;
#endif
    }
    else if (key == "light_max")
    {
#if ENABLE_LIGHT_SENSOR
      float v = val.toFloat();
      if (v >= 0.0f && v <= 1.5f)
        lightClampMax = v;
#endif
    }
    else if (key == "bri_min")
    {
      briMinUser = clamp01(val.toFloat());
    }
    else if (key == "bri_max")
    {
      briMaxUser = clamp01(val.toFloat());
    }
    else if (key == "bri_cap")
    {
      float v = clamp01(val.toFloat());
      if (v < briMinUser)
        v = briMinUser;
      brightnessCap = v;
      if (briMaxUser > brightnessCap)
        briMaxUser = brightnessCap;
    }
    else if (key == "pres_grace")
    {
      uint32_t v = val.toInt();
      if (v < 0)
        v = 0;
      presenceGraceMs = v;
    }
#if ENABLE_MUSIC_MODE
    else if (key == "music_gain")
    {
      float v = val.toFloat();
      if (v >= 0.1f && v <= 5.0f)
        musicGain = v;
    }
    else if (key == "clap")
    {
      bool v;
      if (parseBool(val, v))
        clapEnabled = v;
    }
    else if (key == "clap_thr")
    {
      float v = val.toFloat();
      if (v >= 0.05f && v <= 1.5f)
        clapThreshold = v;
    }
    else if (key == "clap_cool")
    {
      uint32_t v = val.toInt();
      if (v >= 200 && v <= 5000)
        clapCooldownMs = v;
    }
#endif
    else if (key == "ramp_on_ease")
    {
      rampEaseOnType = easeFromString(val);
    }
    else if (key == "ramp_off_ease")
    {
      rampEaseOffType = easeFromString(val);
    }
    else if (key == "ramp_on_pow")
    {
      float v = val.toFloat();
      if (v >= 0.01f && v <= 10.0f)
        rampEaseOnPower = v;
    }
  else if (key == "ramp_off_pow")
  {
    float v = val.toFloat();
    if (v >= 0.01f && v <= 10.0f)
      rampEaseOffPower = v;
  }
  else if (key == "ramp_on_ms")
  {
    uint32_t v = val.toInt();
    if (v >= 50 && v <= 10000)
      rampOnDurationMs = v;
  }
  else if (key == "ramp_off_ms")
  {
    uint32_t v = val.toInt();
    if (v >= 50 && v <= 10000)
      rampOffDurationMs = v;
  }
    else if (key == "pwm_gamma")
    {
      float v = val.toFloat();
      if (v >= 0.5f && v <= 4.0f)
        outputGamma = v;
    }
    else if (key == "quick")
    {
      uint64_t mask = 0;
      if (val.equalsIgnoreCase(F("default")) || val.equalsIgnoreCase(F("none")))
      {
        mask = computeDefaultQuickMask();
      }
      else
      {
        parseQuickCsv(val, mask);
      }
      if (mask != 0)
      {
        quickMask = mask;
        sanitizeQuickMask();
      }
    }
  }
  if (lightClampMin < 0.0f)
    lightClampMin = Settings::LIGHT_CLAMP_MIN_DEFAULT;
  if (lightClampMax > 1.5f)
    lightClampMax = Settings::LIGHT_CLAMP_MAX_DEFAULT;
  if (lightClampMin >= lightClampMax)
  {
    lightClampMin = Settings::LIGHT_CLAMP_MIN_DEFAULT;
    lightClampMax = Settings::LIGHT_CLAMP_MAX_DEFAULT;
  }
#if ENABLE_MUSIC_MODE
  if (musicGain < 0.1f)
    musicGain = Settings::MUSIC_GAIN_DEFAULT;
  if (musicGain > 5.0f)
    musicGain = 5.0f;
#endif
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
      "  quick <CSV|default>- Modi für schnellen Schalter-Tap",
      "  on / off / toggle - Lampe schalten",
      "  sync             - Lampe an Schalterzustand angleichen",
      "  auto on|off       - automatisches Durchschalten",
      "  bri <0..100>      - globale Helligkeit in %",
      "  bri min/max <0..1>- Min/Max-Level setzen",
      "  wake [soft] [mode=N] [bri=XX] <Sek> - Weckfade (Default 180s, optional weich/Mode/Bri)",
      "  wake stop         - Weckfade abbrechen",
      "  sos [stop]        - SOS-Alarm: Lampe 100%, SOS-Muster",
      "  sleep [Minuten]   - Sleep-Fade auf 0, Default 15min",
      "  sleep stop        - Sleep-Fade abbrechen",
      "  ramp <ms>         - Ramp-Dauer (on/off gemeinsam) 50-10000ms",
      "  ramp on <ms>      - Ramp-Dauer nur für Einschalten",
      "  ramp off <ms>     - Ramp-Dauer nur für Ausschalten",
      "  ramp ease on|off <linear|ease|ease-in|ease-out|ease-in-out|flash> [power]",
      "  idleoff <Min>     - Auto-Off nach X Minuten (0=aus)",
      "  touch tune <on> <off> - Touch-Schwellen setzen",
      "  pat scale <0.1-5> - Pattern-Geschwindigkeit",
      "  pat fade on|off   - Pattern-Ausgabe glätten",
      "  pat fade amt <0.01-10> - Stärke der Glättung (größer = langsamer)",
      "  pwm curve <0.5-4> - PWM-Gamma/Linearität anpassen",
      "  demo [Sek]        - Demo-Modus: Quick-Liste mit fester Verweildauer (Default 6s)",
      "  touch hold <ms>   - Hold-Start 500..5000 ms",
      "  touchdim on/off   - Touch-Dimmen aktivieren/deaktivieren",
      "  clap on|off/thr <0..1>/cool <ms>/train [on|off] - Klatschsteuerung (Audio)",
      "  clap <1|2|3> <cmd> - Befehl bei 1/2/3 Klatschen",
      "  presence on|off   - Auto-Off wenn Gerät weg",
      "  presence set <addr>/clear - Gerät binden oder löschen",
      "  presence grace <ms> - Verzögerung vor Auto-Off",
      "  custom v1,v2,...   - Custom-Pattern setzen (0..1)",
      "  custom step <ms>   - Schrittzeit Custom-Pattern",
      "  notify [on1 off1 on2 off2] - Blinksignal (ms)",
      "  morse <text>     - Morse-Blink (dot=200ms, dash=600ms)",
      "  profile save <1-3>/load <1-3> - User-Profile ohne Touch/Presence/Quick",
      "  light gain <f>     - Verstärkung Lichtsensor",
      "  poti on|off/alpha <0..1>/delta <0..0.5>/off <0..0.5>/sample <ms> - Poti-Config",
      "  push on|off/debounce <ms>/double <ms>/hold <ms>/step_ms <ms>/step <0..0.5> - Taster-Config",
      "  music on|off       - Music-Mode (ADC) aktivieren",
      "  midi map           - CC7=bri, CC20=mode(1-8), Note59 toggle, Note60 prev, Note62 next, Note70-77 mode 1-8",
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
  // Reserve virtual slots for profiles after patterns
  for (uint8_t p = 1; p <= PROFILE_SLOTS; ++p)
    sendFeedback(String(PATTERN_COUNT + p) + F(": Profile ") + String(p));
}

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

#if ENABLE_MUSIC_MODE
void executeClapCommand(uint8_t count)
{
  String cmd = (count == 1) ? clapCmd1 : (count == 2) ? clapCmd2 : clapCmd3;
  cmd.trim();
  if (cmd.isEmpty())
    return;
  sendFeedback(String(F("[Clap] ")) + count + F("x -> ") + cmd);
  if (cmd.startsWith(F("clap ")))
    return; // avoid recursion
  handleCommand(cmd);
}
#else
void executeClapCommand(uint8_t)
{
  // Music/clap disabled at build; ignore
}
#endif

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
  if (lower.startsWith("quick"))
  {
    String args = line.substring(5);
    args.trim();
    if (args.isEmpty() || args.equalsIgnoreCase(F("default")))
    {
      quickMask = computeDefaultQuickMask();
      sanitizeQuickMask();
      saveSettings();
      sendFeedback(String(F("[Quick] default -> ")) + quickMaskToCsv());
      return;
    }
    uint64_t mask = 0;
    if (parseQuickCsv(args, mask))
    {
      quickMask = mask;
      sanitizeQuickMask();
      saveSettings();
      sendFeedback(String(F("[Quick] set -> ")) + quickMaskToCsv());
    }
    else
    {
      sendFeedback(F("Usage: quick <idx,idx,...> or quick default"));
    }
    return;
  }
  if (lower == "status")
  {
    printStatus();
    return;
  }
  if (lower == "status raw" || lower == "status json")
  {
    printStatusStructured();
    return;
  }
  if (lower == "sensors" || lower == "read sensors")
  {
    printSensorsStructured();
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
  if (lower == "sync")
  {
    syncLampToSwitch();
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
    if (args.length() == 0 || args == "export")
    {
      String csv;
      for (size_t i = 0; i < customLen; ++i)
      {
        if (i > 0)
          csv += ',';
        csv += String(customPattern[i], 3);
      }
      String msg = String(F("CUSTOM|len=")) + String(customLen) + F("|step=") + String(customStepMs) + F("|vals=") + csv;
      sendFeedback(msg);
      return;
    }
    if (args.startsWith("step"))
    {
      uint32_t v = args.substring(4).toInt();
      if (v >= 20 && v <= 5000)
      {
        customStepMs = v;
        saveSettings();
        sendFeedback(String(F("[Custom] step ms=")) + String(v));
      }
      else
      {
        sendFeedback(F("Usage: custom step <20-5000>"));
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
    else if (idx > (long)PATTERN_COUNT && idx <= (long)PATTERN_COUNT + PROFILE_SLOTS)
    {
      int slot = idx - (long)PATTERN_COUNT;
      loadProfileSlot((uint8_t)slot, true);
    }
    else
    {
      sendFeedback(F("Ungültiger Mode."));
    }
    return;
  }
  if (lower.startsWith("pat scale") || lower.startsWith("pattern scale"))
  {
    int pos = lower.indexOf("scale");
    float v = line.substring(pos + 5).toFloat();
    if (v >= 0.1f && v <= 5.0f)
    {
      patternSpeedScale = v;
      saveSettings();
      sendFeedback(String(F("[Pattern] speed scale=")) + String(v, 2));
    }
    else
    {
      sendFeedback(F("Usage: pat scale 0.1-5"));
    }
    return;
  }
  if (lower.startsWith("pat fade") || lower.startsWith("pattern fade"))
  {
    int pos = lower.indexOf("fade");
    String arg = line.substring(pos + 4);
    arg.trim();
  if (arg.startsWith("amt"))
  {
    float v = arg.substring(3).toFloat();
    if (v >= 0.01f && v <= 10.0f)
    {
      patternFadeStrength = v;
      saveSettings();
      sendFeedback(String(F("[Pattern] fade amt=")) + String(v, 2));
    }
    else
    {
      sendFeedback(F("Usage: pat fade amt 0.01-10.0"));
    }
  }
    else
    {
      bool v;
      if (parseBool(arg, v))
      {
        patternFadeEnabled = v;
        saveSettings();
        sendFeedback(String(F("[Pattern] fade ")) + (v ? F("ON") : F("OFF")));
      }
      else
      {
        sendFeedback(F("Usage: pat fade on|off|amt"));
      }
    }
    return;
  }
  if (lower.startsWith("pwm curve") || lower.startsWith("pwm gamma"))
  {
    int pos = lower.indexOf("curve");
    if (pos < 0)
      pos = lower.indexOf("gamma");
    float v = line.substring(pos + 5).toFloat();
    if (v >= 0.5f && v <= 4.0f)
    {
      outputGamma = v;
      saveSettings();
      sendFeedback(String(F("[PWM] gamma=")) + String(v, 2));
    }
    else
    {
      sendFeedback(F("Usage: pwm curve 0.5-4.0"));
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
  if (lower.startsWith("bri cap"))
  {
    float v = line.substring(7).toFloat();
    v = clamp01(v / 100.0f);
    if (v < briMinUser)
      v = briMinUser;
    brightnessCap = v;
    if (briMaxUser > brightnessCap)
      briMaxUser = brightnessCap;
    saveSettings();
    sendFeedback(String(F("[Bri] cap=")) + String(brightnessCap * 100.0f, 1) + F("%"));
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
  if (lower.startsWith("demo"))
  {
    String arg = line.substring(4);
    arg.trim();
    arg.toLowerCase();
    if (arg == "off" || arg == "stop")
    {
      stopDemo();
    }
    else
    {
      uint32_t dwellMs = 6000;
      if (arg.length() > 0)
      {
        float v = arg.toFloat();
        if (v > 0.0f)
          dwellMs = (uint32_t)(v * 1000.0f);
      }
      startDemo(dwellMs);
    }
    return;
  }
  if (lower.startsWith("ramp"))
  {
    String arg = line.substring(4);
    arg.trim();
    if (lower.startsWith("ramp ease"))
    {
      arg = arg.substring(arg.indexOf("ease") + 4);
      arg.trim();
      bool isOn = arg.startsWith("on");
      bool isOff = arg.startsWith("off");
      if (isOn || isOff)
      {
        arg = arg.substring(isOn ? 2 : 3);
        arg.trim();
      }
      String typeToken;
      float power = -1.0f;
      int space = arg.indexOf(' ');
      if (space >= 0)
      {
        typeToken = arg.substring(0, space);
        power = arg.substring(space + 1).toFloat();
      }
      else
      {
        typeToken = arg;
      }
      uint8_t etype = easeFromString(typeToken);
      if (isnan(power) || power < 0.01f)
        power = 2.0f;
      if (power > 10.0f)
        power = 10.0f;
      if (!isOn && !isOff)
      {
        rampEaseOnType = rampEaseOffType = etype;
        rampEaseOnPower = rampEaseOffPower = power;
        sendFeedback(String(F("[Ramp] ease on/off ")) + easeToString(etype) + F(" pow=") + String(power, 2));
      }
      else if (isOn)
      {
        rampEaseOnType = etype;
        rampEaseOnPower = power;
        sendFeedback(String(F("[Ramp] ease on ")) + easeToString(etype) + F(" pow=") + String(power, 2));
      }
      else if (isOff)
      {
        rampEaseOffType = etype;
        rampEaseOffPower = power;
      sendFeedback(String(F("[Ramp] ease off ")) + easeToString(etype) + F(" pow=") + String(power, 2));
      }
      saveSettings();
    }
    else
    {
      bool isOn = arg.startsWith("on");
      bool isOff = arg.startsWith("off");
      if (isOn || isOff)
      {
        arg = arg.substring(isOn ? 2 : 3);
        arg.trim();
      }
      uint32_t val = arg.toInt();
      if (val >= 50 && val <= 10000)
      {
        if (!isOn && !isOff)
        {
          rampDurationMs = val;
          rampOnDurationMs = val;
          rampOffDurationMs = val;
          sendFeedback(String(F("[Ramp] on/off=")) + String(val) + F(" ms"));
        }
        else if (isOn)
        {
          rampOnDurationMs = val;
          sendFeedback(String(F("[Ramp] on=")) + String(val) + F(" ms"));
        }
        else if (isOff)
        {
          rampOffDurationMs = val;
          sendFeedback(String(F("[Ramp] off=")) + String(val) + F(" ms"));
        }
        saveSettings();
      }
      else
      {
        sendFeedback(F("Usage: ramp <50-10000> | ramp on <ms> | ramp off <ms>"));
      }
    }
    return;
  }
  if (lower.startsWith("notify"))
  {
    std::vector<uint32_t> seq;
    notifyFadeMs = 0;
    String args = line.substring(6);
    args.trim();
    while (args.length() > 0)
    {
      int sp = args.indexOf(' ');
      String tok = (sp >= 0) ? args.substring(0, sp) : args;
      args = (sp >= 0) ? args.substring(sp + 1) : "";
      args.trim();
      if (tok.startsWith("fade"))
      {
        int eq = tok.indexOf('=');
        if (eq >= 0)
          tok = tok.substring(eq + 1);
        uint32_t f = (uint32_t)tok.toInt();
        if (f > 0)
          notifyFadeMs = f;
      }
      else
      {
        uint32_t v = (uint32_t)tok.toInt();
        if (v > 0)
          seq.push_back(v);
      }
    }
    if (seq.empty())
      seq = {120, 60, 120, 200};
    notifySeq = seq;
    notifyIdx = 0;
    notifyStageStartMs = millis();
    notifyInvert = (masterBrightness > 0.8f);
    notifyActive = true;
    notifyPrevLampOn = lampEnabled;
    notifyRestoreLamp = !lampEnabled;
    if (notifyRestoreLamp)
      setLampEnabled(true, "notify");
    String seqStr;
    for (size_t i = 0; i < notifySeq.size(); ++i)
    {
      if (i)
        seqStr += F("/");
      seqStr += String(notifySeq[i]);
    }
    sendFeedback(String(F("[Notify] ")) + seqStr + (notifyInvert ? F(" invert") : F("")));
    return;
  }
  if (lower.startsWith("morse"))
  {
    String text = line.substring(5);
    text.trim();
    if (text.isEmpty())
    {
      sendFeedback(F("Usage: morse <text>"));
      return;
    }
    auto symbol = [](char c) -> const char * {
      switch (c)
      {
      case 'A': return ".-";
      case 'B': return "-...";
      case 'C': return "-.-.";
      case 'D': return "-..";
      case 'E': return ".";
      case 'F': return "..-.";
      case 'G': return "--.";
      case 'H': return "....";
      case 'I': return "..";
      case 'J': return ".---";
      case 'K': return "-.-";
      case 'L': return ".-..";
      case 'M': return "--";
      case 'N': return "-.";
      case 'O': return "---";
      case 'P': return ".--.";
      case 'Q': return "--.-";
      case 'R': return ".-.";
      case 'S': return "...";
      case 'T': return "-";
      case 'U': return "..-";
      case 'V': return "...-";
      case 'W': return ".--";
      case 'X': return "-..-";
      case 'Y': return "-.--";
      case 'Z': return "--..";
      case '1': return ".----";
      case '2': return "..---";
      case '3': return "...--";
      case '4': return "....-";
      case '5': return ".....";
      case '6': return "-....";
      case '7': return "--...";
      case '8': return "---..";
      case '9': return "----.";
      case '0': return "-----";
      default: return nullptr;
      }
    };

    std::vector<uint32_t> seq;
    auto addOnOff = [&](uint32_t on, uint32_t off) {
      seq.push_back(on);
      seq.push_back(off);
    };

    text.toUpperCase();
    for (size_t i = 0; i < text.length(); ++i)
    {
      char c = text.charAt(i);
      if (c == ' ')
      {
        if (!seq.empty())
          seq.back() = 1400; // word gap
        continue;
      }
      const char *code = symbol(c);
      if (!code)
        continue;
      for (const char *p = code; *p; ++p)
      {
        if (*p == '.')
          addOnOff(200, 200);
        else if (*p == '-')
          addOnOff(600, 200);
      }
      if (!seq.empty())
        seq.back() = 600; // letter gap
    }
    if (seq.empty())
    {
      sendFeedback(F("[Morse] no valid symbols"));
      return;
    }
    notifySeq = seq;
    notifyIdx = 0;
    notifyStageStartMs = millis();
    notifyFadeMs = 0;
    notifyInvert = (masterBrightness > 0.8f);
    notifyActive = true;
    notifyPrevLampOn = lampEnabled;
    notifyRestoreLamp = !lampEnabled;
    if (notifyRestoreLamp)
      setLampEnabled(true, "morse");
    sendFeedback(String(F("[Morse] ")) + text);
    return;
  }
  if (lower == "notify stop")
  {
    notifyActive = false;
    if (!notifyPrevLampOn)
      setLampEnabled(false, "notify stop");
    sendFeedback(F("[Notify] stopped"));
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
    else if (arg.startsWith("calib"))
    {
      String which = arg.substring(5);
      which.trim();
      int raw = analogRead(Settings::LIGHT_PIN);
      if (which == "min")
      {
        lightFiltered = raw;
        lightMinRaw = raw;
        if ((int)lightMaxRaw <= (int)lightMinRaw)
          lightMaxRaw = (uint16_t)min(4095, lightMinRaw + 50);
        sendFeedback(String(F("[Light] Calibrated min raw=")) + String(raw) + F(" max=") + String((int)lightMaxRaw));
      }
      else if (which == "max")
      {
        lightFiltered = raw;
        lightMaxRaw = raw;
        if ((int)lightMinRaw >= (int)lightMaxRaw)
          lightMinRaw = (uint16_t)((lightMaxRaw > 50) ? (lightMaxRaw - 50) : 0);
        sendFeedback(String(F("[Light] Calibrated max raw=")) + String(raw) + F(" min=") + String((int)lightMinRaw));
      }
      else
      {
        lightFiltered = raw;
        lightMinRaw = raw;
        lightMaxRaw = raw;
        sendFeedback(String(F("[Light] Calibrated raw=")) + String(raw));
      }
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
    else if (arg.startsWith("alpha"))
    {
      float a = arg.substring(5).toFloat();
      if (a < 0.001f)
        a = 0.001f;
      if (a > 0.8f)
        a = 0.8f;
      lightAlpha = a;
      saveSettings();
      sendFeedback(String(F("[Light] alpha=")) + String(a, 3));
    }
    else if (arg.startsWith("clamp"))
    {
      int pos = arg.indexOf(' ');
      String restClamp = pos >= 0 ? arg.substring(pos + 1) : "";
      restClamp.trim();
      float mn = restClamp.toFloat();
      int pos2 = restClamp.indexOf(' ');
      float mx = (pos2 >= 0) ? restClamp.substring(pos2 + 1).toFloat() : lightClampMax;
      if (mn < 0.0f) mn = 0.0f;
      if (mx > 1.5f) mx = 1.5f;
      if (mn >= mx)
      {
        sendFeedback(F("[Light] clamp invalid (min>=max)"));
      }
      else
      {
        lightClampMin = mn;
        lightClampMax = mx;
        saveSettings();
        sendFeedback(String(F("[Light] clamp ")) + String(mn, 2) + F("..") + String(mx, 2));
      }
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
#if ENABLE_POTI
  if (lower.startsWith("poti"))
  {
    String arg = line.substring(4);
    arg.trim();
    arg.toLowerCase();
    if (arg == "on")
    {
      potiEnabled = true;
      saveSettings();
      sendFeedback(F("[Poti] Enabled"));
    }
    else if (arg == "off")
    {
      potiEnabled = false;
      saveSettings();
      sendFeedback(F("[Poti] Disabled"));
    }
    else if (arg.startsWith("alpha"))
    {
      float v = arg.substring(5).toFloat();
      if (v >= 0.01f && v <= 1.0f)
      {
        potiAlpha = v;
        saveSettings();
        sendFeedback(String(F("[Poti] alpha=")) + String(v, 2));
      }
      else
      {
        sendFeedback(F("Usage: poti alpha 0.01-1.0"));
      }
    }
    else if (arg.startsWith("delta"))
    {
      float v = arg.substring(5).toFloat();
      if (v >= 0.001f && v <= 0.5f)
      {
        potiDeltaMin = v;
        saveSettings();
        sendFeedback(String(F("[Poti] delta=")) + String(v, 3));
      }
      else
      {
        sendFeedback(F("Usage: poti delta 0.001-0.5"));
      }
    }
    else if (arg.startsWith("off"))
    {
      float v = arg.substring(3).toFloat();
      if (v >= 0.0f && v <= 0.5f)
      {
        potiOffThreshold = v;
        saveSettings();
        sendFeedback(String(F("[Poti] off=")) + String(v, 3));
      }
      else
      {
        sendFeedback(F("Usage: poti off 0.0-0.5"));
      }
    }
    else if (arg.startsWith("sample"))
    {
      uint32_t v = arg.substring(6).toInt();
      if (v >= 10 && v <= 2000)
      {
        potiSampleMs = v;
        saveSettings();
        sendFeedback(String(F("[Poti] sample=")) + String(v) + F("ms"));
      }
      else
      {
        sendFeedback(F("Usage: poti sample 10-2000"));
      }
    }
    else
    {
      sendFeedback(String(F("[Poti] ")) + (potiEnabled ? F("ON ") : F("OFF ")) + F("a=") + String(potiAlpha, 2) +
                   F(" d=") + String(potiDeltaMin, 3) + F(" off=") + String(potiOffThreshold, 3) +
                   F(" smpl=") + String(potiSampleMs) + F("ms"));
    }
    return;
  }
#endif
#if ENABLE_PUSH_BUTTON
  if (lower.startsWith("push"))
  {
    String arg = line.substring(4);
    arg.trim();
    arg.toLowerCase();
    if (arg == "on")
    {
      pushEnabled = true;
      saveSettings();
      sendFeedback(F("[Push] Enabled"));
    }
    else if (arg == "off")
    {
      pushEnabled = false;
      saveSettings();
      sendFeedback(F("[Push] Disabled"));
    }
    else if (arg.startsWith("debounce"))
    {
      uint32_t v = arg.substring(8).toInt();
      if (v >= 5 && v <= 500)
      {
        pushDebounceMs = v;
        saveSettings();
        sendFeedback(String(F("[Push] debounce=")) + String(v) + F("ms"));
      }
      else
      {
        sendFeedback(F("Usage: push debounce 5-500"));
      }
    }
    else if (arg.startsWith("double"))
    {
      uint32_t v = arg.substring(6).toInt();
      if (v >= 100 && v <= 5000)
      {
        pushDoubleMs = v;
        saveSettings();
        sendFeedback(String(F("[Push] double=")) + String(v) + F("ms"));
      }
      else
      {
        sendFeedback(F("Usage: push double 100-5000"));
      }
    }
    else if (arg.startsWith("hold"))
    {
      uint32_t v = arg.substring(4).toInt();
      if (v >= 200 && v <= 6000)
      {
        pushHoldMs = v;
        saveSettings();
        sendFeedback(String(F("[Push] hold=")) + String(v) + F("ms"));
      }
      else
      {
        sendFeedback(F("Usage: push hold 200-6000"));
      }
    }
    else if (arg.startsWith("step_ms"))
    {
      uint32_t v = arg.substring(7).toInt();
      if (v >= 50 && v <= 2000)
      {
        pushStepMs = v;
        saveSettings();
        sendFeedback(String(F("[Push] step_ms=")) + String(v) + F("ms"));
      }
      else
      {
        sendFeedback(F("Usage: push step_ms 50-2000"));
      }
    }
    else if (arg.startsWith("step"))
    {
      float v = arg.substring(4).toFloat();
      if (v >= 0.005f && v <= 0.5f)
      {
        pushStep = v;
        saveSettings();
        sendFeedback(String(F("[Push] step=")) + String(v * 100.0f, 1) + F("%"));
      }
      else
      {
        sendFeedback(F("Usage: push step 0.005-0.5"));
      }
    }
    else
    {
      sendFeedback(String(F("[Push] ")) + (pushEnabled ? F("ON ") : F("OFF ")) + F("db=") + String(pushDebounceMs) +
                   F(" dbl=") + String(pushDoubleMs) + F(" hold=") + String(pushHoldMs) +
                   F(" step=") + String(pushStep * 100.0f, 1) + F("%/") + String(pushStepMs) + F("ms"));
    }
    return;
  }
#endif
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
      musicModScale = 1.0f;
      musicBeatEnv = 0.0f;
      musicBeatIntervalMs = 600.0f;
      musicLastBeatMs = 0;
      saveSettings();
      sendFeedback(F("[Music] Disabled"));
    }
    else if (arg.startsWith("sens"))
    {
      float g = arg.substring(4).toFloat();
      if (g < 0.1f) g = 0.1f;
      if (g > 5.0f) g = 5.0f;
      musicGain = g;
      saveSettings();
      sendFeedback(String(F("[Music] gain=")) + String(g, 2));
    }
    else if (arg.startsWith("mode"))
    {
      String m = arg.substring(4);
      m.trim();
      m.toLowerCase();
      if (m == "beat")
        musicMode = 1;
      else
        musicMode = 0;
      // Reset beat tracking when changing mode
      musicModScale = 1.0f;
      musicBeatEnv = 0.0f;
      musicBeatIntervalMs = 600.0f;
      musicLastBeatMs = 0;
      saveSettings();
      sendFeedback(String(F("[Music] mode=")) + (musicMode == 1 ? F("beat") : F("direct")));
    }
    else if (arg == "raw")
    {
      int raw = analogRead(Settings::MUSIC_PIN);
      sendFeedback(String(F("[Music] raw=")) + String(raw));
    }
    else if (arg.startsWith("auto"))
    {
      String rest = arg.substring(4);
      rest.trim();
      rest.toLowerCase();
      if (rest == "on")
      {
        musicAutoLamp = true;
        saveSettings();
        sendFeedback(F("[Music] auto lamp ON"));
      }
      else if (rest == "off")
      {
        musicAutoLamp = false;
        saveSettings();
        sendFeedback(F("[Music] auto lamp OFF"));
      }
      else if (rest.startsWith("thr"))
      {
        float v = rest.substring(3).toFloat();
        if (v < 0.05f) v = 0.05f;
        if (v > 1.5f) v = 1.5f;
        musicAutoThr = v;
        saveSettings();
        sendFeedback(String(F("[Music] auto thr=")) + String(v, 2));
      }
      else
      {
        sendFeedback(F("Usage: music auto on|off|thr <val>"));
      }
    }
    else
    {
      sendFeedback(String(F("[Music] level=")) + String(musicFiltered, 3) + F(" en=") + (musicEnabled ? F("1") : F("0")));
    }
    return;
  }
#endif
  if (lower.startsWith("clap"))
  {
#if ENABLE_MUSIC_MODE
    String argRaw = line.substring(4);
    argRaw.trim();
    String arg = argRaw;
    arg.toLowerCase();
    if (arg == "on")
    {
      clapEnabled = true;
      saveSettings();
      sendFeedback(F("[Clap] Enabled"));
    }
    else if (arg == "off")
    {
      clapEnabled = false;
      clapCount = 0;
      clapWindowStartMs = 0;
      saveSettings();
      sendFeedback(F("[Clap] Disabled"));
    }
    else if (arg.startsWith("thr"))
    {
      float v = arg.substring(3).toFloat();
      if (v >= 0.05f && v <= 1.5f)
      {
        clapThreshold = v;
        saveSettings();
        sendFeedback(String(F("[Clap] thr=")) + String(v, 2));
      }
      else
      {
        sendFeedback(F("Usage: clap thr 0.05-1.5"));
      }
    }
    else if (arg.startsWith("cool"))
    {
      uint32_t v = arg.substring(4).toInt();
      if (v >= 200 && v <= 5000)
      {
        clapCooldownMs = v;
        saveSettings();
        sendFeedback(String(F("[Clap] cool=")) + String(v) + F("ms"));
      }
      else
      {
        sendFeedback(F("Usage: clap cool 200-5000"));
      }
    }
    else if (arg.startsWith("train"))
    {
      String mode = arg.substring(5);
      mode.trim();
      mode.toLowerCase();
      if (mode == "on" || mode.length() == 0)
      {
        clapTraining = true;
        clapTrainLastLog = 0;
        sendFeedback(F("[Clap] Training ON"));
      }
      else if (mode == "off")
      {
        clapTraining = false;
        sendFeedback(F("[Clap] Training OFF"));
      }
      else
      {
        sendFeedback(F("Usage: clap train [on|off]"));
      }
    }
    else if (arg.startsWith("1 ") || arg.startsWith("2 ") || arg.startsWith("3 "))
    {
      uint8_t count = arg[0] - '0';
      String cmd = argRaw.substring(1);
      cmd.trim();
      if (cmd.length() == 0)
      {
        sendFeedback(F("Usage: clap <1|2|3> <command>"));
      }
      else
      {
        if (count == 1) clapCmd1 = cmd;
        else if (count == 2) clapCmd2 = cmd;
        else clapCmd3 = cmd;
        saveSettings();
        sendFeedback(String(F("[Clap] ")) + count + F("x -> ") + cmd);
      }
    }
    else
    {
      sendFeedback(String(F("[Clap] ")) + (clapEnabled ? F("ON ") : F("OFF ")) + F("thr=") + String(clapThreshold, 2) + F(" cool=") + String(clapCooldownMs));
    }
#else
    sendFeedback(F("[Clap] Audio sensor not built (ENABLE_MUSIC_MODE=0)"));
#endif
    return;
  }
  if (lower.startsWith("wake"))
  {
    String rawArgs = line.substring(4);
    rawArgs.trim();
    String lowerArgs = rawArgs;
    lowerArgs.toLowerCase();
    if (lowerArgs == "stop" || lowerArgs == "cancel")
    {
      cancelWakeFade(true);
      return;
    }
    String args = rawArgs;
    bool soft = false;
    int modeIdx = -1;
    float briPct = -1.0f;
    float seconds = -1.0f;
    while (args.length() > 0)
    {
      int sp = args.indexOf(' ');
      String tok = (sp >= 0) ? args.substring(0, sp) : args;
      args = (sp >= 0) ? args.substring(sp + 1) : "";
      tok.trim();
      if (tok.length() == 0)
        continue;
      String ltok = tok;
      ltok.toLowerCase();
      if (ltok == "soft")
      {
        soft = true;
      }
      else if (ltok.startsWith("mode="))
      {
        int v = ltok.substring(5).toInt();
        if (v >= 1 && (size_t)v <= PATTERN_COUNT)
          modeIdx = v;
      }
      else if (ltok.startsWith("bri="))
      {
        float v = tok.substring(4).toFloat();
        if (v >= 0.0f && v <= 100.0f)
          briPct = v;
      }
      else if (seconds < 0.0f)
      {
        float v = tok.toFloat();
        if (v > 0.0f)
          seconds = v;
      }
    }
    uint32_t durationMs = Settings::DEFAULT_WAKE_MS;
    if (seconds > 0.0f)
      durationMs = (uint32_t)(seconds * 1000.0f);
    if (modeIdx >= 1)
      setPattern((size_t)modeIdx - 1, true, false);
    float targetOverride = -1.0f;
    if (briPct >= 0.0f)
    {
      targetOverride = clamp01(briPct / 100.0f);
      masterBrightness = targetOverride;
      logBrightnessChange("wake bri");
    }
    startWakeFade(durationMs, true, soft, targetOverride);
    return;
  }
  if (lower.startsWith("sos"))
  {
    String arg = line.substring(3);
    arg.trim();
    if (arg.equalsIgnoreCase(F("stop")) || arg.equalsIgnoreCase(F("cancel")))
    {
      if (!sosModeActive)
      {
        sendFeedback(F("[SOS] Nicht aktiv"));
      }
      else
      {
        autoCycle = sosPrevAutoCycle;
        setBrightnessPercent(sosPrevBrightness * 100.0f, false);
        size_t restoreIdx = (sosPrevPattern < PATTERN_COUNT) ? sosPrevPattern : 0;
        setPattern(restoreIdx, true, false);
        sosModeActive = false;
        notifyActive = false;
        sleepFadeActive = false;
        wakeFadeActive = false;
        if (sosPrevLampOn)
          setLampEnabled(true, "sos stop");
        else
          setLampEnabled(false, "sos stop");
        saveSettings();
        sendFeedback(F("[SOS] beendet, Zustand wiederhergestellt"));
      }
    }
    else
    {
      if (!sosModeActive)
      {
        sosPrevBrightness = masterBrightness;
        sosPrevPattern = currentPattern;
        sosPrevAutoCycle = autoCycle;
        sosPrevLampOn = lampEnabled;
      }
      autoCycle = false;
      sleepFadeActive = false;
      wakeFadeActive = false;
      notifyActive = false;
      notifyRestoreLamp = true;
      notifyPrevLampOn = lampEnabled;
      setLampEnabled(true, "cmd sos");
      setBrightnessPercent(100.0f, false);
      int sosIdx = findPatternIndexByName("SOS");
      if (sosIdx >= 0)
        setPattern((size_t)sosIdx, true, false);
      sosModeActive = true;
      sendFeedback(F("[SOS] aktiv (100% Helligkeit)"));
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
    patternSpeedScale = 1.0f;
    touchDeltaOn = TOUCH_DELTA_ON_DEFAULT;
    touchDeltaOff = TOUCH_DELTA_OFF_DEFAULT;
    touchDimEnabled = Settings::TOUCH_DIM_DEFAULT_ENABLED;
    touchHoldStartMs = Settings::TOUCH_HOLD_MS_DEFAULT;
    quickMask = computeDefaultQuickMask();
    presenceEnabled = Settings::PRESENCE_DEFAULT_ENABLED;
    presenceGraceMs = Settings::PRESENCE_GRACE_MS_DEFAULT;
    presenceAddr = "";
    rampDurationMs = Settings::DEFAULT_RAMP_MS;
    idleOffMs = Settings::DEFAULT_IDLE_OFF_MS;
    rampEaseOnType = Settings::DEFAULT_RAMP_EASE_ON;
    rampEaseOffType = Settings::DEFAULT_RAMP_EASE_OFF;
    rampEaseOnPower = Settings::DEFAULT_RAMP_POW_ON;
    rampEaseOffPower = Settings::DEFAULT_RAMP_POW_OFF;
    rampOnDurationMs = Settings::DEFAULT_RAMP_ON_MS;
    rampOffDurationMs = Settings::DEFAULT_RAMP_OFF_MS;
    briMinUser = Settings::BRI_MIN_DEFAULT;
    briMaxUser = Settings::BRI_MAX_DEFAULT;
    customLen = 0;
    customStepMs = Settings::CUSTOM_STEP_MS_DEFAULT;
#if ENABLE_LIGHT_SENSOR
    lightSensorEnabled = Settings::LIGHT_SENSOR_DEFAULT_ENABLED;
    lightGain = Settings::LIGHT_GAIN_DEFAULT;
    lightClampMin = Settings::LIGHT_CLAMP_MIN_DEFAULT;
    lightClampMax = Settings::LIGHT_CLAMP_MAX_DEFAULT;
    lightAlpha = Settings::LIGHT_ALPHA;
#endif
#if ENABLE_MUSIC_MODE
    musicEnabled = Settings::MUSIC_DEFAULT_ENABLED;
    musicGain = Settings::MUSIC_GAIN_DEFAULT;
#endif
    patternFadeEnabled = false;
    patternFadeStrength = 1.0f;
    patternFilteredLevel = 0.0f;
    patternFilterLastMs = 0;
    saveSettings();
    sendFeedback(F("[Factory] Settings cleared"));
    return;
  }
  if (lower.startsWith("profile"))
  {
    String arg = line.substring(7);
    arg.trim();
    if (arg.startsWith("save"))
    {
      int slot = arg.substring(4).toInt();
      if (slot >= 1 && slot <= PROFILE_SLOTS)
      {
        String key = String(PREF_KEY_PROFILE_BASE) + String(slot);
        String cfg = buildProfileString();
        prefs.putString(key.c_str(), cfg);
        sendFeedback(String(F("[Profile] Saved slot ")) + String(slot));
      }
      else
      {
        sendFeedback(F("Usage: profile save <1-3>"));
      }
    }
    else if (arg.startsWith("load"))
    {
      int slot = arg.substring(4).toInt();
      if (slot >= 1 && slot <= PROFILE_SLOTS)
      {
        loadProfileSlot((uint8_t)slot, true);
      }
      else
      {
        sendFeedback(F("Usage: profile load <1-3>"));
      }
    }
    else
    {
      sendFeedback(F("profile save <1-3> | profile load <1-3>"));
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

// Active presence scan for target MAC (returns true if found)
bool presenceScanOnce()
{
#if ENABLE_BLE
  if (presenceAddr.isEmpty())
    return false;
  BLEScan *scan = BLEDevice::getScan();
  if (!scan)
    return false;
  const uint32_t SCAN_TIME_S = 3;
  scan->setActiveScan(true);
  scan->setInterval(320);
  scan->setWindow(80);
  BLEScanResults res = scan->start(SCAN_TIME_S, false);
  bool found = false;
  for (int i = 0; i < res.getCount(); ++i)
  {
    BLEAdvertisedDevice d = res.getDevice(i);
    String addr = d.getAddress().toString().c_str();
    if (addr == presenceAddr)
    {
      found = true;
      break;
    }
  }
  sendFeedback(String(F("[Presence] Scan target=")) + presenceAddr + F(" -> ") + (found ? F("found") : F("not found")));
  return found;
#else
  return false;
#endif
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
      wakeSoftCancel = false;
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
      wakeSoftCancel = false;
      patternStartMs = now;
      sendFeedback(F("[Wake] Fade abgeschlossen."));
    }
    return;
  }

  // Presence polling with occasional scan
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

    // occasionally try to spot the target via scan
    const uint32_t SCAN_INTERVAL_MS = 25000;
    if (presenceAddr.length() > 0 && millis() - lastPresenceScanMs >= SCAN_INTERVAL_MS)
    {
      lastPresenceScanMs = millis();
      if (presenceScanOnce())
      {
        detected = true;
        lastPresenceSeenMs = millis();
      }
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
#if ENABLE_SWITCH
      if (switchDebouncedState && !lampEnabled)
#else
      if (!lampEnabled)
#endif
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
  uint32_t scaledElapsed = (uint32_t)((float)elapsed * patternSpeedScale);
  float relative = clamp01(p.evaluate(scaledElapsed));
  float combined = lampEnabled ? relative * masterBrightness * ambientScale * outputScale : 0.0f;
  if (musicEnabled)
    combined *= musicModScale;
  if (notifyActive && !notifySeq.empty())
  {
    uint32_t dtStage = now - notifyStageStartMs;
    uint32_t stageDur = notifySeq[notifyIdx];
    if (dtStage >= stageDur)
    {
      notifyIdx++;
      if (notifyIdx >= notifySeq.size())
      {
        notifyActive = false;
        setLampEnabled(notifyPrevLampOn, "notify done");
        notifyRestoreLamp = false;
      }
      else
      {
        notifyStageStartMs = now;
      }
    }
    if (notifyActive)
    {
      bool onPhase = (notifyIdx % 2 == 0);
      float scale = notifyInvert ? (onPhase ? 0.0f : 1.0f) : (onPhase ? 1.0f : 0.0f);
      if (notifyFadeMs > 0)
      {
        uint32_t dt = now - notifyStageStartMs;
        uint32_t dur = notifySeq[notifyIdx];
        float s = 1.0f;
        if (dt < notifyFadeMs)
          s = (float)dt / (float)notifyFadeMs;
        else if (dt > dur - notifyFadeMs)
          s = (float)(dur - dt) / (float)notifyFadeMs;
        if (s < 0.0f) s = 0.0f;
        if (s > 1.0f) s = 1.0f;
        if (notifyInvert)
          scale = onPhase ? (1.0f - s) : s;
        else
          scale = onPhase ? s : (1.0f - s);
      }
      combined *= scale;
    }
  }
  if (patternFadeEnabled)
  {
    if (patternFilterLastMs == 0)
    {
      patternFilteredLevel = combined;
      patternFilterLastMs = now;
    }
    uint32_t dt = now - patternFilterLastMs;
    patternFilterLastMs = now;
    float base = (float)(rampDurationMs > 0 ? rampDurationMs : 1);
    float alpha = clamp01((float)dt / (base * patternFadeStrength));
    patternFilteredLevel += (combined - patternFilteredLevel) * alpha;
    applyPwmLevel(patternFilteredLevel);
  }
  else
  {
    patternFilteredLevel = combined;
    patternFilterLastMs = now;
    applyPwmLevel(combined);
  }

  uint32_t durEff = p.durationMs > 0 ? (uint32_t)((float)p.durationMs / patternSpeedScale) : 0;
  if (demoActive)
  {
    if (demoList.empty())
    {
      demoActive = false;
    }
    else if (now - demoLastSwitchMs >= demoDwellMs)
    {
      demoIndex = (demoIndex + 1) % demoList.size();
      demoLastSwitchMs = now;
      applyQuickMode(demoList[demoIndex]);
    }
  }

  if (!demoActive && autoCycle && durEff > 0 && elapsed >= durEff)
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
  float a = clamp01(lightAlpha);
  lightFiltered = (1.0f - a) * lightFiltered + a * (float)raw;
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
  float target = (0.2f + 0.8f * norm) * lightGain;
  if (target < lightClampMin)
    target = lightClampMin;
  if (target > lightClampMax)
    target = lightClampMax;
  target = clamp01(target);
  // Heavier smoothing to avoid visible steps when dimming by ambient light
  ambientScale = 0.92f * ambientScale + 0.08f * target;
#endif
}

#if ENABLE_MUSIC_MODE
void updateMusicSensor()
{
  bool active = musicEnabled || clapEnabled || musicAutoLamp;
  if (!active)
  {
    // Reset any lingering modulation state and skip ADC when audio features are off
    musicModScale = 1.0f;
    musicBeatEnv = 0.0f;
    musicBeatIntervalMs = 600.0f;
    musicLastBeatMs = 0;
    return;
  }
  uint32_t now = millis();
  if (now - lastMusicSampleMs < Settings::MUSIC_SAMPLE_MS)
    return;
  lastMusicSampleMs = now;
  int raw = analogRead(Settings::MUSIC_PIN);
  // clamp ADC to valid range (some boards return negative when floating)
  if (raw < 0)
    raw = 0;
  if (raw > 4095)
    raw = 4095;
  // crude envelope: high-pass-ish by subtracting filtered baseline
  static float env = 0.0f;
  float val = (float)raw / 4095.0f;
  musicRawLevel = clamp01(val);
  env = Settings::MUSIC_ALPHA * val + (1.0f - Settings::MUSIC_ALPHA) * env;
  musicFiltered = clamp01(env * musicGain);
  // If no feature needs audio, reset modifiers and bail after updating status fields
  // Modulate brightness scale based on mode
  if (musicEnabled)
  {
    if (musicMode == 0)
    {
      float target = 0.2f + 0.8f * musicFiltered;
      musicModScale = 0.8f * musicModScale + 0.2f * target;
    }
    else // beat mode
    {
      bool rising = (musicFiltered > musicAutoThr) && (musicBeatEnv <= musicAutoThr);
      musicBeatEnv = musicFiltered;
      uint32_t nowMs = millis();
      if (rising)
      {
        if (musicLastBeatMs > 0)
        {
          float interval = (float)(nowMs - musicLastBeatMs);
          if (interval > 200.0f && interval < 2000.0f)
            musicBeatIntervalMs = 0.8f * musicBeatIntervalMs + 0.2f * interval;
        }
        musicLastBeatMs = nowMs;
        musicModScale = 1.0f; // kick on beat
      }
      else
      {
        float decayMs = fmaxf(250.0f, musicBeatIntervalMs * 0.6f);
        float k = expf(-(Settings::MUSIC_SAMPLE_MS) / decayMs);
        musicModScale = 0.3f + (musicModScale - 0.3f) * k;
      }
    }
  }
  // Auto lamp on when switch is ON and ambient loud
  if (musicAutoLamp && switchDebouncedState && musicFiltered >= musicAutoThr)
  {
    setLampEnabled(true, "music auto");
    lastActivityMs = now;
  }
  if (clapTraining && (now - clapTrainLastLog) >= 200)
  {
    clapTrainLastLog = now;
    sendFeedback(String(F("[ClapTrain] env=")) + String(musicFiltered, 3) + F(" thr=") + String(clapThreshold, 2) +
                 F(" above=") + (musicFiltered >= clapThreshold ? F("1") : F("0")));
  }
  if (clapEnabled)
  {
    if (musicFiltered >= clapThreshold)
    {
      if (!clapAbove && (now - clapLastMs) >= clapCooldownMs)
      {
        clapLastMs = now;
        if (clapWindowStartMs == 0)
          clapWindowStartMs = now;
        ++clapCount;
      }
      clapAbove = true;
    }
    else
    {
      clapAbove = false;
    }

    if (clapWindowStartMs && (now - clapWindowStartMs) >= CLAP_WINDOW_MS)
    {
      if (clapCount > 0)
      {
        uint8_t count = clapCount > 3 ? 3 : clapCount;
        executeClapCommand(count);
        sendFeedback(String(F("[Clap] detected ")) + String(count) + F("x"));
      }
      clapCount = 0;
      clapWindowStartMs = 0;
    }
  }
}
#endif

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
  float level = clamp01((float)raw / 4095.0f);
  potiFiltered = potiAlpha * level + (1.0f - potiAlpha) * potiFiltered;

  if (potiLastApplied >= 0.0f && fabs(potiFiltered - potiLastApplied) < potiDeltaMin)
    return;

  potiLastApplied = potiFiltered;
  if (potiFiltered <= potiOffThreshold)
  {
    if (lampEnabled)
      setLampEnabled(false, "poti");
    return;
  }

  float target = briMinUser + (briMaxUser - briMinUser) * potiFiltered;
  float cap = brightnessCap > 0.0f ? brightnessCap : 1.0f;
  if (target > cap)
    target = cap;
  target = clamp01(target);

  if (!lampEnabled)
    setLampEnabled(true, "poti");
  setBrightnessPercent(target * 100.0f, true);
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
#if ENABLE_SWITCH
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_SWITCH, SWITCH_ACTIVE_LEVEL == LOW ? 0 : 1);
#endif
  esp_light_sleep_start();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
#if ENABLE_SWITCH
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0);
#endif
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

#if ENABLE_SWITCH
  pinMode(PIN_SWITCH, INPUT_PULLUP);
#endif
  pinMode(PIN_PWM, OUTPUT);
  digitalWrite(PIN_PWM, LOW); // keep LED off during init
#if ENABLE_SWITCH
  initSwitchState();
#endif
  calibrateTouchBaseline();

#if ENABLE_LIGHT_SENSOR || ENABLE_POTI || ENABLE_MUSIC_MODE
  analogReadResolution(12);
#endif
#if ENABLE_LIGHT_SENSOR
  analogSetPinAttenuation(Settings::LIGHT_PIN, ADC_11db);
  pinMode(Settings::LIGHT_PIN, INPUT);
#endif
#if ENABLE_POTI
  analogSetPinAttenuation(PIN_POTI, ADC_11db);
  pinMode(PIN_POTI, INPUT);
#endif
#if ENABLE_MUSIC_MODE
  analogSetPinAttenuation(Settings::MUSIC_PIN, ADC_11db);
  pinMode(Settings::MUSIC_PIN, INPUT);
#endif
#if ENABLE_PUSH_BUTTON
  pinMode(PIN_PUSHBTN, INPUT_PULLUP);
#endif

  loadSettings();
  ledcSetup(LEDC_CH, LEDC_FREQ, LEDC_RES);
  ledcAttachPin(PIN_PWM, LEDC_CH);
  ledcWrite(LEDC_CH, 0); // explicit off at startup
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
#if ENABLE_SWITCH
  updateSwitchLogic();
#endif
  updateTouchBrightness();
  updateBrightnessRamp();
  updatePatternEngine();
  updateLightSensor();
#if ENABLE_POTI
  updatePoti();
#endif
#if ENABLE_PUSH_BUTTON
  updatePushButton();
#endif
#if ENABLE_MUSIC_MODE
  updateMusicSensor();
#endif
  maybeLightSleep();
  delay(10);
}
