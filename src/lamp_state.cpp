#include "lamp_state.h"

#include <math.h>

#include "lamp_config.h"
#include "pinout.h"
#include "comms.h"
#include "utils.h"
#include "notifications.h"
#include "persistence.h"
#include "pattern.h"
#include "inputs.h"
#include <string.h>

// ---------- Output driver (PWM or DAC) ----------
const int LEDC_CH = 0;
// more conservative
// const int LEDC_FREQ = 2000;
// const int LEDC_RES = 15; // 15-bit resolution @2 kHz still fits LEDC clock

#if ENABLE_ANALOG_OUTPUT
const int LEDC_FREQ = 0;
const int LEDC_RES = 8; // DAC uses 8-bit values
const int PWM_MAX = 255;
#else
// Lower PWM frequency to gain one more bit of resolution (quieter LEDs).
const int LEDC_FREQ = 1000;
const int LEDC_RES = 16; // 16-bit @1 kHz fits LEDC clock
const int PWM_MAX = (1 << LEDC_RES) - 1;
#endif
float outputGamma = Settings::PWM_GAMMA_DEFAULT;
uint32_t lastPwmValue = 0;

static inline void writeOutputRaw(uint32_t value)
{
#if ENABLE_ANALOG_OUTPUT
  if (value > (uint32_t)PWM_MAX)
    value = (uint32_t)PWM_MAX;
  dacWrite(PIN_OUTPUT, (uint8_t)value);
#else
  ledcWrite(LEDC_CH, value);
#endif
}

// ---------- Brightness State ----------
float masterBrightness = Settings::DEFAULT_BRIGHTNESS;
float lastOnBrightness = Settings::DEFAULT_BRIGHTNESS;
bool lampEnabled = false;
bool lampOffPending = false;
float lastLoggedBrightness = Settings::DEFAULT_BRIGHTNESS;
float briMinUser = Settings::BRI_MIN_DEFAULT;
float briMaxUser = Settings::BRI_MAX_DEFAULT;
float brightnessCap = Settings::BRI_CAP_DEFAULT;
float ambientScale = 1.0f;
float outputScale = 1.0f;

// ---------- Ramp State ----------
bool rampActive = false;
float rampStartLevel = 0.0f;
float rampTargetLevel = 0.0f;
uint32_t rampStartMs = 0;
uint32_t rampDurationActive = 0;
bool rampAffectsMaster = true;
uint32_t rampDurationMs = Settings::DEFAULT_RAMP_MS;
uint32_t rampOnDurationMs = Settings::DEFAULT_RAMP_ON_MS;
uint32_t rampOffDurationMs = Settings::DEFAULT_RAMP_OFF_MS;
uint32_t lastActivityMs = 0;
uint8_t rampEaseOnType = Settings::DEFAULT_RAMP_EASE_ON;   // 0=linear,1=ease(smoothstep),2=in,3=out,4=inout
uint8_t rampEaseOffType = Settings::DEFAULT_RAMP_EASE_OFF;
float rampEaseOnPower = Settings::DEFAULT_RAMP_POW_ON;
float rampEaseOffPower = Settings::DEFAULT_RAMP_POW_OFF;
float rampAmbientMultiplier = 1.0f;
static uint8_t rampEaseActiveType = Settings::DEFAULT_RAMP_EASE_ON;
static float rampEaseActivePower = Settings::DEFAULT_RAMP_POW_ON;

// Ramping and timers
uint32_t idleOffMs = Settings::DEFAULT_IDLE_OFF_MS;

/**
 * @brief Write a gamma-corrected PWM value to the LED driver.
 */
