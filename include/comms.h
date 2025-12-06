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

/**
 * @brief Send a single-line feedback message to Serial/BT and BLE notify (if connected).
 */
void sendFeedback(const String &line);

/**
 * @brief Returns true if a BLE client is currently connected (if BLE is enabled).
 */
bool bleActive();

// Presence hooks
void blePresenceUpdate(bool connected, const String &addr);

/**
 * @brief Update the BLE status characteristic (read/notify) with a snapshot string.
 */
void updateBleStatus(const String &statusPayload);
