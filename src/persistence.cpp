#include <Arduino.h>
#include <Preferences.h>

#include "persistence.h"
#include "quickmode.h"
#include "pattern.h"
#include "patterns.h"
#include "lamp_state.h"
#include "lightSensor.h"
#include "utils.h"
#include "microphone.h"
#include "notifications.h"
#include "inputs.h"
#include "filters.h"
#include "presence.h"
#include "print.h"

// ---------- Persistenz ----------
Preferences prefs;
static const char *PREF_NS = "lamp";
static const char *PREF_KEY_B1000 = "b1000";
static const char *PREF_KEY_MODE = "mode";
static const char *PREF_KEY_AUTO = "auto";
static const char *PREF_KEY_THR_ON = "thr_on";
static const char *PREF_KEY_THR_OFF = "thr_off";
static const char *PREF_KEY_PRESENCE_EN = "pres_en";
static const char *PREF_KEY_PRESENCE_ADDR = "pres_addr";
static const char *PREF_KEY_PRESENCE_LIST = "pres_list";
static const char *PREF_KEY_RAMP_MS = "ramp_ms";
static const char *PREF_KEY_RAMP_ON_MS = "ramp_on_ms";
static const char *PREF_KEY_RAMP_OFF_MS = "ramp_off_ms";
static const char *PREF_KEY_RAMP_AMB = "ramp_amb";
static const char *PREF_KEY_IDLE_OFF = "idle_off";
static const char *PREF_KEY_LS_EN = "ls_en";
static const char *PREF_KEY_CUSTOM = "cust";
static const char *PREF_KEY_CUSTOM_MS = "cust_ms";
static const char *PREF_KEY_TRUST_BLE = "trust_ble";
static const char *PREF_KEY_TRUST_BT = "trust_bt";
static const char *PREF_KEY_BLE_NAME = "ble_name";
static const char *PREF_KEY_BT_NAME = "bt_name";
static const char *PREF_KEY_BT_SLEEP_BOOT = "bt_sl_boot";
static const char *PREF_KEY_BT_SLEEP_BLE = "bt_sl_ble";
#if ENABLE_MUSIC_MODE
static const char *PREF_KEY_MUSIC_EN = "music_en";
static const char *PREF_KEY_CLAP_EN = "clap_en";
static const char *PREF_KEY_CLAP_THR = "clap_thr";
static const char *PREF_KEY_CLAP_COOL = "clap_cl";
static const char *PREF_KEY_CLAP_CMD1 = "clap_c1";
static const char *PREF_KEY_CLAP_CMD2 = "clap_c2";
static const char *PREF_KEY_CLAP_CMD3 = "clap_c3";
static const char *PREF_KEY_MUSIC_AUTOLAMP = "mus_auto";
static const char *PREF_KEY_MUSIC_AUTOTHR = "mus_thr";
static const char *PREF_KEY_MUSIC_MODE = "mus_mode";
static const char *PREF_KEY_MUSIC_SMOOTH = "mus_sm";
#endif
static const char *PREF_KEY_TOUCH_DIM = "touch_dim";
static const char *PREF_KEY_TOUCH_DIM_STEP = "touch_dim_step";
static const char *PREF_KEY_LIGHT_GAIN = "light_gain";
static const char *PREF_KEY_BRI_MIN = "bri_min";
static const char *PREF_KEY_BRI_MAX = "bri_max";
static const char *PREF_KEY_PRES_GRACE = "pres_grace";
static const char *PREF_KEY_TOUCH_HOLD = "touch_hold";
static const char *PREF_KEY_PAT_SCALE = "pat_scale";
static const char *PREF_KEY_QUICK_MASK = "qmask";
static const char *PREF_KEY_QUICK_MASK_HI = "qmask_hi";
static const char *PREF_KEY_PAT_FADE = "pat_fade";
static const char *PREF_KEY_PAT_FADE_AMT = "pat_fade_amt";
static const char *PREF_KEY_PAT_LO = "pat_lo";
static const char *PREF_KEY_PAT_HI = "pat_hi";
static const char *PREF_KEY_PAT_INV = "pat_inv";
static const char *PREF_KEY_RAMP_EASE_ON = "ramp_e_on";
static const char *PREF_KEY_RAMP_EASE_OFF = "ramp_e_off";
static const char *PREF_KEY_RAMP_POW_ON = "ramp_p_on";
static const char *PREF_KEY_RAMP_POW_OFF = "ramp_p_off";
static const char *PREF_KEY_LCLAMP_MIN = "lcl_min";
static const char *PREF_KEY_LCLAMP_MAX = "lcl_max";
static const char *PREF_KEY_LIGHT_ALPHA = "light_a";
static const char *PREF_KEY_MUSIC_GAIN = "mus_gain";
static const char *PREF_KEY_NOTIFY_MIN = "notif_min";
static const char *PREF_KEY_PRESENCE_RSSI = "pres_rssi";
static const char *PREF_KEY_PRESENCE_AUTO_ON = "pres_auto_on";
static const char *PREF_KEY_PRESENCE_AUTO_OFF = "pres_auto_off";
#if ENABLE_POTI
static const char *PREF_KEY_POTI_EN = "poti_en";
static const char *PREF_KEY_POTI_ALPHA = "poti_a";
static const char *PREF_KEY_POTI_DELTA = "poti_d";
static const char *PREF_KEY_POTI_OFF = "poti_off";
static const char *PREF_KEY_POTI_SAMPLE = "poti_s";
static const char *PREF_KEY_POTI_MIN = "poti_min";
static const char *PREF_KEY_POTI_MAX = "poti_max";
static const char *PREF_KEY_POTI_INV = "poti_inv";
#endif
#if ENABLE_PUSH_BUTTON
static const char *PREF_KEY_PUSH_EN = "push_en";
static const char *PREF_KEY_PUSH_DB = "push_db";
static const char *PREF_KEY_PUSH_DBL = "push_dbl";
static const char *PREF_KEY_PUSH_HOLD = "push_hold";
static const char *PREF_KEY_PUSH_STEP_MS = "push_s_ms";
static const char *PREF_KEY_PUSH_STEP = "push_step";
#endif
// profile slots profile1..profileN
const char *PREF_KEY_PROFILE_BASE = "profile";
const uint8_t PROFILE_SLOTS = 3;
static const char *PREF_KEY_PWM_GAMMA = "pwm_g";
static const char *PREF_KEY_FILTER_IIR_EN = "fil_iir_en";
static const char *PREF_KEY_FILTER_IIR_A = "fil_iir_a";
static const char *PREF_KEY_FILTER_CLIP_EN = "fil_cl_en";
static const char *PREF_KEY_FILTER_CLIP_AMT = "fil_cl_amt";
static const char *PREF_KEY_FILTER_CLIP_CURVE = "fil_cl_cv";
static const char *PREF_KEY_FILTER_TREM_EN = "fil_tr_en";
static const char *PREF_KEY_FILTER_TREM_RATE = "fil_tr_rt";
static const char *PREF_KEY_FILTER_TREM_DEPTH = "fil_tr_dp";
static const char *PREF_KEY_FILTER_TREM_WAVE = "fil_tr_wv";
static const char *PREF_KEY_FILTER_SPARK_EN = "fil_sp_en";
static const char *PREF_KEY_FILTER_SPARK_DENS = "fil_sp_dn";
static const char *PREF_KEY_FILTER_SPARK_INT = "fil_sp_in";
static const char *PREF_KEY_FILTER_SPARK_DECAY = "fil_sp_dc";
static const char *PREF_KEY_FILTER_COMP_EN = "fil_cp_en";
static const char *PREF_KEY_FILTER_COMP_THR = "fil_cp_th";
static const char *PREF_KEY_FILTER_COMP_RATIO = "fil_cp_rt";
static const char *PREF_KEY_FILTER_COMP_ATTACK = "fil_cp_at";
static const char *PREF_KEY_FILTER_COMP_RELEASE = "fil_cp_rl";
static const char *PREF_KEY_FILTER_ENV_EN = "fil_ev_en";
static const char *PREF_KEY_FILTER_ENV_AT = "fil_ev_at";
static const char *PREF_KEY_FILTER_ENV_RL = "fil_ev_rl";
static const char *PREF_KEY_FILTER_DELAY_EN = "fil_dl_en";
static const char *PREF_KEY_FILTER_DELAY_MS = "fil_dl_ms";
static const char *PREF_KEY_FILTER_DELAY_FB = "fil_dl_fb";
static const char *PREF_KEY_FILTER_DELAY_MIX = "fil_dl_mx";

bool parseQuickCsv(const String &csv, uint64_t &outMask)
{
    String tmp = csv;
    tmp.replace(',', ' ');
    outMask = 0;
    size_t total = quickModeCount();
    int start = 0;
    while (start < tmp.length())
    {
        while (start < tmp.length() && isspace(tmp[start]))
            start++;
        if (start >= tmp.length())
            break;
        int end = start;
        while (end < tmp.length() && !isspace(tmp[end]))
            end++;
        String tok = tmp.substring(start, end);
        int idx = tok.toInt();
        if (idx >= 1 && idx <= (int)total && idx <= 64)
            outMask |= (1ULL << (idx - 1));
        start = end + 1;
    }
    return outMask != 0;
}

/**
 * @brief Build a profile string (cfg import style) without presence/touch/quick.
 */
