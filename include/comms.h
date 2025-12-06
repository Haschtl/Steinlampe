#pragma once

/**
 * @file comms.h
 * @brief Interface for serial/BLE communication handling.
 */

#include <Arduino.h>

/**
 * @brief Initialize all configured communication channels (USB serial, BT serial, BLE).
 */
void setupCommunications();

/**
 * @brief Poll and process incoming data on active communication channels.
 */
void pollCommunications();
