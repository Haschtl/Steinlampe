#pragma once

#include <Arduino.h>

#include "pinout.h"

/**
 * @brief Print averaged touch sensor data for calibration purposes.
 */
void printTouchDebug();

/**
 * @brief Print current mode, brightness and wake/auto state.
 */
void printStatus();

/**
 * @brief Emit structured sensor snapshot for machine parsing.
 */
void printSensorsStructured();

/**
 * @brief Emit a single structured status line for easier parsing (key=value pairs).
 */
void printStatusStructured();

/**
 * @brief Print available serial/BLE command usage.
 */
void printHelp();