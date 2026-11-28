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
#include "lamp_settings.h"
#include "utils.h"
#include "persistence.h"
#include "print.h"
#include "lightSensor.h"
#include "microphone.h"
#include "inputs.h"
#include "ota.h"
#include "presence_ble.h"
#include "presence_radar.h"
#include "quickmode.h"
#include "version.h"
#include "sleepwake.h"
#include "notifications.h"
#include "pattern.h"
#include "demo.h"
#include "gestures.h"
#if ENABLE_BLE_MESH
#include "lamp_mesh.h"
#endif

// SOS shortcut snapshot
bool sosModeActive = false;
float sosPrevBrightness = Settings::DEFAULT_BRIGHTNESS;
size_t sosPrevPattern = 0;
bool sosPrevAutoCycle = false;
bool sosPrevLampOn = false;

// Dispatch table for handleCommand() below - being filled in incrementally,
// one slice of the old if-chain at a time (see memory:
// command-dispatch-table-refactor). Entries are added in the exact order
// their if-check used to appear in the chain: first match wins, same as
// before, so reordering during extraction is exactly the mistake this
// guards against. Anything not yet in the table falls through to the
// original if-chain, still intact below the loop.
enum class CmdMatch
{
    Exact,
    Prefix
};

struct CommandEntry
{
    const char *pattern;
    CmdMatch match;
    void (*handler)(const String &line, const String &lower);
};

static void cmd_Help(const String &line, const String &lower);
static void cmd_List(const String &line, const String &lower);
static void cmd_Quick(const String &line, const String &lower);
static void cmd_GestList(const String &line, const String &lower);
static void cmd_GestDefault(const String &line, const String &lower);
static void cmd_GestClear(const String &line, const String &lower);
static void cmd_GestSet(const String &line, const String &lower);
static void cmd_Mesh(const String &line, const String &lower);
static void cmd_StatusCore(const String &line, const String &lower);
static void cmd_StatusRadar(const String &line, const String &lower);
static void cmd_StatusLight(const String &line, const String &lower);
static void cmd_StatusMusic(const String &line, const String &lower);
static void cmd_StatusPoti(const String &line, const String &lower);
static void cmd_StatusPush(const String &line, const String &lower);
static void cmd_StatusPresence(const String &line, const String &lower);
static void cmd_StatusFilters(const String &line, const String &lower);
static void cmd_StatusIo(const String &line, const String &lower);
static void cmd_StatusMeshStatus(const String &line, const String &lower);
static void cmd_StatusTimers(const String &line, const String &lower);
static void cmd_StatusMisc(const String &line, const String &lower);
static void cmd_Sensors(const String &line, const String &lower);
static void cmd_On(const String &line, const String &lower);
static void cmd_Off(const String &line, const String &lower);
static void cmd_Sync(const String &line, const String &lower);
static void cmd_Toggle(const String &line, const String &lower);
static void cmd_Touch(const String &line, const String &lower);
static void cmd_CalibrateTouch(const String &line, const String &lower);
static void cmd_TouchTune(const String &line, const String &lower);
static void cmd_TouchHold(const String &line, const String &lower);
static void cmd_TouchDimOn(const String &line, const String &lower);
static void cmd_TouchDimOff(const String &line, const String &lower);
static void cmd_TouchDimSpeed(const String &line, const String &lower);
static void cmd_Custom(const String &line, const String &lower);
static void cmd_Next(const String &line, const String &lower);
static void cmd_Prev(const String &line, const String &lower);
static void cmd_Mode(const String &line, const String &lower);
static void cmd_PatScale(const String &line, const String &lower);
static void cmd_PatFade(const String &line, const String &lower);
static void cmd_PatInvert(const String &line, const String &lower);
static void cmd_PatMargin(const String &line, const String &lower);
static void cmd_PatReactive(const String &line, const String &lower);
static void cmd_Filter(const String &line, const String &lower);
static void cmd_PwmCurve(const String &line, const String &lower);
#if ENABLE_EXT_INPUT
static void cmd_Ext(const String &line, const String &lower);
#endif
static void cmd_BriMin(const String &line, const String &lower);
static void cmd_BriMax(const String &line, const String &lower);
static void cmd_Bri(const String &line, const String &lower);
static void cmd_Auto(const String &line, const String &lower);
static void cmd_BtSleep(const String &line, const String &lower);
static void cmd_Demo(const String &line, const String &lower);
static void cmd_Ramp(const String &line, const String &lower);
static void cmd_Notify(const String &line, const String &lower);
static void cmd_Morse(const String &line, const String &lower);
static void cmd_NotifyStop(const String &line, const String &lower);
static void cmd_IdleOff(const String &line, const String &lower);
static void cmd_Light(const String &line, const String &lower);
#if ENABLE_POTI
static void cmd_Poti(const String &line, const String &lower);
#endif
#if ENABLE_PUSH_BUTTON
static void cmd_Push(const String &line, const String &lower);
#endif
#if ENABLE_MUSIC_MODE
static void cmd_Music(const String &line, const String &lower);
#endif
static void cmd_Clap(const String &line, const String &lower);
static void cmd_Wake(const String &line, const String &lower);
static void cmd_Sos(const String &line, const String &lower);
static void cmd_Sleep(const String &line, const String &lower);
static void cmd_PresenceBle(const String &line, const String &lower);
#if ENABLE_RD03
static void cmd_Radar(const String &line, const String &lower);
#endif
static void cmd_Cfg(const String &line, const String &lower);
#if ENABLE_OTA
static void cmd_Ota(const String &line, const String &lower);
#endif
static void cmd_Name(const String &line, const String &lower);
static void cmd_Trust(const String &line, const String &lower);
static void cmd_Factory(const String &line, const String &lower);
static void cmd_Profile(const String &line, const String &lower);
static void cmd_Calibrate(const String &line, const String &lower);