void applyPwmLevel(float normalized)
{
  float level = clamp01(normalized);
  if (briMaxUser < briMinUser)
    briMaxUser = briMinUser;
  float cap = brightnessCap;
  if (cap < briMinUser)
    cap = briMinUser;
  if (cap > 1.0f)
    cap = 1.0f;
  float capFactor = 1.0f;
  if (briMaxUser > 0.0f)
    capFactor = fminf(1.0f, cap / briMaxUser);
  float levelEff = briMinUser + (briMaxUser - briMinUser) * level;
  float levelScaled = briMinUser + (levelEff - briMinUser) * capFactor;
  if (level <= 0.0f)
  {
    writeOutputRaw(0);
    lastPwmValue = 0;
    return;
  }
  float gamma = outputGamma;
  if (gamma < 0.5f)
    gamma = 0.5f;
  if (gamma > 4.0f)
    gamma = 4.0f;
  float pwm = powf(levelScaled, gamma) * PWM_MAX;
  if (pwm < 0.0f)
    pwm = 0.0f;
  if (pwm > PWM_MAX)
    pwm = (float)PWM_MAX;
  uint32_t pwmValue = (uint32_t)(pwm + 0.5f);
  if (pwmValue > (uint32_t)PWM_MAX)
    pwmValue = (uint32_t)PWM_MAX;
  lastPwmValue = pwmValue;
  writeOutputRaw(pwmValue);
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
#if DEBUG_BRIGHTNESS_LOG
  Serial.print(F("[DBG] masterBrightness="));
  Serial.println(masterBrightness, 4);
#endif
}

void logLampState(const char *reason)
{
  bool forceSerial = (reason && (strstr(reason, "init") != nullptr || strstr(reason, "startup") != nullptr));
  String msg = String(F("[Lamp] ")) + (lampEnabled ? F("ON") : (lampOffPending ? F("OFF-PEND"):F("OFF")));
  if (reason && reason[0] != '\0')
  {
    msg += F(" (");
    msg += reason;
    msg += F(")");
  }
  sendFeedback(msg, forceSerial);
}

static float applyEase(float t, uint8_t type, float power)
{
  t = clamp01(t);
  switch (type)
  {
  case 0: // linear
    return t;
  case 2: // ease-in
    return powf(t, power > 0.1f ? power : 1.0f);
  case 3: // ease-out
    return 1.0f - powf(1.0f - t, power > 0.1f ? power : 1.0f);
  case 4: // ease-in-out
  {
    float p = power > 0.1f ? power : 1.0f;
    float t2 = (t < 0.5f) ? 0.5f * powf(t * 2.0f, p) : 1.0f - 0.5f * powf((1.0f - t) * 2.0f, p);
    return t2;
  }
  case 5: // flash: accelerate hard to target
  {
    float expo = power > 0.1f ? (1.0f / power) : 1.0f;
    float v = powf(t, expo);
    if (v > 1.0f)
      v = 1.0f;
    return v;
  }
  case 6: // wave up-down-up (for ramp on; off is complement)
  {
    float y = 0.0f;
    if (t < 0.45f)
    {
      float u = t / 0.45f;
      u = u * u * (3.0f - 2.0f * u);
      y = u; // rise to 1
    }
    else if (t < 0.75f)
    {
      float u = (t - 0.45f) / 0.30f;
      u = u * u * (3.0f - 2.0f * u);
      y = 1.0f - 0.5f * u; // fall to ~0.5
    }
    else
    {
      float u = (t - 0.75f) / 0.25f;
      u = u * u * (3.0f - 2.0f * u);
      y = 0.5f + 0.5f * u; // rise back to 1
    }
    return clamp01(y);
  }
  case 7: // blink-blink then fade up
  {
    // segments: on, off, on, off, ramp
    if (t < 0.1f)
    {
      float u = t / 0.1f;
      return u * u * (3.0f - 2.0f * u);
    }
    if (t < 0.2f)
    {
      float u = (t - 0.1f) / 0.1f;
      return 1.0f - u * u * (3.0f - 2.0f * u);
    }
    if (t < 0.3f)
    {
      float u = (t - 0.2f) / 0.1f;
      return u * u * (3.0f - 2.0f * u);
    }
    if (t < 0.4f)
    {
      float u = (t - 0.3f) / 0.1f;
      return 1.0f - u * u * (3.0f - 2.0f * u);
    }
    float u = (t - 0.4f) / 0.6f;
    float p = power > 0.1f ? power : 2.0f;
    return powf(u, 1.0f / p); // smooth fade to 1
  }
  case 1: // smooth ease (default)
  default:
    return t * t * (3.0f - 2.0f * t);
  }
}

