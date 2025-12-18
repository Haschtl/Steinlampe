#pragma once

#include <Arduino.h>
#include <Preferences.h>

// ---------- Persistenz ----------
extern Preferences prefs;
extern const char *PREF_KEY_PROFILE_BASE; // profile slots profile1..profileN
extern const uint8_t PROFILE_SLOTS;

bool parseQuickCsv(const String &csv, uint64_t &outMask);

/**
 * @brief Build a profile string (cfg import style) without presence/touch/quick.
 */
String buildProfileString();

/**
 * @brief Default profiles for slots 1..3 (used if slot is empty).
 */
String defaultProfileString(uint8_t slot);


/**
 * @brief Apply a profile string (same keys as buildProfileString).
 */
void applyProfileString(const String &cfg);

bool loadProfileSlot(uint8_t slot, bool announce = true);

void exportConfig();

/**
 * @brief Persist current brightness, pattern and auto-cycle flag in NVS.
 */
void saveSettings();

void applyDefaultSettings(float brightnessOverride, bool announce);


/**
 * @brief Restore persisted brightness/pattern settings from NVS.
 */
void loadSettings();


void importConfig(const String &args);