static const CommandEntry COMMAND_TABLE[] = {
    {"help", CmdMatch::Exact, cmd_Help},
    {"list", CmdMatch::Exact, cmd_List},
    {"quick", CmdMatch::Prefix, cmd_Quick},
    {"gest list", CmdMatch::Exact, cmd_GestList},
    {"gest", CmdMatch::Exact, cmd_GestList},
    {"gest default", CmdMatch::Exact, cmd_GestDefault},
    {"gest clear", CmdMatch::Exact, cmd_GestClear},
    {"gest set", CmdMatch::Prefix, cmd_GestSet},
    {"mesh ", CmdMatch::Prefix, cmd_Mesh},
    {"status raw", CmdMatch::Exact, cmd_StatusCore},
    {"status json", CmdMatch::Exact, cmd_StatusCore},
    {"status radar", CmdMatch::Exact, cmd_StatusRadar},
    {"status light", CmdMatch::Exact, cmd_StatusLight},
    {"status music", CmdMatch::Exact, cmd_StatusMusic},
    {"status poti", CmdMatch::Exact, cmd_StatusPoti},
    {"status push", CmdMatch::Exact, cmd_StatusPush},
    {"status presence", CmdMatch::Exact, cmd_StatusPresence},
    {"status filters", CmdMatch::Exact, cmd_StatusFilters},
    {"status io", CmdMatch::Exact, cmd_StatusIo},
    {"status mesh", CmdMatch::Exact, cmd_StatusMeshStatus},
    {"status timers", CmdMatch::Exact, cmd_StatusTimers},
    {"status misc", CmdMatch::Exact, cmd_StatusMisc},
    {"status", CmdMatch::Exact, cmd_StatusCore},
    {"sensors", CmdMatch::Exact, cmd_Sensors},
    {"read sensors", CmdMatch::Exact, cmd_Sensors},
    {"on", CmdMatch::Exact, cmd_On},
    {"off", CmdMatch::Exact, cmd_Off},
    {"sync", CmdMatch::Exact, cmd_Sync},
    {"toggle", CmdMatch::Exact, cmd_Toggle},
    {"touch", CmdMatch::Exact, cmd_Touch},
    {"calibrate touch", CmdMatch::Exact, cmd_CalibrateTouch},
    {"touch tune", CmdMatch::Prefix, cmd_TouchTune},
    {"touch hold", CmdMatch::Prefix, cmd_TouchHold},
    {"touchdim on", CmdMatch::Exact, cmd_TouchDimOn},
    {"touchdim off", CmdMatch::Exact, cmd_TouchDimOff},
    {"touch dim speed", CmdMatch::Prefix, cmd_TouchDimSpeed},
    {"touchdim speed", CmdMatch::Prefix, cmd_TouchDimSpeed},
    {"custom", CmdMatch::Prefix, cmd_Custom},
    {"next", CmdMatch::Exact, cmd_Next},
    {"prev", CmdMatch::Exact, cmd_Prev},
    {"mode", CmdMatch::Prefix, cmd_Mode},
    {"pat scale", CmdMatch::Prefix, cmd_PatScale},
    {"pattern scale", CmdMatch::Prefix, cmd_PatScale},
    {"pat fade", CmdMatch::Prefix, cmd_PatFade},
    {"pattern fade", CmdMatch::Prefix, cmd_PatFade},
    {"pat invert", CmdMatch::Prefix, cmd_PatInvert},
    {"pattern invert", CmdMatch::Prefix, cmd_PatInvert},
    {"pat margin", CmdMatch::Prefix, cmd_PatMargin},
    {"pattern margin", CmdMatch::Prefix, cmd_PatMargin},
    {"pat reactive", CmdMatch::Prefix, cmd_PatReactive},
    {"pattern reactive", CmdMatch::Prefix, cmd_PatReactive},
    {"filter", CmdMatch::Prefix, cmd_Filter},
    {"pwm curve", CmdMatch::Prefix, cmd_PwmCurve},
    {"pwm gamma", CmdMatch::Prefix, cmd_PwmCurve},
#if ENABLE_EXT_INPUT
    {"ext", CmdMatch::Prefix, cmd_Ext},
#endif
    {"bri min", CmdMatch::Prefix, cmd_BriMin},
    {"bri max", CmdMatch::Prefix, cmd_BriMax},
    {"bri", CmdMatch::Prefix, cmd_Bri},
    {"auto", CmdMatch::Prefix, cmd_Auto},
    {"bt sleep", CmdMatch::Prefix, cmd_BtSleep},
    {"demo", CmdMatch::Prefix, cmd_Demo},
    {"ramp", CmdMatch::Prefix, cmd_Ramp},
    // "notify stop" must be checked before the broader "notify" prefix, or
    // it's swallowed by cmd_Notify (a pre-existing bug found while extracting
    // this table - the original if-chain had the exact same ordering mistake,
    // so every "notify stop" ever sent turned the lamp ON with a default
    // blink instead of stopping anything; confirmed on real hardware before
    // this fix). See memory: command-dispatch-table-refactor.
    {"notify stop", CmdMatch::Exact, cmd_NotifyStop},
    {"notify", CmdMatch::Prefix, cmd_Notify},
    {"morse", CmdMatch::Prefix, cmd_Morse},
    {"idleoff", CmdMatch::Prefix, cmd_IdleOff},
    {"light", CmdMatch::Prefix, cmd_Light},
#if ENABLE_POTI
    {"poti", CmdMatch::Prefix, cmd_Poti},
#endif
#if ENABLE_PUSH_BUTTON
    {"push", CmdMatch::Prefix, cmd_Push},
#endif
#if ENABLE_MUSIC_MODE
    {"music", CmdMatch::Prefix, cmd_Music},
#endif
    {"clap", CmdMatch::Prefix, cmd_Clap},
    {"wake", CmdMatch::Prefix, cmd_Wake},
    {"sos", CmdMatch::Prefix, cmd_Sos},
    {"sleep", CmdMatch::Prefix, cmd_Sleep},
    {"presence_ble", CmdMatch::Prefix, cmd_PresenceBle},
#if ENABLE_RD03
    {"radar", CmdMatch::Prefix, cmd_Radar},
#endif
    {"cfg", CmdMatch::Prefix, cmd_Cfg},
#if ENABLE_OTA
    {"ota", CmdMatch::Prefix, cmd_Ota},
#endif
    {"name", CmdMatch::Prefix, cmd_Name},
    {"trust", CmdMatch::Prefix, cmd_Trust},
    {"factory", CmdMatch::Exact, cmd_Factory},
    {"profile", CmdMatch::Prefix, cmd_Profile},
    {"calibrate", CmdMatch::Exact, cmd_Calibrate},
};

static void cmd_Help(const String &line, const String &lower)
{
    printHelp();
}

static void cmd_List(const String &line, const String &lower)
{
    listPatterns();
}

static void cmd_Quick(const String &line, const String &lower)
{
    String args = line.substring(5);
    args.trim();
    if (args.isEmpty() || args.equalsIgnoreCase(F("default")))
    {
        quickMask = 0;
        quickProfileMask = computeDefaultProfileMask();
        sanitizeQuickMask();
        saveSettings();
        sendFeedback(String(F("[Quick] default -> ")) + quickMaskToCsv());
        // Discrete action, safe to push unconditionally - the old
        // [Quick] text line this used to be the only live-update path
        // for is gone now (status protocol consolidation; it was also
        // extracting the wrong substring anyway). See memory:
        // status-parsing-consolidation.
        printStatusCore();
        return;
    }
    uint64_t patternMask = 0;
    uint8_t profileMask = 0;
    if (parseQuickCsv(args, patternMask, profileMask))
    {
        quickMask = patternMask;
        quickProfileMask = profileMask;
        sanitizeQuickMask();
        saveSettings();
        sendFeedback(String(F("[Quick] set -> ")) + quickMaskToCsv());
        printStatusCore();
    }
    else
    {
        sendFeedback(F("Usage: quick <idx,...> | quick default"));
    }
}

static void cmd_GestList(const String &line, const String &lower)
{
    sendFeedback(String(F("[Gest] ")) + gestureBindingsToCsv());
}

static void cmd_GestDefault(const String &line, const String &lower)
{
    resetGestureBindingsToDefault();
    saveSettings();
    sendFeedback(F("[Gest] default (empty)"));
}

static void cmd_GestClear(const String &line, const String &lower)
{
    gestureBindingCount = 0;
    saveSettings();
    sendFeedback(F("[Gest] cleared"));
}

static void cmd_GestSet(const String &line, const String &lower)
{
    String args = line.substring(8);
    args.trim();
    args.toLowerCase();
    if (parseGestureCsv(args))
    {
        saveSettings();
        sendFeedback(String(F("[Gest] set -> ")) + gestureBindingsToCsv());
    }
    else
    {
        sendFeedback(F("[Gest] malformed binding list, nothing changed"));
    }
}

