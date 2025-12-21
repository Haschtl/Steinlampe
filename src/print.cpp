#include <Arduino.h>

#include "pinout.h"

#include "comms.h"
#include "filters.h"
#include "lamp_state.h"
#include "patterns.h"
#include "settings.h"
#include "utils.h"
#include "persistence.h"
#include "print.h"
#include "lightSensor.h"
#include "microphone.h"
#include "inputs.h"
#include "presence.h"
#include "quickmode.h"
#include "sleepwake.h"
#include "notifications.h"
#include "pattern.h"
#include "demo.h"

/**
 * @brief Print averaged touch sensor data for calibration purposes.
 */
void printTouchDebug(const bool &force)
{
#if !ENABLE_TOUCH_DIM
    sendFeedback(F("[Touch] disabled"), force);
#else
    long acc = 0;
    const int samples = 10;
    for (int i = 0; i < samples; ++i)
    {
        acc += touchRead(PIN_TOUCH_DIM);
        delay(5);
    }
    int raw = (int)(acc / samples);
    int delta = touchBaseline - raw;
    sendFeedback(String(F("[Touch] raw=")) + String(raw) + F(" baseline=") + String(touchBaseline) +
                 F(" delta=") + String(delta) + F(" thrOn=") + String(touchDeltaOn) + F(" thrOff=") + String(touchDeltaOff),force);
#endif
}

/**
 * @brief Print current mode, brightness and wake/auto state.
 */
