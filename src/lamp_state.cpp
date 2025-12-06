#include "lamp_state.h"

#include <math.h>

#include "lamp_config.h"
#include "comms.h"
#include "utils.h"

// ---------- LEDC ----------
const int LEDC_CH = 0;
const int LEDC_FREQ = 2000;
const int LEDC_RES = 12;
const int PWM_MAX = (1 << LEDC_RES) - 1;
const float GAMMA = 2.2f;

// ---------- Brightness State ----------
float masterBrightness = Settings::DEFAULT_BRIGHTNESS;
float lastOnBrightness = Settings::DEFAULT_BRIGHTNESS;
bool lampEnabled = false;
bool lampOffPending = false;
float lastLoggedBrightness = Settings::DEFAULT_BRIGHTNESS;
float briMinUser = Settings::BRI_MIN_DEFAULT;
float briMaxUser = Settings::BRI_MAX_DEFAULT;
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
uint32_t lastActivityMs = 0;

/**
 * @brief Write a gamma-corrected PWM value to the LED driver.
 */
void applyPwmLevel(float normalized)
{
  float level = clamp01(normalized);
  if (briMaxUser < briMinUser)
    briMaxUser = briMinUser;
  float levelEff = briMinUser + (briMaxUser - briMinUser) * level;
  if (level <= 0.0f)
  {
    ledcWrite(LEDC_CH, 0);
    return;
  }
  float pwm = powf(levelEff, GAMMA) * PWM_MAX;
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
#if DEBUG_BRIGHTNESS_LOG
  Serial.print(F("[DBG] masterBrightness="));
  Serial.println(masterBrightness, 4);
#endif
}

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

void startBrightnessRamp(float target, uint32_t durationMs, bool affectMaster)
{
  rampAffectsMaster = affectMaster;
  if (affectMaster)
    rampStartLevel = masterBrightness;
  else
    rampStartLevel = outputScale;
  rampTargetLevel = clamp01(target);
  rampStartMs = millis();
  uint32_t dur = (durationMs > 0 ? durationMs : rampDurationMs);
  rampDurationActive = dur;
  rampActive = (dur > 0 && rampStartLevel != rampTargetLevel);
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
  float eased = t * t * (3.0f - 2.0f * t);
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
      ledcWrite(LEDC_CH, 0);
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
    startBrightnessRamp(1.0f, rampDurationMs, false); // ramp output only
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
    startBrightnessRamp(0.0f, rampDurationMs, false);
    logLampState(reason);
  }
}
