#pragma once

#include <Arduino.h>

extern uint64_t quickMask; // bitmask of modes used for quick switch tap cycling (supports up to 64 entries)

size_t quickModeCount();

/**
 * @brief Build default quick-cycle mask: the three profile slots by default.
 */
uint64_t computeDefaultQuickMask();

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