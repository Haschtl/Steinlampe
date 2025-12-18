#include <Arduino.h>

#include "pattern.h"
#include "patterns.h"
#include "persistence.h"
#include "comms.h"

uint64_t quickMask = 0; // bitmask of modes used for quick switch tap cycling (supports up to 64 entries)

size_t quickModeCount()
{
    return PATTERN_COUNT + PROFILE_SLOTS;
}

/**
 * @brief Build default quick-cycle mask: the three profile slots by default.
 */
uint64_t computeDefaultQuickMask()
{
    // Default: the three profile slots (mode indices PATTERN_COUNT .. PATTERN_COUNT+2)
    uint64_t mask = 0;
    for (size_t slot = 0; slot < PROFILE_SLOTS && slot < 3; ++slot)
    {
        size_t idx = PATTERN_COUNT + slot;
        if (idx < 64)
            mask |= (1ULL << idx);
    }
    // Fallback to the first profile bit if nothing set (should not happen)
    if (mask == 0 && PROFILE_SLOTS > 0 && PATTERN_COUNT < 64)
        mask = (1ULL << PATTERN_COUNT);
    return mask;
}

/**
 * @brief Clamp the quick-mode mask to available patterns and ensure non-empty.
 */
void sanitizeQuickMask()
{
    size_t total = quickModeCount();
    uint64_t limitMask = (total >= 64) ? 0xFFFFFFFFFFFFFFFFULL : ((1ULL << total) - 1ULL);
    quickMask &= limitMask;
    if (quickMask == 0)
        quickMask = computeDefaultQuickMask() & limitMask;
}

bool isQuickEnabled(size_t idx)
{
    size_t total = quickModeCount();
    return idx < total && idx < 64 && (quickMask & (1ULL << idx));
}

size_t nextQuickMode(size_t from)
{
    size_t total = quickModeCount();
    if (total == 0)
        return 0;
    for (size_t step = 1; step <= total; ++step)
    {
        size_t idx = (from + step) % total;
        if (isQuickEnabled(idx))
            return idx;
    }
    return from;
}

void applyQuickMode(size_t idx)
{
    if (idx < PATTERN_COUNT)
    {
        setPattern(idx, true, true);
    }
    else
    {
        uint8_t slot = (uint8_t)(idx - PATTERN_COUNT + 1);
        if (!loadProfileSlot(slot, true))
        {
            sendFeedback(F("[Quick] Profile slot empty"));
        }
    }
}

/**
 * @brief Convert quick-mask to comma-separated 1-based indices.
 */
String quickMaskToCsv()
{
    String out;
    size_t total = quickModeCount();
    for (size_t i = 0; i < total; ++i)
    {
        if (isQuickEnabled(i))
        {
            if (!out.isEmpty())
                out += ',';
            out += String(i + 1);
        }
    }
    return out.isEmpty() ? String(F("none")) : out;
}