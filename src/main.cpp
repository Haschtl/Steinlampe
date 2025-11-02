/*
  ESP32 Touch-Lampe (24V) – Kurz = Toggle, Lang = Dimmen
  Extras:
    • Gamma-Korrektur (visuell gleichmäßiges Dimmen)
    • Optionales Poti an GPIO34 zur Anpassung der Touch-Sensitivität

  Hardware (wie im SVG-Schaltplan):
    - Touch-Elektrode -> 1 MΩ Serie -> T4 (GPIO13)
      + 100–470 pF von T4 nach GND, + TVS 5 V von T4 nach GND
    - PWM (LEDC) GPIO23 -> 100 Ω -> MOSFET-Gate, 100 kΩ Gate-Pulldown
    - 24 V LED-Last Low-Side, gemeinsame Masse mit ESP32
    - 24 V -> Buck -> 5 V -> ESP32 VIN/5V
    - Optionales Poti (10 kΩ): 3V3—[Poti]—GND, Wiper -> GPIO34

  Lizenz: MIT
*/

#include <Preferences.h>
#include <math.h>

// ======= Optionen / Pins =======
static const int PIN_PWM = 23;   // MOSFET-Gate (über 100 Ω)
static const int PIN_TOUCH = T4; // T4 entspricht idR GPIO13

// Poti zur Sensitivität (optional). Auf 0 setzen, wenn ungenutzt.
#define ENABLE_POT_SENS 0
static const int PIN_POT = 34; // ADC1_CH6, nur Eingang

// ======= LEDC (PWM) =======
static const int LEDC_CH = 0;
static const int LEDC_FREQ = 2000; // 2 kHz
static const int LEDC_RES = 12;    // 12 Bit
static const int PWM_MAX = (1 << LEDC_RES) - 1;

// Perzeptiver Bereich (vermeidet „ganz aus“ / Sättigung oben)
static const float B_MIN = 0.05f; // 5 %
static const float B_MAX = 0.95f; // 95 %

// Gamma-Korrektur (menschliche Wahrnehmung ~2.0–2.4)
static const float GAMMA = 2.2f;

// ======= Touch / Gesten =======
static const uint16_t TOUCH_SENSITIVITY_DEFAULT = 20; // Basisabstand zur Baseline
static const uint16_t TOUCH_SAMPLES = 6;              // Mittel pro Einzelmessung
static const uint16_t TOUCH_DEBOUNCE_N = 3;           // „berührt“-Samples
static const uint16_t RELEASE_DEBOUNCE_N = 3;         // „nicht berührt“-Samples

static const uint32_t SHORT_PRESS_MAX = 300; // < 300 ms = kurz
static const uint32_t LONG_PRESS_MIN = 300;  // ≥ 300 ms = lang

// Dimmrampe (in perzeptiver Helligkeit, 0..1)
static const float RAMP_STEP_B = 0.0125f;    // ~1.25 % pro Tick
static const uint16_t RAMP_INTERVAL_MS = 25; // alle 25 ms

// Baseline-Anpassung
static const uint32_t BASELINE_IDLE_MS = 3000; // alle 3 s im Idle

// ======= Persistenz =======
Preferences prefs;
static const char *P_NS = "lamp";
static const char *P_KEY_ON = "on";
static const char *P_KEY_B1000 = "b1000"; // Helligkeit * 1000 (0..1000)

// ======= Zustand =======
bool isOn = false;
float b = 0.60f;      // aktuelle perzeptive Helligkeit (0..1)
float bSaved = 0.60f; // gemerkte Helligkeit beim nächsten Einschalten

int baseline = 0;  // Touch-Baseline (Roh)
int threshold = 0; // baseline - sensitivity
int sensitivityDyn = TOUCH_SENSITIVITY_DEFAULT;

bool touchActive = false;
bool dimMode = false;
bool dimUp = true;

uint32_t pressT0 = 0;
uint32_t lastRampTs = 0;
uint32_t lastIdleBL = 0;

// ======= Hilfsfunktionen =======
int touchReadAvg(uint8_t n = TOUCH_SAMPLES)
{
  long s = 0;
  for (uint8_t i = 0; i < n; ++i)
  {
    s += touchRead(PIN_TOUCH);
    delay(5);
  }
  return (int)(s / (long)n);
}

