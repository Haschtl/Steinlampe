#pragma once

/**
 * @file patterns.h
 * @brief Declarations for available LED brightness patterns.
 */

#include <Arduino.h>

/**
 * @brief Radar snapshot handed to a pattern's optional react() hook. Deliberately plain data
 * (no dependency on ENABLE_RD03/presence_radar.h) so patterns.cpp stays decoupled from whether
 * radar is even compiled in - the caller fills this in (or doesn't call react() at all).
 */
struct PatternSensorState
{
  bool radarPresent;
  float radarDistanceCm;
  float radarSpeedCmS;
  uint8_t radarTargetCount;
};

/**
 * @brief Describes a PWM pattern function and its metadata.
 */
struct Pattern
{
  const char *name;                         ///< Human-readable pattern name
  float (*evaluate)(uint32_t elapsedMs);    ///< Callback returning normalized brightness
  uint32_t durationMs;                      ///< Auto-cycle duration in milliseconds
  /// Optional bespoke reaction to radar, called only while this pattern is flagged
  /// sensor-reactive (pat reactive on) and radar is enabled. nullptr = no bespoke reaction,
  /// the generic presence/distance brightness fallback applies instead (see main.cpp).
  float (*react)(float baseLevel, uint32_t elapsedMs, const PatternSensorState &sensors);
};

/// Global pattern table exposed to the rest of the firmware.
extern const Pattern PATTERNS[];
/// Number of entries in PATTERNS.
extern const size_t PATTERN_COUNT;
