#pragma once

/**
 * @file lamp_config.h
 * @brief Compile-time feature toggles for the lamp firmware.
 */

#ifndef ENABLE_BLE
#define ENABLE_BLE 1
#endif

#ifndef ENABLE_BT_SERIAL
#define ENABLE_BT_SERIAL 1
#endif

#ifndef ENABLE_LIGHT_SENSOR
#define ENABLE_LIGHT_SENSOR 1
#endif

#ifndef ENABLE_MUSIC_MODE
#define ENABLE_MUSIC_MODE 0
#endif

#ifndef ENABLE_BLE_MIDI
#define ENABLE_BLE_MIDI 0
#endif

#ifndef ENABLE_BT_MIDI
#define ENABLE_BT_MIDI 0
#endif

#ifndef ENABLE_SWITCH
#define ENABLE_SWITCH 1
#endif

#ifndef ENABLE_POTI
#define ENABLE_POTI 0
#endif

#ifndef ENABLE_PUSH_BUTTON
#define ENABLE_PUSH_BUTTON 0
#endif

#ifndef ENABLE_EXT_INPUT
#define ENABLE_EXT_INPUT 0
#endif
#ifndef DEBUG_BRIGHTNESS_LOG
#define DEBUG_BRIGHTNESS_LOG 0
#endif
