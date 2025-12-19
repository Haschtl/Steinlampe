#include <Arduino.h>

#include "settings.h"
#include "utils.h"
#include "lamp_state.h"

#if ENABLE_LIGHT_SENSOR
bool lightSensorEnabled = Settings::LIGHT_SENSOR_DEFAULT_ENABLED;
float lightFiltered = 0.0f;
uint32_t lastLightSampleMs = 0;
uint16_t lightMinRaw = 4095;
uint16_t lightMaxRaw = 0;
float lightAlpha = Settings::LIGHT_ALPHA;
float rampAmbientFactor = Settings::RAMP_AMBIENT_FACTOR_DEFAULT;
#endif
float lightGain = Settings::LIGHT_GAIN_DEFAULT;
float lightClampMin = Settings::LIGHT_CLAMP_MIN_DEFAULT;
float lightClampMax = Settings::LIGHT_CLAMP_MAX_DEFAULT;


void updateLightSensor()
{
#if ENABLE_LIGHT_SENSOR
    rampAmbientMultiplier = 1.0f;
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

    int range = (int)lightMaxRaw - (int)lightMinRaw;
    // Fallback: if we do not yet have a stable min/max span (very little variation),
    // derive a normalized level directly from the raw ADC reading so that
    // rampAmbientFactor still has an effect instead of staying at 1.0.
    float norm = 0.5f;
    if (range >= 20)
    {
        norm = ((float)lightFiltered - (float)lightMinRaw) / (float)range;
        norm = clamp01(norm);
    }
    else
    {
        norm = clamp01((float)raw / 4095.0f);
    }
    // Map ambient reading to a dimming factor; smooth via low-pass + step clamp to avoid visible jumps
    float target = (0.2f + 0.8f * norm) * lightGain;
    if (target < lightClampMin)
        target = lightClampMin;
    if (target > lightClampMax)
        target = lightClampMax;
    target = clamp01(target);
    const float blend = 0.03f; // slow IIR for ambience
    float next = ambientScale + (target - ambientScale) * blend;
    const float maxStep = 0.02f; // cap per update to hide sudden jumps
    float delta = next - ambientScale;
    if (delta > maxStep)
        next = ambientScale + maxStep;
    else if (delta < -maxStep)
        next = ambientScale - maxStep;
    ambientScale = next;
    // Darker rooms -> longer ramps: multiplier = 1 + factor * darkness
    float span = lightClampMax - lightClampMin;
    float normAmbient = span > 0.001f ? clamp01((ambientScale - lightClampMin) / span) : 1.0f;
    float darkness = 1.0f - normAmbient;
    float mult = 1.0f + rampAmbientFactor * darkness;
    if (mult < 0.1f)
        mult = 0.1f;
    if (mult > 8.0f)
        mult = 8.0f;
    rampAmbientMultiplier = mult;
#endif
}
