#pragma once

/**
 * @file ota.h
 * @brief Firmware OTA updates over the existing text-command channel (BLE, BT-Serial, USB).
 *
 * Firmware bytes arrive base64-encoded inside `ota chunk <base64>` commands (see command.cpp),
 * decoded and written straight into the inactive OTA partition via the ESP32 Update library.
 * Integrity is checked with the Update library's built-in MD5 verification (set via
 * `ota begin ... <md5hex>`, checked automatically in `Update.end()`); a failed check aborts
 * without touching the currently-running firmware.
 */

#include <Arduino.h>

#include "lamp_config.h"

#if ENABLE_OTA

extern bool otaActive;
extern size_t otaExpectedSize;
extern size_t otaReceivedSize;

/**
 * @brief Start an OTA session: size in bytes, expected 32-hex-char MD5, and a version label
 * (only used for the success log message - the version the UI checks comes from STATUS|).
 */
bool otaBegin(size_t size, const String &md5Hex, const String &version);

/**
 * @brief Decode and write one base64-encoded chunk of firmware data.
 */
bool otaChunk(const String &base64Data);

/**
 * @brief Finish the OTA session: verifies size + MD5, and on success schedules a reboot into
 * the new firmware a short delay later (so the success ack reaches the client first).
 */
bool otaEnd();

/**
 * @brief Cancel an in-progress OTA session; the currently-running firmware is untouched.
 */
void otaAbort();

/**
 * @brief Call once per main loop() tick: aborts a stalled session (no chunk for too long) and
 * performs the deferred reboot scheduled by a successful otaEnd().
 */
void otaLoop();

#endif // ENABLE_OTA