static void cmd_Mesh(const String &line, const String &lower)
{
#if ENABLE_BLE_MESH
    String args = line.substring(5);
    args.trim();
    if (args.startsWith("on ") || args.startsWith("off "))
    {
        bool on = args.startsWith("on ");
        uint16_t addr = (uint16_t)strtoul(args.substring(on ? 3 : 4).c_str(), nullptr, 0);
        meshSendOnOff(addr, on);
        sendFeedback(String(F("[Mesh] ")) + (on ? F("on") : F("off")) + F(" -> 0x") + String(addr, HEX));
    }
    else if (args.startsWith("bri "))
    {
        String rest = args.substring(4);
        rest.trim();
        int sp = rest.indexOf(' ');
        if (sp > 0)
        {
            uint16_t addr = (uint16_t)strtoul(rest.substring(0, sp).c_str(), nullptr, 0);
            float pct = rest.substring(sp + 1).toFloat();
            meshSendLightness(addr, pct);
            sendFeedback(String(F("[Mesh] bri -> 0x")) + String(addr, HEX) + F(" ") + fmtF(pct, 1) + F("%"));
        }
        else
        {
            sendFeedback(F("Usage: mesh bri <addr> <pct>"));
        }
    }
#if ENABLE_BLE_MESH_PROVISIONER
    else if (args.startsWith("network create"))
    {
        String rest = args.substring(14);
        rest.trim();
        uint8_t netKey[16], appKey[16];
        bool haveNet = false, haveApp = false;
        if (!rest.isEmpty())
        {
            int sp = rest.indexOf(' ');
            String netHex = sp > 0 ? rest.substring(0, sp) : rest;
            String appHex = sp > 0 ? rest.substring(sp + 1) : String();
            appHex.trim();
            if (netHex.length() == 32 && appHex.length() == 32)
            {
                for (uint8_t i = 0; i < 16; ++i)
                {
                    netKey[i] = (uint8_t)strtoul(netHex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
                    appKey[i] = (uint8_t)strtoul(appHex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
                }
                haveNet = haveApp = true;
            }
            else
            {
                sendFeedback(F("Usage: mesh network create [<netkey32hex> <appkey32hex>]"));
                return;
            }
        }
        meshNetworkCreate(haveNet ? netKey : nullptr, haveApp ? appKey : nullptr);
    }
    else if (args.startsWith("provision scan"))
    {
        String rest = args.substring(14);
        rest.trim();
        uint32_t secs = rest.isEmpty() ? 20 : (uint32_t)rest.toInt();
        meshProvisionScanStart(secs == 0 ? 20 : secs);
    }
    else if (args == "provision stop")
    {
        meshProvisionScanStop();
    }
    else if (args == "provision list")
    {
        sendFeedback(meshProvisionListText());
    }
    else if (args.startsWith("provision add "))
    {
        String rest = args.substring(14);
        rest.trim();
        int sp = rest.indexOf(' ');
        uint8_t idx = (uint8_t)(sp > 0 ? rest.substring(0, sp).toInt() : rest.toInt());
        uint16_t group = sp > 0 ? (uint16_t)strtoul(rest.substring(sp + 1).c_str(), nullptr, 0) : 0;
        meshProvisionAdd(idx, group);
    }
    else if (args.startsWith("provision remove "))
    {
        uint16_t addr = (uint16_t)strtoul(args.substring(17).c_str(), nullptr, 0);
        meshProvisionRemove(addr);
    }
    else if (args == "nodes")
    {
        sendFeedback(meshKnownNodesText());
    }
    else if (args == "keys")
    {
        sendFeedback(meshKeysText());
    }
    else if (args == "reset")
    {
        sendFeedback(meshResetIdentity());
    }
    else if (args == "export nrf")
    {
        sendFeedback(meshExportNrfJson());
    }
    else if (args.startsWith("rename "))
    {
        String rest = args.substring(7);
        // Left-trim only (not rest.trim()) - a full trim would eat the
        // one separating space when the name is deliberately empty
        // (clearing a node back to its fallback "Lamp 0x.." label),
        // leaving no space for indexOf(' ') to find at all. The name
        // itself still gets trimmed, just inside meshRenameNode() via
        // renameKnownNodeByAddr()'s own sanitization, not here.
        while (rest.length() > 0 && rest[0] == ' ')
            rest.remove(0, 1);
        int sp = rest.indexOf(' ');
        if (sp > 0)
        {
            uint16_t addr = (uint16_t)strtoul(rest.substring(0, sp).c_str(), nullptr, 0);
            meshRenameNode(addr, rest.substring(sp + 1));
        }
        else
        {
            sendFeedback(F("Usage: mesh rename <addr> <name>"));
        }
    }
#endif
    else
    {
        sendFeedback(F("Usage: mesh on|off <addr> | mesh bri <addr> <pct>"
#if ENABLE_BLE_MESH_PROVISIONER
                       " | mesh network create [<net> <app>] | mesh provision scan|stop|list|add|remove"
                       " | mesh nodes | mesh rename <addr> <name> | mesh keys | mesh export nrf"
#endif
                       ));
    }
#else
    sendFeedback(F("[Mesh] disabled at build"));
#endif
}

static void cmd_StatusCore(const String &line, const String &lower)
{
    printStatusCore();
}

static void cmd_StatusRadar(const String &line, const String &lower)
{
    printRadarStructured();
}

static void cmd_StatusLight(const String &line, const String &lower)
{
    printLightStructured();
}

static void cmd_StatusMusic(const String &line, const String &lower)
{
    printMusicStructured();
}

static void cmd_StatusPoti(const String &line, const String &lower)
{
    printPotiStructured();
}

static void cmd_StatusPush(const String &line, const String &lower)
{
    printPushStructured();
}

static void cmd_StatusPresence(const String &line, const String &lower)
{
    printPresenceStructured();
}

static void cmd_StatusFilters(const String &line, const String &lower)
{
    printFiltersStructured();
}

static void cmd_StatusIo(const String &line, const String &lower)
{
    printIoStructured();
}

static void cmd_StatusMeshStatus(const String &line, const String &lower)
{
    printMeshStatusStructured();
}

static void cmd_StatusTimers(const String &line, const String &lower)
{
    printTimersStructured();
}

static void cmd_StatusMisc(const String &line, const String &lower)
{
    printMiscStructured();
}

static void cmd_Sensors(const String &line, const String &lower)
{
    printSensorsStructured();
}

static void cmd_On(const String &line, const String &lower)
{
    setLampEnabled(true, "cmd on");
    saveSettings();
    printStatusCore();
}

static void cmd_Off(const String &line, const String &lower)
{
    setLampEnabled(false, "cmd off");
    saveSettings();
    printStatusCore();
}

static void cmd_Sync(const String &line, const String &lower)
{
    syncLampToSwitch();
    saveSettings();
    printStatusCore();
}

static void cmd_Toggle(const String &line, const String &lower)
{
    setLampEnabled(!lampEnabled, "cmd toggle");
    saveSettings();
    printStatusCore();
}

static void cmd_Touch(const String &line, const String &lower)
{
#if ENABLE_TOUCH_DIM
    printTouchDebug();
#else
    sendFeedback(F("[Touch] disabled in build"));
#endif
}

static void cmd_CalibrateTouch(const String &line, const String &lower)
{
#if ENABLE_TOUCH_DIM
    calibrateTouchGuided();
#else
    sendFeedback(F("[Touch] disabled in build"));
#endif
}

static void cmd_TouchTune(const String &line, const String &lower)
{
#if ENABLE_TOUCH_DIM
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
#else
    sendFeedback(F("[Touch] disabled in build"));
#endif
}

static void cmd_TouchHold(const String &line, const String &lower)
{
#if ENABLE_TOUCH_DIM
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
#else
    sendFeedback(F("[Touch] disabled in build"));
#endif
}

static void cmd_TouchDimOn(const String &line, const String &lower)
{
#if ENABLE_TOUCH_DIM
    touchDimEnabled = true;
    saveSettings();
    sendFeedback(F("[TouchDim] Enabled"));
#else
    sendFeedback(F("[Touch] disabled in build"));
#endif
}

static void cmd_TouchDimOff(const String &line, const String &lower)
{
#if ENABLE_TOUCH_DIM
    touchDimEnabled = false;
    saveSettings();
    sendFeedback(F("[TouchDim] Disabled"));
#else
    sendFeedback(F("[Touch] disabled in build"));
#endif
}

static void cmd_TouchDimSpeed(const String &line, const String &lower)
{
#if ENABLE_TOUCH_DIM
    float v = line.substring(line.indexOf("speed") + 5).toFloat();
    if (v < 0.001f)
        v = 0.001f;
    if (v > 0.05f)
        v = 0.05f;
    touchDimStep = v;
    saveSettings();
    sendFeedback(String(F("[TouchDim] speed=")) + String(v, 3));
#else
    sendFeedback(F("[Touch] disabled in build"));
#endif
}

static void cmd_Custom(const String &line, const String &lower)
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
}

static void cmd_Next(const String &line, const String &lower)
{
    size_t next = (currentPattern + 1) % PATTERN_COUNT;
    setPattern(next, true, true);
}

static void cmd_Prev(const String &line, const String &lower)
{
    size_t prev = (currentPattern + PATTERN_COUNT - 1) % PATTERN_COUNT;
    setPattern(prev, true, true);
}

static void cmd_Mode(const String &line, const String &lower)
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
}

