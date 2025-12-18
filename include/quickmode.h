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

size_t nextQuickMode(size_t from);

void applyQuickMode(size_t idx);

/**
 * @brief Convert quick-mask to comma-separated 1-based indices.
 */
String quickMaskToCsv();