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
#define ENABLE_LIGHT_SENSOR 0
#endif
