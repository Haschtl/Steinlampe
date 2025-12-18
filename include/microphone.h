#pragma once

#include <Arduino.h>

#include "settings.h"
#include "comms.h"

#if ENABLE_MUSIC_MODE

extern float patternMusicDirect(uint32_t);
extern float patternMusicBeat(uint32_t);
extern bool musicEnabled;
extern float musicFiltered;
extern float musicDc;
extern float musicEnv;
extern float musicRawLevel;
extern uint32_t lastMusicSampleMs;
extern float musicGain;
extern float musicSmoothing; // 0..1 low-pass for direct mode
extern bool musicAutoLamp;
extern float musicAutoThr;
extern uint8_t musicMode; // 0=direct,1=beat
extern float musicModScale;
extern float musicBeatEnv;
extern float musicBeatIntervalMs;
extern uint32_t musicLastBeatMs;
extern uint32_t musicLastKickMs;
extern float clapPrevEnv;
extern bool clapEnabled;
extern float clapThreshold;
extern uint32_t clapCooldownMs;
extern uint32_t clapLastMs;
extern bool clapAbove;
extern String clapCmd1;
extern String clapCmd2;
extern String clapCmd3;
extern uint8_t clapCount;
extern uint32_t clapWindowStartMs;
extern bool clapTraining;
extern uint32_t clapTrainLastLog;
extern bool musicPatternActive; // true when a Music pattern is selected
#endif


#if ENABLE_MUSIC_MODE
void executeClapCommand(uint8_t count);
#else
void executeClapCommand(uint8_t);
#endif

#if ENABLE_MUSIC_MODE
void updateMusicSensor();
#endif