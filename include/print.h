#pragma once

#include <Arduino.h>

#include "pinout.h"

// Root cause of a real-hardware heap corruption (TLSF asserts/LoadProhibited,
// confirmed via core dumps - see git history around this comment): Arduino's
// own String(float, decimalPlaces) constructor does a raw malloc()/free() of
// a short-lived scratch buffer internally (WString.cpp - its int/long
// sibling overloads correctly use a stack array instead). Called repeatedly
// in a hot path (printStatusStructured()/emitLiveState(), or any per-change
// callback like logBrightnessChange()), this repeats the same few
// nearly-identical small malloc/free sizes rapidly enough to trip a TLSF edge
// case. This helper does the float->string conversion into a stack buffer
// directly, then appends it (`String(const char*)` uses small-string-
// optimization for anything this short - no heap allocation at all for
// typical field values). Shared via this header (not file-local to
// print.cpp) so every float-to-string conversion in the codebase can use it
// - a file-local copy is exactly how logBrightnessChange() in lamp_state.cpp
// kept using the buggy two-argument String() constructor for a long time
// after this was first found and fixed elsewhere.
inline String fmtF(float value, unsigned int decimalPlaces)
{
    char buf[40];
    dtostrf(value, decimalPlaces + 2, decimalPlaces, buf);
    return String(buf);
}

/**
 * @brief Print averaged touch sensor data for calibration purposes.
 */
void printTouchDebug(const bool &force=false);

/**
 * @brief Emit structured sensor snapshot for machine parsing.
 */
void printSensorsStructured(const bool &force=false);

/**
 * @brief Emit a single structured status line for easier parsing (key=value pairs).
 */
void printStatusStructured(const bool &force=false);

/**
 * @brief Queue a compact state event for connected UIs.
 */
void queueLiveState(const bool &force=false);

/**
 * @brief Emit a pending rate-limited state event when due.
 */
void flushLiveState();

/**
 * @brief Print available serial/BLE command usage.
 */
void printHelp(const bool &force=false);
