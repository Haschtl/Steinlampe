#pragma once

/**
 * @file patterns.h
 * @brief Declarations for available LED brightness patterns.
 */

#include <Arduino.h>

/**
 * @brief Describes a PWM pattern function and its metadata.
 */
struct Pattern
{
  const char *name;                         ///< Human-readable pattern name
  float (*evaluate)(uint32_t elapsedMs);    ///< Callback returning normalized brightness
  uint32_t durationMs;                      ///< Auto-cycle duration in milliseconds
};

/// Global pattern table exposed to the rest of the firmware.
extern const Pattern PATTERNS[];
/// Number of entries in PATTERNS.
extern const size_t PATTERN_COUNT;