static void cmd_PatScale(const String &line, const String &lower)
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
}

static void cmd_PatFade(const String &line, const String &lower)
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
}

static void cmd_PatInvert(const String &line, const String &lower)
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
}

static void cmd_PatMargin(const String &line, const String &lower)
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
}

static void cmd_PatReactive(const String &line, const String &lower)
{
    int pos = lower.indexOf("reactive");
    String arg = line.substring(pos + 8);
    arg.trim();
    if (arg.isEmpty())
    {
        sendFeedback(String(F("[Pattern] Reactive[")) + PATTERNS[currentPattern].name + F("]=") +
                     (isPatternReactive(currentPattern) ? F("1") : F("0")) +
                     F(" list=") + patternReactiveMaskToCsv());
    }
    else if (arg.equalsIgnoreCase("on"))
    {
        setPatternReactive(currentPattern, true);
        saveSettings();
        sendFeedback(String(F("[Pattern] ")) + PATTERNS[currentPattern].name + F(" reactive ON"));
    }
    else if (arg.equalsIgnoreCase("off"))
    {
        setPatternReactive(currentPattern, false);
        saveSettings();
        sendFeedback(String(F("[Pattern] ")) + PATTERNS[currentPattern].name + F(" reactive OFF"));
    }
    else if (arg.equalsIgnoreCase("list"))
    {
        sendFeedback(String(F("[Pattern] Reactive list=")) + patternReactiveMaskToCsv());
    }
    else if (arg.equalsIgnoreCase("clear"))
    {
        patternReactiveMask = 0;
        saveSettings();
        sendFeedback(F("[Pattern] Reactive cleared"));
    }
    else
    {
        int sp = arg.indexOf(' ');
        bool ok = false;
        if (sp > 0)
        {
            int idx = arg.substring(0, sp).toInt();
            String sub = arg.substring(sp + 1);
            sub.trim();
            bool v;
            if (idx >= 1 && idx <= (int)PATTERN_COUNT && parseBool(sub, v))
            {
                setPatternReactive((size_t)(idx - 1), v);
                saveSettings();
                sendFeedback(String(F("[Pattern] ")) + PATTERNS[idx - 1].name + F(" reactive ") + (v ? F("ON") : F("OFF")));
                ok = true;
            }
        }
        if (!ok)
            sendFeedback(F("Usage: pat reactive on|off|list|clear|<n> on|off"));
    }
}

static void cmd_Filter(const String &line, const String &lower)
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
                    // `att` is the 3rd of 4 values - bound its end at `p4` only when a
                    // 4th value (`rel`) actually follows; otherwise take it to the end
                    // of the string, same as `rel` always does below. Previously this
                    // required `p4 > 0` just to read `att` at all, so typing exactly 3
                    // numbers ("thr ratio att", no `rel`) silently dropped `att` too and
                    // left it at its default.
                    ratio = rest.substring(p2 + 1, p3).toFloat();
                    att = rest.substring(p3 + 1, p4 > 0 ? (size_t)p4 : rest.length()).toInt();
                    if (p4 > 0)
                        rel = rest.substring(p4 + 1).toInt();
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
    // Deliberately no printFiltersStructured() here, unlike most other
    // groups' sub-command blocks: every one of these sub-types is also
    // driven by a Knob on FilterSection.tsx that fires this exact same
    // command shape on every drag tick, so a structured push-back here
    // would flood the link during a drag (see memory: status protocol
    // redesign). FilterSection.tsx's checkbox-driven toggleFilter()
    // fetches "status filters" explicitly instead.
}

static void cmd_PwmCurve(const String &line, const String &lower)
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
}

#if ENABLE_EXT_INPUT
static void cmd_Ext(const String &line, const String &lower)
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
            // arduino-esp32 3.x: pinMode(INPUT) before analogSetPinAttenuation() -
            // see the same reordering + rationale in main.cpp's setup().
            pinMode(Settings::EXT_INPUT_PIN, INPUT);
            analogSetPinAttenuation(Settings::EXT_INPUT_PIN, ADC_11db);
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
}
#endif

static void cmd_BriMin(const String &line, const String &lower)
{
    float v = line.substring(7).toFloat();
    v = clamp01(v);
    briMinUser = v;
    if (briMaxUser < briMinUser)
        briMaxUser = briMinUser;
    saveSettings();
    sendFeedback(String(F("[Bri] min=")) + String(v, 3));
}

static void cmd_BriMax(const String &line, const String &lower)
{
    float v = line.substring(7).toFloat();
    v = clamp01(v);
    if (v < briMinUser)
        v = briMinUser;
    briMaxUser = v;
    saveSettings();
    sendFeedback(String(F("[Bri] max=")) + String(v, 3));
}

static void cmd_Bri(const String &line, const String &lower)
{
    float value = line.substring(3).toFloat();
    if (value < 0.0f)
        value = 0.0f;
    if (value > 100.0f)
        value = 100.0f;
    setBrightnessPercent(value, true);
}

static void cmd_Auto(const String &line, const String &lower)
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
    printStatusCore();
}

static void cmd_BtSleep(const String &line, const String &lower)
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
}

static void cmd_Demo(const String &line, const String &lower)
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
}

