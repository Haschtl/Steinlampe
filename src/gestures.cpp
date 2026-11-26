#include <Arduino.h>
#include <stdlib.h>

#include "gestures.h"
#include "lamp_config.h"
#include "lamp_state.h"
#include "quickmode.h"
#include "pattern.h"
#include "comms.h"
#if ENABLE_BLE_MESH
#include "lamp_mesh.h"
#endif

// Grammar (semicolon-separated entries, no spaces - see gestures.h and the
// feat/ble-mesh plan this implements for the full rationale):
//   <input><gesture>=<action>[:<p1>[:<p2>]]
// input:   sw<i> (toggle slot i), bt<i> (momentary slot i), po (poti)
// gesture: t<N>[on|off]  - N-tap, optional starting-state suffix
//          s<N>[up|dn]   - N-swing (poti only); up = started low (swung
//                          up-then-down), dn = started high (down-then-up)
//          lp            - long press
//          dl            - double-long (short press+release, then hold)
// action:  quick | toggle | on | off | bri:<pct> | mesh_on:<addr> |
//          mesh_off:<addr> | mesh_bri:<addr>:<pct>
// addr accepts decimal or 0x-hex.

GestureBinding gestureBindings[MAX_GESTURE_BINDINGS];
uint8_t gestureBindingCount = 0;

const GestureBinding *findGestureBinding(uint8_t inputKind, uint8_t inputIndex,
                                          uint8_t gestureKind, uint8_t count, uint8_t startState)
{
    for (uint8_t i = 0; i < gestureBindingCount; ++i)
    {
        const GestureBinding &b = gestureBindings[i];
        if (b.inputKind != inputKind || b.inputIndex != inputIndex ||
            b.gestureKind != gestureKind || b.count != count)
            continue;
        if (b.startState != GS_EITHER && b.startState != startState)
            continue;
        return &b;
    }
    return nullptr;
}

void dispatchGestureAction(const GestureBinding &b)
{
    switch ((GestureAction)b.action)
    {
    case ACTION_QUICK_CYCLE:
    {
        size_t from = currentModeIndex;
        if (from >= quickModeCount())
            from = 0;
        applyQuickMode(nextQuickMode(from));
        break;
    }
    case ACTION_TOGGLE:
        setLampEnabled(!lampEnabled, "gesture");
        break;
    case ACTION_ON:
        setLampEnabled(true, "gesture");
        break;
    case ACTION_OFF:
        setLampEnabled(false, "gesture");
        break;
    case ACTION_SET_BRIGHTNESS:
        setBrightnessPercent(b.param1 / 10.0f, true);
        break;
#if ENABLE_BLE_MESH
    case ACTION_MESH_SET_ON:
        meshSendOnOff(b.param1, true);
        break;
    case ACTION_MESH_SET_OFF:
        meshSendOnOff(b.param1, false);
        break;
    case ACTION_MESH_SET_BRIGHTNESS:
        meshSendLightness(b.param1, b.param2 / 10.0f);
        break;
#else
    case ACTION_MESH_SET_ON:
    case ACTION_MESH_SET_OFF:
    case ACTION_MESH_SET_BRIGHTNESS:
        sendFeedback(F("[Gesture] Mesh action configured, but Mesh disabled at build"));
        break;
#endif
    case ACTION_NONE:
    default:
        break;
    }
}

void resetGestureBindingsToDefault()
{
    // No bindings by default - every gesture falls back to the legacy
    // behavior its own detector already had before this feature existed.
    gestureBindingCount = 0;
}

static String actionToToken(const GestureBinding &b)
{
    String out;
    switch ((GestureAction)b.action)
    {
    case ACTION_QUICK_CYCLE:
        out = F("quick");
        break;
    case ACTION_TOGGLE:
        out = F("toggle");
        break;
    case ACTION_ON:
        out = F("on");
        break;
    case ACTION_OFF:
        out = F("off");
        break;
    case ACTION_SET_BRIGHTNESS:
        out = F("bri:");
        out += String(b.param1 / 10.0f, 1);
        break;
    case ACTION_MESH_SET_ON:
        out = F("mesh_on:0x");
        out += String(b.param1, HEX);
        break;
    case ACTION_MESH_SET_OFF:
        out = F("mesh_off:0x");
        out += String(b.param1, HEX);
        break;
    case ACTION_MESH_SET_BRIGHTNESS:
        out = F("mesh_bri:0x");
        out += String(b.param1, HEX);
        out += ':';
        out += String(b.param2 / 10.0f, 1);
        break;
    default:
        out = F("none");
        break;
    }
    return out;
}

String gestureBindingsToCsv()
{
    String out;
    for (uint8_t i = 0; i < gestureBindingCount; ++i)
    {
        const GestureBinding &b = gestureBindings[i];
        if (!out.isEmpty())
            out += ';';
        if (b.inputKind == GIN_SWITCH)
        {
            out += F("sw");
            out += String(b.inputIndex);
        }
        else if (b.inputKind == GIN_BUTTON)
        {
            out += F("bt");
            out += String(b.inputIndex);
        }
        else
        {
            out += F("po");
        }
        if (b.gestureKind == GK_N_TAP)
        {
            out += 't';
            out += String(b.count);
            if (b.startState == GS_ON)
                out += F("on");
            else if (b.startState == GS_OFF)
                out += F("off");
        }
        else if (b.gestureKind == GK_N_SWING)
        {
            out += 's';
            out += String(b.count);
            if (b.startState == GS_OFF)
                out += F("up");
            else if (b.startState == GS_ON)
                out += F("dn");
        }
        else if (b.gestureKind == GK_LONG_PRESS)
        {
            out += F("lp");
        }
        else
        {
            out += F("dl");
        }
        out += '=';
        out += actionToToken(b);
    }
    return out;
}

