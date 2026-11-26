#pragma once

#include <Arduino.h>
#include <Preferences.h>

// ---------- Persistenz ----------
extern Preferences prefs;
extern const uint8_t PROFILE_SLOTS;
extern const char *PREF_KEY_PROFILE_BASE;

bool parseQuickCsv(const String &csv, uint64_t &outPatternMask, uint8_t &outProfileMask);

/**
 * @brief Build a profile string (cfg import style): pattern/brightness/ramp/speed/margin scene
 * settings only - deliberately excludes hardware/calibration state (presence_ble, touch,
 * quick mask, radar, brightness min/max, pwm gamma, light-sensor and music/clap calibration).
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
 * @brief Request that current settings be persisted to NVS. Debounced - does
 * NOT write immediately; marks state dirty and lets flushSettingsIfDue()
 * (called from the main loop) do the actual NVS write a couple of seconds
 * after the last request, once things have settled. Repeated rapid calls
 * (e.g. a fast toggle/on/off sequence) coalesce into a single write instead
 * of one flash write per call - also sidesteps a real-hardware crash where
 * back-to-back synchronous NVS writes destabilized the BT controller.
 */
void saveSettings();

/**
 * @brief Call every loop() iteration: performs the actual deferred NVS write
 * if saveSettings() was called and the debounce window has elapsed since.
 * A no-op otherwise.
 */
void flushSettingsIfDue();

/**
 * @brief Forces any pending debounced save to happen immediately - call
 * before a reboot/OTA/deep-sleep transition so a just-requested save isn't
 * silently dropped by power loss before its debounce window would have
 * elapsed.
 */
void flushSettingsNow();

void applyDefaultSettings(float brightnessOverride, bool announce);


/**
 * @brief Restore persisted brightness/pattern settings from NVS.
 */
void loadSettings();


void importConfig(const String &args);