String buildProfileString()
{
    String cfg;
    cfg += F("mode=");
    cfg += String(currentPattern + 1);
    cfg += F(" bri=");
    cfg += String(masterBrightness, 3);
    cfg += F(" auto=");
    cfg += autoCycle ? F("on") : F("off");
    cfg += F(" pat_scale=");
    cfg += String(patternSpeedScale, 2);
    cfg += F(" ramp=");
    cfg += rampDurationMs;
    cfg += F(" ramp_on_ms=");
    cfg += rampOnDurationMs;
    cfg += F(" ramp_off_ms=");
    cfg += rampOffDurationMs;
    cfg += F(" pat_fade=");
    cfg += patternFadeEnabled ? F("on") : F("off");
    cfg += F(" pat_fade_amt=");
    cfg += String(patternFadeStrength, 2);
    cfg += F(" pat_inv=");
    cfg += patternInvert ? F("on") : F("off");
    cfg += F(" pat_lo=");
    cfg += String(patternMarginLow, 3);
    cfg += F(" pat_hi=");
    cfg += String(patternMarginHigh, 3);
    cfg += F(" ramp_on_ease=");
    cfg += easeToString(rampEaseOnType);
    cfg += F(" ramp_off_ease=");
    cfg += easeToString(rampEaseOffType);
    cfg += F(" ramp_on_pow=");
    cfg += String(rampEaseOnPower, 2);
    cfg += F(" ramp_off_pow=");
    cfg += String(rampEaseOffPower, 2);
    cfg += F(" bri_min=");
    cfg += String(briMinUser, 3);
    cfg += F(" bri_max=");
    cfg += String(briMaxUser, 3);
    cfg += F(" pwm_gamma=");
    cfg += String(outputGamma, 2);
#if ENABLE_LIGHT_SENSOR
    cfg += F(" ramp_amb=");
    cfg += String(rampAmbientFactor, 2);
    cfg += F(" light_gain=");
    cfg += String(lightGain, 2);
    cfg += F(" light_min=");
    cfg += String(lightClampMin, 2);
    cfg += F(" light_max=");
    cfg += String(lightClampMax, 2);
    cfg += F(" light_alpha=");
    cfg += String(lightAlpha, 3);
    cfg += F(" light=");
    cfg += lightSensorEnabled ? F("on") : F("off");
#endif
#if ENABLE_MUSIC_MODE
    cfg += F(" music=");
    cfg += musicEnabled ? F("on") : F("off");
    cfg += F(" music_gain=");
    cfg += String(musicGain, 2);
    cfg += F(" clap=");
    cfg += clapEnabled ? F("on") : F("off");
    cfg += F(" clap_thr=");
    cfg += String(clapThreshold, 2);
    cfg += F(" clap_cool=");
    cfg += String(clapCooldownMs);
#endif
    return cfg;
}

/**
 * @brief Default profiles for slots 1..3 (used if slot is empty).
 */
String defaultProfileString(uint8_t slot)
{
    String cfg;
    switch (slot)
    {
    case 1: // A: full brightness, constant
        cfg = F("mode=1 bri=1.0 auto=off pat_scale=1 pat_fade=on pat_fade_amt=0.01 pat_inv=off pat_lo=0 pat_hi=1 ramp_on_ease=ease-out ramp_off_ease=ease-out ramp_on_pow=7 ramp_off_pow=2 ramp_on_ms=320 ramp_off_ms=600 ramp_amb=0 bri_min=0.05 bri_max=0.95");
        break;
    case 2: // B: half brightness, constant
        cfg = F("mode=1 bri=0.5 auto=off pat_scale=1 pat_fade=on pat_fade_amt=0.01 pat_inv=off pat_lo=0 pat_hi=1 ramp_on_ease=ease-out ramp_off_ease=ease-out ramp_on_pow=7 ramp_off_pow=2 ramp_on_ms=320 ramp_off_ms=600 ramp_amb=0 bri_min=0.05 bri_max=0.95");
        break;
    case 3: // C: half brightness, pulsierend
        cfg = F("mode=5 bri=0.5 auto=off pat_scale=1 pat_fade=on pat_fade_amt=0.01 pat_inv=off pat_lo=0 pat_hi=1 ramp_on_ease=ease-out ramp_off_ease=ease-out ramp_on_pow=7 ramp_off_pow=2 ramp_on_ms=320 ramp_off_ms=600 ramp_amb=0 bri_min=0.05 bri_max=0.95");
        break;
    default:
        break;
    }
    return cfg;
}


/**
 * @brief Apply a profile string (same keys as buildProfileString).
 */
void applyProfileString(const String &cfg)
{
    // reuse import parser but limited keys
    importConfig(cfg);
    // set pattern if present
    int mpos = cfg.indexOf("mode=");
    if (mpos >= 0)
    {
        int end = cfg.indexOf(' ', mpos);
        String v = (end >= 0) ? cfg.substring(mpos + 5, end) : cfg.substring(mpos + 5);
        int idx = v.toInt();
        if (idx >= 1 && (size_t)idx <= PATTERN_COUNT)
            setPattern((size_t)idx - 1, false, true);
    }
    saveSettings();
}

bool loadProfileSlot(uint8_t slot, bool announce)
{
    if (slot < 1 || slot > PROFILE_SLOTS)
        return false;
    String key = String(PREF_KEY_PROFILE_BASE) + String(slot);
    String cfg = prefs.getString(key.c_str(), "");
    if (cfg.length() == 0)
    {
        cfg = defaultProfileString(slot);
        if (cfg.length() > 0)
            prefs.putString(key.c_str(), cfg);
    }
    if (cfg.length() == 0)
    {
        if (announce)
            sendFeedback(F("[Profile] Slot empty"));
        return false;
    }
    applyProfileString(cfg);
    currentModeIndex = PATTERN_COUNT + (slot - 1);
    if (announce)
    {
        sendFeedback(String(F("[Profile] Loaded slot ")) + String(slot));
        printStatus();
    }
    return true;
}

void exportConfig()
{
    String cfg = F("cfg import ");
    cfg += F("bri=");
    cfg += String(masterBrightness, 3);
    cfg += F(" auto=");
    cfg += autoCycle ? F("on") : F("off");
    cfg += F(" pat_scale=");
    cfg += String(patternSpeedScale, 2);
#if ENABLE_TOUCH_DIM
    cfg += F(" touch_on=");
    cfg += touchDeltaOn;
    cfg += F(" touch_off=");
    cfg += touchDeltaOff;
#endif
    cfg += F(" ramp=");
    cfg += rampDurationMs;
    cfg += F(" idle=");
    cfg += idleOffMs / 60000;
    cfg += F(" presence_en=");
    cfg += presenceEnabled ? F("on") : F("off");
    cfg += F(" presence_addr=");
    cfg += presenceAddr;
    cfg += F(" presence_list=");
    cfg += presenceListCsv();
    cfg += F(" presence_thr=");
    cfg += presenceRssiThreshold;
    cfg += F(" presence_on=");
    cfg += presenceAutoOn ? F("on") : F("off");
    cfg += F(" presence_off=");
    cfg += presenceAutoOff ? F("on") : F("off");
#if ENABLE_TOUCH_DIM
    cfg += F(" touch_dim=");
    cfg += touchDimEnabled ? F("on") : F("off");
    cfg += F(" touch_hold=");
    cfg += touchHoldStartMs;
    cfg += F(" touch_dim_step=");
    cfg += String(touchDimStep, 3);
#endif
    FilterState filt;
    filtersGetState(filt);
    cfg += F(" filter_iir=");
    cfg += filt.iirEnabled ? F("on") : F("off");
    cfg += F(" filter_iir_a=");
    cfg += String(filt.iirAlpha, 3);
    cfg += F(" filter_clip=");
    cfg += filt.clipEnabled ? F("on") : F("off");
    cfg += F(" filter_clip_amt=");
    cfg += String(filt.clipAmount, 2);
    cfg += F(" filter_clip_curve=");
    cfg += filt.clipCurve;
    cfg += F(" filter_trem=");
    cfg += filt.tremEnabled ? F("on") : F("off");
    cfg += F(" filter_trem_rate=");
    cfg += String(filt.tremRateHz, 2);
    cfg += F(" filter_trem_depth=");
    cfg += String(filt.tremDepth, 2);
    cfg += F(" filter_trem_wave=");
    cfg += filt.tremWave;
    cfg += F(" filter_spark=");
    cfg += filt.sparkEnabled ? F("on") : F("off");
    cfg += F(" filter_spark_dens=");
    cfg += String(filt.sparkDensity, 2);
    cfg += F(" filter_spark_int=");
    cfg += String(filt.sparkIntensity, 2);
    cfg += F(" filter_spark_decay=");
    cfg += String(filt.sparkDecayMs);
    cfg += F(" light_gain=");
    cfg += String(lightGain, 2);
    cfg += F(" light_min=");
    cfg += String(lightClampMin, 2);
    cfg += F(" light_max=");
    cfg += String(lightClampMax, 2);
    cfg += F(" bri_min=");
    cfg += String(briMinUser, 3);
    cfg += F(" bri_max=");
    cfg += String(briMaxUser, 3);
    cfg += F(" pres_grace=");
    cfg += presenceGraceMs;
    cfg += F(" pat_fade=");
    cfg += patternFadeEnabled ? F("on") : F("off");
    cfg += F(" pat_fade_amt=");
    cfg += String(patternFadeStrength, 2);
    cfg += F(" pat_inv=");
    cfg += patternInvert ? F("on") : F("off");
    cfg += F(" pat_lo=");
    cfg += String(patternMarginLow, 3);
    cfg += F(" pat_hi=");
    cfg += String(patternMarginHigh, 3);
    cfg += F(" ramp_on_ease=");
    cfg += easeToString(rampEaseOnType);
    cfg += F(" ramp_off_ease=");
    cfg += easeToString(rampEaseOffType);
    cfg += F(" ramp_on_pow=");
    cfg += String(rampEaseOnPower, 2);
    cfg += F(" ramp_off_pow=");
    cfg += String(rampEaseOffPower, 2);
    cfg += F(" quick=");
    cfg += quickMaskToCsv();
#if ENABLE_MUSIC_MODE
    cfg += F(" music_gain=");
    cfg += String(musicGain, 2);
#endif
    if (notifyActive)
        cfg += F(" notify=active");
    sendFeedback(cfg);
}

