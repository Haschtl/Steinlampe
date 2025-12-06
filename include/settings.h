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
constexpr const char *BLE_STATUS_CHAR_UUID = "c5ad78b6-9b77-4a96-9a42-8e6e9a40c123"; ///< Read/Notify current status snapshot
constexpr const char *BT_SERIAL_NAME = "Quarzlampe";

// ---- Defaults ----
constexpr float DEFAULT_BRIGHTNESS = 0.7f;     ///< Used on first boot if no NVS value
constexpr bool DEFAULT_AUTOCYCLE = false;       ///< Auto pattern cycling on first boot
constexpr uint32_t DEFAULT_WAKE_MS = 180000;    ///< Default wake fade duration (3 min)
constexpr uint32_t DEFAULT_SLEEP_MS = 900000;   ///< Default sleep fade duration (15 min)
constexpr uint32_t DEFAULT_RAMP_MS = 400;       ///< Default ramp duration for brightness changes
constexpr uint32_t DEFAULT_IDLE_OFF_MS = 0;     ///< 0 = disabled
constexpr float WAKE_START_LEVEL = 0.02f;      ///< Start level for wake fade
constexpr float WAKE_MIN_TARGET = 0.65f;       ///< Minimal target brightness for wake
constexpr bool PRESENCE_DEFAULT_ENABLED = false;///< Presence auto-off default state
constexpr uint32_t PRESENCE_GRACE_MS_DEFAULT = 3000; ///< Wait before presence-off triggers
constexpr bool TOUCH_DIM_DEFAULT_ENABLED = true;
constexpr float LIGHT_GAIN_DEFAULT = 1.0f;
constexpr float BRI_MIN_DEFAULT = 0.05f;
constexpr float BRI_MAX_DEFAULT = 0.95f;
constexpr uint32_t CUSTOM_STEP_MS_DEFAULT = 800;///< default step time for custom pattern

#if ENABLE_LIGHT_SENSOR
constexpr bool LIGHT_SENSOR_DEFAULT_ENABLED = false;
constexpr uint32_t LIGHT_SAMPLE_MS = 1000;      ///< light sensor sample interval
constexpr float LIGHT_ALPHA = 0.1f;             ///< low-pass filter factor
constexpr int LIGHT_PIN = 35;                   ///< default ADC pin for ambient light
#endif

#if ENABLE_MUSIC_MODE
constexpr bool MUSIC_DEFAULT_ENABLED = false;
constexpr int MUSIC_PIN = 36;                   ///< default ADC pin for music mode
constexpr uint32_t MUSIC_SAMPLE_MS = 25;
constexpr float MUSIC_ALPHA = 0.15f;
#endif
} // namespace Settings