void printStatus(const bool &force)
{
#ifdef ENABLE_HUMAN_STATUS
    String line1 = String(F("Pattern ")) + String(currentPattern + 1) + F("/") + String(PATTERN_COUNT) + F(" '") +
                   PATTERNS[currentPattern].name + F("' | AutoCycle=") + (autoCycle ? F("ON") : F("OFF")) +
                   F(" | Speed=") + String(patternSpeedScale, 2) +
                   F(" | Invert=") + (patternInvert ? F("ON") : F("OFF"));
    if (wakeFadeActive)
        line1 += F(" | Wake");
    if (sleepFadeActive)
        line1 += F(" | Sleep");
    sendFeedback(line1,force);

    String quickLine = String(F("[Quick] ")) + quickMaskToCsv();
    sendFeedback(quickLine,force);

    String line2 = String(F("Lamp=")) + (lampEnabled ? F("ON") : F("OFF")) + F(" | Brightness=") +
                   String(masterBrightness * 100.0f, 1) + F("%");
#if ENABLE_SWITCH
    line2 += F(" | Switch=");
    line2 += (switchDebouncedState ? F("ON") : F("OFF"));
#endif
    sendFeedback(line2,force);

#if ENABLE_MUSIC_MODE
    String clapLine = String(F("[Clap] ")) + (clapEnabled ? F("ON") : F("OFF")) + F(" thr=") + String(clapThreshold, 2) + F(" cool=") + String(clapCooldownMs);
    sendFeedback(clapLine,force);
#endif

    String line3 = F("Ramp=");
    line3 += String(rampOnDurationMs);
    line3 += F("ms (on) / ");
    line3 += String(rampOffDurationMs);
    line3 += F("ms (off) | IdleOff=");
    if (idleOffMs == 0)
        line3 += F("off");
    else
    {
        line3 += String(idleOffMs / 60000);
        line3 += F("m");
    }
#if ENABLE_TOUCH_DIM
    line3 += F(" | TouchDim=");
    line3 += touchDimEnabled ? F("ON") : F("OFF");
    line3 += F(" (step=");
    line3 += String(touchDimStep, 3);
    line3 += F(")");
#endif
    line3 += F(" | PatFade=");
    if (patternFadeEnabled)
    {
        line3 += String(F("ON(")) + String(patternFadeStrength, 2) + F("x)");
    }
    else
    {
        line3 += F("OFF");
    }
    line3 += F(" | RampOn=");
    line3 += easeToString(rampEaseOnType);
    line3 += F("(");
    line3 += String(rampEaseOnPower, 2);
    line3 += F(")");
    line3 += F(" | RampOff=");
    line3 += easeToString(rampEaseOffType);
    line3 += F("(");
    line3 += String(rampEaseOffPower, 2);
    line3 += F(")");
    line3 += F(" | PWM=");
    line3 += String(outputGamma, 2);
#if ENABLE_EXT_INPUT
    line3 += F(" | ExtIn=");
    line3 += extInputEnabled ? F("ON") : F("OFF");
    line3 += extInputAnalog ? F("(ana)") : F("(dig)");
#endif
    sendFeedback(line3,force);

    // Filters
    FilterState filt;
    filtersGetState(filt);
    String filtLine = F("[Filter] ");
    filtLine += F("iir=");
    filtLine += filt.iirEnabled ? F("ON") : F("OFF");
    filtLine += F("(");
    filtLine += String(filt.iirAlpha, 3);
    filtLine += F(") clip=");
    filtLine += filt.clipEnabled ? F("ON") : F("OFF");
    filtLine += F("(");
    filtLine += String(filt.clipAmount, 2);
    filtLine += F(")");
    filtLine += F(" trem=");
    filtLine += filt.tremEnabled ? F("ON") : F("OFF");
    filtLine += F("(");
    filtLine += String(filt.tremRateHz, 2);
    filtLine += F("Hz/");
    filtLine += String(filt.tremDepth, 2);
    filtLine += F(")");
    filtLine += F(" spark=");
    filtLine += filt.sparkEnabled ? F("ON") : F("OFF");
    filtLine += F("(");
    filtLine += String(filt.sparkDensity, 2);
    filtLine += F("/");
    filtLine += String(filt.sparkIntensity, 2);
    filtLine += F("/");
    filtLine += String(filt.sparkDecayMs);
    filtLine += F("ms)");
    filtLine += F(" comp=");
    filtLine += filt.compEnabled ? F("ON") : F("OFF");
    filtLine += F("(");
    filtLine += String(filt.compThr, 2);
    filtLine += F("/");
    filtLine += String(filt.compRatio, 2);
    filtLine += F("/");
    filtLine += String(filt.compAttackMs);
    filtLine += F("/");
    filtLine += String(filt.compReleaseMs);
    filtLine += F(")");
    filtLine += F(" env=");
    filtLine += filt.envEnabled ? F("ON") : F("OFF");
    filtLine += F("(");
    filtLine += String(filt.envAttackMs);
    filtLine += F("/");
    filtLine += String(filt.envReleaseMs);
    filtLine += F(")");
    sendFeedback(filtLine,force);

    String line4 = F("Presence=");
    if (presenceEnabled)
    {
        line4 += F("ON (");
        String devices = presenceListCsv();
        line4 += (devices.isEmpty() ? F("no device") : devices);
        line4 += F(" thr=");
        line4 += presenceRssiThreshold;
        line4 += F("dBm on=");
        line4 += presenceAutoOn ? F("1") : F("0");
        line4 += F(" off=");
        line4 += presenceAutoOff ? F("1") : F("0");
        line4 += F(")");
    }
    else
    {
        line4 += F("OFF");
    }
    sendFeedback(line4,force);

    if (demoActive)
    {
        String demoLine = String(F("[Demo] dwell=")) + String(demoDwellMs) + F("ms list=") + quickMaskToCsv();
        sendFeedback(demoLine,force);
    }

#if ENABLE_BLE
    String line4b = F("Device=");
    line4b += getBLEAddress();
    line4b += F(" | Service=");
    line4b += Settings::BLE_SERVICE_UUID;
    line4b += F(" | Cmd=");
    line4b += Settings::BLE_COMMAND_CHAR_UUID;
    line4b += F(" | Status=");
    line4b += Settings::BLE_STATUS_CHAR_UUID;
    line4b += F(" | BLE=");
    line4b += bleActive() ? F("UP") : F("DOWN");
    sendFeedback(line4b,force);
#endif

    String customLine = String(F("[Custom] len=")) + String(customLen) + F(" stepMs=") + String(customStepMs);
    sendFeedback(customLine,force);

#if ENABLE_TOUCH_DIM
    {
        int raw = touchRead(PIN_TOUCH_DIM);
        int delta = raw - touchBaseline;
        int mag = abs(delta);
        String touchLine = String(F("[Touch] base=")) + String(touchBaseline) + F(" raw=") + String(raw) +
                           F(" delta=") + String(delta) + F(" |mag=") + String(mag) + F(" thrOn=") + String(touchDeltaOn) +
                           F(" thrOff=") + String(touchDeltaOff) + F(" active=") + (touchActive ? F("1") : F("0"));
        sendFeedback(touchLine,force);
    }
#else
    sendFeedback(F("[Touch] N/A"),force);
#endif

#if ENABLE_LIGHT_SENSOR
    String lightLine = F("[Light] ");
    if (lightSensorEnabled)
    {
        lightLine += String(F("raw=")) + String((int)lightFiltered) + F(" min=") + String((int)lightMinRaw) +
                     F(" max=") + String((int)lightMaxRaw) + F(" alpha=") + String(lightAlpha, 3) +
                     F(" ambx=") + String(rampAmbientMultiplier, 2) + F(" rampAmb=") + String(rampAmbientFactor, 2);
    }
    else
    {
        lightLine += F("off rampAmb=");
        lightLine += String(rampAmbientFactor, 2);
    }
    sendFeedback(lightLine,force);
#else
    String lightLine = F("[Light] N/A");
    sendFeedback(lightLine,force);
#endif

#if ENABLE_MUSIC_MODE
    String musicLine = String(F("[Music] ")) + (musicEnabled ? F("ON") : F("OFF"));
    sendFeedback(musicLine,force);
#else
    String musicLine = F("[Music] N/A");
    sendFeedback(musicLine,force);
#endif
#if ENABLE_POTI
    String potiLine = String(F("[Poti] ")) + (potiEnabled ? F("ON") : F("OFF")) + F(" a=") + String(potiAlpha, 2) +
                      F(" d=") + String(potiDeltaMin, 3) + F(" off=") + String(potiOffThreshold, 3) +
                      F(" smpl=") + String(potiSampleMs) + F("ms") +
                      F(" min=") + String(potiCalibMin, 3) + F(" max=") + String(potiCalibMax, 3) +
                      F(" inv=") + (potiInvert ? F("1") : F("0"));
#else
    String potiLine = F("[Poti] N/A");
#endif
    sendFeedback(potiLine,force);

#if ENABLE_PUSH_BUTTON
    String pushLine = String(F("[Push] ")) + (pushEnabled ? F("ON") : F("OFF")) + F(" db=") + String(pushDebounceMs) +
                      F(" dbl=") + String(pushDoubleMs) + F(" hold=") + String(pushHoldMs) +
                      F(" step=") + String(pushStep * 100.0f, 1) + F("%/") + String(pushStepMs) + F("ms");
#else
    String pushLine = F("[Push] N/A");
#endif
    sendFeedback(pushLine,force);
#endif
    printStatusStructured(force);
}