/**
 * @brief Persist current brightness, pattern and auto-cycle flag in NVS.
 */
void saveSettings()
{
    uint16_t b = (uint16_t)(clamp01(masterBrightness) * 1000.0f + 0.5f);
    prefs.putUShort(PREF_KEY_B1000, b);
    prefs.putUShort(PREF_KEY_MODE, (uint16_t)currentPattern);
    prefs.putBool(PREF_KEY_AUTO, autoCycle);
    prefs.putFloat(PREF_KEY_PAT_SCALE, patternSpeedScale);
#if ENABLE_TOUCH_DIM
    prefs.putShort(PREF_KEY_THR_ON, (int16_t)touchDeltaOn);
    prefs.putShort(PREF_KEY_THR_OFF, (int16_t)touchDeltaOff);
    prefs.putUInt(PREF_KEY_TOUCH_HOLD, touchHoldStartMs);
#endif
    prefs.putBool(PREF_KEY_PRESENCE_EN, presenceEnabled);
    prefs.putString(PREF_KEY_PRESENCE_ADDR, presenceAddr);
    prefs.putString(PREF_KEY_PRESENCE_LIST, presenceListCsv());
    prefs.putInt(PREF_KEY_PRESENCE_RSSI, presenceRssiThreshold);
    prefs.putBool(PREF_KEY_PRESENCE_AUTO_ON, presenceAutoOn);
    prefs.putBool(PREF_KEY_PRESENCE_AUTO_OFF, presenceAutoOff);
    prefs.putString(PREF_KEY_TRUST_BLE, trustGetBleCsv());
    prefs.putString(PREF_KEY_TRUST_BT, trustGetBtCsv());
    prefs.putUInt(PREF_KEY_RAMP_MS, rampDurationMs);
    prefs.putUInt(PREF_KEY_RAMP_ON_MS, rampOnDurationMs);
    prefs.putUInt(PREF_KEY_RAMP_OFF_MS, rampOffDurationMs);
    prefs.putUInt(PREF_KEY_IDLE_OFF, idleOffMs);
    prefs.putUChar(PREF_KEY_RAMP_EASE_ON, rampEaseOnType);
    prefs.putUChar(PREF_KEY_RAMP_EASE_OFF, rampEaseOffType);
    prefs.putFloat(PREF_KEY_RAMP_POW_ON, rampEaseOnPower);
    prefs.putFloat(PREF_KEY_RAMP_POW_OFF, rampEaseOffPower);
    prefs.putFloat(PREF_KEY_PAT_LO, patternMarginLow);
    prefs.putFloat(PREF_KEY_PAT_HI, patternMarginHigh);
    prefs.putBool(PREF_KEY_PAT_INV, patternInvert);
    prefs.putFloat(PREF_KEY_NOTIFY_MIN, notifyMinBrightness);
#if ENABLE_BT_SERIAL
    prefs.putUInt(PREF_KEY_BT_SLEEP_BOOT, getBtSleepAfterBootMs());
    prefs.putUInt(PREF_KEY_BT_SLEEP_BLE, getBtSleepAfterBleMs());
#endif
#if ENABLE_LIGHT_SENSOR
    prefs.putFloat(PREF_KEY_RAMP_AMB, rampAmbientFactor);
    prefs.putBool(PREF_KEY_LS_EN, lightSensorEnabled);
    lightMinRaw = 4095;
    lightMaxRaw = 0;
    prefs.putFloat(PREF_KEY_LCLAMP_MIN, lightClampMin);
    prefs.putFloat(PREF_KEY_LCLAMP_MAX, lightClampMax);
    prefs.putFloat(PREF_KEY_LIGHT_ALPHA, lightAlpha);
#endif
#if ENABLE_EXT_INPUT
    prefs.putBool("ext_en", extInputEnabled);
    prefs.putBool("ext_mode", extInputAnalog);
    prefs.putFloat("ext_alpha", extInputAlpha);
    prefs.putFloat("ext_delta", extInputDelta);
#endif
    prefs.putUInt(PREF_KEY_CUSTOM_MS, customStepMs);
    prefs.putBytes(PREF_KEY_CUSTOM, customPattern, sizeof(float) * customLen);
#if ENABLE_MUSIC_MODE
    prefs.putBool(PREF_KEY_MUSIC_EN, musicEnabled);
    prefs.putFloat(PREF_KEY_MUSIC_GAIN, musicGain);
    prefs.putFloat(PREF_KEY_MUSIC_SMOOTH, musicSmoothing);
    prefs.putBool(PREF_KEY_MUSIC_AUTOLAMP, musicAutoLamp);
    prefs.putFloat(PREF_KEY_MUSIC_AUTOTHR, musicAutoThr);
    prefs.putUChar(PREF_KEY_MUSIC_MODE, musicMode);
    prefs.putBool(PREF_KEY_CLAP_EN, clapEnabled);
    prefs.putFloat(PREF_KEY_CLAP_THR, clapThreshold);
    prefs.putUInt(PREF_KEY_CLAP_COOL, clapCooldownMs);
    prefs.putString(PREF_KEY_CLAP_CMD1, clapCmd1);
    prefs.putString(PREF_KEY_CLAP_CMD2, clapCmd2);
    prefs.putString(PREF_KEY_CLAP_CMD3, clapCmd3);
#endif
#if ENABLE_POTI
    prefs.putBool(PREF_KEY_POTI_EN, potiEnabled);
    prefs.putFloat(PREF_KEY_POTI_ALPHA, potiAlpha);
    prefs.putFloat(PREF_KEY_POTI_DELTA, potiDeltaMin);
    prefs.putFloat(PREF_KEY_POTI_OFF, potiOffThreshold);
    prefs.putUInt(PREF_KEY_POTI_SAMPLE, potiSampleMs);
    prefs.putFloat(PREF_KEY_POTI_MIN, potiCalibMin);
    prefs.putFloat(PREF_KEY_POTI_MAX, potiCalibMax);
    prefs.putBool(PREF_KEY_POTI_INV, potiInvert);
#endif
#if ENABLE_PUSH_BUTTON
    prefs.putBool(PREF_KEY_PUSH_EN, pushEnabled);
    prefs.putUInt(PREF_KEY_PUSH_DB, pushDebounceMs);
    prefs.putUInt(PREF_KEY_PUSH_DBL, pushDoubleMs);
    prefs.putUInt(PREF_KEY_PUSH_HOLD, pushHoldMs);
    prefs.putUInt(PREF_KEY_PUSH_STEP_MS, pushStepMs);
    prefs.putFloat(PREF_KEY_PUSH_STEP, pushStep);
#endif
#if ENABLE_TOUCH_DIM
    prefs.putBool(PREF_KEY_TOUCH_DIM, touchDimEnabled);
    prefs.putFloat(PREF_KEY_TOUCH_DIM_STEP, touchDimStep);
#endif
    prefs.putFloat(PREF_KEY_LIGHT_GAIN, lightGain);
    prefs.putFloat(PREF_KEY_BRI_MIN, briMinUser);
    prefs.putFloat(PREF_KEY_BRI_MAX, briMaxUser);
    prefs.putUInt(PREF_KEY_PRES_GRACE, presenceGraceMs);
    prefs.putBool(PREF_KEY_PAT_FADE, patternFadeEnabled);
    prefs.putFloat(PREF_KEY_PAT_FADE_AMT, patternFadeStrength);
    prefs.putUInt(PREF_KEY_QUICK_MASK, (uint32_t)(quickMask & 0xFFFFFFFFULL));
    prefs.putUInt(PREF_KEY_QUICK_MASK_HI, (uint32_t)(quickMask >> 32));
    prefs.putFloat(PREF_KEY_PWM_GAMMA, outputGamma);
    lastLoggedBrightness = masterBrightness;

    // Filters
    FilterState filt;
    filtersGetState(filt);
    prefs.putBool(PREF_KEY_FILTER_IIR_EN, filt.iirEnabled);
    prefs.putFloat(PREF_KEY_FILTER_IIR_A, filt.iirAlpha);
    prefs.putBool(PREF_KEY_FILTER_CLIP_EN, filt.clipEnabled);
    prefs.putFloat(PREF_KEY_FILTER_CLIP_AMT, filt.clipAmount);
    prefs.putUChar(PREF_KEY_FILTER_CLIP_CURVE, filt.clipCurve);
    prefs.putBool(PREF_KEY_FILTER_TREM_EN, filt.tremEnabled);
    prefs.putFloat(PREF_KEY_FILTER_TREM_RATE, filt.tremRateHz);
    prefs.putFloat(PREF_KEY_FILTER_TREM_DEPTH, filt.tremDepth);
    prefs.putUChar(PREF_KEY_FILTER_TREM_WAVE, filt.tremWave);
    prefs.putBool(PREF_KEY_FILTER_SPARK_EN, filt.sparkEnabled);
    prefs.putFloat(PREF_KEY_FILTER_SPARK_DENS, filt.sparkDensity);
    prefs.putFloat(PREF_KEY_FILTER_SPARK_INT, filt.sparkIntensity);
    prefs.putUInt(PREF_KEY_FILTER_SPARK_DECAY, filt.sparkDecayMs);
    prefs.putBool(PREF_KEY_FILTER_COMP_EN, filt.compEnabled);
    prefs.putFloat(PREF_KEY_FILTER_COMP_THR, filt.compThr);
    prefs.putFloat(PREF_KEY_FILTER_COMP_RATIO, filt.compRatio);
    prefs.putUInt(PREF_KEY_FILTER_COMP_ATTACK, filt.compAttackMs);
    prefs.putUInt(PREF_KEY_FILTER_COMP_RELEASE, filt.compReleaseMs);
    prefs.putBool(PREF_KEY_FILTER_ENV_EN, filt.envEnabled);
    prefs.putUInt(PREF_KEY_FILTER_ENV_AT, filt.envAttackMs);
    prefs.putUInt(PREF_KEY_FILTER_ENV_RL, filt.envReleaseMs);
    prefs.putBool(PREF_KEY_FILTER_DELAY_EN, filt.delayEnabled);
    prefs.putUInt(PREF_KEY_FILTER_DELAY_MS, filt.delayMs);
    prefs.putFloat(PREF_KEY_FILTER_DELAY_FB, filt.delayFeedback);
    prefs.putFloat(PREF_KEY_FILTER_DELAY_MIX, filt.delayMix);
}

