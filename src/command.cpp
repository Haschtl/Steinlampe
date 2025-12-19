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
#include "lightSensor.h"
#include "microphone.h"
#include "inputs.h"
#include "presence.h"
#include "quickmode.h"
#include "sleepwake.h"
#include "notifications.h"
#include "pattern.h"
#include "demo.h"

#if ENABLE_BLE
#include <BLEDevice.h>
#endif

// SOS shortcut snapshot
bool sosModeActive = false;
float sosPrevBrightness = Settings::DEFAULT_BRIGHTNESS;
size_t sosPrevPattern = 0;
bool sosPrevAutoCycle = false;
bool sosPrevLampOn = false;


/**
 * @brief Parse and execute a command string from any input channel.
 */
void handleCommand(String line)
{
    line.trim();
    if (line.isEmpty())
        return;

    lastActivityMs = millis();

    String lower = line;
    lower.toLowerCase();

    if (lower == "help")
    {
        printHelp();
        return;
    }
    if (lower == "list")
    {
        listPatterns();
        return;
    }
    if (lower.startsWith("quick"))
    {
        String args = line.substring(5);
        args.trim();
        if (args.isEmpty() || args.equalsIgnoreCase(F("default")))
        {
            quickMask = computeDefaultQuickMask();
            sanitizeQuickMask();
            saveSettings();
            sendFeedback(String(F("[Quick] default -> ")) + quickMaskToCsv());
            return;
        }
        uint64_t mask = 0;
        if (parseQuickCsv(args, mask))
        {
            quickMask = mask;
            sanitizeQuickMask();
            saveSettings();
            sendFeedback(String(F("[Quick] set -> ")) + quickMaskToCsv());
        }
        else
        {
            sendFeedback(F("Usage: quick <idx,...> | quick default"));
        }
        return;
    }
    if (lower == "status")
    {
        printStatus();
        return;
    }
    if (lower == "status raw" || lower == "status json")
    {
        printStatusStructured();
        return;
    }
    if (lower == "sensors" || lower == "read sensors")
    {
        printSensorsStructured();
        return;
    }
    if (lower == "on")
    {
        setLampEnabled(true, "cmd on");
        saveSettings();
        printStatus();
        return;
    }
    if (lower == "off")
    {
        setLampEnabled(false, "cmd off");
        saveSettings();
        printStatus();
        return;
    }
    if (lower == "sync")
    {
        syncLampToSwitch();
        saveSettings();
        printStatus();
        return;
    }
    if (lower == "toggle")
    {
        setLampEnabled(!lampEnabled, "cmd toggle");
        saveSettings();
        printStatus();
        return;
    }
    if (lower == "touch")
    {
        printTouchDebug();
        return;
    }
    if (lower == "calibrate touch")
    {
        calibrateTouchGuided();
        return;
    }
    if (lower.startsWith("touch tune"))
    {
        String args = line.substring(10);
        args.trim();
        int on = 0, off = 0;
        if (sscanf(args.c_str(), "%d %d", &on, &off) == 2 && on > 0 && off > 0 && off < on)
        {
            touchDeltaOn = on;
            touchDeltaOff = off;
            saveSettings();
            sendFeedback(String(F("[Touch] tune on=")) + String(on) + F(" off=") + String(off));
        }
        else
        {
            sendFeedback(F("Usage: touch tune <on> <off>"));
        }
        return;
    }
    if (lower.startsWith("touch hold"))
    {
        uint32_t v = line.substring(line.indexOf("hold") + 4).toInt();
        if (v >= 500 && v <= 5000)
        {
            touchHoldStartMs = v;
            saveSettings();
            sendFeedback(String(F("[Touch] hold ms=")) + String(v));
        }
        else
        {
            sendFeedback(F("Usage: touch hold 500-5000"));
        }
        return;
    }
    if (lower == "touchdim on")
    {
        touchDimEnabled = true;
        saveSettings();
        sendFeedback(F("[TouchDim] Enabled"));
        return;
    }
    if (lower == "touchdim off")
    {
        touchDimEnabled = false;
        saveSettings();
        sendFeedback(F("[TouchDim] Disabled"));
        return;
    }
    if (lower.startsWith("touch dim speed") || lower.startsWith("touchdim speed"))
    {
        float v = line.substring(line.indexOf("speed") + 5).toFloat();
        if (v < 0.001f)
            v = 0.001f;
        if (v > 0.05f)
            v = 0.05f;
        touchDimStep = v;
        saveSettings();
        sendFeedback(String(F("[TouchDim] speed=")) + String(v, 3));
        return;
    }
    if (lower.startsWith("custom"))
    {
        String args = line.substring(6);
        args.trim();
        if (args.length() == 0 || args == "export")
        {
            String csv;
            for (size_t i = 0; i < customLen; ++i)
            {
                if (i > 0)
                    csv += ',';
                csv += String(customPattern[i], 3);
            }
            String msg = String(F("CUSTOM|len=")) + String(customLen) + F("|step=") + String(customStepMs) + F("|vals=") + csv;
            sendFeedback(msg);
            return;
        }
        if (args.startsWith("step"))
        {
            uint32_t v = args.substring(4).toInt();
            if (v >= 20 && v <= 5000)
            {
                customStepMs = v;
                saveSettings();
                sendFeedback(String(F("[Custom] step ms=")) + String(v));
            }
            else
            {
                sendFeedback(F("Usage: custom step 20-5000"));
            }
            return;
        }
        // parse CSV of floats 0..1
        size_t count = 0;
        float vals[CUSTOM_MAX];
        while (args.length() > 0 && count < CUSTOM_MAX)
        {
            int comma = args.indexOf(',');
            String token;
            if (comma >= 0)
            {
                token = args.substring(0, comma);
                args = args.substring(comma + 1);
            }
            else
            {
                token = args;
                args = "";
            }
            token.trim();
            if (token.length() == 0)
                continue;
            float v = token.toFloat();
            if (v < 0.0f)
                v = 0.0f;
            if (v > 1.0f)
                v = 1.0f;
            vals[count++] = v;
        }
        if (count > 0)
        {
            customLen = count;
            for (size_t i = 0; i < count; ++i)
                customPattern[i] = vals[i];
            saveSettings();
            sendFeedback(String(F("[Custom] Stored ")) + String(count) + F(" values"));
        }
        else
        {
            sendFeedback(F("Usage: custom v1,v2,... | custom step <ms>"));
        }
        return;
    }
    if (lower == "next")
    {
        size_t next = (currentPattern + 1) % PATTERN_COUNT;
        setPattern(next, true, true);
        return;
    }
    if (lower == "prev")
    {
        size_t prev = (currentPattern + PATTERN_COUNT - 1) % PATTERN_COUNT;
        setPattern(prev, true, true);
        return;
    }
    if (lower.startsWith("mode"))
    {
        long idx = line.substring(4).toInt();
        if (idx >= 1 && (size_t)idx <= PATTERN_COUNT)
        {
            setPattern((size_t)idx - 1, true, true);
        }
        else if (idx > (long)PATTERN_COUNT && idx <= (long)PATTERN_COUNT + PROFILE_SLOTS)
        {
            int slot = idx - (long)PATTERN_COUNT;
            loadProfileSlot((uint8_t)slot, true);
        }
        else
        {
            sendFeedback(F("Ungültiger Mode."));
        }
        return;
    }
    if (lower.startsWith("pat scale") || lower.startsWith("pattern scale"))
    {
        int pos = lower.indexOf("scale");
        float v = line.substring(pos + 5).toFloat();
        if (v >= 0.1f && v <= 5.0f)
        {
            patternSpeedScale = v;
            saveSettings();
            sendFeedback(String(F("[Pattern] speed scale=")) + String(v, 2));
        }
        else
        {
            sendFeedback(F("Usage: pat scale 0.1-5"));
        }
        return;
    }
    if (lower.startsWith("pat fade") || lower.startsWith("pattern fade"))
    {
        int pos = lower.indexOf("fade");
        String arg = line.substring(pos + 4);
        arg.trim();
        if (arg.startsWith("amt"))
        {
            float v = arg.substring(3).toFloat();
            if (v >= 0.01f && v <= 10.0f)
            {
                patternFadeStrength = v;
                saveSettings();
                sendFeedback(String(F("[Pattern] fade amt=")) + String(v, 2));
            }
            else
            {
                sendFeedback(F("Usage: pat fade amt 0.01-10"));
            }
        }
        else
        {
            bool v;
            if (parseBool(arg, v))
            {
                patternFadeEnabled = v;
                saveSettings();
                sendFeedback(String(F("[Pattern] fade ")) + (v ? F("ON") : F("OFF")));
            }
            else
            {
                sendFeedback(F("Usage: pat fade on|off|amt"));
            }
        }
        return;
    }
    if (lower.startsWith("pat invert") || lower.startsWith("pattern invert"))
    {
        int pos = lower.indexOf("invert");
        String arg = line.substring(pos + 6);
        arg.trim();
        bool v;
        if (arg.length() == 0)
        {
            patternInvert = !patternInvert;
            v = patternInvert;
        }
        else if (!parseBool(arg, v))
        {
            sendFeedback(F("Usage: pat invert on|off"));
            return;
        }
        patternInvert = v;
        saveSettings();
        sendFeedback(String(F("[Pattern] invert ")) + (patternInvert ? F("ON") : F("OFF")));
        return;
    }
    if (lower.startsWith("pat margin") || lower.startsWith("pattern margin"))
    {
        int pos = lower.indexOf("margin");
        String arg = line.substring(pos + 6);
        arg.trim();
        int space = arg.indexOf(' ');
        if (space > 0)
        {
            float lo = arg.substring(0, space).toFloat();
            float hi = arg.substring(space + 1).toFloat();
            lo = clamp01(lo);
            hi = clamp01(hi);
            if (hi < lo)
                hi = lo;
            patternMarginLow = lo;
            patternMarginHigh = hi;
            saveSettings();
            sendFeedback(String(F("[Pattern] margin lo=")) + String(lo, 3) + F(" hi=") + String(hi, 3));
        }
        else
        {
            sendFeedback(F("Usage: pat margin <low> <high>"));
        }
        return;
    }
    if (lower.startsWith("filter"))
    {
        String arg = line.substring(line.indexOf("filter") + 6);
        arg.trim();
        if (arg.startsWith("iir") || arg.startsWith("irr"))
        {
            // tokens: iir <on/off> <alpha>
            String rest = arg.substring(3);
            rest.trim();
            bool en = rest.indexOf("off") == -1;
            float a = Settings::FILTER_IIR_ALPHA_DEFAULT;
            int pos = rest.indexOf(' ');
            if (pos > 0)
            {
                String alphaStr = rest.substring(pos + 1);
                alphaStr.trim();
                if (alphaStr.length() > 0)
                    a = alphaStr.toFloat();
            }
            if (a < 0.0f)
                a = 0.0f;
            if (a > 1.0f)
                a = 1.0f;
            filtersSetIir(en, a);
            saveSettings();
            sendFeedback(String(F("[Filter] IIR ")) + (en ? F("ON ") : F("OFF ")) + F("alpha=") + String(a, 3));
        }
        else if (arg.startsWith("clip"))
        {
            bool en = arg.indexOf("off") == -1;
            float amt = Settings::FILTER_CLIP_AMT_DEFAULT;
            uint8_t curve = Settings::FILTER_CLIP_CURVE_DEFAULT;
            int pos = arg.indexOf(' ');
            if (pos > 0)
            {
                String rest = arg.substring(pos + 1);
                rest.trim();
                if (rest.startsWith("on"))
                {
                    rest = rest.substring(2);
                    rest.trim();
                }
                else if (rest.startsWith("off"))
                {
                    rest = rest.substring(3);
                    rest.trim();
                }
                amt = rest.toFloat();
                if (amt < 0.0f)
                    amt = 0.0f;
                if (amt > 1.0f)
                    amt = 1.0f;
                if (rest.indexOf("soft") >= 0)
                    curve = 1;
                else if (rest.indexOf("tanh") >= 0)
                    curve = 0;
            }
            filtersSetClip(en, amt, curve);
            saveSettings();
            sendFeedback(String(F("[Filter] Clip ")) + (en ? F("ON ") : F("OFF ")) + F("amt=") + String(amt, 2) + F(" curve=") + (curve ? F("soft") : F("tanh")));
        }
        else if (arg.startsWith("trem"))
        {
            bool en = arg.indexOf("off") == -1;
            float rate = Settings::FILTER_TREM_RATE_DEFAULT;
            float depth = Settings::FILTER_TREM_DEPTH_DEFAULT;
            uint8_t wave = Settings::FILTER_TREM_WAVE_DEFAULT;
            // strip leading "trem"
            String rest = arg.substring(4);
            rest.trim();
            // drop leading on/off token if present
            if (rest.startsWith("on"))
            {
                rest = rest.substring(2);
                rest.trim();
            }
            else if (rest.startsWith("off"))
            {
                rest = rest.substring(3);
                rest.trim();
            }
            // parse numbers
            int pos2 = rest.indexOf(' ');
            if (pos2 > 0)
            {
                rate = rest.substring(0, pos2).toFloat();
                String rest2 = rest.substring(pos2 + 1);
                rest2.trim();
                depth = rest2.toFloat();
                if (rest2.indexOf("tri") >= 0)
                    wave = 1;
                else
                    wave = 0;
            }
            else if (rest.length())
            {
                rate = rest.toFloat();
            }
            if (rate < 0.05f)
                rate = 0.05f;
            if (rate > 20.0f)
                rate = 20.0f;
            if (depth < 0.0f)
                depth = 0.0f;
            if (depth > 1.0f)
                depth = 1.0f;
            filtersSetTrem(en, rate, depth, wave);
            saveSettings();
            sendFeedback(String(F("[Filter] Trem ")) + (en ? F("ON ") : F("OFF ")) + F(" rate=") + String(rate, 2) + F(" depth=") + String(depth, 2));
        }
        else if (arg.startsWith("spark"))
        {
            bool en = arg.indexOf("off") == -1;
            float dens = Settings::FILTER_SPARK_DENS_DEFAULT;
            float inten = Settings::FILTER_SPARK_INT_DEFAULT;
            uint32_t dec = Settings::FILTER_SPARK_DECAY_DEFAULT;
            int pos = arg.indexOf(' ');
            if (pos > 0)
            {
                String rest = arg.substring(pos + 1);
                rest.trim();
                if (rest.startsWith("on"))
                {
                    rest = rest.substring(2);
                    rest.trim();
                }
                else if (rest.startsWith("off"))
                {
                    rest = rest.substring(3);
                    rest.trim();
                }
                int p1 = rest.indexOf(' ');
                int p2 = p1 > 0 ? rest.indexOf(' ', p1 + 1) : -1;
                if (p1 > 0)
                {
                    dens = rest.substring(0, p1).toFloat();
                    if (p2 > 0)
                    {
                        inten = rest.substring(p1 + 1, p2).toFloat();
                        dec = rest.substring(p2 + 1).toInt();
                    }
                }
            }
            if (dens < 0.0f)
                dens = 0.0f;
            if (dens > 20.0f)
                dens = 20.0f;
            if (inten < 0.0f)
                inten = 0.0f;
            if (inten > 1.0f)
                inten = 1.0f;
            if (dec < 10)
                dec = 10;
            if (dec > 5000)
                dec = 5000;
            filtersSetSpark(en, dens, inten, dec);
            saveSettings();
            sendFeedback(String(F("[Filter] Spark ")) + (en ? F("ON ") : F("OFF ")) + F(" dens=") + String(dens, 2) + F(" int=") + String(inten, 2) + F(" dec=") + String(dec) + F("ms"));
        }
        else if (arg.startsWith("comp"))
        {
            bool en = arg.indexOf("off") == -1;
            float thr = Settings::FILTER_COMP_THR_DEFAULT;
            float ratio = Settings::FILTER_COMP_RATIO_DEFAULT;
            uint32_t att = Settings::FILTER_COMP_ATTACK_DEFAULT;
            uint32_t rel = Settings::FILTER_COMP_RELEASE_DEFAULT;
            // parse numbers
            int p1 = arg.indexOf(' ');
            if (p1 > 0)
            {
                String rest = arg.substring(p1 + 1);
                rest.trim();
                if (rest.startsWith("on"))
                {
                    rest = rest.substring(2);
                    rest.trim();
                }
                else if (rest.startsWith("off"))
                {
                    rest = rest.substring(3);
                    rest.trim();
                }
                int p2 = rest.indexOf(' ');
                int p3 = p2 > 0 ? rest.indexOf(' ', p2 + 1) : -1;
                int p4 = p3 > 0 ? rest.indexOf(' ', p3 + 1) : -1;
                if (p2 > 0)
                {
                    thr = rest.substring(0, p2).toFloat();
                    if (p3 > 0)
                    {
                        ratio = rest.substring(p2 + 1, p3).toFloat();
                        if (p4 > 0)
                        {
                            att = rest.substring(p3 + 1, p4).toInt();
                            rel = rest.substring(p4 + 1).toInt();
                        }
                    }
                }
            }
            if (thr < 0.0f)
                thr = 0.0f;
            if (thr > 1.2f)
                thr = 1.2f;
            if (ratio < 1.0f)
                ratio = 1.0f;
            if (ratio > 10.0f)
                ratio = 10.0f;
            if (att < 1)
                att = 1;
            if (att > 2000)
                att = 2000;
            if (rel < 1)
                rel = 1;
            if (rel > 4000)
                rel = 4000;
            filtersSetComp(en, thr, ratio, att, rel);
            saveSettings();
            sendFeedback(String(F("[Filter] Comp ")) + (en ? F("ON ") : F("OFF ")) + F(" thr=") + String(thr, 2) + F(" ratio=") + String(ratio, 2) + F(" att=") + String(att) + F("ms rel=") + String(rel) + F("ms"));
        }
        else if (arg.startsWith("env"))
        {
            bool en = arg.indexOf("off") == -1;
            uint32_t att = Settings::FILTER_ENV_ATTACK_DEFAULT;
            uint32_t rel = Settings::FILTER_ENV_RELEASE_DEFAULT;
            int p1 = arg.indexOf(' ');
            if (p1 > 0)
            {
                String rest = arg.substring(p1 + 1);
                rest.trim();
                if (rest.startsWith("on"))
                {
                    rest = rest.substring(2);
                    rest.trim();
                }
                else if (rest.startsWith("off"))
                {
                    rest = rest.substring(3);
                    rest.trim();
                }
                int p2 = rest.indexOf(' ');
                if (p2 > 0)
                {
                    att = rest.substring(0, p2).toInt();
                    rel = rest.substring(p2 + 1).toInt();
                }
            }
            if (att < 1)
                att = 1;
            if (att > 4000)
                att = 4000;
            if (rel < 1)
                rel = 1;
            if (rel > 6000)
                rel = 6000;
            filtersSetEnv(en, att, rel);
            saveSettings();
            sendFeedback(String(F("[Filter] Env ")) + (en ? F("ON ") : F("OFF ")) + F(" att=") + String(att) + F("ms rel=") + String(rel) + F("ms"));
        }
        else if (arg.startsWith("delay"))
        {
            bool en = arg.indexOf("off") == -1;
            uint32_t dMs = Settings::FILTER_DELAY_MS_DEFAULT;
            float fb = Settings::FILTER_DELAY_FB_DEFAULT;
            float mix = Settings::FILTER_DELAY_MIX_DEFAULT;
            int p1 = arg.indexOf(' ');
            if (p1 > 0)
            {
                String rest = arg.substring(p1 + 1);
                rest.trim();
                if (rest.startsWith("on"))
                {
                    rest = rest.substring(2);
                    rest.trim();
                }
                else if (rest.startsWith("off"))
                {
                    rest = rest.substring(3);
                    rest.trim();
                }
                int p2 = rest.indexOf(' ');
                int p3 = p2 > 0 ? rest.indexOf(' ', p2 + 1) : -1;
                if (p2 > 0)
                {
                    dMs = rest.substring(0, p2).toInt();
                    if (p3 > 0)
                    {
                        fb = rest.substring(p2 + 1, p3).toFloat();
                        mix = rest.substring(p3 + 1).toFloat();
                    }
                }
            }
            if (dMs < 10)
                dMs = 10;
            if (dMs > 5000)
                dMs = 5000;
            if (fb < 0.0f)
                fb = 0.0f;
            if (fb > 0.95f)
                fb = 0.95f;
            if (mix < 0.0f)
                mix = 0.0f;
            if (mix > 1.0f)
                mix = 1.0f;
            filtersSetDelay(en, dMs, fb, mix);
            saveSettings();
            sendFeedback(String(F("[Filter] Delay ")) + (en ? F("ON ") : F("OFF ")) + F(" ms=") + String(dMs) + F(" fb=") + String(fb, 2) + F(" mix=") + String(mix, 2));
            return;
        }
        else
        {
            sendFeedback(F("filter iir <on/off> <alpha> | filter clip <on/off> <amt> [tanh|soft] | filter trem <on/off> <rateHz> <depth> [sin|tri] | filter spark <on/off> <dens> <int> <decayMs> | filter comp <on/off> <thr> <ratio> <att> <rel> | filter env <on/off> <att> <rel> | filter delay <on/off> <ms> <fb> <mix>"));
        }
        return;
    }
    if (lower.startsWith("pwm curve") || lower.startsWith("pwm gamma"))
    {
        int pos = lower.indexOf("curve");
        if (pos < 0)
            pos = lower.indexOf("gamma");
        float v = line.substring(pos + 5).toFloat();
        if (v >= 0.5f && v <= 4.0f)
        {
            outputGamma = v;
            saveSettings();
            sendFeedback(String(F("[PWM] gamma=")) + String(v, 2));
        }
        else
        {
            sendFeedback(F("Usage: pwm curve 0.5-4"));
        }
        return;
    }
#if ENABLE_EXT_INPUT
    if (lower.startsWith("ext"))
    {
        String arg = line.substring(3);
        arg.trim();
        if (arg.startsWith("on"))
        {
            extInputEnabled = true;
            saveSettings();
            sendFeedback(F("[Ext] Enabled"));
        }
        else if (arg.startsWith("off"))
        {
            extInputEnabled = false;
            saveSettings();
            sendFeedback(F("[Ext] Disabled"));
        }
        else if (arg.startsWith("mode"))
        {
            if (arg.indexOf("analog") >= 0 || arg.indexOf("ana") >= 0)
                extInputAnalog = true;
            else if (arg.indexOf("dig") >= 0 || arg.indexOf("digital") >= 0)
                extInputAnalog = false;
            else
            {
                sendFeedback(F("Usage: ext mode analog|digital"));
                return;
            }
            if (extInputAnalog)
            {
                analogSetPinAttenuation(Settings::EXT_INPUT_PIN, ADC_11db);
                pinMode(Settings::EXT_INPUT_PIN, INPUT);
            }
            else
            {
                pinMode(Settings::EXT_INPUT_PIN, Settings::EXT_INPUT_ACTIVE_LOW ? INPUT_PULLUP : INPUT);
            }
            saveSettings();
            sendFeedback(String(F("[Ext] Mode=")) + (extInputAnalog ? F("analog") : F("digital")));
        }
        else if (arg.startsWith("alpha"))
        {
            float v = arg.substring(5).toFloat();
            if (v < 0.0f)
                v = 0.0f;
            if (v > 1.0f)
                v = 1.0f;
            extInputAlpha = v;
            saveSettings();
            sendFeedback(String(F("[Ext] alpha=")) + String(v, 3));
        }
        else if (arg.startsWith("delta"))
        {
            float v = arg.substring(5).toFloat();
            if (v < 0.0f)
                v = 0.0f;
            if (v > 1.0f)
                v = 1.0f;
            extInputDelta = v;
            saveSettings();
            sendFeedback(String(F("[Ext] delta=")) + String(v, 3));
        }
        else
        {
            sendFeedback(F("ext on|off | ext mode analog|digital | ext alpha <0-1> | ext delta <0-1>"));
        }
        return;
    }
#endif
    if (lower.startsWith("bri min"))
    {
        float v = line.substring(7).toFloat();
        v = clamp01(v);
        briMinUser = v;
        if (briMaxUser < briMinUser)
            briMaxUser = briMinUser;
        saveSettings();
        sendFeedback(String(F("[Bri] min=")) + String(v, 3));
        return;
    }
    if (lower.startsWith("bri max"))
    {
        float v = line.substring(7).toFloat();
        v = clamp01(v);
        if (v < briMinUser)
            v = briMinUser;
        briMaxUser = v;
        saveSettings();
        sendFeedback(String(F("[Bri] max=")) + String(v, 3));
        return;
    }
    if (lower.startsWith("bri cap"))
    {
        float v = line.substring(7).toFloat();
        v = clamp01(v / 100.0f);
        if (v < briMinUser)
            v = briMinUser;
        brightnessCap = v;
        if (briMaxUser > brightnessCap)
            briMaxUser = brightnessCap;
        saveSettings();
        sendFeedback(String(F("[Bri] cap=")) + String(brightnessCap * 100.0f, 1) + F("%"));
        return;
    }
    if (lower.startsWith("bri"))
    {
        float value = line.substring(3).toFloat();
        if (value < 0.0f)
            value = 0.0f;
        if (value > 100.0f)
            value = 100.0f;
        setBrightnessPercent(value, true);
        return;
    }
    if (lower.startsWith("auto"))
    {
        String arg = line.substring(4);
        arg.trim();
        arg.toLowerCase();
        if (arg == "on")
            autoCycle = true;
        else if (arg == "off")
            autoCycle = false;
        else
            sendFeedback(F("auto on|off"));
        saveSettings();
        printStatus();
        return;
    }
    if (lower.startsWith("bt sleep"))
    {
        String arg = line.substring(8);
        arg.trim();
        arg.toLowerCase();
        if (arg.startsWith("boot"))
        {
            float min = arg.substring(4).toFloat();
            if (min < 0.0f)
                min = 0.0f;
            uint32_t ms = (uint32_t)(min * 60000.0f);
#if ENABLE_BT_SERIAL
            setBtSleepAfterBootMs(ms);
#endif
            saveSettings();
            sendFeedback(String(F("[BT] sleep after boot=")) + String(min, 2) + F(" min"));
        }
        else if (arg.startsWith("ble"))
        {
            float min = arg.substring(3).toFloat();
            if (min < 0.0f)
                min = 0.0f;
            uint32_t ms = (uint32_t)(min * 60000.0f);
#if ENABLE_BT_SERIAL
            setBtSleepAfterBleMs(ms);
#endif
            saveSettings();
            sendFeedback(String(F("[BT] sleep after idle command=")) + String(min, 2) + F(" min"));
        }
        else
        {
            sendFeedback(F("bt sleep boot <min> | bt sleep ble <min> (0=off, idle after last cmd)"));
        }
        return;
    }
    if (lower.startsWith("demo"))
    {
        String arg = line.substring(4);
        arg.trim();
        arg.toLowerCase();
        if (arg == "off" || arg == "stop")
        {
            stopDemo();
        }
        else
        {
            uint32_t dwellMs = 6000;
            if (arg.length() > 0)
            {
                float v = arg.toFloat();
                if (v > 0.0f)
                    dwellMs = (uint32_t)(v * 1000.0f);
            }
            startDemo(dwellMs);
        }
        return;
    }
    if (lower.startsWith("ramp"))
    {
        String arg = line.substring(4);
        arg.trim();
        if (lower.startsWith("ramp ease"))
        {
            arg = arg.substring(arg.indexOf("ease") + 4);
            arg.trim();
            bool isOn = arg.startsWith("on");
            bool isOff = arg.startsWith("off");
            if (isOn || isOff)
            {
                arg = arg.substring(isOn ? 2 : 3);
                arg.trim();
            }
            String typeToken;
            float power = -1.0f;
            int space = arg.indexOf(' ');
            if (space >= 0)
            {
                typeToken = arg.substring(0, space);
                power = arg.substring(space + 1).toFloat();
            }
            else
            {
                typeToken = arg;
            }
            uint8_t etype = easeFromString(typeToken);
            if (isnan(power) || power < 0.01f)
                power = 2.0f;
            if (power > 10.0f)
                power = 10.0f;
            if (!isOn && !isOff)
            {
                rampEaseOnType = rampEaseOffType = etype;
                rampEaseOnPower = rampEaseOffPower = power;
                sendFeedback(String(F("[Ramp] ease on/off ")) + easeToString(etype) + F(" pow=") + String(power, 2));
            }
            else if (isOn)
            {
                rampEaseOnType = etype;
                rampEaseOnPower = power;
                sendFeedback(String(F("[Ramp] ease on ")) + easeToString(etype) + F(" pow=") + String(power, 2));
            }
            else if (isOff)
            {
                rampEaseOffType = etype;
                rampEaseOffPower = power;
                sendFeedback(String(F("[Ramp] ease off ")) + easeToString(etype) + F(" pow=") + String(power, 2));
            }
            saveSettings();
        }
#if ENABLE_LIGHT_SENSOR
        else if (lower.startsWith("ramp ambient") || lower.startsWith("ramp amb"))
        {
            arg = arg.substring(arg.indexOf(' ') + 1);
            arg.trim();
            float v = arg.toFloat();
            if (isnan(v))
                v = rampAmbientFactor;
            if (v < 0.0f)
                v = 0.0f;
            if (v > 5.0f)
                v = 5.0f;
            rampAmbientFactor = v;
            sendFeedback(String(F("[Ramp] ambient factor=")) + String(rampAmbientFactor, 2));
            saveSettings();
        }
#endif
        else
        {
            bool isOn = arg.startsWith("on");
            bool isOff = arg.startsWith("off");
            if (isOn || isOff)
            {
                arg = arg.substring(isOn ? 2 : 3);
                arg.trim();
            }
            uint32_t val = arg.toInt();
            if (val >= 50 && val <= 10000)
            {
                if (!isOn && !isOff)
                {
                    rampDurationMs = val;
                    rampOnDurationMs = val;
                    rampOffDurationMs = val;
                    sendFeedback(String(F("[Ramp] on/off=")) + String(val) + F(" ms"));
                }
                else if (isOn)
                {
                    rampOnDurationMs = val;
                    sendFeedback(String(F("[Ramp] on=")) + String(val) + F(" ms"));
                }
                else if (isOff)
                {
                    rampOffDurationMs = val;
                    sendFeedback(String(F("[Ramp] off=")) + String(val) + F(" ms"));
                }
                saveSettings();
            }
            else
            {
                sendFeedback(F("Usage: ramp <50-10000> | ramp on <ms> | ramp off <ms>"));
            }
        }
        return;
    }
    if (lower.startsWith("notify"))
    {
        std::vector<uint32_t> seq;
        notifyFadeMs = 0;
        String args = line.substring(6);
        args.trim();
        while (args.length() > 0)
        {
            int sp = args.indexOf(' ');
            String tok = (sp >= 0) ? args.substring(0, sp) : args;
            args = (sp >= 0) ? args.substring(sp + 1) : "";
            args.trim();
            if (tok.startsWith("fade"))
            {
                int eq = tok.indexOf('=');
                if (eq >= 0)
                    tok = tok.substring(eq + 1);
                uint32_t f = (uint32_t)tok.toInt();
                if (f > 0)
                    notifyFadeMs = f;
            }
            else
            {
                uint32_t v = (uint32_t)tok.toInt();
                if (v > 0)
                    seq.push_back(v);
            }
        }
        if (seq.empty())
            seq = {120, 60, 120, 200};
        notifySeq = seq;
        notifyIdx = 0;
        notifyStageStartMs = millis();
        notifyInvert = (masterBrightness > 0.8f);
        bool wasActive = notifyActive;
        notifyActive = true;
        if (!wasActive)
        {
            bool effectiveLampOn = lampEnabled && !lampOffPending;
            notifyPrevLampOn = effectiveLampOn;
            notifyRestoreLamp = !effectiveLampOn;
        }
        if (notifyRestoreLamp || !lampEnabled)
            setLampEnabled(true, "notify");
        String seqStr;
        for (size_t i = 0; i < notifySeq.size(); ++i)
        {
            if (i)
                seqStr += F("/");
            seqStr += String(notifySeq[i]);
        }
        sendFeedback(String(F("[Notify] ")) + seqStr + (notifyInvert ? F(" invert") : F("")));
        return;
    }
    if (lower.startsWith("morse"))
    {
        String text = line.substring(5);
        text.trim();
        if (text.isEmpty())
        {
            sendFeedback(F("Usage: morse <text>"));
            return;
        }
        auto symbol = [](char c) -> const char *
        {
            switch (c)
            {
            case 'A':
                return ".-";
            case 'B':
                return "-...";
            case 'C':
                return "-.-.";
            case 'D':
                return "-..";
            case 'E':
                return ".";
            case 'F':
                return "..-.";
            case 'G':
                return "--.";
            case 'H':
                return "....";
            case 'I':
                return "..";
            case 'J':
                return ".---";
            case 'K':
                return "-.-";
            case 'L':
                return ".-..";
            case 'M':
                return "--";
            case 'N':
                return "-.";
            case 'O':
                return "---";
            case 'P':
                return ".--.";
            case 'Q':
                return "--.-";
            case 'R':
                return ".-.";
            case 'S':
                return "...";
            case 'T':
                return "-";
            case 'U':
                return "..-";
            case 'V':
                return "...-";
            case 'W':
                return ".--";
            case 'X':
                return "-..-";
            case 'Y':
                return "-.--";
            case 'Z':
                return "--..";
            case '1':
                return ".----";
            case '2':
                return "..---";
            case '3':
                return "...--";
            case '4':
                return "....-";
            case '5':
                return ".....";
            case '6':
                return "-....";
            case '7':
                return "--...";
            case '8':
                return "---..";
            case '9':
                return "----.";
            case '0':
                return "-----";
            default:
                return nullptr;
            }
        };

        std::vector<uint32_t> seq;
        auto addOnOff = [&](uint32_t on, uint32_t off)
        {
            seq.push_back(on);
            seq.push_back(off);
        };

        text.toUpperCase();
        for (size_t i = 0; i < text.length(); ++i)
        {
            char c = text.charAt(i);
            if (c == ' ')
            {
                if (!seq.empty())
                    seq.back() = 1400; // word gap
                continue;
            }
            const char *code = symbol(c);
            if (!code)
                continue;
            for (const char *p = code; *p; ++p)
            {
                if (*p == '.')
                    addOnOff(200, 200);
                else if (*p == '-')
                    addOnOff(600, 200);
            }
            if (!seq.empty())
                seq.back() = 600; // letter gap
        }
        if (seq.empty())
        {
            sendFeedback(F("[Morse] no valid symbols"));
            return;
        }
        bool wasActive = notifyActive;
        notifySeq = seq;
        notifyIdx = 0;
        notifyStageStartMs = millis();
        notifyFadeMs = 0;
        notifyInvert = (masterBrightness > 0.8f);
        notifyActive = true;
        if (!wasActive)
        {
            bool effectiveLampOn = lampEnabled && !lampOffPending;
            notifyPrevLampOn = effectiveLampOn;
            notifyRestoreLamp = !effectiveLampOn;
        }
        if (notifyRestoreLamp || !lampEnabled)
            setLampEnabled(true, "morse");
        sendFeedback(String(F("[Morse] ")) + text);
        return;
    }
    if (lower == "notify stop")
    {
        notifyActive = false;
        if (!notifyPrevLampOn)
            forceLampOff("notify stop");
        sendFeedback(F("[Notify] stopped"));
        return;
    }
    if (lower.startsWith("idleoff"))
    {
        String arg = line.substring(7);
        arg.trim();
        int minutes = arg.toInt();
        if (minutes < 0)
            minutes = 0;
        idleOffMs = (minutes == 0) ? 0 : (uint32_t)minutes * 60000U;
        saveSettings();
        if (idleOffMs == 0)
            sendFeedback(F("[IdleOff] Disabled"));
        else
            sendFeedback(String(F("[IdleOff] ")) + String(minutes) + F(" min"));
        return;
    }
    if (lower.startsWith("light"))
    {
#if ENABLE_LIGHT_SENSOR
        String arg = line.substring(5);
        arg.trim();
        arg.toLowerCase();
        if (arg == "on")
        {
            lightSensorEnabled = true;
            saveSettings();
            sendFeedback(F("[Light] Enabled"));
        }
        else if (arg == "off")
        {
            lightSensorEnabled = false;
            saveSettings();
            sendFeedback(F("[Light] Disabled"));
        }
        else if (arg.startsWith("calib"))
        {
            String which = arg.substring(5);
            which.trim();
            int raw = analogRead(Settings::LIGHT_PIN);
            if (which == "min")
            {
                lightFiltered = raw;
                lightMinRaw = raw;
                if ((int)lightMaxRaw <= (int)lightMinRaw)
                    lightMaxRaw = (uint16_t)min(4095, lightMinRaw + 50);
                sendFeedback(String(F("[Light] Calibrated min raw=")) + String(raw) + F(" max=") + String((int)lightMaxRaw));
            }
            else if (which == "max")
            {
                lightFiltered = raw;
                lightMaxRaw = raw;
                if ((int)lightMinRaw >= (int)lightMaxRaw)
                    lightMinRaw = (uint16_t)((lightMaxRaw > 50) ? (lightMaxRaw - 50) : 0);
                sendFeedback(String(F("[Light] Calibrated max raw=")) + String(raw) + F(" min=") + String((int)lightMinRaw));
            }
            else
            {
                lightFiltered = raw;
                lightMinRaw = raw;
                lightMaxRaw = raw;
                sendFeedback(String(F("[Light] Calibrated raw=")) + String(raw));
            }
        }
        else if (arg.startsWith("gain"))
        {
            float g = arg.substring(4).toFloat();
            if (g < 0.1f)
                g = 0.1f;
            if (g > 5.0f)
                g = 5.0f;
            lightGain = g;
            saveSettings();
            sendFeedback(String(F("[Light] gain=")) + String(g, 2));
        }
        else if (arg.startsWith("alpha"))
        {
            float a = arg.substring(5).toFloat();
            if (a < 0.001f)
                a = 0.001f;
            if (a > 0.8f)
                a = 0.8f;
            lightAlpha = a;
            saveSettings();
            sendFeedback(String(F("[Light] alpha=")) + String(a, 3));
        }
        else if (arg.startsWith("clamp"))
        {
            int pos = arg.indexOf(' ');
            String restClamp = pos >= 0 ? arg.substring(pos + 1) : "";
            restClamp.trim();
            float mn = restClamp.toFloat();
            int pos2 = restClamp.indexOf(' ');
            float mx = (pos2 >= 0) ? restClamp.substring(pos2 + 1).toFloat() : lightClampMax;
            if (mn < 0.0f)
                mn = 0.0f;
            if (mx > 1.5f)
                mx = 1.5f;
            if (mn >= mx)
            {
                sendFeedback(F("[Light] clamp invalid (min>=max)"));
            }
            else
            {
                lightClampMin = mn;
                lightClampMax = mx;
                saveSettings();
                sendFeedback(String(F("[Light] clamp ")) + String(mn, 2) + F("..") + String(mx, 2));
            }
        }
        else
        {
            sendFeedback(String(F("[Light] raw=")) + String((int)lightFiltered) + F(" en=") + (lightSensorEnabled ? F("1") : F("0")));
        }
#else
        sendFeedback(F("[Light] Sensor disabled at build (ENABLE_LIGHT_SENSOR=0)"));
#endif
        return;
    }
#if ENABLE_POTI
    if (lower.startsWith("poti"))
    {
        String arg = line.substring(4);
        arg.trim();
        arg.toLowerCase();
        if (arg == "on")
        {
            potiEnabled = true;
            saveSettings();
            sendFeedback(F("[Poti] Enabled"));
        }
        else if (arg == "off")
        {
            potiEnabled = false;
            saveSettings();
            sendFeedback(F("[Poti] Disabled"));
        }
        else if (arg.startsWith("alpha"))
        {
            float v = arg.substring(5).toFloat();
            if (v >= 0.01f && v <= 1.0f)
            {
                potiAlpha = v;
                saveSettings();
                sendFeedback(String(F("[Poti] alpha=")) + String(v, 2));
            }
            else
            {
                sendFeedback(F("Usage: poti alpha 0.01-1.0"));
            }
        }
        else if (arg.startsWith("delta"))
        {
            float v = arg.substring(5).toFloat();
            if (v >= 0.001f && v <= 0.5f)
            {
                potiDeltaMin = v;
                saveSettings();
                sendFeedback(String(F("[Poti] delta=")) + String(v, 3));
            }
            else
            {
                sendFeedback(F("Usage: poti delta 0.001-0.5"));
            }
        }
        else if (arg.startsWith("off"))
        {
            float v = arg.substring(3).toFloat();
            if (v >= 0.0f && v <= 0.5f)
            {
                potiOffThreshold = v;
                saveSettings();
                sendFeedback(String(F("[Poti] off=")) + String(v, 3));
            }
            else
            {
                sendFeedback(F("Usage: poti off 0.0-0.5"));
            }
        }
        else if (arg.startsWith("sample"))
        {
            uint32_t v = arg.substring(6).toInt();
            if (v >= 10 && v <= 2000)
            {
                potiSampleMs = v;
                saveSettings();
                sendFeedback(String(F("[Poti] sample=")) + String(v) + F("ms"));
            }
            else
            {
                sendFeedback(F("Usage: poti sample 10-2000"));
            }
        }
        else
        {
            sendFeedback(String(F("[Poti] ")) + (potiEnabled ? F("ON ") : F("OFF ")) + F("a=") + String(potiAlpha, 2) +
                         F(" d=") + String(potiDeltaMin, 3) + F(" off=") + String(potiOffThreshold, 3) +
                         F(" smpl=") + String(potiSampleMs) + F("ms"));
        }
        return;
    }
#endif
#if ENABLE_PUSH_BUTTON
    if (lower.startsWith("push"))
    {
        String arg = line.substring(4);
        arg.trim();
        arg.toLowerCase();
        if (arg == "on")
        {
            pushEnabled = true;
            saveSettings();
            sendFeedback(F("[Push] Enabled"));
        }
        else if (arg == "off")
        {
            pushEnabled = false;
            saveSettings();
            sendFeedback(F("[Push] Disabled"));
        }
        else if (arg.startsWith("debounce"))
        {
            uint32_t v = arg.substring(8).toInt();
            if (v >= 5 && v <= 500)
            {
                pushDebounceMs = v;
                saveSettings();
                sendFeedback(String(F("[Push] debounce=")) + String(v) + F("ms"));
            }
            else
            {
                sendFeedback(F("Usage: push debounce 5-500"));
            }
        }
        else if (arg.startsWith("double"))
        {
            uint32_t v = arg.substring(6).toInt();
            if (v >= 100 && v <= 5000)
            {
                pushDoubleMs = v;
                saveSettings();
                sendFeedback(String(F("[Push] double=")) + String(v) + F("ms"));
            }
            else
            {
                sendFeedback(F("Usage: push double 100-5000"));
            }
        }
        else if (arg.startsWith("hold"))
        {
            uint32_t v = arg.substring(4).toInt();
            if (v >= 200 && v <= 6000)
            {
                pushHoldMs = v;
                saveSettings();
                sendFeedback(String(F("[Push] hold=")) + String(v) + F("ms"));
            }
            else
            {
                sendFeedback(F("Usage: push hold 200-6000"));
            }
        }
        else if (arg.startsWith("step_ms"))
        {
            uint32_t v = arg.substring(7).toInt();
            if (v >= 50 && v <= 2000)
            {
                pushStepMs = v;
                saveSettings();
                sendFeedback(String(F("[Push] step_ms=")) + String(v) + F("ms"));
            }
            else
            {
                sendFeedback(F("Usage: push step_ms 50-2000"));
            }
        }
        else if (arg.startsWith("step"))
        {
            float v = arg.substring(4).toFloat();
            if (v >= 0.005f && v <= 0.5f)
            {
                pushStep = v;
                saveSettings();
                sendFeedback(String(F("[Push] step=")) + String(v * 100.0f, 1) + F("%"));
            }
            else
            {
                sendFeedback(F("Usage: push step 0.005-0.5"));
            }
        }
        else
        {
            sendFeedback(String(F("[Push] ")) + (pushEnabled ? F("ON ") : F("OFF ")) + F("db=") + String(pushDebounceMs) +
                         F(" dbl=") + String(pushDoubleMs) + F(" hold=") + String(pushHoldMs) +
                         F(" step=") + String(pushStep * 100.0f, 1) + F("%/") + String(pushStepMs) + F("ms"));
        }
        return;
    }
#endif
#if ENABLE_MUSIC_MODE
    if (lower.startsWith("music"))
    {
        String arg = line.substring(5);
        arg.trim();
        arg.toLowerCase();
        if (arg.startsWith("sens"))
        {
            float g = arg.substring(4).toFloat();
            if (g < 0.1f)
                g = 0.1f;
            if (g > 12.0f)
                g = 12.0f;
            musicGain = g;
            saveSettings();
            sendFeedback(String(F("[Music] gain=")) + String(g, 2));
        }
        else if (arg.startsWith("smooth"))
        {
            float s = arg.substring(6).toFloat();
            if (s < 0.0f)
                s = 0.0f;
            if (s > 1.0f)
                s = 1.0f;
            musicSmoothing = s;
            saveSettings();
            sendFeedback(String(F("[Music] smooth=")) + String(s, 2));
        }
        else if (arg == "calib")
        {
            sendFeedback(F("[Music] Calibrating... stay quiet, then clap once"));
            // Baseline: 500ms
            uint32_t t0 = millis();
            float dc = 0.0f;
            uint32_t n = 0;
            while (millis() - t0 < 500)
            {
                dc += (float)analogRead(Settings::MUSIC_PIN);
                ++n;
                delay(10);
            }
            if (n == 0)
                n = 1;
            dc /= (float)n;
            float dcNorm = dc / 4095.0f;
            // Peak detect for 1200ms
            float peak = 0.0f;
            float env = 0.0f;
            t0 = millis();
            const float dcAlpha = 0.01f;
            float dcTrack = dcNorm;
            while (millis() - t0 < 1200)
            {
                float v = (float)analogRead(Settings::MUSIC_PIN) / 4095.0f;
                dcTrack = (1.0f - dcAlpha) * dcTrack + dcAlpha * v;
                float d = fabsf(v - dcTrack);
                const float envAlpha = 0.2f;
                env = (1.0f - envAlpha) * env + envAlpha * d;
                if (env > peak)
                    peak = env;
                delay(10);
            }
            if (peak < 0.05f)
                peak = 0.05f; // avoid zero
            // derive gain so peak lands near 0.6
            float targetEnv = 0.6f;
            float g = targetEnv / peak;
            if (g < 0.1f)
                g = 0.1f;
            if (g > 12.0f)
                g = 12.0f;
            musicGain = g;
            // threshold at ~35% of peak
            float thr = peak * g * 0.35f;
            if (thr < 0.05f)
                thr = 0.05f;
            if (thr > 1.0f)
                thr = 1.0f;
            clapThreshold = thr;
            musicDc = dcNorm;
            musicEnv = 0.0f;
            musicFiltered = 0.0f;
            musicSmoothing = 0.4f;
            saveSettings();
            sendFeedback(String(F("[Music] calib gain=")) + String(musicGain, 2) + F(" thr=") + String(clapThreshold, 2));
        }
        else if (arg.startsWith("mode") || arg == "on" || arg == "off")
        {
            sendFeedback(F("[Music] Select pattern 'Music Direct' or 'Music Beat' to use music mode."));
        }
        else if (arg == "raw")
        {
            int raw = analogRead(Settings::MUSIC_PIN);
            sendFeedback(String(F("[Music] raw=")) + String(raw));
        }
        else if (arg.startsWith("auto"))
        {
            String rest = arg.substring(4);
            rest.trim();
            rest.toLowerCase();
            if (rest == "on")
            {
                musicAutoLamp = true;
                saveSettings();
                sendFeedback(F("[Music] auto lamp ON"));
            }
            else if (rest == "off")
            {
                musicAutoLamp = false;
                saveSettings();
                sendFeedback(F("[Music] auto lamp OFF"));
            }
            else if (rest.startsWith("thr"))
            {
                float v = rest.substring(3).toFloat();
                if (v < 0.05f)
                    v = 0.05f;
                if (v > 1.5f)
                    v = 1.5f;
                musicAutoThr = v;
                saveSettings();
                sendFeedback(String(F("[Music] auto thr=")) + String(v, 2));
            }
            else
            {
                sendFeedback(F("Usage: music auto on|off|thr <val>"));
            }
        }
        else
        {
            sendFeedback(String(F("[Music] level=")) + String(musicFiltered, 3) + F(" en=") + (musicEnabled ? F("1") : F("0")) +
                         F(" smooth=") + String(musicSmoothing, 2));
        }
        return;
    }
#endif
    if (lower.startsWith("clap"))
    {
#if ENABLE_MUSIC_MODE
        String argRaw = line.substring(4);
        argRaw.trim();
        String arg = argRaw;
        arg.toLowerCase();
        if (arg == "on")
        {
            clapEnabled = true;
            saveSettings();
            sendFeedback(F("[Clap] Enabled"));
        }
        else if (arg == "off")
        {
            clapEnabled = false;
            clapCount = 0;
            clapWindowStartMs = 0;
            saveSettings();
            sendFeedback(F("[Clap] Disabled"));
        }
        else if (arg.startsWith("thr"))
        {
            float v = arg.substring(3).toFloat();
            if (v >= 0.05f && v <= 1.5f)
            {
                clapThreshold = v;
                saveSettings();
                sendFeedback(String(F("[Clap] thr=")) + String(v, 2));
            }
            else
            {
                sendFeedback(F("Usage: clap thr 0.05-1.5"));
            }
        }
        else if (arg.startsWith("cool"))
        {
            uint32_t v = arg.substring(4).toInt();
            if (v >= 200 && v <= 5000)
            {
                clapCooldownMs = v;
                saveSettings();
                sendFeedback(String(F("[Clap] cool=")) + String(v) + F("ms"));
            }
            else
            {
                sendFeedback(F("Usage: clap cool 200-5000"));
            }
        }
        else if (arg.startsWith("train"))
        {
            String mode = arg.substring(5);
            mode.trim();
            mode.toLowerCase();
            if (mode == "on" || mode.length() == 0)
            {
                clapTraining = true;
                clapTrainLastLog = 0;
                sendFeedback(F("[Clap] Training ON"));
            }
            else if (mode == "off")
            {
                clapTraining = false;
                sendFeedback(F("[Clap] Training OFF"));
            }
            else
            {
                sendFeedback(F("Usage: clap train [on|off]"));
            }
        }
        else if (arg.startsWith("1 ") || arg.startsWith("2 ") || arg.startsWith("3 "))
        {
            uint8_t count = arg[0] - '0';
            String cmd = argRaw.substring(1);
            cmd.trim();
            if (cmd.length() == 0)
            {
                sendFeedback(F("Usage: clap <1|2|3> <command>"));
            }
            else
            {
                if (count == 1)
                    clapCmd1 = cmd;
                else if (count == 2)
                    clapCmd2 = cmd;
                else
                    clapCmd3 = cmd;
                saveSettings();
                sendFeedback(String(F("[Clap] ")) + count + F("x -> ") + cmd);
            }
        }
        else
        {
            sendFeedback(String(F("[Clap] ")) + (clapEnabled ? F("ON ") : F("OFF ")) + F("thr=") + String(clapThreshold, 2) + F(" cool=") + String(clapCooldownMs));
        }
#else
        sendFeedback(F("[Clap] Audio sensor not built (ENABLE_MUSIC_MODE=0)"));
#endif
        return;
    }
    if (lower.startsWith("wake"))
    {
        String rawArgs = line.substring(4);
        rawArgs.trim();
        String lowerArgs = rawArgs;
        lowerArgs.toLowerCase();
        if (lowerArgs == "stop" || lowerArgs == "cancel")
        {
            cancelWakeFade(true);
            return;
        }
        String args = rawArgs;
        bool soft = false;
        int modeIdx = -1;
        float briPct = -1.0f;
        float seconds = -1.0f;
        while (args.length() > 0)
        {
            int sp = args.indexOf(' ');
            String tok = (sp >= 0) ? args.substring(0, sp) : args;
            args = (sp >= 0) ? args.substring(sp + 1) : "";
            tok.trim();
            if (tok.length() == 0)
                continue;
            String ltok = tok;
            ltok.toLowerCase();
            if (ltok == "soft")
            {
                soft = true;
            }
            else if (ltok.startsWith("mode="))
            {
                int v = ltok.substring(5).toInt();
                if (v >= 1 && (size_t)v <= PATTERN_COUNT)
                    modeIdx = v;
            }
            else if (ltok.startsWith("bri="))
            {
                float v = tok.substring(4).toFloat();
                if (v >= 0.0f && v <= 100.0f)
                    briPct = v;
            }
            else if (seconds < 0.0f)
            {
                float v = tok.toFloat();
                if (v > 0.0f)
                    seconds = v;
            }
        }
        uint32_t durationMs = Settings::DEFAULT_WAKE_MS;
        if (seconds > 0.0f)
            durationMs = (uint32_t)(seconds * 1000.0f);
        if (modeIdx >= 1)
            setPattern((size_t)modeIdx - 1, true, false);
        float targetOverride = -1.0f;
        if (briPct >= 0.0f)
        {
            targetOverride = clamp01(briPct / 100.0f);
            masterBrightness = targetOverride;
            logBrightnessChange("wake bri");
        }
        startWakeFade(durationMs, true, soft, targetOverride);
        return;
    }
    if (lower.startsWith("sos"))
    {
        String arg = line.substring(3);
        arg.trim();
        if (arg.equalsIgnoreCase(F("stop")) || arg.equalsIgnoreCase(F("cancel")))
        {
            if (!sosModeActive)
            {
                sendFeedback(F("[SOS] Nicht aktiv"));
            }
            else
            {
                autoCycle = sosPrevAutoCycle;
                setBrightnessPercent(sosPrevBrightness * 100.0f, false);
                size_t restoreIdx = (sosPrevPattern < PATTERN_COUNT) ? sosPrevPattern : 0;
                setPattern(restoreIdx, true, false);
                sosModeActive = false;
                notifyActive = false;
                sleepFadeActive = false;
                wakeFadeActive = false;
                if (sosPrevLampOn)
                    setLampEnabled(true, "sos stop");
                else
                    setLampEnabled(false, "sos stop");
                saveSettings();
                sendFeedback(F("[SOS] beendet, Zustand wiederhergestellt"));
            }
        }
        else
        {
            if (!sosModeActive)
            {
                sosPrevBrightness = masterBrightness;
                sosPrevPattern = currentPattern;
                sosPrevAutoCycle = autoCycle;
                sosPrevLampOn = lampEnabled;
            }
            autoCycle = false;
            sleepFadeActive = false;
            wakeFadeActive = false;
            notifyActive = false;
            notifyRestoreLamp = true;
            notifyPrevLampOn = lampEnabled;
            setLampEnabled(true, "cmd sos");
            setBrightnessPercent(100.0f, false);
            int sosIdx = findPatternIndexByName("SOS");
            if (sosIdx >= 0)
                setPattern((size_t)sosIdx, true, false);
            sosModeActive = true;
            sendFeedback(F("[SOS] aktiv (100% Helligkeit)"));
        }
        return;
    }
    if (lower.startsWith("sleep"))
    {
        String arg = line.substring(5);
        arg.trim();
        arg.toLowerCase();
        if (arg == "stop" || arg == "cancel")
        {
            cancelSleepFade();
            sendFeedback(F("[Sleep] Abgebrochen."));
        }
        else
        {
            uint32_t durMs = Settings::DEFAULT_SLEEP_MS;
            if (arg.length() > 0)
            {
                float minutes = line.substring(5).toFloat();
                if (minutes > 0.0f)
                    durMs = (uint32_t)(minutes * 60000.0f);
            }
            startSleepFade(durMs);
        }
        return;
    }
    if (lower.startsWith("presence"))
    {
        String arg = line.substring(8);
        arg.trim();
        arg.toLowerCase();
        if (arg == "on")
        {
            presenceEnabled = true;
            saveSettings();
            sendFeedback(F("[Presence] Enabled"));
        }
        else if (arg == "off")
        {
            presenceEnabled = false;
            saveSettings();
            sendFeedback(F("[Presence] Disabled"));
        }
        else if (arg.startsWith("set"))
        {
            String addr = line.substring(12);
            addr.trim();
            if (addr.isEmpty() || addr == "me")
            {
                if (lastBleAddr.length() > 0)
                {
                    presenceAddr = lastBleAddr;
                    saveSettings();
                    sendFeedback(String(F("[Presence] Set to connected device ")) + presenceAddr);
                }
                else
                {
                    sendFeedback(F("[Presence] Kein aktives BLE-Geraet gefunden."));
                }
            }
            else if (addr.length() >= 11)
            {
                presenceAddr = addr;
                saveSettings();
                sendFeedback(String(F("[Presence] Set to ")) + presenceAddr);
            }
            else
            {
                sendFeedback(F("Usage: presence set <MAC>"));
            }
        }
        else if (arg == "clear")
        {
            presenceAddr = "";
            saveSettings();
            sendFeedback(F("[Presence] Cleared"));
        }
        else if (arg.startsWith("grace"))
        {
            uint32_t v = line.substring(line.indexOf("grace") + 5).toInt();
            presenceGraceMs = v;
            saveSettings();
            sendFeedback(String(F("[Presence] Grace ")) + String(v) + F(" ms"));
        }
        else
        {
            sendFeedback(String(F("[Presence] ")) + (presenceEnabled ? F("ON ") : F("OFF ")) +
                         F("dev=") + (presenceAddr.isEmpty() ? F("none") : presenceAddr));
        }
        return;
    }
    if (lower.startsWith("cfg"))
    {
        String arg = line.substring(3);
        arg.trim();
        if (arg.startsWith("export"))
        {
            exportConfig();
        }
        else if (arg.startsWith("import"))
        {
            String payload = line.substring(line.indexOf("import") + 6);
            importConfig(payload);
        }
        else
        {
            sendFeedback(F("cfg export | cfg import key=val ..."));
        }
        return;
    }
    if (lower.startsWith("name"))
    {
        String args = line.substring(4);
        args.trim();
        if (args.isEmpty())
        {
            sendFeedback(String(F("[Name] BLE=")) + getBleName() + F(" BT=") + getBtName());
            return;
        }
        int sp = args.indexOf(' ');
        if (sp < 0)
        {
            sendFeedback(F("Usage: name ble <text> | name bt <text>"));
            return;
        }
        String kind = args.substring(0, sp);
        String val = args.substring(sp + 1);
        val.trim();
        if (val.length() < 2 || val.length() > 24)
        {
            sendFeedback(F("Name length 2-24 chars"));
            return;
        }
        if (kind.equalsIgnoreCase(F("ble")))
        {
            setBleName(val);
            saveSettings();
            sendFeedback(String(F("[Name] BLE set to ")) + val);
        }
        else if (kind.equalsIgnoreCase(F("bt")))
        {
            setBtName(val);
            saveSettings();
            sendFeedback(String(F("[Name] BT set to ")) + val);
        }
        else
        {
            sendFeedback(F("Usage: name ble <text> | name bt <text>"));
        }
        return;
    }
    if (lower.startsWith("trust"))
    {
        String args = line.substring(5);
        args.trim();
        if (args.isEmpty() || args.equalsIgnoreCase(F("list")))
        {
            trustListFeedback();
            return;
        }
        // expect: trust <ble|bt> <add|del> <addr>
        int sp1 = args.indexOf(' ');
        int sp2 = sp1 >= 0 ? args.indexOf(' ', sp1 + 1) : -1;
        String kind = sp1 > 0 ? args.substring(0, sp1) : args;
        String action = (sp1 > 0 && sp2 > sp1) ? args.substring(sp1 + 1, sp2) : "";
        String addr = sp2 > 0 ? args.substring(sp2 + 1) : "";
        kind.toLowerCase();
        action.toLowerCase();
        addr.trim();
        bool ok = false;
        if (kind == "ble")
        {
            if (action == "add")
                ok = trustAddBle(addr);
            else if (action == "del" || action == "rem" || action == "rm")
                ok = trustRemoveBle(addr);
        }
        else if (kind == "bt")
        {
            if (action == "add")
                ok = trustAddBt(addr);
            else if (action == "del" || action == "rem" || action == "rm")
                ok = trustRemoveBt(addr);
        }
        if (ok)
        {
            trustListFeedback();
        }
        else
        {
            sendFeedback(F("Usage: trust list | trust ble add <mac> | trust ble del <mac> | trust bt add <mac> | trust bt del <mac>"));
        }
        return;
    }
    if (lower == "factory")
    {
        applyDefaultSettings(-1.0f, true);
        return;
    }
    if (lower.startsWith("profile"))
    {
        String arg = line.substring(7);
        arg.trim();
        if (arg.startsWith("save"))
        {
            int slot = arg.substring(4).toInt();
            if (slot >= 1 && slot <= PROFILE_SLOTS)
            {
                String key = String(PREF_KEY_PROFILE_BASE) + String(slot);
                String cfg = buildProfileString();
                prefs.putString(key.c_str(), cfg);
                sendFeedback(String(F("[Profile] Saved slot ")) + String(slot));
            }
            else
            {
                sendFeedback(F("Usage: profile save <1-3>"));
            }
        }
        else if (arg.startsWith("load"))
        {
            int slot = arg.substring(4).toInt();
            if (slot >= 1 && slot <= PROFILE_SLOTS)
            {
                loadProfileSlot((uint8_t)slot, true);
            }
            else
            {
                sendFeedback(F("Usage: profile load <1-3>"));
            }
        }
        else
        {
            sendFeedback(F("profile save <1-3> | profile load <1-3>"));
        }
        return;
    }
    if (lower == "calibrate")
    {
        calibrateTouchBaseline();
        sendFeedback(F("[Touch] Baseline neu kalibriert."));
        return;
    }

    sendFeedback(F("Unbekanntes Kommando. 'help' tippen."));
}