void startBrightnessRamp(float target, uint32_t durationMs, bool affectMaster, uint8_t easeType, float easePower)
{
  rampAffectsMaster = affectMaster;
  if (affectMaster)
    rampStartLevel = masterBrightness;
  else
    rampStartLevel = outputScale;
  rampTargetLevel = clamp01(target);
  rampStartMs = millis();
  uint32_t durBase = (durationMs > 0 ? durationMs : rampDurationMs);
  float mult = rampAmbientMultiplier;
  if (mult < 0.1f)
    mult = 0.1f;
  if (mult > 8.0f)
    mult = 8.0f;
  uint32_t dur = (uint32_t)fmaxf(10.0f, (float)durBase * mult);
  rampDurationActive = dur;
  rampActive = (dur > 0 && rampStartLevel != rampTargetLevel);
  rampEaseActiveType = easeType;
  rampEaseActivePower = easePower;
  if (!rampActive)
  {
    if (rampAffectsMaster)
    {
      masterBrightness = rampTargetLevel;
      logBrightnessChange("instant");
    }
    else
    {
      outputScale = rampTargetLevel;
    }
  }
}

void updateBrightnessRamp()
{
  if (!rampActive)
    return;
  uint32_t now = millis();
  lastActivityMs = now;
  float t = rampDurationActive > 0 ? clamp01((float)(now - rampStartMs) / (float)rampDurationActive) : 1.0f;
  float eased = applyEase(t, rampEaseActiveType, rampEaseActivePower);
  if (rampAffectsMaster)
    masterBrightness = rampStartLevel + (rampTargetLevel - rampStartLevel) * eased;
  else
    outputScale = rampStartLevel + (rampTargetLevel - rampStartLevel) * eased;
  if (t >= 1.0f)
  {
    if (rampAffectsMaster)
      masterBrightness = rampTargetLevel;
    else
      outputScale = rampTargetLevel;
    rampActive = false;
    if (lampOffPending && rampTargetLevel <= 0.0f)
    {
      lampEnabled = false;
      lampOffPending = false;
      writeOutputRaw(0);
      lastPwmValue = 0;
    }
    if (rampAffectsMaster)
      logBrightnessChange("ramp");
  }
}

/**
 * @brief Enable or disable the lamp output (keeps brightness state).
 */
void setLampEnabled(bool enable, const char *reason)
{
  if (lampEnabled == enable && !(enable && lampOffPending))
    return;
  lastActivityMs = millis();
  if (enable)
  {
    outputScale = 0.0f; // ramp output from dark to full
    lampEnabled = true;
    lampOffPending = false;
    float fallback = (lastOnBrightness > briMinUser) ? lastOnBrightness : Settings::DEFAULT_BRIGHTNESS;
    float target = (masterBrightness > briMinUser) ? masterBrightness : fallback;
    if (target < briMinUser)
      target = briMinUser;
    if (target > briMaxUser)
      target = briMaxUser;
    masterBrightness = target;
    startBrightnessRamp(1.0f, rampOnDurationMs, false, rampEaseOnType, rampEaseOnPower); // ramp output only
    lastOnBrightness = target;
    logLampState(reason);
  }
  else
  {
    // Fade out output to 0, keep master for next ON
    lampOffPending = true;
    if (masterBrightness > briMinUser)
      lastOnBrightness = masterBrightness;
    else if (lastOnBrightness < briMinUser)
      lastOnBrightness = Settings::DEFAULT_BRIGHTNESS;
    startBrightnessRamp(0.0f, rampOffDurationMs, false, rampEaseOffType, rampEaseOffPower);
    logLampState(reason);
  }
}

void forceLampOff(const char *reason)
{
  rampActive = false;
  lampOffPending = false;
  outputScale = 0.0f;
  lampEnabled = false;
  notifyActive = false;
  patternFilteredLevel = 0.0f;
  patternFilterLastMs = 0;
  applyPwmLevel(0.0f);
  if (reason)
    logLampState(reason);
}

/**
 * @brief Set the master brightness in percent, optionally persisting/announcing.
 */
void setBrightnessPercent(float percent, bool persist, bool announce)
{
  float cap = brightnessCap;
  if (cap < briMinUser)
    cap = briMinUser;
  if (cap > 1.0f)
    cap = 1.0f;
  float target = clamp01(percent / 100.0f);
  if (target > cap)
    target = cap;
  uint32_t dur = (target >= masterBrightness) ? rampOnDurationMs : rampOffDurationMs;
  startBrightnessRamp(target, dur);
  if (announce)
  {
    logBrightnessChange("cmd bri");
  }
  if (persist)
    saveSettings();
}
