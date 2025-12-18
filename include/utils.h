#pragma once

/**
 * @file utils.h
 * @brief Small math helpers shared across firmware modules.
 */

float clamp01(float x);
bool parseBool(const String &s, bool &out);
uint8_t easeFromString(const String &s);
String easeToString(uint8_t t);