static void cmd_Ramp(const String &line, const String &lower)
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
        // `arg.toFloat()` returns 0.0 (not NaN) for a non-numeric/empty string, so an
        // `isnan()` check here never actually catches "no value given" - this used to
        // silently zero and persist rampAmbientFactor on a bare "ramp ambient" query.
        // Checking the raw substring for emptiness first is what actually works.
        int sp = arg.indexOf(' ');
        String valueStr = (sp >= 0) ? arg.substring(sp + 1) : String();
        valueStr.trim();
        if (!valueStr.isEmpty())
        {
            float v = valueStr.toFloat();
            if (v < 0.0f)
                v = 0.0f;
            if (v > 5.0f)
                v = 5.0f;
            rampAmbientFactor = v;
            saveSettings();
        }
        sendFeedback(String(F("[Ramp] ambient factor=")) + String(rampAmbientFactor, 2));
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
}

static void cmd_Notify(const String &line, const String &lower)
{
    std::vector<uint32_t> seq;
    notifyFadeMs = 0;
    if (lower.startsWith("notify min"))
    {
        float v = line.substring(10).toFloat();
        v = clamp01(v / 100.0f);
        if (v < 0.0f)
            v = 0.0f;
        if (v > 1.0f)
            v = 1.0f;
        notifyMinBrightness = v;
        saveSettings();
        sendFeedback(String(F("[Notify] min_bri=")) + String(v * 100.0f, 1) + F("%"));
        return;
    }
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
}

static void cmd_Morse(const String &line, const String &lower)
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
}

static void cmd_NotifyStop(const String &line, const String &lower)
{
    notifyActive = false;
    if (!notifyPrevLampOn)
        forceLampOff("notify stop");
    sendFeedback(F("[Notify] stopped"));
}

static void cmd_IdleOff(const String &line, const String &lower)
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
}

static void cmd_Light(const String &line, const String &lower)
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
        // A bare "light clamp" (no numbers) used to fall through to
        // mn=0.0 (toFloat() on an empty string, not an error value) and
        // mx=lightClampMax, which almost always satisfies mn<mx - so it
        // silently reset lightClampMin to 0 instead of just reporting
        // the current clamp. Require both numbers before touching
        // anything.
        if (restClamp.isEmpty())
        {
            sendFeedback(String(F("[Light] clamp ")) + String(lightClampMin, 2) + F("..") + String(lightClampMax, 2));
        }
        else
        {
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
        // No printLightStructured() here: clamp is driven by a
        // RangeSliderRow on LightCard.tsx with no drag throttling at
        // all, unlike the other light sub-commands - pushing a
        // structured update on every drag-move event would flood the
        // link. See memory: status protocol redesign.
        return;
    }
    else
    {
        sendFeedback(String(F("[Light] raw=")) + String((int)lightFiltered) + F(" en=") + (lightSensorEnabled ? F("1") : F("0")));
    }
#else
    sendFeedback(F("[Light] Sensor disabled at build (ENABLE_LIGHT_SENSOR=0)"));
#endif
    // Real structured update (LIGHT|) for the Licht page - see memory:
    // status protocol redesign.
    printLightStructured();
}

#if ENABLE_POTI
static void cmd_Poti(const String &line, const String &lower)
{
    String arg = line.substring(4);
    arg.trim();
    arg.toLowerCase();
    if (arg == "on")
    {
        potiEnabled = true;
        saveSettings();
        sendFeedback(F("[Poti] Enabled"));
        printPotiStructured();
    }
    else if (arg == "off")
    {
        potiEnabled = false;
        saveSettings();
        sendFeedback(F("[Poti] Disabled"));
        printPotiStructured();
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
    else if (arg.startsWith("calib"))
    {
        float minV = 0.0f, maxV = 1.0f;
        int sep = arg.indexOf(' ');
        if (sep > 0)
        {
            String rest = arg.substring(sep + 1);
            rest.trim();
            int sep2 = rest.indexOf(' ');
            if (sep2 > 0)
            {
                minV = rest.substring(0, sep2).toFloat();
                maxV = rest.substring(sep2 + 1).toFloat();
            }
        }
        if (minV >= 0.0f && maxV > minV && maxV <= 1.5f)
        {
            potiCalibMin = minV;
            potiCalibMax = maxV;
            saveSettings();
            sendFeedback(String(F("[Poti] calib min=")) + String(minV, 3) + F(" max=") + String(maxV, 3));
        }
        else
        {
            sendFeedback(F("Usage: poti calib <min 0..1> <max 0..1.5>"));
        }
    }
    else if (arg.startsWith("invert"))
    {
        String v = arg.substring(6);
        v.trim();
        bool val = (v == "1" || v == "on" || v == "true");
        potiInvert = val;
        saveSettings();
        sendFeedback(String(F("[Poti] invert=")) + (potiInvert ? F("ON") : F("OFF")));
    }
    else if (arg.startsWith("mode"))
    {
        String v = arg.substring(4);
        v.trim();
        v.toLowerCase();
        if (v == "bri" || v == "brightness")
        {
            potiRole = POTI_ROLE_BRIGHTNESS;
            saveSettings();
            sendFeedback(F("[Poti] mode=brightness"));
        }
        else if (v == "scan" || v == "quick")
        {
            potiRole = POTI_ROLE_MODE_SCAN;
            saveSettings();
            sendFeedback(F("[Poti] mode=scan"));
        }
        else
        {
            sendFeedback(F("Usage: poti mode brightness|scan"));
        }
    }
    else if (arg.startsWith("target"))
    {
        // Inert today - there is only one output channel (see
        // MULTI_CHANNEL_FEATURE_PLAN.md). Accepted and persisted anyway
        // so an existing config already has a sane value once a
        // multi-channel build actually reads this.
        String v = arg.substring(6);
        v.trim();
        v.toLowerCase();
        if (v == "all")
        {
            potiTarget = CHANNEL_TARGET_ALL;
            saveSettings();
            sendFeedback(F("[Poti] target=all (not yet used - single-channel build)"));
        }
        else
        {
            // String::toInt() returns 0 for non-numeric input (e.g. a
            // typo), which would otherwise be silently accepted as
            // "target=0" instead of rejected - check the input is
            // actually all digits first.
            bool numeric = v.length() > 0;
            for (size_t i = 0; i < v.length() && numeric; ++i)
                numeric = isDigit(v[i]);
            long n = numeric ? v.toInt() : -1;
            if (numeric && n >= 0 && n < CHANNEL_TARGET_ALL)
            {
                potiTarget = (uint8_t)n;
                saveSettings();
                sendFeedback(String(F("[Poti] target=")) + String(n) + F(" (not yet used - single-channel build)"));
            }
            else
            {
                sendFeedback(F("Usage: poti target <channel 0-254>|all"));
            }
        }
    }
    else
    {
        sendFeedback(String(F("[Poti] ")) + (potiEnabled ? F("ON ") : F("OFF ")) + F("a=") + String(potiAlpha, 2) +
                     F(" d=") + String(potiDeltaMin, 3) + F(" off=") + String(potiOffThreshold, 3) +
                     F(" smpl=") + String(potiSampleMs) + F("ms") +
                     F(" min=") + String(potiCalibMin, 3) + F(" max=") + String(potiCalibMax, 3) +
                     F(" inv=") + (potiInvert ? F("1") : F("0")) +
                     F(" mode=") + (potiRole == POTI_ROLE_MODE_SCAN ? F("scan") : F("brightness")) +
                     F(" target=") + (potiTarget == CHANNEL_TARGET_ALL ? F("all") : String(potiTarget)));
    }
    // Deliberately no blanket printPotiStructured() here (only in the
    // on/off branches above) - alpha/delta/off/sample are driven by
    // SliderRow/useSyncedValue controls on PotiCard.tsx that fire this
    // same command shape on every drag tick; a push-back here would
    // flood the link during a drag. See memory: status protocol redesign.
}
#endif

#if ENABLE_PUSH_BUTTON
static void cmd_Push(const String &line, const String &lower)
{
    String arg = line.substring(4);
    arg.trim();
    arg.toLowerCase();
    if (arg == "on")
    {
        pushEnabled = true;
        saveSettings();
        sendFeedback(F("[Push] Enabled"));
        printPushStructured();
    }
    else if (arg == "off")
    {
        pushEnabled = false;
        saveSettings();
        sendFeedback(F("[Push] Disabled"));
        printPushStructured();
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
    // Deliberately no blanket printPushStructured() here (only in the
    // on/off branches above) - debounce/double/hold/step_ms/step are
    // driven by SliderRow/useSyncedValue controls on PushButtonCard.tsx
    // that fire this same command shape on every drag tick. See memory:
    // status protocol redesign.
}
#endif

#if ENABLE_MUSIC_MODE
static void cmd_Music(const String &line, const String &lower)
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
    // Real structured update (MUSIC|) for the Musik page - see memory:
    // status protocol redesign.
    printMusicStructured();
}
#endif

static void cmd_Clap(const String &line, const String &lower)
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
    printMusicStructured();
}

