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
void sendFeedback(const String &line, const bool &force=false);

/**
 * @brief Returns true if a BLE client is currently connected (if BLE is enabled).
 */
bool bleActive();

/** Return true if a BT Serial client is connected (if enabled). */
bool btHasClient();

/** Return last known BLE address (if any). */
String getLastBleAddr();

// Trusted device management
void trustSetBootMs(uint32_t ms);
void trustSetLists(const String &bleCsv, const String &btCsv);
String trustGetBleCsv();
String trustGetBtCsv();
bool trustAddBle(const String &addr, bool persist = true);
bool trustAddBt(const String &addr, bool persist = true);
bool trustRemoveBle(const String &addr, bool persist = true);
bool trustRemoveBt(const String &addr, bool persist = true);
void trustListFeedback();

/**
 * @brief Confirm a pending BT-Serial pairing via a hardware gesture (switch/poti).
 */
void confirmBtPairing(const char *reason);

// Device names
String getBleName();
String getBtName();

#if ENABLE_BT_SERIAL
void setBtSleepAfterBootMs(uint32_t ms);
void setBtSleepAfterBleMs(uint32_t ms);
uint32_t getBtSleepAfterBootMs();
uint32_t getBtSleepAfterBleMs();
#endif


/**
 * @brief Update the BLE status characteristic (read/notify) with a snapshot string.
 */
void updateBleStatus(const String &statusPayload);

String getBLEAddress();

bool presenceBleScanOnce();

// PresenceBLE tracking (defined in presence_ble.cpp)
// extern String presenceBleAddr;
// extern bool presenceBleEnabled;
// extern uint32_t presenceBleGraceMs;
// extern uint32_t presenceBleGraceDeadline;
// extern bool presenceBlePrevConnected;
// extern String lastBleAddr;
// extern String lastBtAddr;
// extern uint32_t lastPresenceBleSeenMs;
// extern uint32_t lastPresenceBleScanMs;

void setBtName(const String &name);
void setBleName(const String &name);
