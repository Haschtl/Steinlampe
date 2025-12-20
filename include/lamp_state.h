#pragma once

#include <Arduino.h>

#include "settings.h"

// Shared LEDC configuration
extern const int LEDC_CH;
extern const int LEDC_FREQ;
extern const int LEDC_RES;
extern const int PWM_MAX;
extern float outputGamma;
extern uint32_t lastPwmValue;    // last written PWM value (0..PWM_MAX)
extern const uint32_t OFF_RAW;   // raw output representing "off" (respects inversion)

// Brightness state
extern float masterBrightness;   // user-facing brightness 0..1
extern float lastOnBrightness;   // last non-zero brightness
extern bool lampEnabled;
extern bool lampOffPending;
extern float lastLoggedBrightness;
extern float briMinUser;
extern float briMaxUser;

// Output multipliers
extern float ambientScale; // light sensor scaling
extern float outputScale;  // on/off ramp scaling

// Ramp state
extern bool rampActive;
extern uint32_t rampDurationMs; // generic ramp (bri command)
extern uint32_t rampOnDurationMs;
extern uint32_t rampOffDurationMs;
extern uint32_t lastActivityMs;
extern uint8_t rampEaseOnType;
extern uint8_t rampEaseOffType;
extern float rampEaseOnPower;
extern float rampEaseOffPower;
extern float rampAmbientMultiplier;

// Ramping and timers
extern uint32_t idleOffMs;

// Core lamp helpers
void applyPwmLevel(float normalized);
void logBrightnessChange(const char *reason);
void logLampState(const char *reason = nullptr);
void startBrightnessRamp(float target, uint32_t durationMs, bool affectMaster = true, uint8_t easeType = 1, float easePower = 2.0f);
void updateBrightnessRamp();
void setLampEnabled(bool enable, const char *reason = nullptr, bool skipRamp = false);
void forceLampOff(const char *reason = nullptr);
void setBrightnessPercent(float percent, bool persist = false, bool announce = true, bool fast = false);
