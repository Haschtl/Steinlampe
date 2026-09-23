/*
  Quarzlampe – Pattern-Demo ohne Touch-Elektroden

  - spielt mehrere PWM-Muster nacheinander ab (Steinlampe-Demo)
  - globale Helligkeit sowie Musterwahl können später über beliebige Eingaben erfolgen
    (aktuell via Serieller Konsole / optional BLE / BT-Serial)
  - alter Metall-Kippschalter (GPIO32) schaltet EIN/AUS; kurzer Aus->Ein-Impuls blättert Modi
  - der Metallhebel wirkt zusätzlich als Touch-Elektrode (GPIO27/T7) zum Dimmen bei langem Berühren
*/

#include "lamp_config.h"

#include <Arduino.h>
#include <math.h>
#include <vector>
#include <ctype.h>
#include <esp_system.h>

#include "comms.h"
#include "filters.h"
#include "lamp_state.h"
#include "patterns.h"
#include "settings.h"
#include "utils.h"
#include "persistence.h"
#include "print.h"
#include "pinout.h"
#include "lightSensor.h"
#include "microphone.h"
#include "inputs.h"
#include "presence_ble.h"
#include "presence_radar.h"
#include "quickmode.h"
#include "sleepwake.h"
#include "notifications.h"
#include "pattern.h"
#include "demo.h"

#if ENABLE_BLE
#include <BLEDevice.h>
#endif

#include <esp_sleep.h>
#include <esp_spp_api.h>

bool ensureBaseMac()
{
#if defined(ARDUINO_ARCH_ESP32)
  uint8_t mac[6] = {0};
  esp_err_t err = esp_efuse_mac_get_default(mac);
  if (err == ESP_OK)
    return true;
  // fallback: locally administered MAC derived from chip ID + random byte
  uint32_t chip = (uint32_t)ESP.getEfuseMac();
  mac[0] = 0x02; // locally administered unicast
  mac[1] = 0x00;
  mac[2] = (chip >> 16) & 0xFF;
  mac[3] = (chip >> 8) & 0xFF;
  mac[4] = chip & 0xFF;
  mac[5] = (uint8_t)(esp_random() & 0xFF);
  esp_base_mac_addr_set(mac);
  return false;
#else
  return true;
#endif
}

/**
 * @brief Drive the active animation (wake fade or pattern) and auto-cycle.
 */
