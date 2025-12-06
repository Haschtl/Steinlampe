#pragma once

/**
 * @file settings.h
 * @brief Central configuration for user-tunable defaults and identifiers.
 */

namespace Settings
{
// ---- Names / IDs ----
constexpr const char *BLE_DEVICE_NAME = "Quarzlampe";
constexpr const char *BLE_SERVICE_UUID = "d94d86d7-1eaf-47a4-9d1e-7a90bf34e66b";
constexpr const char *BLE_COMMAND_CHAR_UUID = "4bb5047d-0d8b-4c5e-81cd-6fb5c0d1d1f7";
constexpr const char *BT_SERIAL_NAME = "Quarzlampe-SPP";

// ---- Defaults ----
constexpr float DEFAULT_BRIGHTNESS = 0.7f;     ///< Used on first boot if no NVS value
constexpr bool DEFAULT_AUTOCYCLE = false;       ///< Auto pattern cycling on first boot
constexpr uint32_t DEFAULT_WAKE_MS = 180000;    ///< Default wake fade duration (3 min)
constexpr uint32_t DEFAULT_SLEEP_MS = 900000;   ///< Default sleep fade duration (15 min)
constexpr float WAKE_START_LEVEL = 0.02f;      ///< Start level for wake fade
constexpr float WAKE_MIN_TARGET = 0.65f;       ///< Minimal target brightness for wake
} // namespace Settings
