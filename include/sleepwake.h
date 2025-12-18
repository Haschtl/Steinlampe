#pragma once

#include "Arduino.h"


extern bool wakeFadeActive;
extern uint32_t wakeStartMs;
extern uint32_t wakeDurationMs;
extern float wakeTargetLevel;
extern bool wakeSoftCancel;

extern bool sleepFadeActive;
extern uint32_t sleepStartMs;
extern uint32_t sleepDurationMs;
extern float sleepStartLevel;

/**
 * @brief Start a sunrise-style wake fade over the given duration.
 */
void startWakeFade(uint32_t durationMs, bool announce, bool softCancel, float targetOverride);

/**
 * @brief Abort any active wake fade animation.
 */
void cancelWakeFade(bool announce);

/**
 * @brief Start a sleep fade down to zero over the given duration.
 */
void startSleepFade(uint32_t durationMs);

/**
 * @brief Cancel an active sleep fade.
 */
void cancelSleepFade();