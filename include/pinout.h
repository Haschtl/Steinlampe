#pragma once

#include <Arduino.h>

// ---------- Pins ----------

// MOSFET-Gate
#ifndef PIN_PWM
#define PIN_PWM 23
#endif

#ifndef ANALOG_OUT_PIN
#define ANALOG_OUT_PIN 25
#endif
static const int PIN_ANALOG = ANALOG_OUT_PIN; // DAC output (0-255)

#if ENABLE_ANALOG_OUTPUT
static const int PIN_OUTPUT = PIN_ANALOG;
#else
static const int PIN_OUTPUT = PIN_PWM;
#endif

#if ENABLE_SWITCH
static const int PIN_SWITCH = 32; // Kippschalter (digital)
#endif
static const int PIN_TOUCH_DIM = T7; // Touch-Elektrode am Metallschalter (GPIO27)
#if ENABLE_POTI
static const int PIN_POTI = 39;
#endif
#if ENABLE_PUSH_BUTTON
static const int PIN_PUSHBTN = 34;
static const int PUSH_ACTIVE_LEVEL = LOW;
#endif
