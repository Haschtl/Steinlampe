#pragma once

/**
 * @file settings.h
 * @brief Central configuration for user-tunable defaults and identifiers.
 */

#include <Arduino.h>

namespace Settings
{
// ---- Names / IDs ----
#if QUARZLAMPE
constexpr const char *BLE_NAME_DEFAULT = "Quarzlampe";
constexpr const char *BT_NAME_DEFAULT = "Quarzlampe-SPP";
#elif SCHREIBTISCHLAMPE
constexpr const char *BLE_NAME_DEFAULT = "Schreibtischlampe";
constexpr const char *BT_NAME_DEFAULT = "Schreibtischlampe-SPP";
#else
constexpr const char *BLE_NAME_DEFAULT = "Superduperlampe";
constexpr const char *BT_NAME_DEFAULT = "Superduperlampe-SPP";
#endif
constexpr const char *BLE_SERVICE_UUID = "d94d86d7-1eaf-47a4-9d1e-7a90bf34e66b";
constexpr const char *BLE_COMMAND_CHAR_UUID = "4bb5047d-0d8b-4c5e-81cd-6fb5c0d1d1f7";
constexpr const char *BLE_STATUS_CHAR_UUID = "c5ad78b6-9b77-4a96-9a42-8e6e9a40c123"; ///< Read/Notify current status snapshot

// ---- Defaults ----
constexpr float DEFAULT_BRIGHTNESS = 0.7f;     ///< Used on first boot if no NVS value
constexpr bool DEFAULT_AUTOCYCLE = false;       ///< Auto pattern cycling on first boot
constexpr uint32_t DEFAULT_WAKE_MS = 180000;    ///< Default wake fade duration (3 min)
constexpr uint32_t DEFAULT_SLEEP_MS = 900000;   ///< Default sleep fade duration (15 min)
constexpr uint32_t DEFAULT_RAMP_MS = 400;       ///< Default ramp duration for brightness changes
constexpr uint32_t DEFAULT_RAMP_ON_MS = 950;    ///< Default on-ramp
constexpr uint32_t DEFAULT_RAMP_OFF_MS = 2200;   ///< Default off-ramp
constexpr uint32_t DEFAULT_IDLE_OFF_MS = 0;     ///< 0 = disabled
constexpr uint8_t DEFAULT_RAMP_EASE_ON = 3;     ///< ease-out
constexpr uint8_t DEFAULT_RAMP_EASE_OFF = 3;    ///< ease-out
constexpr float DEFAULT_RAMP_POW_ON = 7.0f;     ///< easing power for on-ramp
constexpr float DEFAULT_RAMP_POW_OFF = 2.0f;    ///< easing power for off-ramp
constexpr float RAMP_AMBIENT_FACTOR_DEFAULT = 0.0f; ///< Extra ramp length per darkness (0 = off)
constexpr float PATTERN_MARGIN_LOW_DEFAULT = 0.0f;  ///< lower bound for pattern output
constexpr float PATTERN_MARGIN_HIGH_DEFAULT = 1.0f; ///< upper bound for pattern output
constexpr bool PATTERN_INVERT_DEFAULT = false;      ///< invert pattern output
constexpr bool FILTER_IIR_DEFAULT = false;
constexpr float FILTER_IIR_ALPHA_DEFAULT = 0.2f;
constexpr bool FILTER_CLIP_DEFAULT = false;
constexpr float FILTER_CLIP_AMT_DEFAULT = 0.15f;
constexpr uint8_t FILTER_CLIP_CURVE_DEFAULT = 0; // 0=tanh,1=soft
constexpr bool FILTER_TREM_DEFAULT = false;
constexpr float FILTER_TREM_RATE_DEFAULT = 1.5f;
constexpr float FILTER_TREM_DEPTH_DEFAULT = 0.3f;
constexpr uint8_t FILTER_TREM_WAVE_DEFAULT = 0; // 0=sin,1=tri
constexpr bool FILTER_SPARK_DEFAULT = false;
constexpr float FILTER_SPARK_DENS_DEFAULT = 0.6f; // events per second
constexpr float FILTER_SPARK_INT_DEFAULT = 0.25f;
constexpr uint32_t FILTER_SPARK_DECAY_DEFAULT = 200; // ms
constexpr bool FILTER_COMP_DEFAULT = false;
constexpr float FILTER_COMP_THR_DEFAULT = 0.8f;
constexpr float FILTER_COMP_RATIO_DEFAULT = 3.0f;
constexpr uint32_t FILTER_COMP_ATTACK_DEFAULT = 20;
constexpr uint32_t FILTER_COMP_RELEASE_DEFAULT = 180;
constexpr bool FILTER_ENV_DEFAULT = false;
constexpr uint32_t FILTER_ENV_ATTACK_DEFAULT = 30;
constexpr uint32_t FILTER_ENV_RELEASE_DEFAULT = 120;
constexpr bool FILTER_FOLD_DEFAULT = false;
constexpr float FILTER_FOLD_AMT_DEFAULT = 0.2f;
constexpr bool FILTER_DELAY_DEFAULT = false;
constexpr uint32_t FILTER_DELAY_MS_DEFAULT = 180;
constexpr float FILTER_DELAY_FB_DEFAULT = 0.35f;
constexpr float FILTER_DELAY_MIX_DEFAULT = 0.3f;
constexpr float WAKE_START_LEVEL = 0.02f;      ///< Start level for wake fade
constexpr float WAKE_MIN_TARGET = 0.65f;       ///< Minimal target brightness for wake
constexpr bool PRESENCE_DEFAULT_ENABLED = false;///< Presence auto-off default state
constexpr uint32_t PRESENCE_GRACE_MS_DEFAULT = 3000; ///< Wait before presence-off triggers
constexpr bool TOUCH_DIM_DEFAULT_ENABLED = false;
constexpr uint32_t TOUCH_HOLD_MS_DEFAULT = 1000; ///< Default touch hold start (ms)
constexpr float TOUCH_DIM_STEP_DEFAULT = 0.005f; ///< Default step for touch-dimming per tick
constexpr float LIGHT_GAIN_DEFAULT = 1.0f;
constexpr float LIGHT_CLAMP_MIN_DEFAULT = 0.2f;
constexpr float LIGHT_CLAMP_MAX_DEFAULT = 1.0f;
constexpr float BRI_MIN_DEFAULT = 0.05f;
constexpr float BRI_MAX_DEFAULT = 0.95f;
constexpr float BRI_CAP_DEFAULT = 1.0f;       ///< Hard cap for brightness (0..1)
constexpr uint32_t CUSTOM_STEP_MS_DEFAULT = 800;///< default step time for custom pattern
// Require an incoming command before any feedback is printed/sent (set via -DREQUIRE_FEEDBACK_HANDSHAKE=1)
#ifndef REQUIRE_FEEDBACK_HANDSHAKE
#define REQUIRE_FEEDBACK_HANDSHAKE 1
#endif
constexpr bool FEEDBACK_NEEDS_HANDSHAKE = REQUIRE_FEEDBACK_HANDSHAKE;

#ifndef ENABLE_HELP_TEXT
#define ENABLE_HELP_TEXT 0
#endif

#ifndef ENABLE_FILTERS
#define ENABLE_FILTERS 1
#endif

#if ENABLE_EXT_INPUT
#ifndef EXT_INPUT_PIN_NUM
#define EXT_INPUT_PIN_NUM 33
#endif
constexpr int EXT_INPUT_PIN = EXT_INPUT_PIN_NUM; ///< GPIO for optional external input
constexpr bool EXT_INPUT_ANALOG_DEFAULT = true;  ///< true=analog brightness, false=digital on/off
constexpr bool EXT_INPUT_ACTIVE_LOW = true;      ///< for digital mode
constexpr uint32_t EXT_INPUT_SAMPLE_MS = 50;     ///< sample interval for analog
constexpr float EXT_INPUT_ALPHA = 0.2f;          ///< low-pass alpha for analog
constexpr float EXT_INPUT_DELTA = 0.02f;         ///< minimum change to apply brightness update
#endif

#if ENABLE_POTI
constexpr uint32_t POTI_SAMPLE_MS = 80;
constexpr float POTI_ALPHA = 0.25f;
constexpr float POTI_DELTA_MIN = 0.025f;
constexpr float POTI_OFF_THRESHOLD = 0.02f;
#endif

#if ENABLE_PUSH_BUTTON
constexpr uint32_t PUSH_DEBOUNCE_MS = 25;
constexpr uint32_t PUSH_DOUBLE_MS = 450;
constexpr uint32_t PUSH_HOLD_MS = 700;
constexpr uint32_t PUSH_BRI_STEP_MS = 180;
constexpr float PUSH_BRI_STEP = 0.05f;         ///< step in normalized brightness while holding
#endif

#if ENABLE_LIGHT_SENSOR
constexpr bool LIGHT_SENSOR_DEFAULT_ENABLED = false;
constexpr uint32_t LIGHT_SAMPLE_MS = 200;       ///< light sensor sample interval
constexpr float LIGHT_ALPHA = 0.1f;             ///< low-pass filter factor
constexpr int LIGHT_PIN = 35;                   ///< default ADC pin for ambient light
#endif

#if ENABLE_MUSIC_MODE
constexpr bool MUSIC_DEFAULT_ENABLED = false;
constexpr int MUSIC_PIN = 36;                   ///< default ADC pin for music mode
constexpr uint32_t MUSIC_SAMPLE_MS = 25;
constexpr float MUSIC_ALPHA = 0.25f;
constexpr float MUSIC_GAIN_DEFAULT = 1.0f;
constexpr bool CLAP_DEFAULT_ENABLED = false;
constexpr float CLAP_THRESHOLD_DEFAULT = 0.35f; ///< normalized 0..1
constexpr uint32_t CLAP_COOLDOWN_MS_DEFAULT = 800;
#endif

#if ENABLE_BT_SERIAL
// Auto-sleep timers for Classic BT serial (0 = disabled)
constexpr uint32_t BT_SLEEP_AFTER_BOOT_MS = 0; ///< Turn off BT-SERIAL after X ms from boot
constexpr uint32_t BT_SLEEP_AFTER_BLE_MS = 0;  ///< Turn off BT-SERIAL after X ms since last BLE/BT command (BLE stays on)
#endif

// PWM curve
constexpr float PWM_GAMMA_DEFAULT = 2.8f; ///< Gamma/curve to linearize perceived brightness
} // namespace Settings
