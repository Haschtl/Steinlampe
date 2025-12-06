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
static const uint32_t DOUBLE_TAP_MS = 600;    // schneller Doppel-Tipp -> Wake-Kick

// Touch-Schwellwerte: bei schwachem Signal reduziert
static const int TOUCH_DELTA_ON = 6;  // Counts unterhalb Baseline => "Touch aktiv"
static const int TOUCH_DELTA_OFF = 4; // Hysterese
static const uint32_t TOUCH_SAMPLE_DT_MS = 25;
static const uint32_t TOUCH_HOLD_START_MS = 500;
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

// ---------- Zustand ----------
size_t currentPattern = 0;
uint32_t patternStartMs = 0;
float masterBrightness = Settings::DEFAULT_BRIGHTNESS; // 0..1
bool autoCycle = Settings::DEFAULT_AUTOCYCLE;
bool lampEnabled = false;
bool lampOffPending = false;

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
bool dimRampUp = true;
bool brightnessChangedByTouch = false;

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
uint32_t rampDurationMs = 0;

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
 * @brief Start a smooth ramp towards target brightness over durationMs.
 */
void startBrightnessRamp(float target, uint32_t durationMs)
{
  rampStartLevel = masterBrightness;
  rampTargetLevel = clamp01(target);
  rampStartMs = millis();
  rampDurationMs = durationMs;
  rampActive = (durationMs > 0 && rampStartLevel != rampTargetLevel);
  if (!rampActive)
  {
    masterBrightness = rampTargetLevel;
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
  float t = rampDurationMs > 0 ? clamp01((float)(now - rampStartMs) / (float)rampDurationMs) : 1.0f;
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
  // prefs.putBool(PREF_KEY_AUTO, autoCycle);
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
  // autoCycle = prefs.getBool(PREF_KEY_AUTO, Settings::DEFAULT_AUTOCYCLE);
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
void setLampEnabled(bool enable)
{
  if (lampEnabled == enable)
    return;
  if (enable)
  {
    lampEnabled = true;
    lampOffPending = false;
    startBrightnessRamp(masterBrightness <= 0.0f ? Settings::DEFAULT_BRIGHTNESS : masterBrightness, 400);
  }
  else
  {
    lampOffPending = true;
    startBrightnessRamp(0.0f, 250);
    cancelWakeFade(false);
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
  int delta = touchBaseline - raw;
  sendFeedback(String(F("[Touch] raw=")) + String(raw) + F(" baseline=") + String(touchBaseline) +
               F(" delta=") + String(delta) + F(" thrOn=") + String(TOUCH_DELTA_ON) + F(" thrOff=") + String(TOUCH_DELTA_OFF));
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
      setLampEnabled(true);
    }
    else
    {
      modeTapArmed = lampEnabled;
      lastSwitchOffMs = now;
      setLampEnabled(false);
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
  int delta = touchBaseline - raw;

  if (!touchActive)
  {
    touchBaseline = (touchBaseline * 15 + raw) / 16;
    if (delta > TOUCH_DELTA_ON)
    {
      touchActive = true;
      touchStartMs = now;
      touchLastRampMs = now;
      brightnessChangedByTouch = false;
      dimRampUp = (masterBrightness < 0.5f);
    }
    return;
  }

  if (delta < TOUCH_DELTA_OFF)
  {
    touchActive = false;
    touchBaseline = (touchBaseline * 7 + raw) / 8;
    if (brightnessChangedByTouch)
    {
      sendFeedback(String(F("[Brightness] ")) + String(masterBrightness * 100.0f, 1) + F(" %"));
      saveSettings();
      brightnessChangedByTouch = false;
    }
    return;
  }

  if (!lampEnabled)
    return;

  if ((now - touchStartMs) >= TOUCH_HOLD_START_MS && (now - touchLastRampMs) >= DIM_RAMP_DT_MS)
  {
    touchLastRampMs = now;
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
  startBrightnessRamp(clamp01(percent / 100.0f), 400);
  if (announce)
  {
    sendFeedback(String(F("[Brightness] ")) + String(percent, 1) + F(" %"));
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
  sendFeedback(line);
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
      "  auto on|off       - automatisches Durchschalten",
      "  bri <0..100>      - globale Helligkeit in %",
      "  wake [Sekunden]   - sanfter Weckfade starten (Default 180s)",
      "  wake stop         - Weckfade abbrechen",
      "  sleep [Minuten]   - Sleep-Fade auf 0, Default 15min",
      "  sleep stop        - Sleep-Fade abbrechen",
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
  if (lower == "touch")
  {
    printTouchDebug();
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

  if (sleepFadeActive)
  {
    uint32_t elapsedSleep = now - sleepStartMs;
    float progress = sleepDurationMs > 0 ? clamp01((float)elapsedSleep / (float)sleepDurationMs) : 1.0f;
    float level = sleepStartLevel * (1.0f - progress);
    applyPwmLevel(level);
    if (progress >= 1.0f)
    {
      sleepFadeActive = false;
      setLampEnabled(false);
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
  maybeLightSleep();
  delay(10);
}