void applyDefaultSettings(float brightnessOverride, bool announce)
{
    prefs.begin(PREF_NS, false);
    prefs.clear();
    prefs.end();

    masterBrightness = (brightnessOverride >= 0.0f) ? clamp01(brightnessOverride) : Settings::DEFAULT_BRIGHTNESS;
    autoCycle = Settings::DEFAULT_AUTOCYCLE;
    patternSpeedScale = 1.0f;
#if ENABLE_TOUCH_DIM
    touchDeltaOn = TOUCH_DELTA_ON_DEFAULT;
    touchDeltaOff = TOUCH_DELTA_OFF_DEFAULT;
    touchDimEnabled = Settings::TOUCH_DIM_DEFAULT_ENABLED;
    touchHoldStartMs = Settings::TOUCH_HOLD_MS_DEFAULT;
    touchDimStep = Settings::TOUCH_DIM_STEP_DEFAULT;
#endif
    quickMask = computeDefaultQuickMask();
    presenceEnabled = Settings::PRESENCE_DEFAULT_ENABLED;
    presenceGraceMs = Settings::PRESENCE_GRACE_MS_DEFAULT;
    presenceAddr = "";
    presenceClearDevices();
    presenceRssiThreshold = Settings::PRESENCE_RSSI_THRESHOLD_DEFAULT;
    presenceAutoOn = Settings::PRESENCE_AUTO_ON_DEFAULT;
    presenceAutoOff = Settings::PRESENCE_AUTO_OFF_DEFAULT;
    presenceLastOffByPresence = false;
    rampDurationMs = Settings::DEFAULT_RAMP_MS;
    idleOffMs = Settings::DEFAULT_IDLE_OFF_MS;
    rampEaseOnType = Settings::DEFAULT_RAMP_EASE_ON;
    rampEaseOffType = Settings::DEFAULT_RAMP_EASE_OFF;
    rampEaseOnPower = Settings::DEFAULT_RAMP_POW_ON;
    rampEaseOffPower = Settings::DEFAULT_RAMP_POW_OFF;
    rampOnDurationMs = Settings::DEFAULT_RAMP_ON_MS;
    rampOffDurationMs = Settings::DEFAULT_RAMP_OFF_MS;
    briMinUser = Settings::BRI_MIN_DEFAULT;
    briMaxUser = Settings::BRI_MAX_DEFAULT;
    customLen = 0;
    customStepMs = Settings::CUSTOM_STEP_MS_DEFAULT;
#if ENABLE_POTI
    potiEnabled = true;
    potiAlpha = Settings::POTI_ALPHA;
    potiDeltaMin = Settings::POTI_DELTA_MIN;
    potiOffThreshold = Settings::POTI_OFF_THRESHOLD;
    potiSampleMs = Settings::POTI_SAMPLE_MS;
    potiCalibMin = Settings::POTI_MIN_DEFAULT;
    potiCalibMax = Settings::POTI_MAX_DEFAULT;
    potiInvert = Settings::POTI_INVERT_DEFAULT;
#endif
#if ENABLE_LIGHT_SENSOR
    rampAmbientFactor = Settings::RAMP_AMBIENT_FACTOR_DEFAULT;
    lightSensorEnabled = Settings::LIGHT_SENSOR_DEFAULT_ENABLED;
    lightGain = Settings::LIGHT_GAIN_DEFAULT;
    lightClampMin = Settings::LIGHT_CLAMP_MIN_DEFAULT;
    lightClampMax = Settings::LIGHT_CLAMP_MAX_DEFAULT;
    lightAlpha = Settings::LIGHT_ALPHA;
    lightMinRaw = 4095;
    lightMaxRaw = 0;
#endif
#if ENABLE_MUSIC_MODE
    musicEnabled = Settings::MUSIC_DEFAULT_ENABLED;
    musicGain = Settings::MUSIC_GAIN_DEFAULT;
    musicSmoothing = 0.4f;
    musicAutoLamp = false;
    musicAutoThr = 0.4f;
    musicMode = 0;
    clapEnabled = Settings::CLAP_DEFAULT_ENABLED;
    clapThreshold = Settings::CLAP_THRESHOLD_DEFAULT;
    clapCooldownMs = Settings::CLAP_COOLDOWN_MS_DEFAULT;
    clapCmd1 = "";
    clapCmd2 = "";
    clapCmd3 = "";
#endif
    patternFadeEnabled = false;
    patternFadeStrength = 1.0f;
    patternFilteredLevel = 0.0f;
    patternInvert = Settings::PATTERN_INVERT_DEFAULT;
    patternMarginLow = Settings::PATTERN_MARGIN_LOW_DEFAULT;
    patternMarginHigh = Settings::PATTERN_MARGIN_HIGH_DEFAULT;
    notifyMinBrightness = Settings::NOTIFY_MIN_BRI_DEFAULT;
#if ENABLE_EXT_INPUT
    extInputEnabled = false;
    extInputAnalog = Settings::EXT_INPUT_ANALOG_DEFAULT;
    extInputAlpha = Settings::EXT_INPUT_ALPHA;
    extInputDelta = Settings::EXT_INPUT_DELTA;
    extInputFiltered = -1.0f;
    extInputLastApplied = -1.0f;
    extInputLastDigital = Settings::EXT_INPUT_ACTIVE_LOW;
#endif
    trustSetLists("", "");
    setBleName(Settings::BLE_NAME_DEFAULT);
    setBtName(Settings::BT_NAME_DEFAULT);
    filtersInit();
    saveSettings();
    if (brightnessOverride >= 0.0f)
    {
        setLampEnabled(true, "secure-default");
    }
    if (announce)
        sendFeedback(F("[Defaults] Settings reset to factory values"));
}


/**
 * @brief Restore persisted brightness/pattern settings from NVS.
 */
