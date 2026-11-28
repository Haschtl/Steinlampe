#pragma once

#include <Arduino.h>

#include "pinout.h"

// Root cause of a real-hardware heap corruption (TLSF asserts/LoadProhibited,
// confirmed via core dumps - see git history around this comment): Arduino's
// own String(float, decimalPlaces) constructor does a raw malloc()/free() of
// a short-lived scratch buffer internally (WString.cpp - its int/long
// sibling overloads correctly use a stack array instead). Called repeatedly
// in a hot path (printStatusCore()/emitLiveState(), or any per-change
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
 * @brief Emit the slimmed, always-on core status line (STATUS|) -
 * lamp/pattern/ramp/switch/buttons and the per-feature ON/OFF glance each
 * Home chip and the Advanced tile list need to decide whether to show at
 * all. Formerly printStatusStructured(); STATUS1/STATUS2 are retired, their
 * fields moved to the on-demand groups below. See memory: status protocol
 * redesign.
 */
void printStatusCore(const bool &force=false);

// Per-page status groups, fetched on demand ("status radar" etc.) instead of
// riding along on every printStatusCore() broadcast.
void printRadarStructured(const bool &force=false);
void printLightStructured(const bool &force=false);
void printMusicStructured(const bool &force=false);
void printPotiStructured(const bool &force=false);
void printPushStructured(const bool &force=false);
void printPresenceStructured(const bool &force=false);
void printFiltersStructured(const bool &force=false);
void printIoStructured(const bool &force=false);
// Added 2026-11-28, splitting printStatusCore() further - mesh_net/
// mesh_nodes/mesh_role/wake_active/sleep_active/notify_active/idle_min/
// quick/gest/touch_dim_step/fw_ver/fw_env had all accumulated in the core
// line despite only ever being read by one specific Advanced page each,
// growing it to 566+ bytes (30+ small-MTU indicate chunks for a single
// status fetch) - see memory: status protocol redesign, and its own
// follow-up note on this exact regrowth.
void printMeshStatusStructured(const bool &force=false);
void printTimersStructured(const bool &force=false);
void printMiscStructured(const bool &force=false);

/**
 * @brief Broadcasts everything: printStatusCore() plus every on-demand group
 * above. For the genuinely broad-change call sites only (boot, a full
 * defaults/profile/config load) - everything else should call printStatusCore()
 * alone, or the one group it actually touched.
 */
void printStatusAll(const bool &force=false);

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
