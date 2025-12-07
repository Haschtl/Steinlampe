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
constexpr const char *BT_SERIAL_NAME = "Quarzlampe-SPP";

// ---- Defaults ----
constexpr float DEFAULT_BRIGHTNESS = 0.7f;     ///< Used on first boot if no NVS value
constexpr bool DEFAULT_AUTOCYCLE = false;       ///< Auto pattern cycling on first boot
constexpr uint32_t DEFAULT_WAKE_MS = 180000;    ///< Default wake fade duration (3 min)
constexpr uint32_t DEFAULT_SLEEP_MS = 900000;   ///< Default sleep fade duration (15 min)
constexpr uint32_t DEFAULT_RAMP_MS = 400;       ///< Default ramp duration for brightness changes
constexpr uint32_t DEFAULT_RAMP_ON_MS = DEFAULT_RAMP_MS;  ///< Default on-ramp
constexpr uint32_t DEFAULT_RAMP_OFF_MS = DEFAULT_RAMP_MS; ///< Default off-ramp
constexpr uint32_t DEFAULT_IDLE_OFF_MS = 0;     ///< 0 = disabled
constexpr float WAKE_START_LEVEL = 0.02f;      ///< Start level for wake fade
constexpr float WAKE_MIN_TARGET = 0.65f;       ///< Minimal target brightness for wake
constexpr bool PRESENCE_DEFAULT_ENABLED = false;///< Presence auto-off default state
constexpr uint32_t PRESENCE_GRACE_MS_DEFAULT = 3000; ///< Wait before presence-off triggers
constexpr bool TOUCH_DIM_DEFAULT_ENABLED = true;
constexpr uint32_t TOUCH_HOLD_MS_DEFAULT = 1000; ///< Default touch hold start (ms)
constexpr float LIGHT_GAIN_DEFAULT = 1.0f;
constexpr float LIGHT_CLAMP_MIN_DEFAULT = 0.2f;
constexpr float LIGHT_CLAMP_MAX_DEFAULT = 1.0f;
constexpr float BRI_MIN_DEFAULT = 0.05f;
constexpr float BRI_MAX_DEFAULT = 0.95f;
constexpr float BRI_CAP_DEFAULT = 1.0f;       ///< Hard cap for brightness (0..1)
constexpr uint32_t CUSTOM_STEP_MS_DEFAULT = 800;///< default step time for custom pattern

#if ENABLE_POTI
constexpr int POTI_PIN = 39;                    ///< ADC pin for optional brightness knob
constexpr uint32_t POTI_SAMPLE_MS = 80;
constexpr float POTI_ALPHA = 0.25f;
constexpr float POTI_DELTA_MIN = 0.025f;
constexpr float POTI_OFF_THRESHOLD = 0.02f;
#endif

#if ENABLE_PUSH_BUTTON
constexpr int PUSH_BUTTON_PIN = 34;            ///< GPIO for optional momentary push button
constexpr uint32_t PUSH_DEBOUNCE_MS = 25;
constexpr uint32_t PUSH_DOUBLE_MS = 450;
constexpr uint32_t PUSH_HOLD_MS = 700;
constexpr uint32_t PUSH_BRI_STEP_MS = 180;
constexpr float PUSH_BRI_STEP = 0.05f;         ///< step in normalized brightness while holding
#endif

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
constexpr float MUSIC_GAIN_DEFAULT = 1.0f;
constexpr bool CLAP_DEFAULT_ENABLED = false;
constexpr float CLAP_THRESHOLD_DEFAULT = 0.35f; ///< normalized 0..1
constexpr uint32_t CLAP_COOLDOWN_MS_DEFAULT = 800;
#endif

// PWM curve
constexpr float PWM_GAMMA_DEFAULT = 2.2f; ///< Gamma/curve to linearize perceived brightness
} // namespace Settings
