#include "Arduino.h"

// ---------- Hilfsfunktionen ----------

/**
 * @brief Clamp a float value to the inclusive range [0, 1].
 */
float clamp01(float x)
{
    if (x < 0.0f)
        return 0.0f;
    if (x > 1.0f)
        return 1.0f;
    return x;
}


bool parseBool(const String &s, bool &out)
{
    if (s.equalsIgnoreCase(F("on")) || s.equalsIgnoreCase(F("true")) || s == "1")
    {
        out = true;
        return true;
    }
    if (s.equalsIgnoreCase(F("off")) || s.equalsIgnoreCase(F("false")) || s == "0")
    {
        out = false;
        return true;
    }
    return false;
}

uint8_t easeFromString(const String &s)
{
    String l = s;
    l.toLowerCase();
    if (l == "linear")
        return 0;
    if (l == "ease")
        return 1;
    if (l == "ease-in" || l == "easein")
        return 2;
    if (l == "ease-out" || l == "easeout")
        return 3;
    if (l == "ease-in-out" || l == "easeinout")
        return 4;
    if (l == "flash")
        return 5;
    if (l == "wave")
        return 6;
    if (l == "blink")
        return 7;
    return 1;
}

String easeToString(uint8_t t)
{
    switch (t)
    {
    case 0:
        return F("linear");
    case 2:
        return F("ease-in");
    case 3:
        return F("ease-out");
    case 4:
        return F("ease-in-out");
    case 5:
        return F("flash");
    case 6:
        return F("wave");
    case 7:
        return F("blink");
    case 1:
    default:
        return F("ease");
    }
}
