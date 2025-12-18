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
void printTouchDebug()
{
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
                 F(" delta=") + String(delta) + F(" thrOn=") + String(touchDeltaOn) + F(" thrOff=") + String(touchDeltaOff));
}

/**
 * @brief Print current mode, brightness and wake/auto state.
 */
void printStatus()
{
    String payload;
    String line1 = String(F("Pattern ")) + String(currentPattern + 1) + F("/") + String(PATTERN_COUNT) + F(" '") +
                   PATTERNS[currentPattern].name + F("' | AutoCycle=") + (autoCycle ? F("ON") : F("OFF")) +
                   F(" | Speed=") + String(patternSpeedScale, 2);
    if (wakeFadeActive)
        line1 += F(" | Wake");
    if (sleepFadeActive)
        line1 += F(" | Sleep");
    sendFeedback(line1);
    payload += line1 + '\n';

    String quickLine = String(F("[Quick] ")) + quickMaskToCsv();
    sendFeedback(quickLine);
    payload += quickLine + '\n';

    String line2 = String(F("Lamp=")) + (lampEnabled ? F("ON") : F("OFF")) + F(" | Brightness=") +
                   String(masterBrightness * 100.0f, 1) + F("% | Cap=") + String(brightnessCap * 100.0f, 1) + F("%");
#if ENABLE_SWITCH
    line2 += F(" | Switch=");
    line2 += (switchDebouncedState ? F("ON") : F("OFF"));
#endif
    sendFeedback(line2);
    payload += line2 + '\n';

#if ENABLE_MUSIC_MODE
    String clapLine = String(F("[Clap] ")) + (clapEnabled ? F("ON") : F("OFF")) + F(" thr=") + String(clapThreshold, 2) + F(" cool=") + String(clapCooldownMs);
    sendFeedback(clapLine);
    payload += clapLine + '\n';
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
    line3 += F(" | TouchDim=");
    line3 += touchDimEnabled ? F("ON") : F("OFF");
    line3 += F(" (step=");
    line3 += String(touchDimStep, 3);
    line3 += F(")");
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
    sendFeedback(line3);
    payload += line3 + '\n';

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
    sendFeedback(filtLine);
    payload += filtLine + '\n';

    String line4 = F("Presence=");
    if (presenceEnabled)
    {
        line4 += F("ON (");
        line4 += (presenceAddr.isEmpty() ? F("no device") : presenceAddr);
        line4 += F(")");
    }
    else
    {
        line4 += F("OFF");
    }
    sendFeedback(line4);
    payload += line4 + '\n';

    if (demoActive)
    {
        String demoLine = String(F("[Demo] dwell=")) + String(demoDwellMs) + F("ms list=") + quickMaskToCsv();
        sendFeedback(demoLine);
        payload += demoLine + '\n';
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
    sendFeedback(line4b);
    payload += line4b + '\n';
#endif

    String customLine = String(F("[Custom] len=")) + String(customLen) + F(" stepMs=") + String(customStepMs);
    sendFeedback(customLine);
    payload += customLine + '\n';

    int raw = touchRead(PIN_TOUCH_DIM);
    int delta = raw - touchBaseline;
    int mag = abs(delta);
    String touchLine = String(F("[Touch] base=")) + String(touchBaseline) + F(" raw=") + String(raw) +
                       F(" delta=") + String(delta) + F(" |mag=") + String(mag) + F(" thrOn=") + String(touchDeltaOn) +
                       F(" thrOff=") + String(touchDeltaOff) + F(" active=") + (touchActive ? F("1") : F("0"));
    sendFeedback(touchLine);
    payload += touchLine + '\n';

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
    sendFeedback(lightLine);
    payload += lightLine + '\n';
#else
    String lightLine = F("[Light] N/A");
    sendFeedback(lightLine);
    payload += lightLine + '\n';
#endif

#if ENABLE_MUSIC_MODE
    String musicLine = String(F("[Music] ")) + (musicEnabled ? F("ON") : F("OFF"));
    sendFeedback(musicLine);
    payload += musicLine + '\n';
#else
    String musicLine = F("[Music] N/A");
    sendFeedback(musicLine);
    payload += musicLine + '\n';
#endif
#if ENABLE_POTI
    String potiLine = String(F("[Poti] ")) + (potiEnabled ? F("ON") : F("OFF")) + F(" a=") + String(potiAlpha, 2) +
                      F(" d=") + String(potiDeltaMin, 3) + F(" off=") + String(potiOffThreshold, 3) +
                      F(" smpl=") + String(potiSampleMs) + F("ms");
#else
    String potiLine = F("[Poti] N/A");
#endif
    sendFeedback(potiLine);
    payload += potiLine + '\n';

#if ENABLE_PUSH_BUTTON
    String pushLine = String(F("[Push] ")) + (pushEnabled ? F("ON") : F("OFF")) + F(" db=") + String(pushDebounceMs) +
                      F(" dbl=") + String(pushDoubleMs) + F(" hold=") + String(pushHoldMs) +
                      F(" step=") + String(pushStep * 100.0f, 1) + F("%/") + String(pushStepMs) + F("ms");
#else
    String pushLine = F("[Push] N/A");
#endif
    sendFeedback(pushLine);
    payload += pushLine + '\n';

    updateBleStatus(payload);
    printStatusStructured();
}

/**
 * @brief Emit structured sensor snapshot for machine parsing.
 */
void printSensorsStructured()
{
    String line = F("SENSORS|");
    line += F("touch_base=");
    line += String(touchBaseline);
    int raw = touchRead(PIN_TOUCH_DIM);
    line += F("|touch_raw=");
    line += String(raw);
    line += F("|touch_delta=");
    line += String(touchBaseline - raw);
    line += F("|touch_active=");
    line += touchActive ? F("1") : F("0");
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
    sendFeedback(line);
}
/**
 * @brief Emit a single structured status line for easier parsing (key=value pairs).
 */
void printStatusStructured()
{
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
    line += F("|cap=");
    line += String(brightnessCap * 100.0f, 1);
    line += F("|lamp=");
    line += lampEnabled ? F("ON") : F("OFF");
#if ENABLE_SWITCH
    line += F("|switch=");
    line += switchDebouncedState ? F("ON") : F("OFF");
#else
    line += F("|switch=N/A");
#endif
    line += F("|touch_dim=");
    line += touchDimEnabled ? F("1") : F("0");
    line += F("|touch_dim_step=");
    line += String(touchDimStep, 3);
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
    line += F("|ramp_amb=");
    line += String(rampAmbientFactor, 2);
    line += F("|idle_min=");
    line += idleOffMs == 0 ? F("0") : String(idleOffMs / 60000);
    line += F("|pat_speed=");
    line += String(patternSpeedScale, 2);
    line += F("|pat_fade=");
    line += patternFadeEnabled ? String(patternFadeStrength, 2) : F("off");
    line += F("|pat_lo=");
    line += String(patternMarginLow, 3);
    line += F("|pat_hi=");
    line += String(patternMarginHigh, 3);
    line += F("|quick=");
    line += quickMaskToCsv();
    line += F("|presence=");
    line += presenceEnabled ? (presenceAddr.isEmpty() ? F("ON(no-dev)") : presenceAddr) : F("OFF");
#if ENABLE_EXT_INPUT
    line += F("|ext_in=");
    line += extInputEnabled ? F("ON") : F("OFF");
    line += F("|ext_mode=");
    line += extInputAnalog ? F("ana") : F("dig");
    line += F("|ext_alpha=");
    line += String(extInputAlpha, 3);
    line += F("|ext_delta=");
    line += String(extInputDelta, 3);
    line += F("|ext_val=");
    line += extInputFiltered >= 0.0f ? String(extInputFiltered, 3) : F("N/A");
#endif
#if ENABLE_BT_SERIAL
    line += F("|bt_sleep_boot_ms=");
    line += String(getBtSleepAfterBootMs());
    line += F("|bt_sleep_ble_ms=");
    line += String(getBtSleepAfterBleMs());
#endif
    line += F("|custom_len=");
    line += String(customLen);
    line += F("|custom_step_ms=");
    line += String(customStepMs);
    line += F("|demo=");
    line += demoActive ? F("ON") : F("OFF");
    line += F("|gamma=");
    line += String(outputGamma, 2);
    line += F("|pwm_raw=");
    line += String((uint32_t)lastPwmValue);
    line += F("|pwm_max=");
    line += String((uint32_t)PWM_MAX);
    line += F("|bri_min=");
    line += String(briMinUser * 100.0f, 1);
    line += F("|bri_max=");
    line += String(briMaxUser * 100.0f, 1);
#if ENABLE_LIGHT_SENSOR
    line += F("|light=");
    line += lightSensorEnabled ? F("ON") : F("OFF");
    line += F("|light_gain=");
    line += String(lightGain, 2);
    line += F("|light_min=");
    line += String(lightClampMin, 2);
    line += F("|light_max=");
    line += String(lightClampMax, 2);
    line += F("|light_alpha=");
    line += String(lightAlpha, 3);
    line += F("|light_raw=");
    line += String((int)lightFiltered);
    line += F("|light_raw_min=");
    line += String((int)lightMinRaw);
    line += F("|light_raw_max=");
    line += String((int)lightMaxRaw);
#else
    line += F("|light=N/A");
#endif
#if ENABLE_MUSIC_MODE
    line += F("|music=");
    line += musicEnabled ? F("ON") : F("OFF");
    line += F("|music_gain=");
    line += String(musicGain, 2);
    line += F("|music_auto=");
    line += musicAutoLamp ? F("ON") : F("OFF");
    line += F("|music_thr=");
    line += String(musicAutoThr, 2);
    line += F("|music_mode=");
    line += (musicMode == 1 ? F("beat") : F("direct"));
    line += F("|music_mod=");
    line += String(musicModScale, 3);
    line += F("|music_kick_ms=");
    line += (musicLastKickMs > 0 ? String(millis() - musicLastKickMs) : F("N/A"));
    line += F("|music_env=");
    line += String(musicFiltered, 3);
    line += F("|music_level=");
    line += String(musicRawLevel, 3);
    line += F("|music_smooth=");
    line += String(musicSmoothing, 2);
    line += F("|clap=");
    line += clapEnabled ? F("ON") : F("OFF");
    line += F("|clap_thr=");
    line += String(clapThreshold, 2);
    line += F("|clap_cool=");
    line += String(clapCooldownMs);
    line += F("|clap_cmd1=");
    line += clapCmd1;
    line += F("|clap_cmd2=");
    line += clapCmd2;
    line += F("|clap_cmd3=");
    line += clapCmd3;
#else
    line += F("|music=N/A|clap=N/A");
#endif
#if ENABLE_POTI
    line += F("|poti=");
    line += potiEnabled ? F("ON") : F("OFF");
    line += F("|poti_alpha=");
    line += String(potiAlpha, 2);
    line += F("|poti_delta=");
    line += String(potiDeltaMin, 3);
    line += F("|poti_off=");
    line += String(potiOffThreshold, 3);
    line += F("|poti_sample=");
    line += String(potiSampleMs);
#else
    line += F("|poti=N/A");
#endif
#if ENABLE_PUSH_BUTTON
    line += F("|push=");
    line += pushEnabled ? F("ON") : F("OFF");
    line += F("|push_db=");
    line += String(pushDebounceMs);
    line += F("|push_dbl=");
    line += String(pushDoubleMs);
    line += F("|push_hold=");
    line += String(pushHoldMs);
    line += F("|push_step_ms=");
    line += String(pushStepMs);
    line += F("|push_step=");
    line += String(pushStep, 3);
#else
    line += F("|push=N/A");
#endif
    {
        FilterState filt;
        filtersGetState(filt);
        line += F("|filter_iir=");
        line += filt.iirEnabled ? F("ON") : F("OFF");
        line += F("|filter_alpha=");
        line += String(filt.iirAlpha, 3);
        line += F("|filter_clip=");
        line += filt.clipEnabled ? F("ON") : F("OFF");
        line += F("|filter_clip_amt=");
        line += String(filt.clipAmount, 2);
        line += F("|filter_clip_curve=");
        line += filt.clipCurve;
        line += F("|filter_trem=");
        line += filt.tremEnabled ? F("ON") : F("OFF");
        line += F("|filter_trem_rate=");
        line += String(filt.tremRateHz, 2);
        line += F("|filter_trem_depth=");
        line += String(filt.tremDepth, 2);
        line += F("|filter_trem_wave=");
        line += filt.tremWave;
        line += F("|filter_spark=");
        line += filt.sparkEnabled ? F("ON") : F("OFF");
        line += F("|filter_spark_dens=");
        line += String(filt.sparkDensity, 2);
        line += F("|filter_spark_int=");
        line += String(filt.sparkIntensity, 2);
        line += F("|filter_spark_decay=");
        line += String(filt.sparkDecayMs);
        line += F("|filter_comp=");
        line += filt.compEnabled ? F("ON") : F("OFF");
        line += F("|filter_comp_thr=");
        line += String(filt.compThr, 2);
        line += F("|filter_comp_ratio=");
        line += String(filt.compRatio, 2);
        line += F("|filter_comp_att=");
        line += String(filt.compAttackMs);
        line += F("|filter_comp_rel=");
        line += String(filt.compReleaseMs);
        line += F("|filter_env=");
        line += filt.envEnabled ? F("ON") : F("OFF");
        line += F("|filter_env_att=");
        line += String(filt.envAttackMs);
        line += F("|filter_env_rel=");
        line += String(filt.envReleaseMs);
        line += F("|filter_delay=");
        line += filt.delayEnabled ? F("ON") : F("OFF");
        line += F("|filter_delay_ms=");
        line += String(filt.delayMs);
        line += F("|filter_delay_fb=");
        line += String(filt.delayFeedback, 2);
        line += F("|filter_delay_mix=");
        line += String(filt.delayMix, 2);
    }
    sendFeedback(line);
}

/**
 * @brief Print available serial/BLE command usage.
 */
void printHelp()
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
        "  presence on|off   - Auto-Off wenn Gerät weg",
        "  presence set <addr>/clear - Gerät binden oder löschen",
        "  presence grace <ms> - Verzögerung vor Auto-Off",
        "  custom v1,v2,...   - Custom-Pattern setzen (0..1)",
        "  custom step <ms>   - Schrittzeit Custom-Pattern",
        "  notify [on1 off1 on2 off2] - Blinksignal (ms)",
        "  music sens <f>/smooth <0-1>/auto on|off/thr <f> - Musik-Parameter (Patterns Music Direct/Beat)",
        "  morse <text>     - Morse-Blink (dot=200ms, dash=600ms)",
        "  profile save <1-3>/load <1-3> - User-Profile ohne Touch/Presence/Quick",
        "  light gain <f>     - Verstärkung Lichtsensor",
        "  poti on|off/alpha <0..1>/delta <0..0.5>/off <0..0.5>/sample <ms> - Poti-Config",
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
        sendFeedback(String(l));
#else
    sendFeedback(F("[Help] disabled to save flash. Commands in README."));
#endif
}