void updatePatternEngine()
{
  uint32_t now = millis();
  if (wakeFadeActive)
  {
    lastActivityMs = now;
    if (!lampEnabled)
    {
      wakeFadeActive = false;
      wakeSoftCancel = false;
      return;
    }
    uint32_t elapsedWake = now - wakeStartMs;
    float progress = (wakeDurationMs > 0) ? clamp01((float)elapsedWake / (float)wakeDurationMs) : 1.0f;
    float eased = progress * progress * (3.0f - 2.0f * progress);
    float level = clamp01(Settings::WAKE_START_LEVEL + (wakeTargetLevel - Settings::WAKE_START_LEVEL) * eased);
    applyPwmLevel(level);
    if (progress >= 1.0f)
    {
      wakeFadeActive = false;
      wakeSoftCancel = false;
      patternStartMs = now;
      sendFeedback(F("[Wake] Fade abgeschlossen."));
    }
    return;
  }
  else
  {
    presenceBleDetected = false;
    presenceBlePrevConnected = false;
    presenceBleGraceDeadline = 0;
  }

  // PresenceBLE polling with occasional scan
  bool anyClient = btHasClient();
  if (bleActive())
  {
    anyClient = true;
    if (lastBleAddr.length() == 0)
      lastBleAddr = getLastBleAddr();
  }
  if (presenceBleEnabled && presenceBleHasDevices())
  {
    uint32_t nowMs = millis();
    bool wasDetected = presenceBleDetected;
    bool detected = false;

    // Connected client counts if it matches one of the targets
    if (anyClient && lastBleAddr.length() > 0 && presenceBleIsTarget(lastBleAddr))
    {
      detected = true;
      lastPresenceBleSeenMs = nowMs;
    }

    // fall back to legacy single addr for BT serial if we ever capture it
    if (!detected && anyClient && presenceBleAddr.length() > 0 && presenceBleIsTarget(presenceBleAddr))
    {
      detected = true;
      lastPresenceBleSeenMs = nowMs;
    }

    // occasionally try to spot the target via scan
    const uint32_t SCAN_INTERVAL_MS = 25000;
    if (!detected && nowMs - lastPresenceBleScanMs >= SCAN_INTERVAL_MS)
    {
      lastPresenceBleScanMs = nowMs;
      if (presenceBleScanOnce())
      {
        detected = true;
      }
    }

    if (lastPresenceBleSeenMs > 0 && (nowMs - lastPresenceBleSeenMs) <= presenceBleGraceMs)
      detected = true;

    if (detected)
    {
      presenceBleGraceDeadline = 0;
      presenceBlePrevConnected = true;
      if (!presenceBleDetected)
        sendFeedback(F("[PresenceBLE] detected"));
      presenceBleDetected = true;
      lastPresenceBleSeenMs = nowMs;
      if (lampEnabled)
        presenceBleLastOffByPresence = false;
      if (presenceBleAutoOn && presenceBleLastOffByPresence && !lampEnabled &&
          (presenceBleAlwaysOverride || hardwareWantsOn()))
      {
        setLampEnabled(true, "presence_ble connect");
        presenceBleLastOffByPresence = false;
        sendFeedback(F("[PresenceBLE] Detected -> Lamp ON"));
      }
    }
    else if (presenceBleGraceDeadline == 0 && (presenceBlePrevConnected || lastPresenceBleSeenMs > 0))
    {
      presenceBleGraceDeadline = nowMs + presenceBleGraceMs;
      sendFeedback(String(F("[PresenceBLE] No device -> pending OFF in ")) + String(presenceBleGraceMs) + F("ms"));
      presenceBleDetected = false;
    }
    else if (!detected && wasDetected)
    {
      presenceBleDetected = false;
      sendFeedback(F("[PresenceBLE] no device detected"));
    }
  }

  if (presenceBleGraceDeadline > 0 && millis() >= presenceBleGraceDeadline)
  {
    presenceBleGraceDeadline = 0;
    if (presenceBleAutoOff && lampEnabled)
    {
      // Do not force off if hardware (switch/poti) currently wants the lamp on, unless overridden.
      if (hardwareWantsOn() && !presenceBleAlwaysOverride)
      {
        sendFeedback(F("[PresenceBLE] Grace timeout ignored (hardware ON)"));
      }
      else
      {
        setLampEnabled(false, "presence_ble grace");
        presenceBleLastOffByPresence = true;
        sendFeedback(F("[PresenceBLE] Grace timeout -> Lamp OFF"));
        return;
      }
    }
  }

  // idle-off timer
  if (idleOffMs > 0 && lampEnabled && !rampActive
#if ENABLE_SWITCH
      && !switchDebouncedState
#endif
  )
  {
    if (now - lastActivityMs >= idleOffMs)
    {
      setLampEnabled(false, "idleoff");
      sendFeedback(F("[IdleOff] Timer -> Lamp OFF"));
    }
  }

  if (sleepFadeActive)
  {
    lastActivityMs = now;
    uint32_t elapsedSleep = now - sleepStartMs;
    float progress = sleepDurationMs > 0 ? clamp01((float)elapsedSleep / (float)sleepDurationMs) : 1.0f;
    float level = sleepStartLevel * (1.0f - progress);
    applyPwmLevel(level);
    if (progress >= 1.0f)
    {
      sleepFadeActive = false;
      setLampEnabled(false, "sleep done");
      sendFeedback(F("[Sleep] Fade abgeschlossen."));
    }
    return;
  }

  // If lamp is off and no pending ramps/notifications, force output to 0 and clear filters.
  if (!lampEnabled && !notifyActive && !rampActive && !wakeFadeActive && !sleepFadeActive)
  {
    patternFilteredLevel = 0.0f;
    patternFilterLastMs = 0;
    applyPwmLevel(0.0f);
    return;
  }

  const Pattern &p = PATTERNS[currentPattern];
  uint32_t elapsed = now - patternStartMs;
  uint32_t scaledElapsed = (uint32_t)((float)elapsed * patternSpeedScale);
  float relative = clamp01(p.evaluate(scaledElapsed));
  if (patternInvert)
    relative = 1.0f - relative;
  float span = patternMarginHigh - patternMarginLow;
  if (span < 0.0f)
    span = 0.0f;
  float adjusted = patternMarginLow + clamp01(relative) * span;
  if (adjusted < 0.0f)
    adjusted = 0.0f;
  if (adjusted > 1.0f)
    adjusted = 1.0f;
  relative = adjusted;
  float combined = lampEnabled ? relative * masterBrightness * ambientScale * outputScale : 0.0f;

  // Notifications: ignore pattern; use brightness+ambient only with a floor.
  if (notifyActive && !notifySeq.empty())
  {
    uint32_t dtStage = now - notifyStageStartMs;
    uint32_t stageDur = notifySeq[notifyIdx];
    if (dtStage >= stageDur)
    {
      notifyIdx++;
      if (notifyIdx >= notifySeq.size())
      {
        notifyActive = false;
        if (notifyPrevLampOn)
        {
          setLampEnabled(true, "notify done");
        }
        else
        {
          // setLampEnabled(false, "notify done");
          forceLampOff("notify done");
        }
        notifyRestoreLamp = false;
      }
      else
      {
        notifyStageStartMs = now;
      }
    }
    if (notifyActive)
    {
      // Start from brightness * ambient, with a minimum brightness to stay visible.
      float base = masterBrightness;
      if (base < notifyMinBrightness)
        base = notifyMinBrightness;
      base *= ambientScale;

      bool onPhase = (notifyIdx % 2 == 0);
      float scale = notifyInvert ? (onPhase ? 0.0f : 1.0f) : (onPhase ? 1.0f : 0.0f);
      if (notifyFadeMs > 0)
      {
        uint32_t dt = now - notifyStageStartMs;
        uint32_t dur = notifySeq[notifyIdx];
        float s = 1.0f;
        if (dt < notifyFadeMs)
          s = (float)dt / (float)notifyFadeMs;
        else if (dt > dur - notifyFadeMs)
          s = (float)(dur - dt) / (float)notifyFadeMs;
        if (s < 0.0f)
          s = 0.0f;
        if (s > 1.0f)
          s = 1.0f;
        if (notifyInvert)
          scale = onPhase ? (1.0f - s) : s;
        else
          scale = onPhase ? s : (1.0f - s);
      }
      combined = base * scale;
    }
  }

#if ENABLE_MUSIC_MODE
  if (musicEnabled && !notifyActive)
    combined *= musicModScale;
#endif
  if (patternFadeEnabled)
  {
    if (patternFilterLastMs == 0)
    {
      patternFilteredLevel = combined;
      patternFilterLastMs = now;
    }
    uint32_t dt = now - patternFilterLastMs;
    patternFilterLastMs = now;
    float base = (float)(rampDurationMs > 0 ? rampDurationMs : 1);
    float alpha = clamp01((float)dt / (base * patternFadeStrength));
    patternFilteredLevel += (combined - patternFilteredLevel) * alpha;
    float out = filtersApply(patternFilteredLevel, now);
    applyPwmLevel(out);
  }
  else
  {
    patternFilteredLevel = combined;
    patternFilterLastMs = now;
    float out = filtersApply(combined, now);
    applyPwmLevel(out);
  }

  uint32_t durEff = p.durationMs > 0 ? (uint32_t)((float)p.durationMs / patternSpeedScale) : 0;
  if (demoActive)
  {
    if (demoList.empty())
    {
      demoActive = false;
    }
    else if (now - demoLastSwitchMs >= demoDwellMs)
    {
      demoIndex = (demoIndex + 1) % demoList.size();
      demoLastSwitchMs = now;
      applyQuickMode(demoList[demoIndex]);
    }
  }

  if (!demoActive && autoCycle && durEff > 0 && elapsed >= durEff)
  {
    setPattern((currentPattern + 1) % PATTERN_COUNT, true, false);
  }
}