static void cmd_Wake(const String &line, const String &lower)
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
}

static void cmd_Sos(const String &line, const String &lower)
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
}

static void cmd_Sleep(const String &line, const String &lower)
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
}

static void cmd_PresenceBle(const String &line, const String &lower)
{
    String arg = line.substring(12);
    arg.trim();
    arg.toLowerCase();
    auto sendStatus = []() {
        sendFeedback(String(F("[PresenceBLE] ")) + (presenceBleEnabled ? F("ON") : F("OFF")) +
                     F(" devices=") + (presenceBleListCsv().length() ? presenceBleListCsv() : String(F("none"))) +
                     F(" thr=") + String(presenceBleRssiThreshold) + F("dBm on=") + (presenceBleAutoOn ? F("1") : F("0")) +
                     F(" off=") + (presenceBleAutoOff ? F("1") : F("0")) + F(" grace=") + String(presenceBleGraceMs) + F("ms") +
                     F(" hwoverride=") + (presenceBleAlwaysOverride ? F("1") : F("0")));
    };
    if (arg == "on")
    {
        presenceBleEnabled = true;
        saveSettings();
        sendFeedback(F("[PresenceBLE] Enabled"));
    }
    else if (arg == "off")
    {
        presenceBleEnabled = false;
        saveSettings();
        sendFeedback(F("[PresenceBLE] Disabled"));
    }
    else if (arg.startsWith("set"))
    {
        String addr = line.substring(16);
        addr.trim();
        if (addr.isEmpty() || addr == "me")
        {
            if (lastBleAddr.length() > 0)
            {
                presenceBleClearDevices();
                presenceBleAddDevice(lastBleAddr);
                saveSettings();
                sendFeedback(String(F("[PresenceBLE] Set to connected device ")) + lastBleAddr);
            }
            else
            {
                sendFeedback(F("[PresenceBLE] Kein aktives BLE-Geraet gefunden."));
            }
        }
        else if (addr.length() >= 11)
        {
            presenceBleClearDevices();
            presenceBleAddDevice(addr);
            saveSettings();
            sendFeedback(String(F("[PresenceBLE] Set to ")) + addr);
        }
        else
        {
            sendFeedback(F("Usage: presence_ble set <MAC>"));
        }
    }
    else if (arg.startsWith("add"))
    {
        String addr = line.substring(line.indexOf("add") + 3);
        addr.trim();
        if (addr == "me" && lastBleAddr.length() > 0)
        {
            presenceBleAddDevice(lastBleAddr);
            saveSettings();
            sendFeedback(String(F("[PresenceBLE] Added connected ")) + lastBleAddr);
        }
        else if (addr.length() >= 11)
        {
            if (presenceBleAddDevice(addr))
                sendFeedback(String(F("[PresenceBLE] Added ")) + addr);
            else
                sendFeedback(F("[PresenceBLE] Already on list"));
            saveSettings();
        }
        else
        {
            sendFeedback(F("Usage: presence_ble add <MAC>"));
        }
    }
    else if (arg.startsWith("del"))
    {
        String addr = line.substring(line.indexOf("del") + 3);
        addr.trim();
        if (presenceBleRemoveDevice(addr))
        {
            sendFeedback(String(F("[PresenceBLE] Removed ")) + addr);
            saveSettings();
        }
        else
        {
            sendFeedback(F("[PresenceBLE] Not found"));
        }
    }
    else if (arg == "clear")
    {
        presenceBleClearDevices();
        saveSettings();
        sendFeedback(F("[PresenceBLE] Cleared"));
    }
    else if (arg.startsWith("grace"))
    {
        // Unlike every sibling setting in this command group, this had no
        // emptiness/bounds check at all - a bare "presence_ble grace" (no
        // number), e.g. from the web UI's field being cleared on blur,
        // silently persisted 0ms (toInt() on an empty string), killing the
        // "still recently seen" bridge between scans this value backs.
        String valueStr = line.substring(line.indexOf("grace") + 5);
        valueStr.trim();
        if (valueStr.isEmpty())
        {
            sendFeedback(String(F("[PresenceBLE] Grace ")) + String(presenceBleGraceMs) + F(" ms"));
        }
        else
        {
            long v = valueStr.toInt();
            if (v < 500)
                v = 500;
            if (v > 600000)
                v = 600000;
            presenceBleGraceMs = (uint32_t)v;
            saveSettings();
            sendFeedback(String(F("[PresenceBLE] Grace ")) + String(v) + F(" ms"));
        }
    }
    else if (arg.startsWith("thr"))
    {
        int v = line.substring(line.indexOf("thr") + 3).toInt();
        if (v < -120)
            v = -120;
        if (v > -5)
            v = -5;
        presenceBleRssiThreshold = v;
        saveSettings();
        sendFeedback(String(F("[PresenceBLE] RSSI >= ")) + String(v) + F(" dBm"));
    }
    else if (arg.startsWith("auto on"))
    {
        String vstr = arg.substring(7);
        vstr.trim();
        bool v;
        if (parseBool(vstr, v))
        {
            presenceBleAutoOn = v;
            saveSettings();
            sendFeedback(String(F("[PresenceBLE] Auto-ON ")) + (v ? F("ON") : F("OFF")));
        }
    }
    else if (arg.startsWith("auto off"))
    {
        String vstr = arg.substring(8);
        vstr.trim();
        bool v;
        if (parseBool(vstr, v))
        {
            presenceBleAutoOff = v;
            saveSettings();
            sendFeedback(String(F("[PresenceBLE] Auto-OFF ")) + (v ? F("ON") : F("OFF")));
        }
    }
    else if (arg.startsWith("on "))
    {
        bool v;
        if (parseBool(arg.substring(3), v))
        {
            presenceBleAutoOn = v;
            saveSettings();
            sendFeedback(String(F("[PresenceBLE] Auto-ON ")) + (v ? F("ON") : F("OFF")));
        }
    }
    else if (arg.startsWith("off "))
    {
        bool v;
        if (parseBool(arg.substring(4), v))
        {
            presenceBleAutoOff = v;
            saveSettings();
            sendFeedback(String(F("[PresenceBLE] Auto-OFF ")) + (v ? F("ON") : F("OFF")));
        }
    }
    else if (arg.startsWith("hwoverride"))
    {
        String sub = arg.substring(10);
        sub.trim();
        bool v;
        if (parseBool(sub, v))
        {
            presenceBleAlwaysOverride = v;
            saveSettings();
            sendFeedback(String(F("[PresenceBLE] HW-Override ")) + (v ? F("ON") : F("OFF")));
        }
        else
        {
            sendFeedback(F("Usage: presence_ble hwoverride on|off"));
        }
    }
    else if (arg == "list")
    {
        sendStatus();
    }
    else
    {
        sendStatus();
    }
    // Real structured update (PRESENCE|) for the Anwesenheit page - see
    // memory: status protocol redesign.
    printPresenceStructured();
}

