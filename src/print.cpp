#include <Arduino.h>

#include "lamp_config.h"
#include "pinout.h"

#include "comms.h"
#include "filters.h"
#include "lamp_state.h"
#include "patterns.h"
#include "lamp_settings.h"
#include "utils.h"
#include "persistence.h"
#include "print.h"
#include "lightSensor.h"
#include "microphone.h"
#include "inputs.h"
#include "presence_ble.h"
#include "ota.h"
#include "presence_radar.h"
#include "quickmode.h"
#include "gestures.h"
#include "lamp_mesh.h"
#include "sleepwake.h"
#include "version.h"
#include "sleepwake.h"
#include "notifications.h"
#include "pattern.h"
#include "demo.h"

static constexpr uint32_t LIVE_STATE_MIN_INTERVAL_MS = 100;
static uint32_t liveStateLastMs = 0;
static bool liveStatePending = false;

static String liveStateLastKey;

static void emitLiveState(const bool &force)
{
    const bool lampOn = (lampEnabled && !lampOffPending);
    // Noise-tolerant change key: drops the raw ADC value and rounds the
    // normalized poti to 1%, so jittery sensor reads don't spam identical
    // STATE events. Only a meaningful change (or a forced emit) goes out.
    //
    // `key`/`line` are function-static and reserved once, not local/rebuilt
    // every call: this runs on every meaningful poti change (far more often
    // than an on-demand status command), and repeatedly growing a fresh
    // local String via many small += calls - even with an upfront reserve()
    // on each new instance - still means a malloc()+free() pair per call.
    // Confirmed on real hardware that *that* allocation churn (not a leak,
    // not static corruption - heap_caps_check_integrity_all() found nothing
    // wrong at any single checkpoint) is what eventually corrupts the TLSF
    // heap once ENABLE_POTI's extra fields are in the mix; keeping these
    // buffers alive across calls means the one-time reserve() below is the
    // only allocation that ever happens for them, for the device's entire
    // uptime.
    static String key;
    static bool keyReserved = false;
    if (!keyReserved)
    {
        key.reserve(200);
        keyReserved = true;
    }
    key = lampOn ? F("1|") : F("0|");
    key += fmtF(masterBrightness * 100.0f, 1);
    key += '|';
    key += String(currentPattern + 1);
    key += '|';
    key += fmtF(lastAppliedLevel * 100.0f, 1);
#if ENABLE_POTI
    key += '|';
    key += fmtF(potiFiltered, 2);
#endif
    if (!force && key == liveStateLastKey)
    {
        liveStatePending = false;
        return;
    }
    static bool liveStateLastKeyReserved = false;
    if (!liveStateLastKeyReserved)
    {
        liveStateLastKey.reserve(200);
        liveStateLastKeyReserved = true;
    }
    liveStateLastKey = key;

    static String line;
    static bool lineReserved = false;
    if (!lineReserved)
    {
        line.reserve(300);
        lineReserved = true;
    }
    line = F("STATE|lamp=");
    line += lampOn ? F("ON") : F("OFF");
    line += F("|bri=");
    line += fmtF(masterBrightness * 100.0f, 1);
    line += F("|pattern=");
    line += String(currentPattern + 1);
    line += F("|out=");
    line += fmtF(lastAppliedLevel * 100.0f, 1);
#if ENABLE_POTI
    line += F("|poti=");
    line += fmtF(potiFiltered, 3);
    line += F("|poti_raw=");
    line += String(potiLastRaw);
#else
    line += F("|poti=N/A");
#endif
    sendFeedback(line, force);
    liveStateLastMs = millis();
    liveStatePending = false;
}

void queueLiveState(const bool &force)
{
    uint32_t now = millis();
    if (force || liveStateLastMs == 0 || (now - liveStateLastMs) >= LIVE_STATE_MIN_INTERVAL_MS)
    {
        emitLiveState(force);
        return;
    }
    liveStatePending = true;
}

void flushLiveState()
{
    if (liveStatePending && (millis() - liveStateLastMs) >= LIVE_STATE_MIN_INTERVAL_MS)
        emitLiveState(false);
}

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
 * @brief Emit structured sensor snapshot for machine parsing.
 */
