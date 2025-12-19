#pragma once

#include <Arduino.h>

// ---------- Pins ----------
static const int PIN_PWM = 23; // MOSFET-Gate

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
static const int PIN_POTI = Settings::POTI_PIN;
#endif
#if ENABLE_PUSH_BUTTON
static const int PIN_PUSHBTN = Settings::PUSH_BUTTON_PIN;
static const int PUSH_ACTIVE_LEVEL = LOW;
#endif
