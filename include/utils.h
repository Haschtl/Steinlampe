#pragma once

/**
 * @file utils.h
 * @brief Small math helpers shared across firmware modules.
 */

/**
 * @brief Clamp a float value to the inclusive range [0, 1].
 */
inline float clamp01(float x)
{
  if (x < 0.0f)
    return 0.0f;
  if (x > 1.0f)
    return 1.0f;
  return x;
}