void loadSettings()
{
    prefs.begin(PREF_NS, false);
    trustSetLists(prefs.getString(PREF_KEY_TRUST_BLE, ""), prefs.getString(PREF_KEY_TRUST_BT, ""));
    uint16_t b = prefs.getUShort(PREF_KEY_B1000, (uint16_t)(Settings::DEFAULT_BRIGHTNESS * 1000.0f));
    masterBrightness = clamp01(b / 1000.0f);
    uint16_t idx = prefs.getUShort(PREF_KEY_MODE, 0);
    if (idx >= PATTERN_COUNT)
        idx = 0;
    currentPattern = idx;
    if (strcmp(PATTERNS[currentPattern].name, "SOS") == 0)
        currentPattern = 0;
    autoCycle = prefs.getBool(PREF_KEY_AUTO, Settings::DEFAULT_AUTOCYCLE);
    patternSpeedScale = prefs.getFloat(PREF_KEY_PAT_SCALE, 1.0f);
    if (patternSpeedScale < 0.1f)
        patternSpeedScale = 0.1f;
    else if (patternSpeedScale > 5.0f)
        patternSpeedScale = 5.0f;
    patternMarginLow = prefs.getFloat(PREF_KEY_PAT_LO, Settings::PATTERN_MARGIN_LOW_DEFAULT);
    patternMarginHigh = prefs.getFloat(PREF_KEY_PAT_HI, Settings::PATTERN_MARGIN_HIGH_DEFAULT);
    if (patternMarginLow < 0.0f)
        patternMarginLow = 0.0f;
    if (patternMarginHigh > 1.0f)
        patternMarginHigh = 1.0f;
    if (patternMarginHigh < patternMarginLow)
        patternMarginHigh = patternMarginLow;
    patternInvert = prefs.getBool(PREF_KEY_PAT_INV, Settings::PATTERN_INVERT_DEFAULT);
#if ENABLE_TOUCH_DIM
    touchDeltaOn = prefs.getShort(PREF_KEY_THR_ON, TOUCH_DELTA_ON_DEFAULT);
    touchDeltaOff = prefs.getShort(PREF_KEY_THR_OFF, TOUCH_DELTA_OFF_DEFAULT);
    if (touchDeltaOn < 1)
        touchDeltaOn = TOUCH_DELTA_ON_DEFAULT;
    if (touchDeltaOff < 1 || touchDeltaOff >= touchDeltaOn)
        touchDeltaOff = TOUCH_DELTA_OFF_DEFAULT;
    touchDimEnabled = prefs.getBool(PREF_KEY_TOUCH_DIM, Settings::TOUCH_DIM_DEFAULT_ENABLED);
    touchHoldStartMs = prefs.getUInt(PREF_KEY_TOUCH_HOLD, Settings::TOUCH_HOLD_MS_DEFAULT);
    if (touchHoldStartMs < 500)
        touchHoldStartMs = 500;
    else if (touchHoldStartMs > 5000)
        touchHoldStartMs = 5000;
    touchDimStep = prefs.getFloat(PREF_KEY_TOUCH_DIM_STEP, Settings::TOUCH_DIM_STEP_DEFAULT);
    if (touchDimStep < 0.001f)
        touchDimStep = 0.001f;
    if (touchDimStep > 0.05f)
        touchDimStep = 0.05f;
#endif
    {
        uint64_t lo = prefs.getUInt(PREF_KEY_QUICK_MASK, (uint32_t)(computeDefaultQuickMask() & 0xFFFFFFFFULL));
        uint64_t hi = prefs.getUInt(PREF_KEY_QUICK_MASK_HI, 0);
        quickMask = (hi << 32) | lo;
    }
    sanitizeQuickMask();
    patternFadeEnabled = prefs.getBool(PREF_KEY_PAT_FADE, false);
    patternFadeStrength = prefs.getFloat(PREF_KEY_PAT_FADE_AMT, 1.0f);
    if (patternFadeStrength < 0.01f)
        patternFadeStrength = 0.01f;
    if (patternFadeStrength > 10.0f)
        patternFadeStrength = 10.0f;
#if ENABLE_BT_SERIAL
    setBtSleepAfterBootMs(prefs.getUInt(PREF_KEY_BT_SLEEP_BOOT, Settings::BT_SLEEP_AFTER_BOOT_MS));
    setBtSleepAfterBleMs(prefs.getUInt(PREF_KEY_BT_SLEEP_BLE, Settings::BT_SLEEP_AFTER_BLE_MS));
#endif
#if ENABLE_LIGHT_SENSOR
    rampAmbientFactor = prefs.getFloat(PREF_KEY_RAMP_AMB, Settings::RAMP_AMBIENT_FACTOR_DEFAULT);
    if (rampAmbientFactor < 0.0f)
        rampAmbientFactor = 0.0f;
    if (rampAmbientFactor > 3.0f)
        rampAmbientFactor = 3.0f;
#endif
#if ENABLE_EXT_INPUT
    extInputEnabled = prefs.getBool("ext_en", false);
    extInputAnalog = prefs.getBool("ext_mode", Settings::EXT_INPUT_ANALOG_DEFAULT);
    extInputAlpha = prefs.getFloat("ext_alpha", Settings::EXT_INPUT_ALPHA);
    extInputDelta = prefs.getFloat("ext_delta", Settings::EXT_INPUT_DELTA);
    extInputFiltered = -1.0f;
    extInputLastApplied = -1.0f;
    extInputLastDigital = Settings::EXT_INPUT_ACTIVE_LOW;
#endif
    patternMarginLow = prefs.getFloat(PREF_KEY_PAT_LO, Settings::PATTERN_MARGIN_LOW_DEFAULT);
    patternMarginHigh = prefs.getFloat(PREF_KEY_PAT_HI, Settings::PATTERN_MARGIN_HIGH_DEFAULT);
    if (patternMarginLow < 0.0f)
        patternMarginLow = 0.0f;
    if (patternMarginHigh > 1.0f)
        patternMarginHigh = 1.0f;
    if (patternMarginHigh < patternMarginLow)
        patternMarginHigh = patternMarginLow;
    notifyMinBrightness = prefs.getFloat(PREF_KEY_NOTIFY_MIN, Settings::NOTIFY_MIN_BRI_DEFAULT);
    if (notifyMinBrightness < 0.0f)
        notifyMinBrightness = 0.0f;
    if (notifyMinBrightness > 1.0f)
        notifyMinBrightness = 1.0f;
    presenceEnabled = prefs.getBool(PREF_KEY_PRESENCE_EN, Settings::PRESENCE_DEFAULT_ENABLED);
    presenceAddr = prefs.getString(PREF_KEY_PRESENCE_ADDR, "");
    presenceClearDevices();
    {
        String list = prefs.getString(PREF_KEY_PRESENCE_LIST, presenceAddr);
        list.trim();
        int start = 0;
        while (start < list.length())
        {
            int comma = list.indexOf(',', start);
            if (comma < 0)
                comma = list.length();
            String tok = list.substring(start, comma);
            tok.trim();
            if (tok.length() > 0)
                presenceAddDevice(tok);
            start = comma + 1;
        }
    }
    presenceRssiThreshold = prefs.getInt(PREF_KEY_PRESENCE_RSSI, Settings::PRESENCE_RSSI_THRESHOLD_DEFAULT);
    presenceAutoOn = prefs.getBool(PREF_KEY_PRESENCE_AUTO_ON, Settings::PRESENCE_AUTO_ON_DEFAULT);
    presenceAutoOff = prefs.getBool(PREF_KEY_PRESENCE_AUTO_OFF, Settings::PRESENCE_AUTO_OFF_DEFAULT);
    rampDurationMs = prefs.getUInt(PREF_KEY_RAMP_MS, Settings::DEFAULT_RAMP_MS);
    if (rampDurationMs < 50)
        rampDurationMs = Settings::DEFAULT_RAMP_MS;
    rampOnDurationMs = prefs.getUInt(PREF_KEY_RAMP_ON_MS, Settings::DEFAULT_RAMP_ON_MS);
    rampOffDurationMs = prefs.getUInt(PREF_KEY_RAMP_OFF_MS, Settings::DEFAULT_RAMP_OFF_MS);
#if ENABLE_LIGHT_SENSOR
    rampAmbientFactor = prefs.getFloat(PREF_KEY_RAMP_AMB, Settings::RAMP_AMBIENT_FACTOR_DEFAULT);
#endif
    if (rampOnDurationMs < 50)
        rampOnDurationMs = Settings::DEFAULT_RAMP_ON_MS;
    if (rampOffDurationMs < 50)
        rampOffDurationMs = Settings::DEFAULT_RAMP_OFF_MS;
#if ENABLE_LIGHT_SENSOR
    if (rampAmbientFactor < 0.0f)
        rampAmbientFactor = 0.0f;
    if (rampAmbientFactor > 5.0f)
        rampAmbientFactor = 5.0f;
#endif
    idleOffMs = prefs.getUInt(PREF_KEY_IDLE_OFF, Settings::DEFAULT_IDLE_OFF_MS);
    rampEaseOnType = prefs.getUChar(PREF_KEY_RAMP_EASE_ON, Settings::DEFAULT_RAMP_EASE_ON);
    rampEaseOffType = prefs.getUChar(PREF_KEY_RAMP_EASE_OFF, Settings::DEFAULT_RAMP_EASE_OFF);
    rampEaseOnPower = prefs.getFloat(PREF_KEY_RAMP_POW_ON, Settings::DEFAULT_RAMP_POW_ON);
    rampEaseOffPower = prefs.getFloat(PREF_KEY_RAMP_POW_OFF, Settings::DEFAULT_RAMP_POW_OFF);
    if (rampEaseOnType > 7)
        rampEaseOnType = Settings::DEFAULT_RAMP_EASE_ON;
    if (rampEaseOffType > 7)
        rampEaseOffType = Settings::DEFAULT_RAMP_EASE_OFF;
    if (rampEaseOnPower < 0.01f)
        rampEaseOnPower = Settings::DEFAULT_RAMP_POW_ON;
    if (rampEaseOffPower < 0.01f)
        rampEaseOffPower = Settings::DEFAULT_RAMP_POW_OFF;
    if (rampEaseOnPower > 10.0f)
        rampEaseOnPower = 10.0f;
    if (rampEaseOffPower > 10.0f)
        rampEaseOffPower = 10.0f;
#if ENABLE_LIGHT_SENSOR
    lightSensorEnabled = prefs.getBool(PREF_KEY_LS_EN, Settings::LIGHT_SENSOR_DEFAULT_ENABLED);
#endif
    filtersInit();
    bool iirEn = prefs.getBool(PREF_KEY_FILTER_IIR_EN, Settings::FILTER_IIR_DEFAULT);
    float iirA = prefs.getFloat(PREF_KEY_FILTER_IIR_A, Settings::FILTER_IIR_ALPHA_DEFAULT);
    filtersSetIir(iirEn, iirA);
    bool clipEn = prefs.getBool(PREF_KEY_FILTER_CLIP_EN, Settings::FILTER_CLIP_DEFAULT);
    float clipAmt = prefs.getFloat(PREF_KEY_FILTER_CLIP_AMT, Settings::FILTER_CLIP_AMT_DEFAULT);
    uint8_t clipCurve = prefs.getUChar(PREF_KEY_FILTER_CLIP_CURVE, Settings::FILTER_CLIP_CURVE_DEFAULT);
    filtersSetClip(clipEn, clipAmt, clipCurve);
    bool tremEn = prefs.getBool(PREF_KEY_FILTER_TREM_EN, Settings::FILTER_TREM_DEFAULT);
    float tremRate = prefs.getFloat(PREF_KEY_FILTER_TREM_RATE, Settings::FILTER_TREM_RATE_DEFAULT);
    float tremDepth = prefs.getFloat(PREF_KEY_FILTER_TREM_DEPTH, Settings::FILTER_TREM_DEPTH_DEFAULT);
    uint8_t tremWave = prefs.getUChar(PREF_KEY_FILTER_TREM_WAVE, Settings::FILTER_TREM_WAVE_DEFAULT);
    filtersSetTrem(tremEn, tremRate, tremDepth, tremWave);
    bool spEn = prefs.getBool(PREF_KEY_FILTER_SPARK_EN, Settings::FILTER_SPARK_DEFAULT);
    float spDens = prefs.getFloat(PREF_KEY_FILTER_SPARK_DENS, Settings::FILTER_SPARK_DENS_DEFAULT);
    float spInt = prefs.getFloat(PREF_KEY_FILTER_SPARK_INT, Settings::FILTER_SPARK_INT_DEFAULT);
    uint32_t spDec = prefs.getUInt(PREF_KEY_FILTER_SPARK_DECAY, Settings::FILTER_SPARK_DECAY_DEFAULT);
    filtersSetSpark(spEn, spDens, spInt, spDec);
    bool compEn = prefs.getBool(PREF_KEY_FILTER_COMP_EN, Settings::FILTER_COMP_DEFAULT);
    float compThr = prefs.getFloat(PREF_KEY_FILTER_COMP_THR, Settings::FILTER_COMP_THR_DEFAULT);
    float compRatio = prefs.getFloat(PREF_KEY_FILTER_COMP_RATIO, Settings::FILTER_COMP_RATIO_DEFAULT);
    uint32_t compAt = prefs.getUInt(PREF_KEY_FILTER_COMP_ATTACK, Settings::FILTER_COMP_ATTACK_DEFAULT);
    uint32_t compRl = prefs.getUInt(PREF_KEY_FILTER_COMP_RELEASE, Settings::FILTER_COMP_RELEASE_DEFAULT);
    filtersSetComp(compEn, compThr, compRatio, compAt, compRl);
    bool envEn = prefs.getBool(PREF_KEY_FILTER_ENV_EN, Settings::FILTER_ENV_DEFAULT);
    uint32_t envAt = prefs.getUInt(PREF_KEY_FILTER_ENV_AT, Settings::FILTER_ENV_ATTACK_DEFAULT);
    uint32_t envRl = prefs.getUInt(PREF_KEY_FILTER_ENV_RL, Settings::FILTER_ENV_RELEASE_DEFAULT);
    filtersSetEnv(envEn, envAt, envRl);
    bool delEn = prefs.getBool(PREF_KEY_FILTER_DELAY_EN, Settings::FILTER_DELAY_DEFAULT);
    uint32_t delMs = prefs.getUInt(PREF_KEY_FILTER_DELAY_MS, Settings::FILTER_DELAY_MS_DEFAULT);
    float delFb = prefs.getFloat(PREF_KEY_FILTER_DELAY_FB, Settings::FILTER_DELAY_FB_DEFAULT);
    float delMix = prefs.getFloat(PREF_KEY_FILTER_DELAY_MIX, Settings::FILTER_DELAY_MIX_DEFAULT);
    filtersSetDelay(delEn, delMs, delFb, delMix);
    customStepMs = prefs.getUInt(PREF_KEY_CUSTOM_MS, Settings::CUSTOM_STEP_MS_DEFAULT);
    if (customStepMs < 100)
        customStepMs = Settings::CUSTOM_STEP_MS_DEFAULT;
    size_t maxFloats = CUSTOM_MAX;
    size_t readBytes = prefs.getBytesLength(PREF_KEY_CUSTOM);
    if (readBytes > 0 && readBytes <= sizeof(float) * CUSTOM_MAX)
    {
        customLen = readBytes / sizeof(float);
        prefs.getBytes(PREF_KEY_CUSTOM, customPattern, readBytes);
    }
    else
    {
        // Default custom pattern: gentle wave / pulse
        const float defVals[] = {0.1f, 0.3f, 0.6f, 0.9f, 0.6f, 0.3f};
        customLen = sizeof(defVals) / sizeof(defVals[0]);
        for (size_t i = 0; i < customLen && i < CUSTOM_MAX; ++i)
            customPattern[i] = defVals[i];
        customStepMs = 600;
    }
#if ENABLE_MUSIC_MODE
    musicEnabled = prefs.getBool(PREF_KEY_MUSIC_EN, Settings::MUSIC_DEFAULT_ENABLED);
    musicGain = prefs.getFloat(PREF_KEY_MUSIC_GAIN, Settings::MUSIC_GAIN_DEFAULT);
    if (musicGain < 0.1f)
        musicGain = 0.1f;
    if (musicGain > 12.0f)
        musicGain = 12.0f;
    musicSmoothing = prefs.getFloat(PREF_KEY_MUSIC_SMOOTH, 0.4f);
    if (musicSmoothing < 0.0f)
        musicSmoothing = 0.0f;
    if (musicSmoothing > 1.0f)
        musicSmoothing = 1.0f;
    musicAutoLamp = prefs.getBool(PREF_KEY_MUSIC_AUTOLAMP, false);
    musicAutoThr = prefs.getFloat(PREF_KEY_MUSIC_AUTOTHR, 0.4f);
    if (musicAutoThr < 0.05f)
        musicAutoThr = 0.05f;
    if (musicAutoThr > 1.5f)
        musicAutoThr = 1.5f;
    musicMode = prefs.getUChar(PREF_KEY_MUSIC_MODE, 0);
    if (musicMode > 1)
        musicMode = 0;
    clapEnabled = prefs.getBool(PREF_KEY_CLAP_EN, Settings::CLAP_DEFAULT_ENABLED);
    clapThreshold = prefs.getFloat(PREF_KEY_CLAP_THR, Settings::CLAP_THRESHOLD_DEFAULT);
    clapCooldownMs = prefs.getUInt(PREF_KEY_CLAP_COOL, Settings::CLAP_COOLDOWN_MS_DEFAULT);
    clapCmd1 = prefs.getString(PREF_KEY_CLAP_CMD1, clapCmd1);
    clapCmd2 = prefs.getString(PREF_KEY_CLAP_CMD2, clapCmd2);
    clapCmd3 = prefs.getString(PREF_KEY_CLAP_CMD3, clapCmd3);
    if (clapThreshold < 0.05f)
        clapThreshold = 0.05f;
    if (clapThreshold > 1.5f)
        clapThreshold = 1.5f;
    if (clapCooldownMs < 200)
        clapCooldownMs = 200;
#endif
#if ENABLE_POTI
    potiEnabled = prefs.getBool(PREF_KEY_POTI_EN, true);
    potiAlpha = prefs.getFloat(PREF_KEY_POTI_ALPHA, Settings::POTI_ALPHA);
    if (potiAlpha < 0.01f)
        potiAlpha = 0.01f;
    if (potiAlpha > 1.0f)
        potiAlpha = 1.0f;
    potiDeltaMin = prefs.getFloat(PREF_KEY_POTI_DELTA, Settings::POTI_DELTA_MIN);
    if (potiDeltaMin < 0.001f)
        potiDeltaMin = 0.001f;
    if (potiDeltaMin > 0.5f)
        potiDeltaMin = 0.5f;
    potiOffThreshold = prefs.getFloat(PREF_KEY_POTI_OFF, Settings::POTI_OFF_THRESHOLD);
    if (potiOffThreshold < 0.0f)
        potiOffThreshold = 0.0f;
    if (potiOffThreshold > 0.5f)
        potiOffThreshold = 0.5f;
    potiSampleMs = prefs.getUInt(PREF_KEY_POTI_SAMPLE, Settings::POTI_SAMPLE_MS);
    if (potiSampleMs < 10)
        potiSampleMs = 10;
    if (potiSampleMs > 2000)
        potiSampleMs = 2000;
    potiCalibMin = prefs.getFloat(PREF_KEY_POTI_MIN, Settings::POTI_MIN_DEFAULT);
    potiCalibMax = prefs.getFloat(PREF_KEY_POTI_MAX, Settings::POTI_MAX_DEFAULT);
    if (potiCalibMax < potiCalibMin + 0.05f)
    {
        potiCalibMin = Settings::POTI_MIN_DEFAULT;
        potiCalibMax = Settings::POTI_MAX_DEFAULT;
    }
    potiInvert = prefs.getBool(PREF_KEY_POTI_INV, Settings::POTI_INVERT_DEFAULT);
#endif
#if ENABLE_PUSH_BUTTON
    pushEnabled = prefs.getBool(PREF_KEY_PUSH_EN, true);
    pushDebounceMs = prefs.getUInt(PREF_KEY_PUSH_DB, Settings::PUSH_DEBOUNCE_MS);
    if (pushDebounceMs < 5)
        pushDebounceMs = 5;
    if (pushDebounceMs > 500)
        pushDebounceMs = 500;
    pushDoubleMs = prefs.getUInt(PREF_KEY_PUSH_DBL, Settings::PUSH_DOUBLE_MS);
    if (pushDoubleMs < 100)
        pushDoubleMs = 100;
    if (pushDoubleMs > 5000)
        pushDoubleMs = 5000;
    pushHoldMs = prefs.getUInt(PREF_KEY_PUSH_HOLD, Settings::PUSH_HOLD_MS);
    if (pushHoldMs < 200)
        pushHoldMs = 200;
    if (pushHoldMs > 6000)
        pushHoldMs = 6000;
    pushStepMs = prefs.getUInt(PREF_KEY_PUSH_STEP_MS, Settings::PUSH_BRI_STEP_MS);
    if (pushStepMs < 50)
        pushStepMs = 50;
    if (pushStepMs > 2000)
        pushStepMs = 2000;
    pushStep = prefs.getFloat(PREF_KEY_PUSH_STEP, Settings::PUSH_BRI_STEP);
    if (pushStep < 0.005f)
        pushStep = 0.005f;
    if (pushStep > 0.5f)
        pushStep = 0.5f;
#endif
    outputGamma = prefs.getFloat(PREF_KEY_PWM_GAMMA, Settings::PWM_GAMMA_DEFAULT);
    if (outputGamma < 0.5f || outputGamma > 4.0f)
        outputGamma = Settings::PWM_GAMMA_DEFAULT;
#if ENABLE_LIGHT_SENSOR
    lightGain = prefs.getFloat(PREF_KEY_LIGHT_GAIN, Settings::LIGHT_GAIN_DEFAULT);
    lightClampMin = prefs.getFloat(PREF_KEY_LCLAMP_MIN, Settings::LIGHT_CLAMP_MIN_DEFAULT);
    lightClampMax = prefs.getFloat(PREF_KEY_LCLAMP_MAX, Settings::LIGHT_CLAMP_MAX_DEFAULT);
    lightAlpha = prefs.getFloat(PREF_KEY_LIGHT_ALPHA, Settings::LIGHT_ALPHA);
    if (lightAlpha < 0.001f)
        lightAlpha = 0.001f;
    if (lightAlpha > 0.8f)
        lightAlpha = 0.8f;
    if (lightClampMin < 0.0f)
        lightClampMin = 0.0f;
    if (lightClampMax > 1.5f)
        lightClampMax = 1.0f;
    if (lightClampMin >= lightClampMax)
    {
        lightClampMin = Settings::LIGHT_CLAMP_MIN_DEFAULT;
        lightClampMax = Settings::LIGHT_CLAMP_MAX_DEFAULT;
    }
#endif
    briMinUser = prefs.getFloat(PREF_KEY_BRI_MIN, Settings::BRI_MIN_DEFAULT);
    briMaxUser = prefs.getFloat(PREF_KEY_BRI_MAX, Settings::BRI_MAX_DEFAULT);
    presenceGraceMs = prefs.getUInt(PREF_KEY_PRES_GRACE, Settings::PRESENCE_GRACE_MS_DEFAULT);
#if ENABLE_LIGHT_SENSOR
    lastLoggedBrightness = masterBrightness;
    lightMinRaw = 4095;
    lightMaxRaw = 0;
#endif
    setPattern(currentPattern, false, false);
}