void printSensorsStructured(const bool &force)
{
    static String line;
    static bool lineReserved = false;
    if (!lineReserved)
    {
        line.reserve(400);
        lineReserved = true;
    }
    line = F("SENSORS|");
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
    // Core line (keep short for BLE MTU). Function-static and reserved once
    // rather than a fresh local String per call - see emitLiveState()'s
    // comment above for why: the repeated malloc()+free() of a fresh String
    // each call (even with its own upfront reserve()) is what corrupts the
    // TLSF heap once ENABLE_POTI's extra fields are in play, confirmed on
    // real hardware. Keeping the buffer alive is only safe if the one-time
    // reserve() below actually covers the real content: 420 did not -
    // desk_lamp's real STATUS line (presence_ble_* fields, "fw_env=desk_lamp",
    // etc.) runs ~555 bytes, so `line` silently exceeded capacity and
    // String::concat() reallocated on literally the first "status" call
    // every time - the exact same TLSF-corruption mechanism as the
    // malloc()+free() bug above, just reached via realloc() instead. Root-
    // caused on real hardware via a controlled A/B test (a line that stays
    // under reserve(), e.g. an unrecognized command, never crashes; one
    // that exceeds it always does) rather than by inspecting this length
    // directly - double-check the real byte count again if this field set
    // grows further, instead of trusting a round number.
    static String line;
    static bool lineReserved = false;
    if (!lineReserved)
    {
        // Bumped from 700 to make room for |gest=<csv> - up to
        // MAX_GESTURE_BINDINGS (24) entries, each up to ~35 bytes in the
        // worst case (e.g. "bt255t255off=mesh_bri:0xffff:6553.5;") - real
        // configs are far shorter (button slots are 0-3 in practice), but
        // size for the worst case to avoid the exact "undersized reserve()"
        // heap-corruption class fixed earlier on this branch. Bumped again,
        // 1200 -> 1260, for the two small |mesh_net=0/1|mesh_nodes=<n> fields
        // added alongside it - cheap fields, but this reserve() has already
        // bitten us once for being measured too tight, so round up generously
        // rather than compute the exact new total.
        line.reserve(1260);
        lineReserved = true;
    }
    line = F("STATUS|");
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
    line += fmtF(masterBrightness * 100.0f, 1);
    line += F("|lamp=");
    line += (lampEnabled && !lampOffPending) ? F("ON") : F("OFF");
#if ENABLE_BUTTONS
    if (firstPowerToggleButton())
    {
        line += F("|switch=");
        line += switchDebouncedState ? F("ON") : F("OFF");
    }
    else
    {
        line += F("|switch=N/A");
    }
    line += F("|buttons=");
    for (size_t bi = 0; bi < BUTTON_COUNT; ++bi)
    {
        if (bi > 0)
            line += F(",");
        line += BUTTONS[bi].label;
        line += F(":");
        line += (BUTTONS[bi].type == BTN_TOGGLE) ? F("toggle") : F("momentary");
        line += F(":");
        switch (BUTTONS[bi].role)
        {
        case ROLE_POWER:
            line += F("power");
            break;
        case ROLE_MODE_NEXT:
            line += F("modenext");
            break;
        case ROLE_BRIGHTNESS_UP:
            line += F("briup");
            break;
        case ROLE_BRIGHTNESS_DOWN:
            line += F("bridown");
            break;
        }
        line += F(":");
        // Inert today - see CHANNEL_TARGET_ALL in pinout.h.
        line += (BUTTONS[bi].target == CHANNEL_TARGET_ALL) ? F("all") : String(BUTTONS[bi].target);
        line += F(":");
        if (BUTTONS[bi].type == BTN_TOGGLE)
            line += (buttonStates[bi].debounced ? F("on") : F("off"));
        else
            line += (buttonStates[bi].debounced ? F("pressed") : F("released"));
    }
#else
    line += F("|switch=N/A");
#endif
#if ENABLE_TOUCH_DIM
    line += F("|touch_dim=");
    line += touchDimEnabled ? F("1") : F("0");
    line += F("|touch_dim_step=");
    line += fmtF(touchDimStep, 3);
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
    line += fmtF(rampEaseOnPower, 2);
    line += F("|ramp_off_pow=");
    line += fmtF(rampEaseOffPower, 2);
#if ENABLE_LIGHT_SENSOR
    line += F("|ramp_amb=");
    line += fmtF(rampAmbientFactor, 2);
#endif
    line += F("|idle_min=");
    line += idleOffMs == 0 ? F("0") : String(idleOffMs / 60000);
    // Never exposed before this - the frontend had no way to show "wake/sleep
    // fade is currently running" (e.g. to swap a Start button for Stop) and
    // had to guess from its own local click history instead.
    line += F("|wake_active=");
    line += wakeFadeActive ? F("1") : F("0");
    line += F("|sleep_active=");
    line += sleepFadeActive ? F("1") : F("0");
    // Shared by both Notify and Morse (morse reuses the same notify blink engine -
    // see command.cpp's "morse" branch), same reasoning as wake/sleep above.
    line += F("|notify_active=");
    line += notifyActive ? F("1") : F("0");
    line += F("|pat_speed=");
    line += fmtF(patternSpeedScale, 2);
    line += F("|pat_fade=");
    line += patternFadeEnabled ? fmtF(patternFadeStrength, 2) : F("off");
    line += F("|pat_inv=");
    line += patternInvert ? F("1") : F("0");
    line += F("|pat_lo=");
    line += fmtF(patternMarginLow, 3);
    line += F("|pat_hi=");
    line += fmtF(patternMarginHigh, 3);
    line += F("|quick=");
    line += quickMaskToCsv();
    line += F("|gest=");
    line += gestureBindingsToCsv();
#if ENABLE_BLE_MESH_PROVISIONER
    // Cheap glance state for the frontend's Mesh card (has a network been
    // set up yet, roughly how many lamps are in it) - the full node list
    // (names, addresses) is deliberately NOT in this high-frequency status
    // line, same reasoning as |presence_ble_count= vs. the actual device
    // list: fetched on demand via `mesh nodes` instead (see meshProtocol.ts).
    line += F("|mesh_net=");
    line += meshHasNetworkIdentity() ? F("1") : F("0");
    line += F("|mesh_nodes=");
    line += String(meshKnownNodeCount());
#endif
    line += F("|pat_reactive=");
    line += isPatternReactive(currentPattern) ? F("1") : F("0");
    line += F("|presence_ble=");
    line += presenceBleEnabled ? F("ON") : F("OFF");
    line += F("|presence_ble_count=");
    line += (int)presenceBleDevices.size();
    line += F("|presence_ble_thr=");
    line += presenceBleRssiThreshold;
    line += F("|presence_ble_on=");
    line += presenceBleAutoOn ? F("1") : F("0");
    line += F("|presence_ble_off=");
    line += presenceBleAutoOff ? F("1") : F("0");
    line += F("|presence_ble_list=");
    line += presenceBleListCsv();
    line += F("|presence_ble_grace=");
    line += presenceBleGraceMs;
    line += F("|presence_ble_hwoverride=");
    line += presenceBleAlwaysOverride ? F("1") : F("0");
#if ENABLE_RD03
    line += F("|radar=");
    line += radarEnabled ? F("ON") : F("OFF");
    line += F("|radar_present=");
    line += radarPresent ? F("1") : F("0");
    line += F("|radar_dist=");
    line += fmtF(radarDistanceCm, 1);
    line += F("|radar_speed=");
    line += fmtF(radarSpeedCmS, 1);
    line += F("|radar_targets=");
    line += String(radarTargetCount);
    line += F("|radar_dim=");
    line += radarDimEnabled ? F("1") : F("0");
    line += F("|radar_dim_near=");
    line += fmtF(radarDimNearCm, 1);
    line += F("|radar_dim_far=");
    line += fmtF(radarDimFarCm, 1);
    line += F("|radar_motion=");
    line += radarMotionOnEnabled ? F("1") : F("0");
    line += F("|radar_motion_thr=");
    line += fmtF(radarMotionSpeedThreshold, 1);
    line += F("|radar_motion_hold=");
    line += radarMotionHoldMs;
    line += F("|radar_offdist=");
    line += radarOffDistanceEnabled ? F("1") : F("0");
    line += F("|radar_off_cm=");
    line += fmtF(radarOffDistanceCm, 1);
    line += F("|radar_off_grace=");
    line += radarOffGraceMs;
    line += F("|radar_hwoverride=");
    line += radarHwOverride ? F("1") : F("0");
    line += F("|radar_velocity=");
    line += fmtF(radarVelocityFactor, 2);
#else
    line += F("|radar=N/A");
#endif
#if ENABLE_BLE
    line += F("|ble=");
    line += bleActive() ? F("UP") : F("DOWN");
#endif
    line += F("|fw_ver=");
    line += FIRMWARE_VERSION;
    line += F("|fw_env=");
    line += FIRMWARE_ENV;
#if ENABLE_OTA
    line += F("|ota_active=");
    line += otaActive ? F("1") : F("0");
#endif
    sendFeedback(line,force);
    updateBleStatus(line);

    // Detail line: IO, PWM, light, music, inputs - static/reserved-once, see
    // the comment on `line` above. desk_lamp's real STATUS1 measured ~455
    // bytes against this 500 reserve - too close for comfort given how
    // badly undersizing `line` above went unnoticed; bumped with the same
    // margin.
    static String lineIO;
    static bool lineIOReserved = false;
    if (!lineIOReserved)
    {
        lineIO.reserve(700);
        lineIOReserved = true;
    }
    lineIO = F("STATUS1|");
#if ENABLE_EXT_INPUT
    lineIO += F("ext_in=");
    lineIO += extInputEnabled ? F("ON") : F("OFF");
    lineIO += F("|ext_mode=");
    lineIO += extInputAnalog ? F("ana") : F("dig");
    lineIO += F("|ext_alpha=");
    lineIO += fmtF(extInputAlpha, 3);
    lineIO += F("|ext_delta=");
    lineIO += fmtF(extInputDelta, 3);
    lineIO += F("|ext_val=");
    lineIO += extInputFiltered >= 0.0f ? fmtF(extInputFiltered, 3) : F("N/A");
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
    lineIO += fmtF(outputGamma, 2);
    lineIO += F("|pwm_raw=");
    lineIO += String((uint32_t)lastPwmValue);
    lineIO += F("|pwm_max=");
    lineIO += String((uint32_t)PWM_MAX);
    lineIO += F("|bri_min=");
    lineIO += fmtF(briMinUser * 100.0f, 1);
    lineIO += F("|bri_max=");
    lineIO += fmtF(briMaxUser * 100.0f, 1);
    lineIO += F("|notif_min=");
    lineIO += fmtF(notifyMinBrightness * 100.0f, 1);
#if ENABLE_LIGHT_SENSOR
    lineIO += F("|light=");
    lineIO += lightSensorEnabled ? F("ON") : F("OFF");
    lineIO += F("|light_gain=");
    lineIO += fmtF(lightGain, 2);
    lineIO += F("|light_min=");
    lineIO += fmtF(lightClampMin, 2);
    lineIO += F("|light_max=");
    lineIO += fmtF(lightClampMax, 2);
    lineIO += F("|light_alpha=");
    lineIO += fmtF(lightAlpha, 3);
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
    lineIO += fmtF(musicGain, 2);
    lineIO += F("|music_auto=");
    lineIO += musicAutoLamp ? F("ON") : F("OFF");
    lineIO += F("|music_thr=");
    lineIO += fmtF(musicAutoThr, 2);
    lineIO += F("|music_mode=");
    lineIO += (musicMode == 1 ? F("beat") : F("direct"));
    lineIO += F("|music_mod=");
    lineIO += fmtF(musicModScale, 3);
    lineIO += F("|music_kick_ms=");
    lineIO += (musicLastKickMs > 0 ? String(millis() - musicLastKickMs) : F("N/A"));
    lineIO += F("|music_env=");
    lineIO += fmtF(musicFiltered, 3);
    lineIO += F("|music_level=");
    lineIO += fmtF(musicRawLevel, 3);
    lineIO += F("|music_smooth=");
    lineIO += fmtF(musicSmoothing, 2);
    lineIO += F("|clap=");
    lineIO += clapEnabled ? F("ON") : F("OFF");
    lineIO += F("|clap_thr=");
    lineIO += fmtF(clapThreshold, 2);
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
    lineIO += fmtF(potiAlpha, 2);
    lineIO += F("|poti_delta=");
    lineIO += fmtF(potiDeltaMin, 3);
    lineIO += F("|poti_off=");
    lineIO += fmtF(potiOffThreshold, 3);
    lineIO += F("|poti_sample=");
    lineIO += String(potiSampleMs);
    lineIO += F("|poti_min=");
    lineIO += fmtF(potiCalibMin, 3);
    lineIO += F("|poti_max=");
    lineIO += fmtF(potiCalibMax, 3);
    lineIO += F("|poti_inv=");
    lineIO += potiInvert ? F("1") : F("0");
    lineIO += F("|poti_mode=");
    lineIO += (potiRole == POTI_ROLE_MODE_SCAN) ? F("scan") : F("brightness");
    lineIO += F("|poti_target=");
    lineIO += (potiTarget == CHANNEL_TARGET_ALL) ? F("all") : String(potiTarget);
    lineIO += F("|poti_val=");
    lineIO += fmtF(potiFiltered, 3);
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
    lineIO += fmtF(pushStep, 3);
#else
    lineIO += F("|push=N/A");
#endif
    sendFeedback(lineIO,force);
    updateBleStatus(lineIO);

    // Filters/status chunk (separate to stay under BLE MTU) - static/reserved
    // once, see the comment on `line` above. Measured real content at 494
    // bytes against this 450 reserve - the second of two undersized buffers
    // in this function (the first being `line` above); fixing only one and
    // retesting is what caught this.
    static String line2;
    static bool line2Reserved = false;
    if (!line2Reserved)
    {
        line2.reserve(700);
        line2Reserved = true;
    }
    line2 = F("STATUS2|");
    {
        FilterState filt;
        filtersGetState(filt);
        line2 += F("filter_iir=");
        line2 += filt.iirEnabled ? F("ON") : F("OFF");
        line2 += F("|filter_alpha=");
        line2 += fmtF(filt.iirAlpha, 3);
        line2 += F("|filter_clip=");
        line2 += filt.clipEnabled ? F("ON") : F("OFF");
        line2 += F("|filter_clip_amt=");
        line2 += fmtF(filt.clipAmount, 2);
        line2 += F("|filter_clip_curve=");
        line2 += filt.clipCurve;
        line2 += F("|filter_trem=");
        line2 += filt.tremEnabled ? F("ON") : F("OFF");
        line2 += F("|filter_trem_rate=");
        line2 += fmtF(filt.tremRateHz, 2);
        line2 += F("|filter_trem_depth=");
        line2 += fmtF(filt.tremDepth, 2);
        line2 += F("|filter_trem_wave=");
        line2 += filt.tremWave;
        line2 += F("|filter_spark=");
        line2 += filt.sparkEnabled ? F("ON") : F("OFF");
        line2 += F("|filter_spark_dens=");
        line2 += fmtF(filt.sparkDensity, 2);
        line2 += F("|filter_spark_int=");
        line2 += fmtF(filt.sparkIntensity, 2);
        line2 += F("|filter_spark_decay=");
        line2 += String(filt.sparkDecayMs);
        line2 += F("|filter_comp=");
        line2 += filt.compEnabled ? F("ON") : F("OFF");
        line2 += F("|filter_comp_thr=");
        line2 += fmtF(filt.compThr, 2);
        line2 += F("|filter_comp_ratio=");
        line2 += fmtF(filt.compRatio, 2);
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
        line2 += fmtF(filt.delayFeedback, 2);
        line2 += F("|filter_delay_mix=");
        line2 += fmtF(filt.delayMix, 2);
    }
    sendFeedback(line2,force);
    updateBleStatus(line2);

    // Marks a complete structured snapshot. Keep the core state in the marker
    // as well so a BLE client can recover it with a characteristic read when
    // notifications were unavailable or a notification chunk was lost.
    //
    // This used to be guarded by `#if SEND_STATUS_END`, a macro that was never
    // defined anywhere in the project - so this line was dead code in every
    // build, and the frontend's refreshStatus() (which waits specifically for
    // STATUS_END| to know the snapshot is complete) was guaranteed to time out
    // on every single connect. Unconditional now; the frontend hard-depends on
    // this marker, it was never meant to be optional.
    static String lineEnd;
    static bool lineEndReserved = false;
    if (!lineEndReserved)
    {
        // Was reserve(60) - plenty for the ~38 bytes this line actually
        // needs, but root-caused on real hardware (bisecting this exact
        // function line by line under a repeatable crash) as the specific
        // allocation whose small size collided with something the BT
        // controller itself allocates at a similar size, corrupting shared
        // TLSF heap state - not a true overflow. Bumped to match the other
        // reserve()s in this function rather than trying to find the
        // "safe" small size, since that would just move the collision
        // instead of avoiding the size class deterministically.
        lineEnd.reserve(700);
        lineEndReserved = true;
    }
    lineEnd = F("STATUS_END|lamp=");
    lineEnd += (lampEnabled && !lampOffPending) ? F("ON") : F("OFF");
    lineEnd += F("|bri=");
    lineEnd += fmtF(masterBrightness * 100.0f, 1);
    lineEnd += F("|pattern=");
    lineEnd += String(currentPattern + 1);
    sendFeedback(lineEnd, force);
    updateBleStatus(lineEnd);
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
        "  pat reactive on|off|list|clear|<n> on|off - Sensor-Reaktivitäts-Flag pro Pattern (Verhalten folgt separat)",
        "  pwm curve <0.5-4> - PWM-Gamma/Linearität anpassen",
        "  demo [Sek]        - Demo-Modus: Quick-Liste mit fester Verweildauer (Default 6s)",
        "  touch hold <ms>   - Hold-Start 500..5000 ms",
        "  touchdim on/off   - Touch-Dimmen aktivieren/deaktivieren",
        "  clap on|off/thr <0..1>/cool <ms>/train [on|off] - Klatschsteuerung (Audio)",
        "  clap <1|2|3> <cmd> - Befehl bei 1/2/3 Klatschen",
        "  presence_ble on|off   - PresenceBLE aktivieren/deaktivieren",
        "  presence_ble add <addr>/del <addr>/clear - Geräte-Liste verwalten",
        "  presence_ble set <addr> - Liste überschreiben (Kompatibilität)",
        "  presence_ble thr <-dBm> - RSSI-Schwelle (z.B. -75)",
        "  presence_ble auto on|off <on|off> - Auto-Licht AN/OFF Aktionen",
        "  presence_ble grace <ms> - Verzögerung vor Auto-Off",
        "  presence_ble hwoverride on|off - Auto on/off auch gegen Schalter/Poti erzwingen (default off)",
        "  radar on|off       - RD-03/RD-03D Radar aktivieren/deaktivieren",
        "  radar dim on|off/near <cm>/far <cm> - Touchless-Dimming per Distanz",
        "  radar motion on|off/thr <cm/s>/hold <ms> - Auto-ON bei Bewegung",
        "  radar offdist on|off/cm <cm>/grace <ms> - Auto-OFF bei Distanz",
        "  radar debug on|off - Raw-Frame-Hexdump (Protokoll-Bringup)",
        "  radar hwoverride on|off - Motion/Offdist auch gegen Schalter/Poti erzwingen (default off)",
        "  radar velocity <faktor> - Helligkeits-Modifier bei Geschwindigkeit (1=aus, >1 heller, <1 dunkler)",
        "  ota begin <size> <md5> <version> - OTA-Update starten",
        "  ota chunk <base64> - Firmware-Chunk senden",
        "  ota end - Übertragung abschließen (prüft MD5, startet neu bei Erfolg)",
        "  ota abort - laufendes OTA-Update abbrechen",
        "  ota status - aktuelle Version und OTA-Fortschritt",
        "  custom v1,v2,...   - Custom-Pattern setzen (0..1)",
        "  custom step <ms>   - Schrittzeit Custom-Pattern",
        "  notify [on1 off1 on2 off2] - Blinksignal (ms)",
        "  music sens <f>/smooth <0-1>/auto on|off/thr <f> - Musik-Parameter (Patterns Music Direct/Beat)",
        "  morse <text>     - Morse-Blink (dot=200ms, dash=600ms)",
        "  profile save <1-6>/load <1-6> - Szene (Pattern/Helligkeit/Ramp/Speed), keine Kalibrierung",
        "  profile show <1-6> - Slot-Inhalt anzeigen ohne zu laden",
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