/**
 * @brief Enter light sleep briefly when lamp is idle to save power.
 */
void maybeLightSleep()
{
#if !ENABLE_BLE
  if (lampEnabled || wakeFadeActive || sleepFadeActive || rampActive)
    return;
  esp_sleep_enable_timer_wakeup(200000); // 200 ms
#if ENABLE_SWITCH
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_SWITCH, SWITCH_ACTIVE_LEVEL == LOW ? 0 : 1);
#endif
  esp_light_sleep_start();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
#if ENABLE_SWITCH
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0);
#endif
#endif
}

// ---------- Setup / Loop ----------
/**
 * @brief Arduino setup hook: configure IO, restore state, start comms.
 */
void setup()
{
  pinMode(PIN_OUTPUT, OUTPUT);
#if ENABLE_ANALOG_OUTPUT
  dacWrite(PIN_OUTPUT, OFF_RAW); // explicit off for analog driver
#else
  digitalWrite(PIN_OUTPUT, LOW); // keep LED off during init
#endif

  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("Quarzlampe PWM-Demo"));
  ensureBaseMac();
  // Allow secure-boot window via switch or poti
#if ENABLE_SWITCH || ENABLE_POTI
  bootStartMs = millis();
  secureBootToggleCount = 0;
  secureBootLatched = false;
  secureBootWindowClosed = false;
  startupHoldActive = true;
