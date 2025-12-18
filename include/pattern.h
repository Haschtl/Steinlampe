#pragma once

#include "Arduino.h"

// Active pattern state (index and start time)
extern size_t currentPattern;
extern uint32_t patternStartMs;

// General flags/state
extern bool autoCycle;
extern float patternSpeedScale;
extern size_t currentModeIndex; // tracks current mode (patterns + profile slots)

extern bool patternFadeEnabled;
extern float patternFadeStrength; // multiplier for smoothing duration (1.0 = rampDurationMs)
extern float patternMarginLow;
extern float patternMarginHigh;
extern float patternFilteredLevel;
extern uint32_t patternFilterLastMs;

// Custom pattern editor storage
static const size_t CUSTOM_MAX = 32;
extern float customPattern[CUSTOM_MAX];
extern size_t customLen;
extern uint32_t customStepMs;

float patternCustom(uint32_t ms);

/**
 * @brief Log the currently selected pattern and its index.
 */
void announcePattern(const bool &force=false);

/**
 * @brief Find the index of a pattern by name (case-insensitive), -1 if missing.
 */
int findPatternIndexByName(const char *name);

/**
 * @brief Change the active pattern and optionally announce/persist it.
 */
void setPattern(size_t index, bool announce, bool persist);

/**
 * @brief List all available patterns and their indices.
 */
void listPatterns();