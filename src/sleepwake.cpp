#include "Arduino.h"

#include "utils.h"
#include "settings.h"
#include "lamp_state.h"
#include "comms.h"

bool wakeFadeActive = false;
uint32_t wakeStartMs = 0;
uint32_t wakeDurationMs = 0;
float wakeTargetLevel = 0.8f;
bool wakeSoftCancel = false;

bool sleepFadeActive = false;
uint32_t sleepStartMs = 0;
uint32_t sleepDurationMs = 0;
float sleepStartLevel = 0.0f;

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
    setLampEnabled(true, "wake fade");
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
    setLampEnabled(true, "sleep fade");
    sendFeedback(String(F("[Sleep] Fade ueber ")) + String(durationMs / 1000) + F("s"));
}

/**
 * @brief Cancel an active sleep fade.
 */
void cancelSleepFade()
{
    sleepFadeActive = false;
}