/**
 * @brief Emit structured sensor snapshot for machine parsing.
 */
void printSensorsStructured(const bool &force)
{
    String line = F("SENSORS|");
#if ENABLE_TOUCH_DIM
    line += F("touch_base=");
    line += String(touchBaseline);
    int raw = touchRead(PIN_TOUCH_DIM);
    line += F("|touch_raw=");
    line += String(raw);
    line += F("|touch_delta=");
    line += String(touchBaseline - raw);
    line += F("|touch_active=");
    line += touchActive ? F("1") : F("0");
#else
    line += F("touch=N/A");
#endif
#if ENABLE_LIGHT_SENSOR
    line += F("|light_raw=");
    line += String((int)lightFiltered);
    line += F("|light_min=");
    line += String((int)lightMinRaw);
    line += F("|light_max=");
    line += String((int)lightMaxRaw);
    line += F("|light_amb_mult=");
    line += String(rampAmbientMultiplier, 2);
    line += F("|ramp_amb=");
    line += String(rampAmbientFactor, 2);
#else
    line += F("|light_raw=N/A");
#endif
#if ENABLE_MUSIC_MODE
    line += F("|music_env=");
    line += String(musicFiltered, 3);
    line += F("|music_auto=");
    line += musicAutoLamp ? F("ON") : F("OFF");
    line += F("|music_thr=");
    line += String(musicAutoThr, 2);
    line += F("|music_mode=");
    line += (musicMode == 1) ? F("beat") : F("direct");
    line += F("|music_mod=");
    line += String(musicModScale, 3);
    line += F("|music_kick_ms=");
    line += (musicLastKickMs > 0 ? String(millis() - musicLastKickMs) : F("N/A"));
    line += F("|music_level=");
    line += String(musicRawLevel, 3);
#else
    line += F("|music_env=N/A");
#endif
    sendFeedback(line,force);
}
/**
 * @brief Emit a single structured status line for easier parsing (key=value pairs).
 */
