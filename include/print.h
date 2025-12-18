#pragma once

#include <Arduino.h>

#include "pinout.h"

/**
 * @brief Print averaged touch sensor data for calibration purposes.
 */
void printTouchDebug(const bool &force=false);

/**
 * @brief Print current mode, brightness and wake/auto state.
 */
void printStatus(const bool &force=false);

/**
 * @brief Emit structured sensor snapshot for machine parsing.
 */
void printSensorsStructured(const bool &force=false);

/**
 * @brief Emit a single structured status line for easier parsing (key=value pairs).
 */
void printStatusStructured(const bool &force=false);

/**
 * @brief Print available serial/BLE command usage.
 */
void printHelp(const bool &force=false);