// Parses one entry's input+gesture prefix (everything before '='). Returns
// false if it doesn't match the grammar.
static bool parseKey(const String &key, GestureBinding &b)
{
    int pos = 0;
    int len = key.length();
    if (key.startsWith("sw"))
    {
        b.inputKind = GIN_SWITCH;
        pos = 2;
    }
    else if (key.startsWith("bt"))
    {
        b.inputKind = GIN_BUTTON;
        pos = 2;
    }
    else if (key.startsWith("po"))
    {
        b.inputKind = GIN_POTI;
        b.inputIndex = 0;
        pos = 2;
    }
    else
    {
        return false;
    }
    if (b.inputKind != GIN_POTI)
    {
        int start = pos;
        while (pos < len && isDigit(key[pos]))
            pos++;
        if (pos == start)
            return false;
        b.inputIndex = (uint8_t)key.substring(start, pos).toInt();
    }
    if (pos >= len)
        return false;
    char g = key[pos];
    if (g == 't' || g == 's')
    {
        pos++;
        int start = pos;
        while (pos < len && isDigit(key[pos]))
            pos++;
        if (pos == start)
            return false;
        b.count = (uint8_t)key.substring(start, pos).toInt();
        String suffix = key.substring(pos);
        if (g == 't')
        {
            b.gestureKind = GK_N_TAP;
            if (suffix == "on")
                b.startState = GS_ON;
            else if (suffix == "off")
                b.startState = GS_OFF;
            else if (suffix.isEmpty())
                b.startState = GS_EITHER;
            else
                return false;
        }
        else
        {
            b.gestureKind = GK_N_SWING;
            if (suffix == "up")
                b.startState = GS_OFF; // started low, swung up first
            else if (suffix == "dn")
                b.startState = GS_ON; // started high, swung down first
            else if (suffix.isEmpty())
                b.startState = GS_EITHER;
            else
                return false;
        }
        return true;
    }
    String rest = key.substring(pos);
    if (rest == "lp")
    {
        b.gestureKind = GK_LONG_PRESS;
        b.count = 0;
        b.startState = GS_EITHER;
        return true;
    }
    if (rest == "dl")
    {
        b.gestureKind = GK_DOUBLE_LONG;
        b.count = 0;
        b.startState = GS_EITHER;
        return true;
    }
    return false;
}

static bool parseAction(const String &val, GestureBinding &b)
{
    int c1 = val.indexOf(':');
    String head = c1 >= 0 ? val.substring(0, c1) : val;
    String tail = c1 >= 0 ? val.substring(c1 + 1) : String();

    if (head == "quick")
    {
        b.action = ACTION_QUICK_CYCLE;
        return true;
    }
    if (head == "toggle")
    {
        b.action = ACTION_TOGGLE;
        return true;
    }
    if (head == "on")
    {
        b.action = ACTION_ON;
        return true;
    }
    if (head == "off")
    {
        b.action = ACTION_OFF;
        return true;
    }
    if (head == "bri")
    {
        if (tail.isEmpty())
            return false;
        b.action = ACTION_SET_BRIGHTNESS;
        b.param1 = (uint16_t)(tail.toFloat() * 10.0f + 0.5f);
        return true;
    }
    if (head == "mesh_on" || head == "mesh_off")
    {
        if (tail.isEmpty())
            return false;
        b.action = (head == "mesh_on") ? ACTION_MESH_SET_ON : ACTION_MESH_SET_OFF;
        b.param1 = (uint16_t)strtoul(tail.c_str(), nullptr, 0);
        return true;
    }
    if (head == "mesh_bri")
    {
        int c2 = tail.indexOf(':');
        if (c2 < 0)
            return false;
        String addrStr = tail.substring(0, c2);
        String pctStr = tail.substring(c2 + 1);
        if (addrStr.isEmpty() || pctStr.isEmpty())
            return false;
        b.action = ACTION_MESH_SET_BRIGHTNESS;
        b.param1 = (uint16_t)strtoul(addrStr.c_str(), nullptr, 0);
        b.param2 = (uint16_t)(pctStr.toFloat() * 10.0f + 0.5f);
        return true;
    }
    return false;
}

bool parseGestureCsv(const String &csv)
{
    String tmp = csv;
    tmp.trim();
    if (tmp.isEmpty())
    {
        gestureBindingCount = 0;
        return true;
    }

    GestureBinding parsed[MAX_GESTURE_BINDINGS];
    uint8_t parsedCount = 0;

    int start = 0;
    int len = tmp.length();
    while (start < len)
    {
        int end = tmp.indexOf(';', start);
        if (end < 0)
            end = len;
        String entry = tmp.substring(start, end);
        start = end + 1;
        entry.trim();
        if (entry.isEmpty())
            continue;
        if (parsedCount >= MAX_GESTURE_BINDINGS)
            return false;

        int eq = entry.indexOf('=');
        if (eq <= 0)
            return false;
        String key = entry.substring(0, eq);
        String val = entry.substring(eq + 1);

        GestureBinding b{};
        if (!parseKey(key, b))
            return false;
        if (!parseAction(val, b))
            return false;
        parsed[parsedCount++] = b;
    }

    memcpy(gestureBindings, parsed, parsedCount * sizeof(GestureBinding));
    gestureBindingCount = parsedCount;
    return true;
}