void printStatusStructured(const bool &force)
{
    // Core line (keep short for BLE MTU)
    String line = F("STATUS|");
    line += F("pattern=");
    line += String(currentPattern + 1);
    line += F("|pattern_total=");
    line += String(PATTERN_COUNT);
    line += F("|pattern_name=");
    line += PATTERNS[currentPattern].name;
    line += F("|pat_ms=");
    line += String(millis() - patternStartMs);
    line += F("|auto=");
    line += autoCycle ? F("1") : F("0");
    line += F("|bri=");
    line += String(masterBrightness * 100.0f, 1);
    line += F("|lamp=");
    line += lampEnabled ? F("ON") : F("OFF");
#if ENABLE_SWITCH
    line += F("|switch=");
    line += switchDebouncedState ? F("ON") : F("OFF");
#else
    line += F("|switch=N/A");
#endif
#if ENABLE_TOUCH_DIM
    line += F("|touch_dim=");
    line += touchDimEnabled ? F("1") : F("0");
    line += F("|touch_dim_step=");
    line += String(touchDimStep, 3);
#else
    line += F("|touch=N/A");
#endif
    line += F("|ramp_on_ms=");
    line += String(rampOnDurationMs);
    line += F("|ramp_off_ms=");
    line += String(rampOffDurationMs);
    line += F("|ramp_on_ease=");
    line += easeToString(rampEaseOnType);
    line += F("|ramp_off_ease=");
    line += easeToString(rampEaseOffType);
    line += F("|ramp_on_pow=");
    line += String(rampEaseOnPower, 2);
    line += F("|ramp_off_pow=");
    line += String(rampEaseOffPower, 2);
#if ENABLE_LIGHT_SENSOR
    line += F("|ramp_amb=");
    line += String(rampAmbientFactor, 2);
#endif
    line += F("|idle_min=");
    line += idleOffMs == 0 ? F("0") : String(idleOffMs / 60000);
    line += F("|pat_speed=");
    line += String(patternSpeedScale, 2);
    line += F("|pat_fade=");
    line += patternFadeEnabled ? String(patternFadeStrength, 2) : F("off");
    line += F("|pat_inv=");
    line += patternInvert ? F("1") : F("0");
    line += F("|pat_lo=");
    line += String(patternMarginLow, 3);
    line += F("|pat_hi=");
    line += String(patternMarginHigh, 3);
    line += F("|quick=");
    line += quickMaskToCsv();
    line += F("|presence=");
    line += presenceEnabled ? F("ON") : F("OFF");
    line += F("|presence_count=");
    line += (int)presenceDevices.size();
    line += F("|presence_thr=");
    line += presenceRssiThreshold;
    line += F("|presence_on=");
    line += presenceAutoOn ? F("1") : F("0");
    line += F("|presence_off=");
    line += presenceAutoOff ? F("1") : F("0");
    line += F("|presence_list=");
    line += presenceListCsv();
    line += F("|presence_grace=");
    line += presenceGraceMs;
#if ENABLE_BLE
    line += F("|ble=");
    line += bleActive() ? F("UP") : F("DOWN");
#endif
    sendFeedback(line,force);
    updateBleStatus(line);

    // Detail line: IO, PWM, light, music, inputs
    String lineIO = F("STATUS1|");
#if ENABLE_EXT_INPUT
    lineIO += F("ext_in=");
    lineIO += extInputEnabled ? F("ON") : F("OFF");
    lineIO += F("|ext_mode=");
    lineIO += extInputAnalog ? F("ana") : F("dig");
    lineIO += F("|ext_alpha=");
    lineIO += String(extInputAlpha, 3);
    lineIO += F("|ext_delta=");
    lineIO += String(extInputDelta, 3);
    lineIO += F("|ext_val=");
    lineIO += extInputFiltered >= 0.0f ? String(extInputFiltered, 3) : F("N/A");
#endif
#if ENABLE_BT_SERIAL
    lineIO += F("|bt_sleep_boot_ms=");
    lineIO += String(getBtSleepAfterBootMs());
    lineIO += F("|bt_sleep_ble_ms=");
    lineIO += String(getBtSleepAfterBleMs());
#else
    lineIO += F("|bt_sleep_boot_ms=N/A|bt_sleep_ble_ms=N/A");
#endif
    lineIO += F("|custom_len=");
    lineIO += String(customLen);
    lineIO += F("|custom_step_ms=");
    lineIO += String(customStepMs);
    lineIO += F("|demo=");
    lineIO += demoActive ? F("ON") : F("OFF");
    lineIO += F("|out=");
#if ENABLE_ANALOG_OUTPUT
    lineIO += F("ana");
#else
    lineIO += F("pwm");
#endif
    lineIO += F("|gamma=");
    lineIO += String(outputGamma, 2);
    lineIO += F("|pwm_raw=");
    lineIO += String((uint32_t)lastPwmValue);
    lineIO += F("|pwm_max=");
    lineIO += String((uint32_t)PWM_MAX);
    lineIO += F("|bri_min=");
    lineIO += String(briMinUser * 100.0f, 1);
    lineIO += F("|bri_max=");
    lineIO += String(briMaxUser * 100.0f, 1);
    lineIO += F("|notif_min=");
    lineIO += String(notifyMinBrightness * 100.0f, 1);
#if ENABLE_LIGHT_SENSOR
    lineIO += F("|light=");
    lineIO += lightSensorEnabled ? F("ON") : F("OFF");
    lineIO += F("|light_gain=");
    lineIO += String(lightGain, 2);
    lineIO += F("|light_min=");
    lineIO += String(lightClampMin, 2);
    lineIO += F("|light_max=");
    lineIO += String(lightClampMax, 2);
    lineIO += F("|light_alpha=");
    lineIO += String(lightAlpha, 3);
    lineIO += F("|light_raw=");
    lineIO += String((int)lightFiltered);
    lineIO += F("|light_raw_min=");
    lineIO += String((int)lightMinRaw);
    lineIO += F("|light_raw_max=");
    lineIO += String((int)lightMaxRaw);
#else
    lineIO += F("|light=N/A");
#endif
#if ENABLE_MUSIC_MODE
    lineIO += F("|music=");
    lineIO += musicEnabled ? F("ON") : F("OFF");
    lineIO += F("|music_gain=");
    lineIO += String(musicGain, 2);
    lineIO += F("|music_auto=");
    lineIO += musicAutoLamp ? F("ON") : F("OFF");
    lineIO += F("|music_thr=");
    lineIO += String(musicAutoThr, 2);
    lineIO += F("|music_mode=");
    lineIO += (musicMode == 1 ? F("beat") : F("direct"));
    lineIO += F("|music_mod=");
    lineIO += String(musicModScale, 3);
    lineIO += F("|music_kick_ms=");
    lineIO += (musicLastKickMs > 0 ? String(millis() - musicLastKickMs) : F("N/A"));
    lineIO += F("|music_env=");
    lineIO += String(musicFiltered, 3);
    lineIO += F("|music_level=");
    lineIO += String(musicRawLevel, 3);
    lineIO += F("|music_smooth=");
    lineIO += String(musicSmoothing, 2);
    lineIO += F("|clap=");
    lineIO += clapEnabled ? F("ON") : F("OFF");
    lineIO += F("|clap_thr=");
    lineIO += String(clapThreshold, 2);
    lineIO += F("|clap_cool=");
    lineIO += String(clapCooldownMs);
    lineIO += F("|clap_cmd1=");
    lineIO += clapCmd1;
    lineIO += F("|clap_cmd2=");
    lineIO += clapCmd2;
    lineIO += F("|clap_cmd3=");
    lineIO += clapCmd3;
#else
    lineIO += F("|music=N/A|clap=N/A");
#endif
#if ENABLE_POTI
    lineIO += F("|poti=");
    lineIO += potiEnabled ? F("ON") : F("OFF");
    lineIO += F("|poti_alpha=");
    lineIO += String(potiAlpha, 2);
    lineIO += F("|poti_delta=");
    lineIO += String(potiDeltaMin, 3);
    lineIO += F("|poti_off=");
    lineIO += String(potiOffThreshold, 3);
    lineIO += F("|poti_sample=");
    lineIO += String(potiSampleMs);
    lineIO += F("|poti_min=");
    lineIO += String(potiCalibMin, 3);
    lineIO += F("|poti_max=");
    lineIO += String(potiCalibMax, 3);
    lineIO += F("|poti_inv=");
    lineIO += potiInvert ? F("1") : F("0");
    lineIO += F("|poti_val=");
    lineIO += String(potiFiltered, 3);
    lineIO += F("|poti_raw=");
    lineIO += String(potiLastRaw);
#else
    lineIO += F("|poti=N/A");
#endif
#if ENABLE_PUSH_BUTTON
    lineIO += F("|push=");
    lineIO += pushEnabled ? F("ON") : F("OFF");
    lineIO += F("|push_db=");
    lineIO += String(pushDebounceMs);
    lineIO += F("|push_dbl=");
    lineIO += String(pushDoubleMs);
    lineIO += F("|push_hold=");
    lineIO += String(pushHoldMs);
    lineIO += F("|push_step_ms=");
    lineIO += String(pushStepMs);
    lineIO += F("|push_step=");
    lineIO += String(pushStep, 3);
#else
    lineIO += F("|push=N/A");
#endif
    sendFeedback(lineIO,force);
    updateBleStatus(lineIO);

    // Filters/status chunk (separate to stay under BLE MTU)
    String line2 = F("STATUS2|");
    {
        FilterState filt;
        filtersGetState(filt);
        line2 += F("filter_iir=");
        line2 += filt.iirEnabled ? F("ON") : F("OFF");
        line2 += F("|filter_alpha=");
        line2 += String(filt.iirAlpha, 3);
        line2 += F("|filter_clip=");
        line2 += filt.clipEnabled ? F("ON") : F("OFF");
        line2 += F("|filter_clip_amt=");
        line2 += String(filt.clipAmount, 2);
        line2 += F("|filter_clip_curve=");
        line2 += filt.clipCurve;
        line2 += F("|filter_trem=");
        line2 += filt.tremEnabled ? F("ON") : F("OFF");
        line2 += F("|filter_trem_rate=");
        line2 += String(filt.tremRateHz, 2);
        line2 += F("|filter_trem_depth=");
        line2 += String(filt.tremDepth, 2);
        line2 += F("|filter_trem_wave=");
        line2 += filt.tremWave;
        line2 += F("|filter_spark=");
        line2 += filt.sparkEnabled ? F("ON") : F("OFF");
        line2 += F("|filter_spark_dens=");
        line2 += String(filt.sparkDensity, 2);
        line2 += F("|filter_spark_int=");
        line2 += String(filt.sparkIntensity, 2);
        line2 += F("|filter_spark_decay=");
        line2 += String(filt.sparkDecayMs);
        line2 += F("|filter_comp=");
        line2 += filt.compEnabled ? F("ON") : F("OFF");
        line2 += F("|filter_comp_thr=");
        line2 += String(filt.compThr, 2);
        line2 += F("|filter_comp_ratio=");
        line2 += String(filt.compRatio, 2);
        line2 += F("|filter_comp_att=");
        line2 += String(filt.compAttackMs);
        line2 += F("|filter_comp_rel=");
        line2 += String(filt.compReleaseMs);
        line2 += F("|filter_env=");
        line2 += filt.envEnabled ? F("ON") : F("OFF");
        line2 += F("|filter_env_att=");
        line2 += String(filt.envAttackMs);
        line2 += F("|filter_env_rel=");
        line2 += String(filt.envReleaseMs);
        line2 += F("|filter_delay=");
        line2 += filt.delayEnabled ? F("ON") : F("OFF");
        line2 += F("|filter_delay_ms=");
        line2 += String(filt.delayMs);
        line2 += F("|filter_delay_fb=");
        line2 += String(filt.delayFeedback, 2);
        line2 += F("|filter_delay_mix=");
        line2 += String(filt.delayMix, 2);
    }
    sendFeedback(line2,force);
    updateBleStatus(line2);
}

