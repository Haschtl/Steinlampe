#pragma once

#include <Arduino.h>

extern uint64_t quickMask; // bitmask of PATTERNS used for quick switch tap cycling (supports up to 64 entries)
// Separate bitmask for profile slots (bit N = profile slot N+1), rather than
// packing them into the high bits of quickMask above PATTERN_COUNT - with 65
// compiled patterns that packing scheme already needed bit indices >= 64,
// which isQuickEnabled() has always had to reject (uint64_t only has 64
// bits), silently making most/all profile slots impossible to quick-cycle.
// See memory: quick-mode-profile-mask-overflow.
extern uint8_t quickProfileMask;

size_t quickModeCount();

/**
 * @brief Default profile slots enabled for quick-cycling (the first up to 3).
 */
uint8_t computeDefaultProfileMask();

/**
 * @brief Clamp the quick-mode mask to available patterns and ensure non-empty.
 */
void sanitizeQuickMask();

bool isQuickEnabled(size_t idx);

/**
 * @brief How many modes are currently enabled for quick-cycling.
 */
size_t enabledQuickModeCount();

/**
 * @brief The n-th enabled quick-cycle mode (0-based, in ascending index order).
 * Clamps n to the last enabled entry; returns 0 if none are enabled.
 */
size_t nthEnabledQuickMode(size_t n);

size_t nextQuickMode(size_t from);

void applyQuickMode(size_t idx);

/**
 * @brief Convert quick-mask to comma-separated 1-based indices.
 */
String quickMaskToCsv();