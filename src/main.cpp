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
#include "utils.h"

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

static const int TOUCH_DELTA_ON = 22;  // Counts unterhalb Baseline => "Touch aktiv"
static const int TOUCH_DELTA_OFF = 15; // Hysterese
static const uint32_t TOUCH_SAMPLE_DT_MS = 25;
static const uint32_t TOUCH_HOLD_START_MS = 500;
static const float DIM_RAMP_STEP = 0.02f;
static const uint32_t DIM_RAMP_DT_MS = 80;
static const float DIM_MIN = 0.10f;
static const float DIM_MAX = 0.95f;

// ---------- Wake ----------
static const uint32_t WAKE_DEFAULT_MS = 180000; // 3 Minuten
static const float WAKE_START_LEVEL = 0.02f;
static const float WAKE_MIN_TARGET = 0.65f;

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
float masterBrightness = 0.7f; // 0..1
bool autoCycle = false;
bool lampEnabled = false;

bool switchRawState = false;
bool switchDebouncedState = false;
uint32_t switchLastDebounceMs = 0;
uint32_t lastSwitchOffMs = 0;
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
 * @brief Persist current brightness, pattern and auto-cycle flag in NVS.
 */
void saveSettings()
{
  uint16_t b = (uint16_t)(clamp01(masterBrightness) * 1000.0f + 0.5f);
  prefs.putUShort(PREF_KEY_B1000, b);
  prefs.putUShort(PREF_KEY_MODE, (uint16_t)currentPattern);
  prefs.putBool(PREF_KEY_AUTO, autoCycle);
}

/**
 * @brief Restore persisted brightness/pattern settings from NVS.
 */
void loadSettings()
{
  prefs.begin(PREF_NS, false);
  uint16_t b = prefs.getUShort(PREF_KEY_B1000, 700);
  masterBrightness = clamp01(b / 1000.0f);
  uint16_t idx = prefs.getUShort(PREF_KEY_MODE, 0);
  if (idx >= PATTERN_COUNT)
    idx = 0;
  currentPattern = idx;
  autoCycle = prefs.getBool(PREF_KEY_AUTO, true);
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
  lampEnabled = enable;
  if (!lampEnabled)
  {
    cancelWakeFade(false);
    ledcWrite(LEDC_CH, 0);
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
  Serial.print(F("[Touch] raw="));
  Serial.print(raw);
  Serial.print(F(" baseline="));
  Serial.print(touchBaseline);
  Serial.print(F(" delta="));
  Serial.print(delta);
  Serial.print(F(" thrOn="));
  Serial.print(TOUCH_DELTA_ON);
  Serial.print(F(" thrOff="));
  Serial.println(TOUCH_DELTA_OFF);
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
  modeTapArmed = false;
}

/**
 * @brief Log the currently selected pattern and its index.
 */
void announcePattern()
{
  Serial.print(F("[Mode] "));
  Serial.print(currentPattern + 1);
  Serial.print(F("/"));
  Serial.print(PATTERN_COUNT);
  Serial.print(F(" - "));
  Serial.println(PATTERNS[currentPattern].name);
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
      Serial.print(F("[Brightness] "));
      Serial.print(masterBrightness * 100.0f, 1);
      Serial.println(F(" %"));
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
  wakeTargetLevel = clamp01(fmax(masterBrightness, WAKE_MIN_TARGET));
  setLampEnabled(true);
  wakeFadeActive = true;
  if (announce)
  {
    Serial.print(F("[Wake] Starte Fade über "));
    Serial.print(durationMs / 1000.0f, 1);
    Serial.println(F(" Sekunden."));
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
    Serial.println(F("[Wake] Abgebrochen."));
  }
}


/**
 * @brief Set the master brightness in percent, optionally persisting/announcing.
 */
void setBrightnessPercent(float percent, bool persist = false, bool announce = true)
{
  masterBrightness = clamp01(percent / 100.0f);
  if (announce)
  {
    Serial.print(F("[Brightness] "));
    Serial.print(percent, 1);
    Serial.println(F(" %"));
  }
  if (persist)
    saveSettings();
}

/**
 * @brief Print current mode, brightness and wake/auto state.
 */
void printStatus()
{
  Serial.print(F("Pattern: "));
  Serial.print(currentPattern + 1);
  Serial.print(F(" '"));
  Serial.print(PATTERNS[currentPattern].name);
  Serial.print(F("'  Brightness "));
  Serial.print(masterBrightness * 100.0f, 1);
  Serial.print(F("%  AutoCycle "));
  Serial.print(autoCycle ? F("ON") : F("OFF"));
  Serial.print(F("  Lamp "));
  Serial.print(lampEnabled ? F("ON") : F("OFF"));
  if (wakeFadeActive)
  {
    Serial.println(F("  Wake ACTIVE"));
  }
  else
  {
    Serial.println();
  }
}

/**
 * @brief Print available serial/BLE command usage.
 */
void printHelp()
{
  Serial.println(F("Serien-Kommandos:"));
  Serial.println(F("  list              - verfügbare Muster"));
  Serial.println(F("  mode <1..N>       - bestimmtes Muster wählen"));
  Serial.println(F("  next / prev       - weiter oder zurück"));
  Serial.println(F("  auto on|off       - automatisches Durchschalten"));
  Serial.println(F("  bri <0..100>      - globale Helligkeit in %"));
  Serial.println(F("  wake [Sekunden]   - sanfter Weckfade starten (Default 180s)"));
  Serial.println(F("  wake stop         - Weckfade abbrechen"));
  Serial.println(F("  touch             - aktuellen Touch-Rohwert anzeigen"));
  Serial.println(F("  status            - aktuellen Zustand anzeigen"));
  Serial.println(F("  help              - diese Übersicht"));
}

/**
 * @brief List all available patterns and their indices.
 */
void listPatterns()
{
  for (size_t i = 0; i < PATTERN_COUNT; ++i)
  {
    Serial.print(i + 1);
    Serial.print(F(": "));
    Serial.println(PATTERNS[i].name);
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
      Serial.println(F("Ungültiger Mode."));
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
      Serial.println(F("auto on|off"));
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
      uint32_t durationMs = WAKE_DEFAULT_MS;
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

  Serial.println(F("Unbekanntes Kommando. 'help' tippen."));
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
    float level = clamp01(WAKE_START_LEVEL + (wakeTargetLevel - WAKE_START_LEVEL) * eased);
    applyPwmLevel(level);
    if (progress >= 1.0f)
    {
      wakeFadeActive = false;
      patternStartMs = now;
      Serial.println(F("[Wake] Fade abgeschlossen."));
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
  updatePatternEngine();
  delay(10);
}