#if ENABLE_RD03
static void cmd_Radar(const String &line, const String &lower)
{
    String arg = line.substring(5);
    arg.trim();
    arg.toLowerCase();
    auto sendStatus = []() {
        sendFeedback(String(F("[Radar] ")) + (radarEnabled ? F("ON") : F("OFF")) +
                     F(" present=") + (radarPresent ? F("1") : F("0")) +
                     F(" dist=") + String(radarDistanceCm, 1) + F("cm speed=") + String(radarSpeedCmS, 1) +
                     F("cm/s targets=") + String(radarTargetCount) +
                     F(" dim=") + (radarDimEnabled ? F("1") : F("0")) +
                     F(" near=") + String(radarDimNearCm, 1) + F(" far=") + String(radarDimFarCm, 1) +
                     F(" motion=") + (radarMotionOnEnabled ? F("1") : F("0")) +
                     F(" thr=") + String(radarMotionSpeedThreshold, 1) + F(" hold=") + String(radarMotionHoldMs) +
                     F(" offdist=") + (radarOffDistanceEnabled ? F("1") : F("0")) +
                     F(" off_cm=") + String(radarOffDistanceCm, 1) + F(" off_grace=") + String(radarOffGraceMs) +
                     F(" hwoverride=") + (radarHwOverride ? F("1") : F("0")) +
                     F(" velocity=") + String(radarVelocityFactor, 2));
    };
    if (arg == "on")
    {
        radarEnabled = true;
        saveSettings();
        sendFeedback(F("[Radar] Enabled"));
    }
    else if (arg == "off")
    {
        radarEnabled = false;
        saveSettings();
        sendFeedback(F("[Radar] Disabled"));
    }
    else if (arg.startsWith("dim"))
    {
        String sub = arg.substring(3);
        sub.trim();
        if (sub == "on")
        {
            radarDimEnabled = true;
            saveSettings();
            sendFeedback(F("[Radar] Dim Enabled"));
        }
        else if (sub == "off")
        {
            radarDimEnabled = false;
            saveSettings();
            sendFeedback(F("[Radar] Dim Disabled"));
        }
        else if (sub.startsWith("near"))
        {
            radarDimNearCm = sub.substring(4).toFloat();
            saveSettings();
            sendFeedback(String(F("[Radar] Dim near=")) + String(radarDimNearCm, 1) + F("cm"));
        }
        else if (sub.startsWith("far"))
        {
            radarDimFarCm = sub.substring(3).toFloat();
            saveSettings();
            sendFeedback(String(F("[Radar] Dim far=")) + String(radarDimFarCm, 1) + F("cm"));
        }
        else
        {
            sendFeedback(F("Usage: radar dim on|off|near <cm>|far <cm>"));
        }
    }
    else if (arg.startsWith("motion"))
    {
        String sub = arg.substring(6);
        sub.trim();
        if (sub == "on")
        {
            radarMotionOnEnabled = true;
            saveSettings();
            sendFeedback(F("[Radar] Motion Enabled"));
        }
        else if (sub == "off")
        {
            radarMotionOnEnabled = false;
            saveSettings();
            sendFeedback(F("[Radar] Motion Disabled"));
        }
        else if (sub.startsWith("thr"))
        {
            radarMotionSpeedThreshold = sub.substring(3).toFloat();
            saveSettings();
            sendFeedback(String(F("[Radar] Motion thr=")) + String(radarMotionSpeedThreshold, 1) + F("cm/s"));
        }
        else if (sub.startsWith("hold"))
        {
            long v = sub.substring(4).toInt();
            radarMotionHoldMs = v < 0 ? 0 : (uint32_t)v;
            saveSettings();
            sendFeedback(String(F("[Radar] Motion hold=")) + String(radarMotionHoldMs) + F("ms"));
        }
        else
        {
            sendFeedback(F("Usage: radar motion on|off|thr <cm/s>|hold <ms>"));
        }
    }
    else if (arg.startsWith("offdist"))
    {
        String sub = arg.substring(7);
        sub.trim();
        if (sub == "on")
        {
            radarOffDistanceEnabled = true;
            saveSettings();
            sendFeedback(F("[Radar] Off-distance Enabled"));
        }
        else if (sub == "off")
        {
            radarOffDistanceEnabled = false;
            saveSettings();
            sendFeedback(F("[Radar] Off-distance Disabled"));
        }
        else if (sub.startsWith("cm"))
        {
            radarOffDistanceCm = sub.substring(2).toFloat();
            saveSettings();
            sendFeedback(String(F("[Radar] Off-distance=")) + String(radarOffDistanceCm, 1) + F("cm"));
        }
        else if (sub.startsWith("grace"))
        {
            long v = sub.substring(5).toInt();
            radarOffGraceMs = v < 0 ? 0 : (uint32_t)v;
            saveSettings();
            sendFeedback(String(F("[Radar] Off-distance grace=")) + String(radarOffGraceMs) + F("ms"));
        }
        else
        {
            sendFeedback(F("Usage: radar offdist on|off|cm <cm>|grace <ms>"));
        }
    }
    else if (arg.startsWith("debug"))
    {
        String sub = arg.substring(5);
        sub.trim();
        bool v;
        if (parseBool(sub, v))
        {
            radarDebugRaw = v;
            sendFeedback(String(F("[Radar] Debug raw ")) + (v ? F("ON") : F("OFF")));
        }
        else
        {
            sendFeedback(F("Usage: radar debug on|off"));
        }
    }
    else if (arg.startsWith("hwoverride"))
    {
        String sub = arg.substring(10);
        sub.trim();
        bool v;
        if (parseBool(sub, v))
        {
            radarHwOverride = v;
            saveSettings();
            sendFeedback(String(F("[Radar] HW-Override ")) + (v ? F("ON") : F("OFF")));
        }
        else
        {
            sendFeedback(F("Usage: radar hwoverride on|off"));
        }
    }
    else if (arg.startsWith("velocity"))
    {
        String sub = arg.substring(8);
        sub.trim();
        if (sub.length() > 0)
        {
            float v = sub.toFloat();
            if (v < 0.0f)
                v = 0.0f;
            if (v > 10.0f)
                v = 10.0f;
            radarVelocityFactor = v;
            saveSettings();
            sendFeedback(String(F("[Radar] Velocity factor=")) + String(radarVelocityFactor, 2) +
                         (radarVelocityFactor == 1.0f ? F(" (disabled)") : F("")));
        }
        else
        {
            sendFeedback(F("Usage: radar velocity <factor> (1 = disabled, >1 brighter, <1 dimmer with motion)"));
        }
    }
    else
    {
        sendStatus();
    }
    // Real structured update (RADAR|) for the Radar page - see memory:
    // status protocol redesign.
    printRadarStructured();
}
#endif

