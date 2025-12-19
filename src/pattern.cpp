#include "Arduino.h"

#include "settings.h"
#include "utils.h"
#include "patterns.h"
#include "comms.h"
#include "microphone.h"
#include "persistence.h"

// Active pattern state (index and start time)
size_t currentPattern = 0;
uint32_t patternStartMs = 0;

// General flags/state
bool autoCycle = Settings::DEFAULT_AUTOCYCLE;
float patternSpeedScale = 1.0f;
size_t currentModeIndex = 0; // tracks current mode (patterns + profile slots)

bool patternFadeEnabled = false;
float patternFadeStrength = 1.0f; // multiplier for smoothing duration (1.0 = rampDurationMs)
bool patternInvert = Settings::PATTERN_INVERT_DEFAULT;
float patternMarginLow = Settings::PATTERN_MARGIN_LOW_DEFAULT;
float patternMarginHigh = Settings::PATTERN_MARGIN_HIGH_DEFAULT;
float patternFilteredLevel = 0.0f;
uint32_t patternFilterLastMs = 0;

// Custom pattern editor storage
static const size_t CUSTOM_MAX = 32;
float customPattern[CUSTOM_MAX];
size_t customLen = 0;
uint32_t customStepMs = Settings::CUSTOM_STEP_MS_DEFAULT;

float patternCustom(uint32_t ms)
{
    if (customLen == 0)
        return 0.8f;
    uint32_t idx = (ms / customStepMs) % customLen;
    return clamp01(customPattern[idx]);
}

/**
 * @brief Log the currently selected pattern and its index.
 */
void announcePattern(const bool &force)
{
    sendFeedback(String(F("[Mode] ")) + String(currentPattern + 1) + F("/") + String(PATTERN_COUNT) + F(" - ") + PATTERNS[currentPattern].name,force);
}

/**
 * @brief Find the index of a pattern by name (case-insensitive), -1 if missing.
 */
int findPatternIndexByName(const char *name)
{
    if (!name)
        return -1;
    for (size_t i = 0; i < PATTERN_COUNT; ++i)
    {
        if (String(PATTERNS[i].name).equalsIgnoreCase(name))
            return (int)i;
    }
    return -1;
}

/**
 * @brief Change the active pattern and optionally announce/persist it.
 */
void setPattern(size_t index, bool announce, bool persist)
{
    if (index >= PATTERN_COUNT)
    {
        index = 0;
    }
    currentPattern = index;
    currentModeIndex = index;
#if ENABLE_MUSIC_MODE
    {
        String name = PATTERNS[currentPattern].name;
        name.toLowerCase();
        if (name.indexOf(F("music direct")) >= 0)
        {
            musicEnabled = true;
            musicMode = 0;
            musicPatternActive = true;
            musicModScale = 1.0f;
            musicLastKickMs = 0;
        }
        else if (name.indexOf(F("music beat")) >= 0)
        {
            musicEnabled = true;
            musicMode = 1;
            musicPatternActive = true;
            musicModScale = 1.0f;
            musicBeatEnv = 0.0f;
            musicLastBeatMs = 0;
            musicLastKickMs = 0;
        }
        else
        {
            musicPatternActive = false;
            musicEnabled = false;
            musicModScale = 1.0f;
            musicLastKickMs = 0;
        }
    }
#endif
    patternStartMs = millis();
    if (announce)
        announcePattern(false);
    if (persist)
        saveSettings();
}

/**
 * @brief List all available patterns and their indices.
 */
void listPatterns()
{
    for (size_t i = 0; i < PATTERN_COUNT; ++i)
    {
        sendFeedback(String(i + 1) + F(": ") + PATTERNS[i].name);
    }
    // Reserve virtual slots for profiles after patterns
    for (uint8_t p = 1; p <= PROFILE_SLOTS; ++p)
        sendFeedback(String(PATTERN_COUNT + p) + F(": Profile ") + String(p));
}
