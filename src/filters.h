#pragma once

#include <Arduino.h>

struct FilterState
{
  bool iirEnabled;
  float iirAlpha;
  float iirValue;

  bool clipEnabled;
  float clipAmount;
  uint8_t clipCurve; // 0=tanh,1=softsign

  bool tremEnabled;
  float tremRateHz;
  float tremDepth;
  uint8_t tremWave; // 0=sine,1=triangle
  uint32_t tremStartMs;

  bool sparkEnabled;
  float sparkDensity;   // events per second
  float sparkIntensity; // multiplier delta
  uint32_t sparkDecayMs;
  float sparkValue;
  uint32_t sparkLastMs;
};

void filtersInit();
float filtersApply(float in, uint32_t nowMs);

void filtersSetIir(bool en, float alpha);
void filtersSetClip(bool en, float amt, uint8_t curve);
void filtersSetTrem(bool en, float rateHz, float depth, uint8_t wave);
void filtersSetSpark(bool en, float density, float intensity, uint32_t decayMs);

void filtersGetState(FilterState &out);