/**
 * @brief Print available serial/BLE command usage.
 */
void printHelp(const bool &force)
{
#if ENABLE_HELP_TEXT
    const char *lines[] = {
        "Serien-Kommandos:",
        "  list              - verfügbare Muster",
        "  mode <1..N>       - bestimmtes Muster wählen",
        "  next / prev       - weiter oder zurück",
        "  quick <CSV|default>- Modi für schnellen Schalter-Tap",
        "  on / off / toggle - Lampe schalten",
        "  sync             - Lampe an Schalterzustand angleichen",
        "  auto on|off       - automatisches Durchschalten",
        "  bri <0..100>      - globale Helligkeit in %",
        "  bri min/max <0..1>- Min/Max-Level setzen",
        "  wake [soft] [mode=N] [bri=XX] <Sek> - Weckfade (Default 180s, optional weich/Mode/Bri)",
        "  wake stop         - Weckfade abbrechen",
        "  sos [stop]        - SOS-Alarm: Lampe 100%, SOS-Muster",
        "  sleep [Minuten]   - Sleep-Fade auf 0, Default 15min",
        "  sleep stop        - Sleep-Fade abbrechen",
        "  ramp <ms>         - Ramp-Dauer (on/off gemeinsam) 50-10000ms",
        "  ramp on <ms>      - Ramp-Dauer nur für Einschalten",
        "  ramp off <ms>     - Ramp-Dauer nur für Ausschalten",
        "  ramp ease on|off <linear|ease|ease-in|ease-out|ease-in-out|flash> [power]",
        "  idleoff <Min>     - Auto-Off nach X Minuten (0=aus)",
        "  touch tune <on> <off> - Touch-Schwellen setzen",
        "  pat scale <0.1-5> - Pattern-Geschwindigkeit",
        "  pat fade on|off   - Pattern-Ausgabe glätten",
        "  pat fade amt <0.01-10> - Stärke der Glättung (größer = langsamer)",
        "  pwm curve <0.5-4> - PWM-Gamma/Linearität anpassen",
        "  demo [Sek]        - Demo-Modus: Quick-Liste mit fester Verweildauer (Default 6s)",
        "  touch hold <ms>   - Hold-Start 500..5000 ms",
        "  touchdim on/off   - Touch-Dimmen aktivieren/deaktivieren",
        "  clap on|off/thr <0..1>/cool <ms>/train [on|off] - Klatschsteuerung (Audio)",
        "  clap <1|2|3> <cmd> - Befehl bei 1/2/3 Klatschen",
        "  presence on|off   - Presence aktivieren/deaktivieren",
        "  presence add <addr>/del <addr>/clear - Geräte-Liste verwalten",
        "  presence set <addr> - Liste überschreiben (Kompatibilität)",
        "  presence thr <-dBm> - RSSI-Schwelle (z.B. -75)",
        "  presence auto on|off <on|off> - Auto-Licht AN/OFF Aktionen",
        "  presence grace <ms> - Verzögerung vor Auto-Off",
        "  custom v1,v2,...   - Custom-Pattern setzen (0..1)",
        "  custom step <ms>   - Schrittzeit Custom-Pattern",
        "  notify [on1 off1 on2 off2] - Blinksignal (ms)",
        "  music sens <f>/smooth <0-1>/auto on|off/thr <f> - Musik-Parameter (Patterns Music Direct/Beat)",
        "  morse <text>     - Morse-Blink (dot=200ms, dash=600ms)",
        "  profile save <1-3>/load <1-3> - User-Profile ohne Touch/Presence/Quick",
        "  light gain <f>     - Verstärkung Lichtsensor",
        "  poti on|off/alpha <0..1>/delta <0..0.5>/off <0..0.5>/sample <ms>/calib <min> <max>/invert on|off - Poti-Config",
        "  push on|off/debounce <ms>/double <ms>/hold <ms>/step_ms <ms>/step <0..0.5> - Taster-Config",
        "  midi map           - CC7=bri, CC20=mode(1-8), Note59 toggle, Note60 prev, Note62 next, Note70-77 mode 1-8",
        "  calibrate touch    - Geführte Touch-Kalibrierung",
        "  calibrate         - Touch-Baseline neu messen",
        "  touch             - aktuellen Touch-Rohwert anzeigen",
        "  status            - aktuellen Zustand anzeigen",
        "  factory           - Reset aller Settings",
        "  help              - diese Übersicht",
    };
    for (auto l : lines)
        sendFeedback(String(l),force);
#else
    sendFeedback(F("[Help] disabled to save flash. Commands in README."),force);
#endif
}
