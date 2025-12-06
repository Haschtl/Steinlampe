/**
 * @file patterns.cpp
 * @brief Collection of PWM brightness patterns for the quartz lamp demo.
 *
 * Each pattern returns a normalized brightness (0..1) depending on elapsed milliseconds.
 * The table at the bottom exposes the patterns to the main firmware for sequencing.
 */

#include "patterns.h"

#include <math.h>

#include "utils.h"

/// Soft base brightness – calm mode
float patternConstant(uint32_t)
{
  return 0.85f;
}

/// Slow breathe using eased sine wave
float patternBreathing(uint32_t ms)
{
  float phase = (ms % 7000u) / 7000.0f;
  float wave = (1.0f - cosf(TWO_PI * phase)) * 0.5f;
  float eased = wave * wave * (3.0f - 2.0f * wave);
  return 0.25f + 0.7f * eased;
}

/// Gentle pulse without hard peaks
float patternPulse(uint32_t ms)
{
  float phase = (ms % 4200u) / 4200.0f;
  float wave = sinf(TWO_PI * phase);
  float env = powf(fabsf(wave), 1.6f);
  return 0.25f + 0.7f * env;
}

/// Subtle sparkle via stacked sine components
float patternSparkle(uint32_t ms)
{
  float t = ms / 1000.0f;
  float slow = 0.55f + 0.18f * sinf(t * 0.35f * TWO_PI);
  float ripple = 0.15f * sinf(t * 3.6f * TWO_PI) + 0.10f * sinf(t * 5.9f * TWO_PI + 1.1f) + 0.05f * sinf(t * 11.0f * TWO_PI + 2.0f);
  return clamp01(slow + ripple);
}

/// Candle flicker: slow wobble plus high-frequency flutter
float patternCandle(uint32_t ms)
{
  float t = ms / 1000.0f;
  float wobble = 0.25f * sinf(t * 0.9f * TWO_PI) + 0.15f * sinf(t * 1.5f * TWO_PI + 1.7f);
  float flutter = 0.10f * sinf(t * 7.8f * TWO_PI + sinf(t * 2.3f * TWO_PI));
  float jitter = 0.05f * sinf(t * 13.7f * TWO_PI + 0.9f) + 0.03f * sinf(t * 21.1f * TWO_PI + 2.6f);
  return clamp01(0.35f + wobble + flutter + jitter);
}

/// Campfire: embers, tongues and sporadic sparks
float patternCampfire(uint32_t ms)
{
  float t = ms / 1000.0f;
  float embers = 0.40f + 0.25f * sinf(t * 0.45f * TWO_PI) + 0.15f * sinf(t * 0.9f * TWO_PI + 2.1f);
  float tongues = 0.18f * sinf(t * 3.4f * TWO_PI + sinf(t * 0.3f * TWO_PI));
  float sparks = 0.05f * sinf(t * 9.0f * TWO_PI + 1.9f) + 0.04f * sinf(t * 12.5f * TWO_PI + 0.7f);
  float kick = 0.08f * sinf(powf(fmodf(t, 3.5f), 1.4f) * 3.5f * TWO_PI);
  return clamp01(embers + tongues + sparks + kick);
}

/// Linear interpolation between preset levels
float patternStepFade(uint32_t ms)
{
  static const float stops[] = {0.15f, 0.45f, 0.9f, 0.35f};
  static const uint32_t segmentMs = 2500;
  size_t seg = (ms / segmentMs) % (sizeof(stops) / sizeof(stops[0]));
  float progress = (ms % segmentMs) / (float)segmentMs;
  float start = stops[seg];
  float end = stops[(seg + 1) % (sizeof(stops) / sizeof(stops[0]))];
  return start + (end - start) * progress;
}

/// Starry twinkle: slow base wave + two flicker layers
float patternTwinkle(uint32_t ms)
{
  float t = ms / 1000.0f;
  float slow = 0.3f + 0.2f * sinf(t * 0.25f * TWO_PI);
  float wave = 0.5f + 0.25f * sinf(t * 0.9f * TWO_PI + sinf(t * 0.15f * TWO_PI));
  float flicker = 0.08f * sinf(t * 7.3f * TWO_PI + 1.7f) + 0.05f * sinf(t * 12.1f * TWO_PI);
  return clamp01(slow + wave + flicker);
}

// Custom pattern is provided by main.cpp (patternCustom)
extern float patternCustom(uint32_t ms);
#if ENABLE_MUSIC_MODE
extern float patternMusic(uint32_t ms);
#endif

/// Exported pattern table consumed by the main firmware
const Pattern PATTERNS[] = {
    {"Konstant", patternConstant, 8000},
    {"Atmung", patternBreathing, 15000},
    {"Pulsierend", patternPulse, 12000},
    {"Funkeln", patternSparkle, 12000},
    {"Kerze", patternCandle, 16000},
    {"Lagerfeuer", patternCampfire, 18000},
    {"Stufen", patternStepFade, 14000},
    {"Zwinkern", patternTwinkle, 16000},
    {"Custom", patternCustom, 0},
#if ENABLE_MUSIC_MODE
    {"Musik", patternMusic, 0},
#endif
};

const size_t PATTERN_COUNT = sizeof(PATTERNS) / sizeof(PATTERNS[0]);