int pwmFromPerceptual(float perc)
{
  // clamp in [B_MIN, B_MAX]
  if (perc < 0.0f)
    perc = 0.0f;
  if (perc > 1.0f)
    perc = 1.0f;

  // Map 0..1 -> B_MIN..B_MAX
  float bLin = B_MIN + (B_MAX - B_MIN) * perc;
  // Gamma-Korrektur: perzeptiv linear -> PWM (linear)
  float pwmf = powf(bLin, GAMMA) * PWM_MAX;
  int pwm = (int)roundf(pwmf);
  if (pwm < 0)
    pwm = 0;
  if (pwm > PWM_MAX)
    pwm = PWM_MAX;
  return pwm;
}

void applyBrightness(float perc)
{
  int pwm = pwmFromPerceptual(perc);
  ledcWrite(LEDC_CH, isOn ? pwm : 0);
}

void setOn(bool on)
{
  isOn = on;
  if (isOn)
  {
    if (b < 0.001f)
      b = (bSaved > 0.0f ? bSaved : 0.60f);
    applyBrightness(b);
  }
  else
  {
    ledcWrite(LEDC_CH, 0);
  }
}

void saveState()
{
  prefs.putBool(P_KEY_ON, isOn);
  uint16_t b1000 = (uint16_t)roundf(b * 1000.0f);
  if (b1000 > 1000)
    b1000 = 1000;
  prefs.putUShort(P_KEY_B1000, b1000);
}

void loadState()
{
  bool hasOn = prefs.isKey(P_KEY_ON);
  bool hasB = prefs.isKey(P_KEY_B1000);
  if (hasB)
  {
    uint16_t b1000 = prefs.getUShort(P_KEY_B1000, 600);
    b = bSaved = (float)b1000 / 1000.0f;
    if (b < 0.0f)
      b = bSaved = 0.0f;
    if (b > 1.0f)
      b = bSaved = 1.0f;
  }
  isOn = hasOn ? prefs.getBool(P_KEY_ON, true) : false;
}

void setupPWM()
{
  ledcSetup(LEDC_CH, LEDC_FREQ, LEDC_RES);
  ledcAttachPin(PIN_PWM, LEDC_CH);
  applyBrightness(b);
  if (!isOn)
    ledcWrite(LEDC_CH, 0);
}

void calibrateBaseline()
{
  baseline = touchReadAvg(16);
  threshold = baseline - sensitivityDyn;
}

bool readTouchDebounced()
{
  int below = 0;
  for (int i = 0; i < TOUCH_DEBOUNCE_N; ++i)
  {
    int v = touchReadAvg();
    if (v < threshold)
      ++below;
  }
  return (below >= TOUCH_DEBOUNCE_N);
}

bool readReleaseDebounced()
{
  int above = 0;
  for (int i = 0; i < RELEASE_DEBOUNCE_N; ++i)
  {
    int v = touchReadAvg();
    if (v >= threshold)
      ++above;
  }
  return (above >= RELEASE_DEBOUNCE_N);
}

void rampTick()
{
  if (!dimMode)
    return;
  uint32_t now = millis();
  if (now - lastRampTs < RAMP_INTERVAL_MS)
    return;
  lastRampTs = now;

  // Dimmen in perzeptiver Domäne
  if (dimUp)
  {
    b += RAMP_STEP_B;
    if (b >= B_MAX)
    {
      b = B_MAX;
      dimUp = false;
    }
  }
  else
  {
    b -= RAMP_STEP_B;
    if (b <= B_MIN)
    {
      b = B_MIN;
      dimUp = true;
    }
  }
  if (!isOn)
    isOn = true;
  applyBrightness(b);
}

void maybeIdleBaselineAdjust()
{
  uint32_t now = millis();
  if (now - lastIdleBL < BASELINE_IDLE_MS)
    return;
  lastIdleBL = now;

  if (!touchActive)
  {
    int m = touchReadAvg(8);
    baseline = (baseline * 3 + m) / 4;
    threshold = baseline - sensitivityDyn;
  }
}