static void cmd_Cfg(const String &line, const String &lower)
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
}

#if ENABLE_OTA
static void cmd_Ota(const String &line, const String &lower)
{
    String arg = line.substring(3);
    arg.trim();
    String argLower = arg;
    argLower.toLowerCase();
    if (argLower.startsWith("begin"))
    {
        String rest = arg.substring(5);
        rest.trim();
        int sp1 = rest.indexOf(' ');
        int sp2 = sp1 >= 0 ? rest.indexOf(' ', sp1 + 1) : -1;
        if (sp1 > 0 && sp2 > sp1)
        {
            size_t size = (size_t)rest.substring(0, sp1).toInt();
            String md5 = rest.substring(sp1 + 1, sp2);
            String version = rest.substring(sp2 + 1);
            version.trim();
            otaBegin(size, md5, version);
        }
        else
        {
            sendFeedback(F("Usage: ota begin <size> <md5hex> <version>"));
        }
    }
    else if (argLower.startsWith("chunk"))
    {
        String data = arg.substring(5);
        data.trim();
        otaChunk(data);
    }
    else if (argLower == "end")
    {
        otaEnd();
    }
    else if (argLower == "abort")
    {
        otaAbort();
    }
    else if (argLower == "status")
    {
        sendFeedback(String(F("[OTA] version=")) + FIRMWARE_VERSION + F(" env=") + FIRMWARE_ENV +
                     F(" active=") + (otaActive ? F("1") : F("0")) +
                     F(" received=") + String(otaReceivedSize) + F("/") + String(otaExpectedSize));
    }
    else
    {
        sendFeedback(F("Usage: ota begin <size> <md5> <version> | ota chunk <base64> | ota end | ota abort | ota status"));
    }
}
#endif

static void cmd_Name(const String &line, const String &lower)
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
}

static void cmd_Trust(const String &line, const String &lower)
{
    String args = line.substring(5);
    args.trim();
    if (args.isEmpty() || args.equalsIgnoreCase(F("list")))
    {
        trustListFeedback();
        return;
    }
    // expect: trust <ble|bt> <add|del> <addr>, or trust ble bonds / trust ble clearbonds (no addr)
    int sp1 = args.indexOf(' ');
    int sp2 = sp1 >= 0 ? args.indexOf(' ', sp1 + 1) : -1;
    String kind = sp1 > 0 ? args.substring(0, sp1) : args;
    String action = sp1 > 0 ? (sp2 > sp1 ? args.substring(sp1 + 1, sp2) : args.substring(sp1 + 1)) : "";
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
            // Also drops the device's BLE bond (crypto keys), not just
            // the app-level trust entry - see trustRemoveBle() in comms.cpp.
            ok = trustRemoveBle(addr);
#if ENABLE_BLE
        else if (action == "bonds")
            // NimBLE's own bond store, independent of the trust list
            // above - useful to spot a "ghost" bond (e.g. one from an
            // aborted/never-confirmed pairing) that trust list removal
            // alone wouldn't show. trustListFeedback() below prints it.
            ok = true;
        else if (action == "clearbonds")
            // Full reset of the crypto side only (trust list is
            // untouched) - use when the lamp and a client's OS Bluetooth
            // stack disagree about being bonded (e.g. after an
            // erase-flash re-created the lamp's keys from scratch): wipe
            // the lamp's bonds here, then forget/unpair the lamp on the
            // client's OS side too, and reconnect to re-pair both ends
            // from a clean slate.
            ok = trustClearBleBonds();
#endif
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
        sendFeedback(F("Usage: trust list | trust ble add <mac> | trust ble del <mac> | trust bt add <mac> | trust bt del <mac>"
#if ENABLE_BLE
                       " | trust ble bonds | trust ble clearbonds"
#endif
                       ));
    }
}

static void cmd_Factory(const String &line, const String &lower)
{
    applyDefaultSettings(-1.0f, true);
}

static void cmd_Profile(const String &line, const String &lower)
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
            sendFeedback(F("Usage: profile save <1-6>"));
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
            sendFeedback(F("Usage: profile load <1-6>"));
        }
    }
    else if (arg.startsWith("show"))
    {
        int slot = arg.substring(4).toInt();
        if (slot >= 1 && slot <= PROFILE_SLOTS)
        {
            String key = String(PREF_KEY_PROFILE_BASE) + String(slot);
            // isKey() first: getString() on a key that was never saved logs an
            // ESP_LOGE "nvs_get_str len fail: ... NOT_FOUND" on every call -
            // noisy on Serial for the (common) case of an unused slot, and this
            // is a normal/expected state, not an actual error.
            String cfg = prefs.isKey(key.c_str()) ? prefs.getString(key.c_str(), "") : "";
            if (cfg.length() == 0)
                cfg = defaultProfileString((uint8_t)slot);
            sendFeedback(String(F("[Profile ")) + String(slot) + F("] ") + (cfg.length() ? cfg : String(F("empty"))));
        }
        else
        {
            sendFeedback(F("Usage: profile show <1-6>"));
        }
    }
    else
    {
        sendFeedback(F("profile save <1-6> | profile load <1-6> | profile show <1-6>"));
    }
}

static void cmd_Calibrate(const String &line, const String &lower)
{
    calibrateTouchBaseline();
    sendFeedback(F("[Touch] Baseline neu kalibriert."));
}

/**
 * @brief Parse and execute a command string from any input channel.
 */
void handleCommand(const String &line)
{
    // Trimming is the caller's job now (processInputChar() already does
    // this on `buffer` before calling; the MIDI dispatch paths construct
    // already-clean strings) - `line` used to be a by-value parameter, and
    // that copy, plus the `lower` copy just below, were part of the
    // allocation pattern that reproduced a TLSF heap corruption on real
    // hardware (see the other fixes alongside this one). Taking `line` by
    // reference removes one of those two copies entirely.
    if (line.isEmpty())
        return;

    lastActivityMs = millis();

    // `lower` is function-static and reserved once rather than a fresh
    // local copy every call, same reasoning. 64 bytes comfortably covers
    // every normal command; the rare long "ota chunk <base64>" command (up
    // to ~5600 bytes, see LINE_BUFFER_MAX) still works, it just triggers one
    // real reallocation on that uncommon, non-hot path.
    static String lower;
    static bool lowerReserved = false;
    if (!lowerReserved)
    {
        lower.reserve(64);
        lowerReserved = true;
    }
    lower = line;
    lower.toLowerCase();

    // Dispatch table lookup (see COMMAND_TABLE above handleCommand) - first
    // match wins, same semantics the original sequential if-chain had.
    for (const auto &entry : COMMAND_TABLE)
    {
        bool hit = entry.match == CmdMatch::Exact ? (lower == entry.pattern) : lower.startsWith(entry.pattern);
        if (hit)
        {
            entry.handler(line, lower);
            return;
        }
    }

    sendFeedback(F("Unbekanntes Kommando. 'help' tippen."));
}