void importConfig(const String &args)
{
    // Format: key=value whitespace separated (e.g., ramp=400 idle=0 touch_on=8 touch_off=5 presence_en=on)
    int idx = 0;
    String rest = args;
    rest.trim();
    while (rest.length() > 0)
    {
        int spacePos = rest.indexOf(' ');
        String token;
        if (spacePos >= 0)
        {
            token = rest.substring(0, spacePos);
            rest = rest.substring(spacePos + 1);
            rest.trim();
        }
        else
        {
            token = rest;
            rest = "";
        }
        int eqPos = token.indexOf('=');
        if (eqPos <= 0)
            continue;
        String key = token.substring(0, eqPos);
        String val = token.substring(eqPos + 1);
        key.toLowerCase();
        val.trim();
        if (key == "ramp")
        {
            uint32_t v = val.toInt();
            if (v >= 50 && v <= 10000)
            {
                rampDurationMs = v;
                rampOnDurationMs = v;
                rampOffDurationMs = v;
            }
        }
        else if (key == "ramp_on_ms")
        {
            uint32_t v = val.toInt();
            if (v >= 50 && v <= 10000)
                rampOnDurationMs = v;
        }
        else if (key == "ramp_off_ms")
        {
            uint32_t v = val.toInt();
            if (v >= 50 && v <= 10000)
                rampOffDurationMs = v;
        }
#if ENABLE_LIGHT_SENSOR
        else if (key == "ramp_amb")
        {
            float v = val.toFloat();
            if (v < 0.0f)
                v = 0.0f;
            if (v > 5.0f)
                v = 5.0f;
            rampAmbientFactor = v;
        }
#endif
        else if (key == "idle")
        {
            int minutes = val.toInt();
            if (minutes < 0)
                minutes = 0;
            idleOffMs = minutes == 0 ? 0 : (uint32_t)minutes * 60000U;
        }
#if ENABLE_TOUCH_DIM
        else if (key == "touch_on")
        {
            int v = val.toInt();
            if (v > 0)
                touchDeltaOn = v;
        }
        else if (key == "touch_off")
        {
            int v = val.toInt();
            if (v > 0)
                touchDeltaOff = v;
        }
        else if (key == "touch_hold")
        {
            uint32_t v = val.toInt();
            if (v >= 500 && v <= 5000)
                touchHoldStartMs = v;
        }
#endif
        else if (key == "pat_scale")
        {
            float v = val.toFloat();
            if (v >= 0.1f && v <= 5.0f)
                patternSpeedScale = v;
        }
        else if (key == "pat_fade")
        {
            bool v;
            if (parseBool(val, v))
                patternFadeEnabled = v;
        }
        else if (key == "pat_fade_amt")
        {
            float v = val.toFloat();
            if (v >= 0.01f && v <= 10.0f)
                patternFadeStrength = v;
        }
        else if (key == "pat_inv")
        {
            bool v;
            if (parseBool(val, v))
                patternInvert = v;
        }
        else if (key == "pat_lo")
        {
            float v = clamp01(val.toFloat());
            patternMarginLow = v;
            if (patternMarginHigh < patternMarginLow)
                patternMarginHigh = patternMarginLow;
        }
        else if (key == "pat_hi")
        {
            float v = clamp01(val.toFloat());
            patternMarginHigh = v;
            if (patternMarginHigh < patternMarginLow)
                patternMarginHigh = patternMarginLow;
        }
        else if (key == "bri")
        {
            float v = val.toFloat();
            masterBrightness = clamp01(v);
            lastLoggedBrightness = masterBrightness;
        }
        else if (key == "auto")
        {
            bool v;
            if (parseBool(val, v))
                autoCycle = v;
        }
        else if (key == "presence_en")
        {
            bool v;
            if (parseBool(val, v))
                presenceEnabled = v;
        }
        else if (key == "presence_addr")
        {
            presenceAddr = val;
            presenceClearDevices();
            if (presenceAddr.length() > 0)
                presenceAddDevice(presenceAddr);
        }
        else if (key == "presence_list")
        {
            presenceClearDevices();
            int start = 0;
            while (start < val.length())
            {
                int comma = val.indexOf(',', start);
                if (comma < 0)
                    comma = val.length();
                String tok = val.substring(start, comma);
                tok.trim();
                if (tok.length() > 0)
                    presenceAddDevice(tok);
                start = comma + 1;
            }
            if (presenceHasDevices())
                presenceAddr = presenceDevices.back();
            else
                presenceAddr = "";
        }
        else if (key == "presence_thr")
        {
            int v = val.toInt();
            if (v < -120)
                v = -120;
            if (v > 0)
                v = -10;
            presenceRssiThreshold = v;
        }
        else if (key == "presence_on")
        {
            bool v;
            if (parseBool(val, v))
                presenceAutoOn = v;
        }
        else if (key == "presence_off")
        {
            bool v;
            if (parseBool(val, v))
                presenceAutoOff = v;
        }
#if ENABLE_TOUCH_DIM
        else if (key == "touch_dim")
        {
            bool v;
            if (parseBool(val, v))
                touchDimEnabled = v;
        }
        else if (key == "touch_dim_step")
        {
            float v = val.toFloat();
            if (v < 0.001f)
                v = 0.001f;
            if (v > 0.05f)
                v = 0.05f;
            touchDimStep = v;
        }
#endif
        else if (key == "filter_iir")
        {
            bool v;
            if (parseBool(val, v))
                filtersSetIir(v, Settings::FILTER_IIR_ALPHA_DEFAULT);
        }
        else if (key == "filter_iir_a")
        {
            float v = val.toFloat();
            if (v < 0.0f)
                v = 0.0f;
            if (v > 1.0f)
                v = 1.0f;
            FilterState f;
            filtersGetState(f);
            filtersSetIir(f.iirEnabled, v);
        }
        else if (key == "filter_clip")
        {
            bool v;
            if (parseBool(val, v))
            {
                FilterState f;
                filtersGetState(f);
                filtersSetClip(v, f.clipAmount, f.clipCurve);
            }
        }
        else if (key == "filter_clip_amt")
        {
            float v = val.toFloat();
            if (v < 0.0f)
                v = 0.0f;
            if (v > 1.0f)
                v = 1.0f;
            FilterState f;
            filtersGetState(f);
            filtersSetClip(f.clipEnabled, v, f.clipCurve);
        }
        else if (key == "filter_clip_curve")
        {
            uint8_t v = (uint8_t)val.toInt();
            if (v > 1)
                v = 0;
            FilterState f;
            filtersGetState(f);
            filtersSetClip(f.clipEnabled, f.clipAmount, v);
        }
        else if (key == "filter_comp")
        {
            bool v;
            if (parseBool(val, v))
            {
                FilterState f;
                filtersGetState(f);
                filtersSetComp(v, f.compThr, f.compRatio, f.compAttackMs, f.compReleaseMs);
            }
        }
        else if (key == "filter_comp_thr")
        {
            float v = val.toFloat();
            if (v < 0.0f)
                v = 0.0f;
            if (v > 1.2f)
                v = 1.2f;
            FilterState f;
            filtersGetState(f);
            filtersSetComp(f.compEnabled, v, f.compRatio, f.compAttackMs, f.compReleaseMs);
        }
        else if (key == "filter_comp_ratio")
        {
            float v = val.toFloat();
            if (v < 1.0f)
                v = 1.0f;
            if (v > 10.0f)
                v = 10.0f;
            FilterState f;
            filtersGetState(f);
            filtersSetComp(f.compEnabled, f.compThr, v, f.compAttackMs, f.compReleaseMs);
        }
        else if (key == "filter_comp_att")
        {
            uint32_t v = val.toInt();
            if (v < 1)
                v = 1;
            if (v > 2000)
                v = 2000;
            FilterState f;
            filtersGetState(f);
            filtersSetComp(f.compEnabled, f.compThr, f.compRatio, v, f.compReleaseMs);
        }
        else if (key == "filter_comp_rel")
        {
            uint32_t v = val.toInt();
            if (v < 1)
                v = 1;
            if (v > 4000)
                v = 4000;
            FilterState f;
            filtersGetState(f);
            filtersSetComp(f.compEnabled, f.compThr, f.compRatio, f.compAttackMs, v);
        }
        else if (key == "filter_env")
        {
            bool v;
            if (parseBool(val, v))
            {
                FilterState f;
                filtersGetState(f);
                filtersSetEnv(v, f.envAttackMs, f.envReleaseMs);
            }
        }
        else if (key == "filter_env_att")
        {
            uint32_t v = val.toInt();
            if (v < 1)
                v = 1;
            if (v > 4000)
                v = 4000;
            FilterState f;
            filtersGetState(f);
            filtersSetEnv(f.envEnabled, v, f.envReleaseMs);
        }
        else if (key == "filter_env_rel")
        {
            uint32_t v = val.toInt();
            if (v < 1)
                v = 1;
            if (v > 6000)
                v = 6000;
            FilterState f;
            filtersGetState(f);
            filtersSetEnv(f.envEnabled, f.envAttackMs, v);
        }
        else if (key == "filter_trem")
        {
            bool v;
            if (parseBool(val, v))
            {
                FilterState f;
                filtersGetState(f);
                filtersSetTrem(v, f.tremRateHz, f.tremDepth, f.tremWave);
            }
        }
        else if (key == "filter_trem_rate")
        {
            float v = val.toFloat();
            if (v < 0.05f)
                v = 0.05f;
            if (v > 20.0f)
                v = 20.0f;
            FilterState f;
            filtersGetState(f);
            filtersSetTrem(f.tremEnabled, v, f.tremDepth, f.tremWave);
        }
        else if (key == "filter_trem_depth")
        {
            float v = val.toFloat();
            if (v < 0.0f)
                v = 0.0f;
            if (v > 1.0f)
                v = 1.0f;
            FilterState f;
            filtersGetState(f);
            filtersSetTrem(f.tremEnabled, f.tremRateHz, v, f.tremWave);
        }
        else if (key == "filter_trem_wave")
        {
            uint8_t v = (uint8_t)val.toInt();
            if (v > 1)
                v = 0;
            FilterState f;
            filtersGetState(f);
            filtersSetTrem(f.tremEnabled, f.tremRateHz, f.tremDepth, v);
        }
        else if (key == "filter_spark")
        {
            bool v;
            if (parseBool(val, v))
            {
                FilterState f;
                filtersGetState(f);
                filtersSetSpark(v, f.sparkDensity, f.sparkIntensity, f.sparkDecayMs);
            }
        }
        else if (key == "filter_spark_dens")
        {
            float v = val.toFloat();
            if (v < 0.0f)
                v = 0.0f;
            if (v > 20.0f)
                v = 20.0f;
            FilterState f;
            filtersGetState(f);
            filtersSetSpark(f.sparkEnabled, v, f.sparkIntensity, f.sparkDecayMs);
        }
        else if (key == "filter_spark_int")
        {
            float v = val.toFloat();
            if (v < 0.0f)
                v = 0.0f;
            if (v > 1.0f)
                v = 1.0f;
            FilterState f;
            filtersGetState(f);
            filtersSetSpark(f.sparkEnabled, f.sparkDensity, v, f.sparkDecayMs);
        }
        else if (key == "filter_spark_decay")
        {
            uint32_t v = val.toInt();
            if (v < 10)
                v = 10;
            if (v > 5000)
                v = 5000;
            FilterState f;
            filtersGetState(f);
            filtersSetSpark(f.sparkEnabled, f.sparkDensity, f.sparkIntensity, v);
        }
        else if (key == "filter_delay")
        {
            bool v;
            if (parseBool(val, v))
            {
                FilterState f;
                filtersGetState(f);
                filtersSetDelay(v, f.delayMs, f.delayFeedback, f.delayMix);
            }
        }
        else if (key == "filter_delay_ms")
        {
            uint32_t v = val.toInt();
            if (v < 10)
                v = 10;
            if (v > 5000)
                v = 5000;
            FilterState f;
            filtersGetState(f);
            filtersSetDelay(f.delayEnabled, v, f.delayFeedback, f.delayMix);
        }
        else if (key == "filter_delay_fb")
        {
            float v = val.toFloat();
            if (v < 0.0f)
                v = 0.0f;
            if (v > 0.95f)
                v = 0.95f;
            FilterState f;
            filtersGetState(f);
            filtersSetDelay(f.delayEnabled, f.delayMs, v, f.delayMix);
        }
        else if (key == "filter_delay_mix")
        {
            float v = val.toFloat();
            if (v < 0.0f)
                v = 0.0f;
            if (v > 1.0f)
                v = 1.0f;
            FilterState f;
            filtersGetState(f);
            filtersSetDelay(f.delayEnabled, f.delayMs, f.delayFeedback, v);
        }
        else if (key == "light_gain")
        {
#if ENABLE_LIGHT_SENSOR
            lightGain = val.toFloat();
            if (lightGain < 0.1f)
                lightGain = 0.1f;
            if (lightGain > 5.0f)
                lightGain = 5.0f;
#endif
        }
        else if (key == "light_alpha")
        {
#if ENABLE_LIGHT_SENSOR
            float v = val.toFloat();
            if (v < 0.001f)
                v = 0.001f;
            if (v > 0.8f)
                v = 0.8f;
            lightAlpha = v;
#endif
        }
        else if (key == "light_min")
        {
#if ENABLE_LIGHT_SENSOR
            float v = val.toFloat();
            if (v >= 0.0f && v <= 1.0f)
                lightClampMin = v;
#endif
        }
        else if (key == "light_max")
        {
#if ENABLE_LIGHT_SENSOR
            float v = val.toFloat();
            if (v >= 0.0f && v <= 1.5f)
                lightClampMax = v;
#endif
        }
        else if (key == "bri_min")
        {
            briMinUser = clamp01(val.toFloat());
        }
        else if (key == "bri_max")
        {
            briMaxUser = clamp01(val.toFloat());
        }
        else if (key == "notif_min")
        {
            float v = clamp01(val.toFloat());
            notifyMinBrightness = v;
        }
        else if (key == "pres_grace")
        {
            uint32_t v = val.toInt();
            if (v < 0)
                v = 0;
            presenceGraceMs = v;
        }
#if ENABLE_MUSIC_MODE
        else if (key == "music_gain")
        {
            float v = val.toFloat();
            if (v >= 0.1f && v <= 5.0f)
                musicGain = v;
        }
        else if (key == "clap")
        {
            bool v;
            if (parseBool(val, v))
                clapEnabled = v;
        }
        else if (key == "clap_thr")
        {
            float v = val.toFloat();
            if (v >= 0.05f && v <= 1.5f)
                clapThreshold = v;
        }
        else if (key == "clap_cool")
        {
            uint32_t v = val.toInt();
            if (v >= 200 && v <= 5000)
                clapCooldownMs = v;
        }
#endif
        else if (key == "ramp_on_ease")
        {
            rampEaseOnType = easeFromString(val);
        }
        else if (key == "ramp_off_ease")
        {
            rampEaseOffType = easeFromString(val);
        }
        else if (key == "ramp_on_pow")
        {
            float v = val.toFloat();
            if (v >= 0.01f && v <= 10.0f)
                rampEaseOnPower = v;
        }
        else if (key == "ramp_off_pow")
        {
            float v = val.toFloat();
            if (v >= 0.01f && v <= 10.0f)
                rampEaseOffPower = v;
        }
        else if (key == "ramp_on_ms")
        {
            uint32_t v = val.toInt();
            if (v >= 50 && v <= 10000)
                rampOnDurationMs = v;
        }
        else if (key == "ramp_off_ms")
        {
            uint32_t v = val.toInt();
            if (v >= 50 && v <= 10000)
                rampOffDurationMs = v;
        }
        else if (key == "pwm_gamma")
        {
            float v = val.toFloat();
            if (v >= 0.5f && v <= 4.0f)
                outputGamma = v;
        }
        else if (key == "quick")
        {
            uint64_t mask = 0;
            if (val.equalsIgnoreCase(F("default")) || val.equalsIgnoreCase(F("none")))
            {
                mask = computeDefaultQuickMask();
            }
            else
            {
                parseQuickCsv(val, mask);
            }
            if (mask != 0)
            {
                quickMask = mask;
                sanitizeQuickMask();
            }
        }
    }
    if (lightClampMin < 0.0f)
        lightClampMin = Settings::LIGHT_CLAMP_MIN_DEFAULT;
    if (lightClampMax > 1.5f)
        lightClampMax = Settings::LIGHT_CLAMP_MAX_DEFAULT;
    if (lightClampMin >= lightClampMax)
    {
        lightClampMin = Settings::LIGHT_CLAMP_MIN_DEFAULT;
        lightClampMax = Settings::LIGHT_CLAMP_MAX_DEFAULT;
    }
#if ENABLE_MUSIC_MODE
    if (musicGain < 0.1f)
        musicGain = Settings::MUSIC_GAIN_DEFAULT;
    if (musicGain > 5.0f)
        musicGain = 5.0f;
#endif
    saveSettings();
    sendFeedback(F("[Config] Imported"));
    if (briMaxUser < briMinUser)
        briMaxUser = briMinUser;
    printStatus();
}