#if ENABLE_POT_SENS
// map ADC (0..4095) auf sinnvollen Sensitivitätsbereich
int mapPotToSensitivity(int raw)
{
  // Bereich je nach Aufbau: z.B. 12..50 (kleiner = empfindlicher ESP32-Rohwertabstand)
  const int S_MIN = 12;
  const int S_MAX = 50;
  if (raw < 0)
    raw = 0;
  if (raw > 4095)
    raw = 4095;
  return S_MIN + (int)((S_MAX - S_MIN) * (raw / 4095.0f));
}
#endif

// ======= Setup / Loop =======
void setup()
{
  Serial.begin(115200);
  delay(200);

#if ENABLE_POT_SENS
  // ADC vorbereiten (ESP32: ADC1 auf 12-bit, 11dB für vollen 3.3V-Bereich)
  analogReadResolution(12); // Arduino-ESP32 3.x unterstützt dies
  analogSetPinAttenuation(PIN_POT, ADC_11db);
  pinMode(PIN_POT, INPUT);
#endif

  prefs.begin(P_NS, false);
  loadState();

  // initiale Sensitivität
  sensitivityDyn = TOUCH_SENSITIVITY_DEFAULT;

  setupPWM();
  calibrateBaseline();

  setOn(isOn);

  Serial.println(F("ESP32 Touch-Lampe bereit (Gamma & Poti-Sens)."));
  Serial.print(F("Baseline="));
  Serial.print(baseline);
  Serial.print(F("  Thr="));
  Serial.print(threshold);
  Serial.print(F("  Sens="));
  Serial.println(sensitivityDyn);
}

void loop()
{
  // Optional: Sensitivität per Poti nachführen
#if ENABLE_POT_SENS
  static float potLP = 0;            // Low-Pass
  int raw = analogRead(PIN_POT);     // 0..4095
  potLP = 0.8f * potLP + 0.2f * raw; // glätten
  int sensNew = mapPotToSensitivity((int)potLP);
  if (sensNew != sensitivityDyn)
  {
    sensitivityDyn = sensNew;
    threshold = baseline - sensitivityDyn;
  }
#endif

  // 1) Berührungs-Start
  if (!touchActive)
  {
    if (readTouchDebounced())
    {
      touchActive = true;
      pressT0 = millis();
      dimMode = false;
    }
  }

  // 2) Während Berührung: ggf. in Dimm-Modus wechseln + rampen
  if (touchActive)
  {
    uint32_t held = millis() - pressT0;

    if (!dimMode && held >= LONG_PRESS_MIN)
    {
      dimMode = true;
      if (!isOn)
      {
        isOn = true;
        if (b < B_MIN)
          b = (bSaved > 0 ? bSaved : 0.60f);
      }
      // Richtung festlegen anhand aktueller Helligkeit
      dimUp = (b < (B_MIN + B_MAX) * 0.5f);
      lastRampTs = 0;
    }

    if (dimMode)
      rampTick();

    // Loslassen?
    if (readReleaseDebounced())
    {
      touchActive = false;
      uint32_t dur = millis() - pressT0;

      if (!dimMode && dur < SHORT_PRESS_MAX)
      {
        // Kurz-Tap → Toggle
        if (isOn)
        {
          isOn = false;
          ledcWrite(LEDC_CH, 0);
        }
        else
        {
          isOn = true;
          if (b < B_MIN)
            b = (bSaved > 0 ? bSaved : 0.60f);
          applyBrightness(b);
        }
      }
      else if (dimMode)
      {
        // Langdruck beendet → Helligkeit merken
        bSaved = b;
      }

      saveState();

      delay(200);
      int calm = touchReadAvg(10);
      baseline = (baseline * 2 + calm) / 3;
      threshold = baseline - sensitivityDyn;
    }
  }
  else
  {
    // 3) Idle: Baseline-Drift
    maybeIdleBaselineAdjust();
  }

  // Debug alle 1 s
  static uint32_t lastPr = 0;
  if (millis() - lastPr > 1000)
  {
    lastPr = millis();
    int v = touchReadAvg(3);
    Serial.print(F("touch="));
    Serial.print(v);
    Serial.print(F(" base="));
    Serial.print(baseline);
    Serial.print(F(" thr="));
    Serial.print(threshold);
    Serial.print(F(" sens="));
    Serial.print(sensitivityDyn);
    Serial.print(F(" on="));
    Serial.print(isOn);
    Serial.print(F(" b="));
    Serial.println(b, 3);
  }
}