#endif

#if ENABLE_SWITCH
  pinMode(PIN_SWITCH, INPUT_PULLUP);
#endif
#if ENABLE_TOUCH_DIM
  calibrateTouchBaseline();
#endif

#if ENABLE_LIGHT_SENSOR || ENABLE_POTI || ENABLE_MUSIC_MODE || ENABLE_EXT_INPUT
  analogReadResolution(12);
#endif
#if ENABLE_LIGHT_SENSOR
  analogSetPinAttenuation(Settings::LIGHT_PIN, ADC_11db);
  pinMode(Settings::LIGHT_PIN, INPUT);
#endif
#if ENABLE_POTI
  analogSetPinAttenuation(PIN_POTI, ADC_11db);
  pinMode(PIN_POTI, INPUT);
#endif
#if ENABLE_EXT_INPUT
  if (extInputAnalog)
  {
    analogSetPinAttenuation(Settings::EXT_INPUT_PIN, ADC_11db);
    pinMode(Settings::EXT_INPUT_PIN, INPUT);
  }
  else
  {
    pinMode(Settings::EXT_INPUT_PIN, Settings::EXT_INPUT_ACTIVE_LOW ? INPUT_PULLUP : INPUT);
  }
#endif
#if ENABLE_MUSIC_MODE
  analogSetPinAttenuation(Settings::MUSIC_PIN, ADC_11db);
  pinMode(Settings::MUSIC_PIN, INPUT);
#endif
#if ENABLE_PUSH_BUTTON
  pinMode(PIN_PUSHBTN, INPUT_PULLUP);
#endif
#if ENABLE_RD03
  radarSetup();
#endif

  loadSettings();
  trustSetBootMs(millis());

#if !ENABLE_ANALOG_OUTPUT
  ledcSetup(LEDC_CH, LEDC_FREQ, LEDC_RES);
  ledcAttachPin(PIN_OUTPUT, LEDC_CH);
  ledcWrite(LEDC_CH, OFF_RAW); // explicit off at startup
#else
  dacWrite(PIN_OUTPUT, OFF_RAW);
#endif
  patternStartMs = millis();

  announcePattern(true);
  printHelp(true);
  printStatus(true);
  setupCommunications();
  updatePatternEngine();

#if ENABLE_SWITCH
  initSwitchState();
#endif
}

/**
 * @brief Arduino loop hook: process inputs and refresh PWM output.
 */
void loop()
{
  if (startupHoldActive)
  {
    uint32_t now = millis();
    processStartupSwitch();
    if ((now - bootStartMs) < SECURE_BOOT_WINDOW_MS)
    {
      if (lampEnabled)
        setLampEnabled(false, "startup-hold");
#if ENABLE_ANALOG_OUTPUT
      dacWrite(PIN_OUTPUT, OFF_RAW);
#else
      ledcWrite(LEDC_CH, OFF_RAW);
#endif
      delay(10);
      return;
    }
    startupHoldActive = false;
    secureBootWindowClosed = true;
  }
  pollCommunications();
#if ENABLE_SWITCH
  updateSwitchLogic();
#endif
#if ENABLE_TOUCH_DIM
  updateTouchBrightness();
#endif
  updateBrightnessRamp();
  updatePatternEngine();
  updateLightSensor();
#if ENABLE_POTI
  updatePoti();
#endif
#if ENABLE_PUSH_BUTTON
  updatePushButton();
#endif
#if ENABLE_MUSIC_MODE
  updateMusicSensor();
#endif
#if ENABLE_EXT_INPUT
  updateExternalInput();
#endif
#if ENABLE_RD03
  updateRadar();
#endif
  flushLiveState();
  maybeLightSleep();
  delay(10);
}
