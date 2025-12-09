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

  bool compEnabled;
  float compThr;
  float compRatio;
  uint32_t compAttackMs;
  uint32_t compReleaseMs;
  float compGain;
  uint32_t compLastMs;

  bool envEnabled;
  uint32_t envAttackMs;
  uint32_t envReleaseMs;
  float envValue;
  uint32_t envLastMs;

  bool foldEnabled;
  float foldAmt;

  bool delayEnabled;
  uint32_t delayMs;
  float delayFeedback;
  float delayMix;

  bool sparkEnabled;
  float sparkDensity;   // events per second
  float sparkIntensity; // multiplier delta
  uint32_t sparkDecayMs;
  float sparkValue;
  uint32_t sparkLastMs;
};

#if ENABLE_FILTERS
void filtersInit();
float filtersApply(float in, uint32_t nowMs);

void filtersSetIir(bool en, float alpha);
void filtersSetClip(bool en, float amt, uint8_t curve);
void filtersSetTrem(bool en, float rateHz, float depth, uint8_t wave);
void filtersSetSpark(bool en, float density, float intensity, uint32_t decayMs);
void filtersSetComp(bool en, float thr, float ratio, uint32_t attackMs, uint32_t releaseMs);
void filtersSetEnv(bool en, uint32_t attackMs, uint32_t releaseMs);
void filtersSetFold(bool en, float amt);
void filtersSetDelay(bool en, uint32_t delayMs, float feedback, float mix);

void filtersGetState(FilterState &out);
#else
inline void filtersInit() {}
inline float filtersApply(float in, uint32_t) { return in; }
inline void filtersSetIir(bool, float) {}
inline void filtersSetClip(bool, float, uint8_t) {}
inline void filtersSetTrem(bool, float, float, uint8_t) {}
inline void filtersSetSpark(bool, float, float, uint32_t) {}
inline void filtersSetComp(bool, float, float, uint32_t, uint32_t) {}
inline void filtersSetEnv(bool, uint32_t, uint32_t) {}
inline void filtersSetFold(bool, float) {}
inline void filtersSetDelay(bool, uint32_t, float, float) {}
inline void filtersGetState(FilterState &out) { out = {}; }
#endif
