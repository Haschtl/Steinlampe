#pragma once

#include <Arduino.h>

#include "settings.h"


#if ENABLE_LIGHT_SENSOR
extern bool lightSensorEnabled;
extern float lightFiltered ;
extern uint32_t lastLightSampleMs;
extern uint16_t lightMinRaw;
extern uint16_t lightMaxRaw;
extern float lightAlpha;
extern float rampAmbientFactor;
#endif
extern float lightGain;
extern float lightClampMin;
extern float lightClampMax;


void updateLightSensor();