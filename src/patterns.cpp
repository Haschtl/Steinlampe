/**
 * @file patterns.cpp
 * @brief Collection of PWM brightness patterns for the quartz lamp demo.
 *
 * Each pattern returns a normalized brightness (0..1) depending on elapsed milliseconds.
 * The table at the bottom exposes the patterns to the main firmware for sequencing.
 */

#include "patterns.h"

#include <math.h>

#include "utils.h"

// Evaluate a simple on/off sequence defined by durations and levels.
static float evalSequence(uint32_t ms, const uint16_t *durations, const float *levels, size_t count)
{
  if (!durations || !levels || count == 0)
    return 0.0f;
  uint32_t total = 0;
  for (size_t i = 0; i < count; ++i)
    total += durations[i];
  if (total == 0)
    return 0.0f;
  uint32_t t = ms % total;
  uint32_t acc = 0;
  for (size_t i = 0; i < count; ++i)
  {
    acc += durations[i];
    if (t < acc)
      return levels[i];
  }
  return 0.0f;
}

// --- Simple hash-based noise helpers (deterministic, non-repeating for very long ranges) ---
static inline float hash11(uint32_t x)
{
  x ^= x * 0x27d4eb2du;
  x ^= x >> 15;
  x *= 0x85ebca6bu;
  x ^= x >> 13;
  return (x & 0x00FFFFFF) / 16777215.0f;
}

// Smooth noise: sample every stepMs and crossfade, giving organic randomness without short repeats.
static float smoothNoise(uint32_t ms, uint32_t stepMs, uint32_t salt)
{
  if (stepMs == 0)
    stepMs = 50;
  uint32_t aIdx = (ms / stepMs);
  uint32_t bIdx = aIdx + 1;
  float t = (ms % stepMs) / (float)stepMs;
  float fa = hash11(aIdx ^ salt);
  float fb = hash11(bIdx ^ salt);
  // Smoothstep interpolation
  t = t * t * (3.0f - 2.0f * t);
  return fa + (fb - fa) * t;
}

// Forward declaration for cross-references
float patternThunder(uint32_t ms);

/// Soft base brightness – calm mode
float patternConstant(uint32_t)
{
  return 1.0f;
}

/// Slow breathe using eased sine wave
float patternBreathing(uint32_t ms)
{
  float phase = (ms % 7000u) / 7000.0f;
  float wave = (1.0f - cosf(TWO_PI * phase)) * 0.5f;
  float eased = wave * wave * (3.0f - 2.0f * wave);
  return 0.25f + 0.7f * eased;
}

/// Warm asymmetric breathing: slower rise, quicker fall
float patternBreathingWarm(uint32_t ms)
{
  const float base = 0.28f;
  const float peak = 0.9f;
  const float risePortion = 0.62f; // percent of cycle spent rising
  const uint32_t period = 8800;
  float phase = (ms % period) / (float)period;
  float t = 0.0f;
  if (phase < risePortion)
  {
    // ease-in-out rise for a soft inhale
    t = phase / risePortion;
    t = t * t * (3.0f - 2.0f * t);
  }
  else
  {
    // gentle, curved exhale without a hard cut
    float x = (phase - risePortion) / (1.0f - risePortion);
    t = 1.0f - (x * x * (3.0f - 2.0f * x));
  }
  // tiny drift to avoid mechanical repetition
  float drift = (smoothNoise(ms, 1400, 0x17) - 0.5f) * 0.03f;
  return clamp01(base + (peak - base) * t + drift);
}

/// Clean sine wave
float patternSinus(uint32_t ms)
{
  float phase = (ms % 6500u) / 6500.0f;
  float wave = 0.5f + 0.5f * sinf(TWO_PI * phase);
  return clamp01(0.18f + 0.78f * wave);
}

/// Gentle pulse without hard peaks
float patternPulse(uint32_t ms)
{
  float phase = (ms % 4200u) / 4200.0f;
  float wave = sinf(TWO_PI * phase);
  float env = powf(fabsf(wave), 1.6f);
  return 0.25f + 0.7f * env;
}

/// Heartbeat: double-beat with short rest (strong + softer)
float patternHeartbeat(uint32_t ms)
{
  const uint32_t period = 1900;
  uint32_t t = ms % period;
  float level = 0.16f + (smoothNoise(ms, 850, 0x2A) - 0.5f) * 0.02f;
  auto beat = [](uint32_t dt, uint32_t width, float peak) {
    if (dt >= width) return 0.0f;
    float x = dt / (float)width;
    float rise = x < 0.18f ? (x / 0.18f) : 1.0f;
    float decay = expf(-(x > 0.18f ? (x - 0.18f) : 0.0f) * 7.0f);
    float snap = (x < 0.12f) ? (x / 0.12f) : 1.0f;
    return peak * rise * decay * snap;
  };
  // first beat at t=0, second at 280ms
  level += beat(t, 240, 1.0f);
  if (t > 280) level += beat(t - 280, 210, 0.78f);
  // small dip after second beat
  if (t > 520 && t < 700)
    level -= 0.05f * ((t - 520) / 180.0f);
  // tiny breathing between cycles
  level += (smoothNoise(ms, 420, 0x3B) - 0.5f) * 0.04f;
  return clamp01(level);
}

/// Asym breathing v2: slow inhale, quick exhale with micro drift
float patternBreathing2(uint32_t ms)
{
  const float base = 0.18f;
  const float peak = 0.92f;
  const uint32_t period = 7200;
  float phase = (ms % period) / (float)period;
  float t;
  if (phase < 0.62f)
  {
    t = phase / 0.62f;
    t = t * t * (3.0f - 2.0f * t);
  }
  else
  {
    t = 1.0f - ((phase - 0.62f) / 0.38f);
    t = t * t * t;
  }
  float drift = (smoothNoise(ms, 1800, 0x19) - 0.5f) * 0.06f;
  float shimmer = (smoothNoise(ms, 120, 0x29) - 0.5f) * 0.03f;
  return clamp01(base + (peak - base) * t + drift + shimmer);
}

/// Angular triangle-like wave: clean zig-zag up/down
float patternZigZag(uint32_t ms)
{
  const uint32_t period = 5200;
  float phase = (ms % period) / (float)period;
  float tri = 1.0f - fabsf(2.0f * phase - 1.0f); // perfect 0..1..0 triangle
  // smooth the corners slightly to avoid stepping artifacts
  tri = tri * tri * (3.0f - 2.0f * tri);
  return clamp01(0.08f + 0.92f * tri);
}

/// Steep ramp with hard drop: sawtooth shape
float patternSawtooth(uint32_t ms)
{
  const uint32_t period = 4200;
  float phase = (ms % period) / (float)period; // 0..1
  // Pure ramp up, instant drop to base
  return clamp01(0.10f + 0.90f * phase);
}

/// Comet: rising tail with short falloff and jitter
float patternComet(uint32_t ms)
{
  const uint32_t period = 5200;
  float phase = (ms % period) / (float)period;
  float rise = phase < 0.82f ? (phase / 0.82f) : 1.0f;
  float fall = (phase > 0.82f) ? expf(-(phase - 0.82f) * 22.0f) : 1.0f;
  float tail = rise * fall;
  float shimmer = (smoothNoise(ms, 90, 0x5A) - 0.5f) * 0.08f;
  float trail = (smoothNoise(ms, 220, 0x6A) - 0.5f) * 0.05f;
  return clamp01(0.14f + 0.86f * tail + shimmer + trail);
}

/// Aurora: layered slow waves with soft randomness
float patternAurora(uint32_t ms)
{
  float t = ms / 1000.0f;
  float slow = 0.35f + 0.20f * sinf(t * 0.18f * TWO_PI + 0.7f);
  float mid = 0.18f * sinf(t * 0.42f * TWO_PI + sinf(t * 0.07f * TWO_PI));
  float noise = (smoothNoise(ms, 900, 0x4C) - 0.5f) * 0.10f;
  float shimmer = (smoothNoise(ms, 140, 0x5C) - 0.5f) * 0.05f;
  return clamp01(slow + mid + noise + shimmer);
}

/// High-speed strobe with light jitter to avoid aliasing
float patternStrobe(uint32_t ms)
{
  const uint32_t basePeriod = 90; // ~11 Hz
  uint32_t period = basePeriod + (uint32_t)((hash11(ms / basePeriod + 0x33u) - 0.5f) * 16.0f); // slight jitter
  if (period < 60)
    period = 60;
  uint32_t t = ms % period;
  uint32_t onMs = (uint32_t)(period * 0.16f);
  float flash = (t < onMs) ? 1.0f : 0.02f;
  float shimmer = (smoothNoise(ms, 18, 0x7Fu) - 0.5f) * 0.08f;
  return clamp01(flash + shimmer);
}

/// Gamma probe: cycles through three fixed levels with eased ramps
float patternGammaProbe(uint32_t ms)
{
  static const float levels[] = {0.10f, 0.40f, 0.80f, 0.40f};
  static const uint32_t rampMs = 240;
  static const uint32_t holdMs = 1100;
  const size_t count = sizeof(levels) / sizeof(levels[0]);
  uint32_t segLen = rampMs + holdMs;
  size_t seg = (ms / segLen) % count;
  uint32_t local = ms % segLen;
  float from = levels[seg];
  float to = levels[(seg + 1) % count];
  float level;
  if (local < rampMs)
  {
    float t = local / (float)rampMs;
    t = t * t * (3.0f - 2.0f * t);
    level = from + (to - from) * t;
  }
  else
  {
    level = to;
  }
  float micro = (smoothNoise(ms, 420, 0x6Du) - 0.5f) * 0.02f;
  return clamp01(level + micro);
}

/// Polizei (DE): blau-blau / rot-rot mit kurzen Pausen
float patternPoliceDE(uint32_t ms)
{
  // cycle: BB pause RR pause
  const uint16_t durations[] = {160, 160, 240, 160, 160, 320}; // B,B,pause,R,R,pause
  const float levels[] = {1.0f, 0.0f, 0.6f, 1.0f, 0.0f, 0.4f};
  return evalSequence(ms, durations, levels, sizeof(durations) / sizeof(durations[0]));
}

/// Camera flash: single pop + lingering afterglow, occasional double
float patternCameraFlash(uint32_t ms)
{
  const uint32_t period = 5200;
  uint32_t t = ms % period;
  float base = 0.08f;
  float flash = 0.0f;
  bool dbl = hash11(ms / period) > 0.72f; // sometimes double
  uint32_t firstDur = 140;
  if (t < firstDur)
    flash = 1.0f;
  else
  {
    float decay = expf(-(float)(t - firstDur) / 380.0f);
    flash = 0.8f * decay;
    if (dbl && t > 260 && t < 260 + firstDur)
      flash = fmaxf(flash, 0.9f);
  }
  float afterglow = expf(-(float)t / 2200.0f) * 0.15f;
  return clamp01(base + flash + afterglow);
}

/// Heartbeat alarm: bright double-beat with clear idle base
float patternHeartbeatAlarm(uint32_t ms)
{
  const uint32_t period = 1700;
  uint32_t t = ms % period;
  float level = 0.10f;
  auto beat = [](uint32_t dt, uint32_t width, float peak) {
    if (dt >= width) return 0.0f;
    float x = dt / (float)width;
    float rise = x < 0.12f ? (x / 0.12f) : 1.0f;
    float decay = expf(-(x > 0.12f ? (x - 0.12f) : 0.0f) * 9.0f);
    return peak * rise * decay;
  };
  level += beat(t, 180, 1.0f);
  if (t > 250) level += beat(t - 250, 160, 0.8f);
  // clear idle line after beats
  if (t > 500 && t < 700)
    level = fmaxf(level, 0.18f);
  return clamp01(level);
}

/// TV static: bright flicker with random micro-blips
float patternTVStatic(uint32_t ms)
{
  float base = 0.4f + (smoothNoise(ms, 45, 0x71) - 0.5f) * 0.15f;
  float mid = (smoothNoise(ms, 18, 0x72) - 0.5f) * 0.22f;
  float blip = 0.0f;
  if (hash11(ms * 3u) > 0.93f)
    blip = 0.5f;
  return clamp01(base + mid + blip);
}

/// HAL-9000: eerie pulsing eye with occasional spikes
float patternHal9000(uint32_t ms)
{
  float t = ms / 1000.0f;
  float slow = 0.35f + 0.22f * sinf(t * 0.22f * TWO_PI);
  float pulse = 0.25f * sinf(t * 1.3f * TWO_PI + 0.6f) * sinf(t * 0.45f * TWO_PI + 0.9f);
  float spike = 0.0f;
  if (hash11(ms / 900u) > 0.88f)
  {
    float x = (ms % 900u) / 900.0f;
    spike = 0.35f * expf(-x * 8.0f);
  }
  return clamp01(slow + pulse + spike);
}

/// Subtle sparkle via stacked sine components
float patternSparkle(uint32_t ms)
{
  float t = ms / 1000.0f;
  float slow = 0.55f + 0.18f * sinf(t * 0.35f * TWO_PI);
  float ripple = 0.15f * sinf(t * 3.6f * TWO_PI) + 0.10f * sinf(t * 5.9f * TWO_PI + 1.1f) + 0.05f * sinf(t * 11.0f * TWO_PI + 2.0f);
  return clamp01(slow + ripple);
}

/// Candle flicker: slow wobble plus high-frequency flutter
float patternCandle(uint32_t ms)
{
  // Layered smooth noise for non-repeating flicker, with rare soft pops
  float base = 0.36f;
  float slow = (smoothNoise(ms, 1000, 0x11) - 0.5f) * 0.22f; // very slow drift
  float mid = (smoothNoise(ms, 200, 0x22) - 0.5f) * 0.26f;   // main flicker
  float fast = (smoothNoise(ms, 70, 0x33) - 0.5f) * 0.14f;   // fast shimmer
  float spark = (smoothNoise(ms, 40, 0x44) - 0.5f) * 0.07f;  // micro jitter
  float pop = 0.0f;
  if (hash11(ms / 220u) > 0.92f)
  {
    float x = (ms % 220u) / 220.0f;
    pop = 0.12f * expf(-x * 10.0f);
  }
  return clamp01(base + slow + mid + fast + spark + pop);
}

/// Softer candle: gentle wobble, subdued jitter
float patternCandleSoft(uint32_t ms)
{
  float base = 0.42f;
  float slow = (smoothNoise(ms, 1200, 0x55) - 0.5f) * 0.18f;
  float mid = (smoothNoise(ms, 260, 0x66) - 0.5f) * 0.18f;
  float fast = (smoothNoise(ms, 95, 0x77) - 0.5f) * 0.08f;
  return clamp01(base + slow + mid + fast);
}

/// Campfire: embers, tongues and sporadic sparks
float patternCampfire(uint32_t ms)
{
  float base = 0.45f;
  float embers = (smoothNoise(ms, 1500, 0x88) - 0.5f) * 0.23f; // slow glowing bed
  float tongues = (smoothNoise(ms, 340, 0x99) - 0.5f) * 0.28f; // moving flames
  float sparks = (smoothNoise(ms, 130, 0xAA) - 0.5f) * 0.16f;  // flicker
  float crackle = (smoothNoise(ms, 55, 0xBB) - 0.5f) * 0.10f;  // fast crackle
  float burst = 0.0f;
  if (hash11(ms / 180u) > 0.94f)
  {
    float x = (ms % 180u) / 180.0f;
    burst = 0.18f * expf(-x * 9.0f);
  }
  return clamp01(base + embers + tongues + sparks + crackle + burst);
}

/// Linear interpolation between preset levels
float patternStepFade(uint32_t ms)
{
  // Pure steps: climb up, then back down
  static const float steps[] = {0.15f, 0.32f, 0.5f, 0.68f, 0.9f, 0.68f, 0.5f, 0.32f};
  static const uint32_t holdMs = 900;
  size_t idx = (ms / holdMs) % (sizeof(steps) / sizeof(steps[0]));
  float level = steps[idx];
  // light smoothing over 10% of the window to avoid audible PWM clicks
  float prog = (ms % holdMs) / (float)holdMs;
  if (prog < 0.08f)
  {
    float prev = steps[(idx + sizeof(steps) / sizeof(steps[0]) - 1) % (sizeof(steps) / sizeof(steps[0]))];
    float t = prog / 0.08f;
    level = prev + (level - prev) * (t * t * (3.0f - 2.0f * t));
  }
  else if (prog > 0.92f)
  {
    float next = steps[(idx + 1) % (sizeof(steps) / sizeof(steps[0]))];
    float t = (prog - 0.92f) / 0.08f;
    level = level + (next - level) * (t * t * (3.0f - 2.0f * t));
  }
  return level;
}

/// Starry twinkle: slow base wave + two flicker layers
float patternTwinkle(uint32_t ms)
{
  float t = ms / 1000.0f;
  float slow = 0.3f + 0.2f * sinf(t * 0.25f * TWO_PI);
  float wave = 0.5f + 0.25f * sinf(t * 0.9f * TWO_PI + sinf(t * 0.15f * TWO_PI));
  float flicker = 0.08f * sinf(t * 7.3f * TWO_PI + 1.7f) + 0.05f * sinf(t * 12.1f * TWO_PI);
  return clamp01(slow + wave + flicker);
}

/// Distant storm: dark base, rare soft flashes with long afterglow
float patternDistantStorm(uint32_t ms)
{
  float base = 0.03f + (smoothNoise(ms, 1400, 0x1A) - 0.5f) * 0.03f;
  const uint32_t window = 9000;
  uint32_t idx = ms / window;
  uint32_t start = idx * window;
  float flash = 0.0f;
  if (hash11(idx * 0xC7u) > 0.6f)
  {
    uint32_t offset = (uint32_t)(hash11(idx * 0x31u) * (window - 1100));
    uint32_t t = ms - start;
    if (t >= offset && t < offset + 1100)
    {
      uint32_t dt = t - offset;
      if (dt < 120)
        flash = 0.9f;
      else
      {
        float decay = expf(-(float)(dt - 120) / 420.0f);
        flash = 0.7f * decay;
      }
    }
  }
  return clamp01(base + flash);
}

/// Rolling thunder: slow swell plus occasional double-flash
float patternRollingThunder(uint32_t ms)
{
  float swell = 0.05f + 0.05f * sinf((ms / 1000.0f) * 0.35f * TWO_PI);
  const uint32_t window = 7500;
  uint32_t idx = ms / window;
  uint32_t start = idx * window;
  float flash = 0.0f;
  if (hash11(idx * 0x99u) > 0.5f)
  {
    uint32_t baseOff = (uint32_t)(hash11(idx * 0x21u) * (window - 900));
    uint32_t t = ms - start;
    if (t >= baseOff && t < baseOff + 400)
    {
      flash = 1.0f;
    }
    else if (t >= baseOff + 420 && t < baseOff + 900)
    {
      uint32_t dt = t - (baseOff + 420);
      float decay = expf(-(float)dt / 250.0f);
      flash = 0.9f * decay;
    }
  }
  return clamp01(swell + flash);
}

/// Heat lightning: diffuse wide pulses with gentle shimmer
float patternHeatLightning(uint32_t ms)
{
  float base = 0.04f + (smoothNoise(ms, 1200, 0xA1) - 0.5f) * 0.02f;
  const uint32_t period = 6200;
  float phase = (ms % period) / (float)period;
  float env = 0.0f;
  if (phase < 0.3f)
  {
    float t = phase / 0.3f;
    env = t * t;
  }
  else if (phase < 0.8f)
  {
    float t = 1.0f - (phase - 0.3f) / 0.5f;
    env = t * t;
  }
  float shimmer = (smoothNoise(ms, 90, 0xB2) - 0.5f) * 0.08f;
  return clamp01(base + 0.55f * env + shimmer);
}

/// Strobe front: short burst cluster then long pause
float patternStrobeFront(uint32_t ms)
{
  const uint32_t cycle = 9500;
  uint32_t t = ms % cycle;
  if (t < 1200)
  {
    uint32_t mod = t % 180;
    return (mod < 60) ? 1.0f : 0.15f;
  }
  return 0.05f + (smoothNoise(ms, 800, 0x5E) - 0.5f) * 0.03f;
}

/// Sheet lightning: broad pulses with subtle flicker
float patternSheetLightning(uint32_t ms)
{
  const uint32_t period = 5200;
  float phase = (ms % period) / (float)period;
  float pulse = 0.0f;
  if (phase < 0.5f)
  {
    float t = phase / 0.5f;
    pulse = t * t * 0.9f;
  }
  else
  {
    float t = 1.0f - ((phase - 0.5f) / 0.5f);
    pulse = t * 0.9f;
  }
  float flicker = (smoothNoise(ms, 55, 0x7C) - 0.5f) * 0.12f;
  return clamp01(0.08f + pulse + flicker);
}

/// Mixed storm: random pick between flash styles
float patternMixedStorm(uint32_t ms)
{
  uint32_t idx = ms / 5000;
  float choice = hash11(idx * 0xEFu);
  if (choice < 0.2f)
    return patternDistantStorm(ms);
  if (choice < 0.4f)
    return patternRollingThunder(ms);
  if (choice < 0.6f)
    return patternHeatLightning(ms);
  if (choice < 0.8f)
    return patternStrobeFront(ms);
  return patternThunder(ms);
}
/// Fireflies: dark base with rare, soft pulses
float patternFireflies(uint32_t ms)
{
  float base = 0.06f + (smoothNoise(ms, 1300, 0xD1) - 0.5f) * 0.03f;
  const uint32_t window = 1300;
  uint32_t idx = ms / window;
  uint32_t start = idx * window;
  float flash = 0.0f;
  // multiple small pulses possible per window, but softer
  for (int k = 0; k < 3; ++k)
  {
    uint32_t salt = idx * 0x9E37u + (uint32_t)k * 0x45u;
    float chance = hash11(salt);
    if (chance < 0.5f)
      continue;
    uint32_t offset = (uint32_t)(hash11(salt ^ 0xAAu) * (window - 280));
    uint32_t t = ms - start;
    if (t >= offset && t < offset + 280)
    {
      uint32_t dt = t - offset;
      float env = expf(-(float)dt / 190.0f);
      float rise = (dt < 70) ? (dt / 70.0f) : 1.0f;
      flash += 0.55f * rise * env * (0.65f + 0.35f * hash11(salt ^ 0x11u));
    }
  }
  return clamp01(base + flash);
}

/// Leuchtstoffröhre: mains ripple with occasional brief sputter
float patternFluorescent(uint32_t ms)
{
  float t = ms / 1000.0f;
  // mains ripple around a base level
  float ripple = 0.05f * sinf(t * TWO_PI * 2.0f) + 0.03f * sinf(t * TWO_PI * 6.0f);
  float shimmer = (smoothNoise(ms, 22, 0xC1) - 0.5f) * 0.05f;
  float base = 0.70f + ripple + shimmer;

  // occasional sputter/dropout
  const uint32_t window = 4200;
  uint32_t idx = ms / window;
  if (hash11(idx * 0xE7u) > 0.82f)
  {
    uint32_t start = idx * window;
    uint32_t off = (uint32_t)(hash11(idx * 0x5Fu) * (window - 520));
    uint32_t dt = ms - start;
    if (dt >= off && dt < off + 520)
    {
      float x = (dt - off) / 520.0f;
      float dip = 0.35f * expf(-x * 4.5f) * (0.6f + 0.4f * sinf(x * TWO_PI * 3.0f));
      base -= dip;
    }
  }
  return clamp01(base);
}

/// Popcorn: dark base with quick popping spikes
float patternPopcorn(uint32_t ms)
{
  float base = 0.04f + (smoothNoise(ms, 800, 0xC5) - 0.5f) * 0.03f;
  const uint32_t window = 900;
  uint32_t idx = ms / window;
  uint32_t start = idx * window;
  float pop = 0.0f;
  // up to 3 pops per window with diminishing probability
  for (int k = 0; k < 3; ++k)
  {
    uint32_t salt = idx * 0x812u + (uint32_t)k * 0x3Du;
    if (hash11(salt) < (0.45f - 0.1f * k))
      continue;
    uint32_t offset = (uint32_t)(hash11(salt ^ 0x55u) * (window - 180));
    uint32_t t = ms - start;
    if (t >= offset && t < offset + 180)
    {
      uint32_t dt = t - offset;
      float rise = (dt < 40) ? (dt / 40.0f) : 1.0f;
      float decay = expf(-(float)(dt > 40 ? (dt - 40) : 0) / 90.0f);
      pop += (0.6f + 0.4f * hash11(salt ^ 0x99u)) * rise * decay;
    }
  }
  return clamp01(base + pop);
}

/// Festive twinkle: gentle wave with occasional spark bursts
float patternChristmas(uint32_t ms)
{
  float t = ms / 1000.0f;
  float wave = 0.25f + 0.23f * sinf(t * 0.22f * TWO_PI);
  float shimmer = (smoothNoise(ms, 180, 0xD4) - 0.5f) * 0.08f;
  float burst = 0.0f;
  const uint32_t window = 2300;
  uint32_t idx = ms / window;
  if (hash11(idx * 0xABu) > 0.72f)
  {
    uint32_t start = idx * window;
    uint32_t off = (uint32_t)(hash11(idx * 0x37u) * (window - 520));
    uint32_t dt = ms - start;
    if (dt >= off && dt < off + 520)
    {
      float x = (dt - off) / 520.0f;
      float env = sinf(x * PI); // soft bell
      burst = 0.38f * env * env;
    }
  }
  return clamp01(wave + shimmer + burst);
}

/// Lightsaber idle: slow pulse with micro flicker
float patternSaberIdle(uint32_t ms)
{
  float t = ms / 1000.0f;
  float pulse = 0.15f * sinf(t * TWO_PI * 0.9f) + 0.45f;
  float shimmer = (smoothNoise(ms, 55, 0x77) - 0.5f) * 0.05f;
  float drift = (smoothNoise(ms, 1200, 0x91) - 0.5f) * 0.05f;
  return clamp01(pulse + shimmer + drift);
}

/// Lightsaber clash: dark idle, sporadic bright flares with decay
float patternSaberClash(uint32_t ms)
{
  float base = 0.16f + (smoothNoise(ms, 1100, 0x42) - 0.5f) * 0.05f;
  const uint32_t window = 1400;
  uint32_t idx = ms / window;
  uint32_t start = idx * window;
  float flare = 0.0f;
  if (hash11(idx * 0x1337u) > 0.45f)
  {
    uint32_t off = (uint32_t)(hash11(idx * 0x51u) * (window - 380));
    uint32_t dt = ms - start;
    if (dt >= off && dt < off + 380)
    {
      float x = (float)(dt - off);
      float rise = x < 30.0f ? (x / 30.0f) : 1.0f;
      float decay = expf(-(x > 30.0f ? (x - 30.0f) : 0.0f) / 95.0f);
      float spark = (smoothNoise(ms, 22, 0xA5) - 0.5f) * 0.18f;
      float crack = (smoothNoise(ms, 11, 0xB3) - 0.5f) * 0.10f;
      flare = (1.05f + spark + crack) * rise * decay;
    }
  }
  return clamp01(base + flare);
}

/// Emergency bridge: double flash then pause
float patternEmergencyBridge(uint32_t ms)
{
  static const uint16_t durations[] = {160, 160, 160, 780};
  static const float levels[] = {1.0f, 0.0f, 1.0f, 0.05f};
  return evalSequence(ms, durations, levels, sizeof(durations) / sizeof(durations[0]));
}

/// Arc reactor: subtle centered glow with tiny breathing
float patternArcReactor(uint32_t ms)
{
  float phase = (ms % 5200u) / 5200.0f;
  float wave = 0.55f + 0.08f * sinf(phase * TWO_PI);
  float micro = (smoothNoise(ms, 85, 0x3C) - 0.5f) * 0.04f;
  return clamp01(wave + micro);
}

/// Warp core: asymmetrical thump with slight jitter
float patternWarpCore(uint32_t ms)
{
  const uint32_t period = 850;
  uint32_t t = ms % period;
  float env = (t < 320) ? (t / 320.0f) : (1.0f - (t - 320) / 530.0f);
  if (env < 0.0f) env = 0.0f;
  float wobble = (smoothNoise(ms, 70, 0x59) - 0.5f) * 0.06f;
  return clamp01(0.25f + 0.5f * env * env + wobble);
}

/// KITT scanner: swinging emphasis
float patternKittScanner(uint32_t ms)
{
  float phase = (ms % 2800u) / 2800.0f;
  float tri = (phase < 0.5f) ? (phase * 2.0f) : (2.0f - phase * 2.0f);
  float glow = 0.08f + 0.65f * tri * tri;
  float tail = (smoothNoise(ms, 120, 0x6D) - 0.5f) * 0.05f;
  return clamp01(glow + tail);
}

/// Tron grid: stepped pulses on a beat
float patternTronGrid(uint32_t ms)
{
  const uint32_t beat = 500; // 120 BPM
  uint32_t t = ms % beat;
  float step = (t < 120) ? 1.0f : (t < 240 ? 0.55f : (t < 320 ? 0.3f : 0.08f));
  float digitalNoise = (smoothNoise(ms, 65, 0x2E) - 0.5f) * 0.06f;
  return clamp01(step + digitalNoise);
}

/// Sunset fade: slow warm rise then gentle fall
float patternSunset(uint32_t ms)
{
  const uint32_t period = 42000; // 42s cycle
  const float base = 0.12f;
  const float peak = 0.95f;
  float phase = (ms % period) / (float)period;
  float t;
  if (phase < 0.4f)
  {
    t = phase / 0.4f; // rise
    t = t * t * (3.0f - 2.0f * t);
  }
  else
  {
    t = 1.0f - ((phase - 0.4f) / 0.6f); // fall
    t = t * t; // slow decay
  }
  return clamp01(base + (peak - base) * t);
}

/// Thunder: low ambient light, occasional sharp flashes with afterglow and afterflash
float patternThunder(uint32_t ms)
{
  const float base = 0.05f;
  float ambient = (smoothNoise(ms, 520, 0xC1) - 0.5f) * 0.06f;

  // One flash window per ~6.5s
  const uint32_t windowMs = 6500;
  uint32_t winIdx = ms / windowMs;
  uint32_t winStart = winIdx * windowMs;
  float chance = hash11(winIdx * 0x9E3779B9u + 0x77);
  float flash = 0.0f;
  if (chance > 0.32f)
  {
    uint32_t offset = (uint32_t)(hash11(winIdx * 0xA5B35705u + 0x44) * (windowMs - 950));
    uint32_t t = ms - winStart;
    if (t >= offset && t < offset + 950)
    {
      uint32_t dt = t - offset;
      if (dt < 120)
      {
        flash = 1.0f; // hard flash
      }
      else
      {
        float decay = expf(-(float)(dt - 120) / 240.0f);
        float micro = (sinf((float)dt * 0.09f) + 1.0f) * 0.08f;
        flash = 0.8f * decay + micro * decay;
      }
      // afterflash shortly after main (only sometimes)
      if (dt > 220 && dt < 460 && hash11(winIdx * 0x51F19E7Du + 0x12) > 0.58f)
      {
        float af = expf(-(float)(dt - 220) / 120.0f) * 0.5f;
        flash = fmaxf(flash, af);
      }
    }
  }

  return clamp01(base + ambient + flash);
}

/// Simple alert: steady blink on/off
float patternAlert(uint32_t ms)
{
  // double flash with short pause: on-off-on-off/pause
  static const uint16_t durations[] = {320, 220, 320, 780};
  static const float levels[] = {1.0f, 0.0f, 1.0f, 0.0f};
  return evalSequence(ms, durations, levels, sizeof(durations) / sizeof(durations[0]));
}

/// SOS in Morse (... --- ...), repeats
float patternSOS(uint32_t ms)
{
  // Morse timing: dot=1u, dash=3u, intra=1u, letter=3u, word=7u, u=200ms
  static const uint16_t durations[] = {
      200, 200, 200, 200, 200, 600,   // S (...): dot space dot space dot letter-gap
      600, 200, 600, 200, 600, 600,   // O (---): dash space dash space dash letter-gap
      200, 200, 200, 200, 200, 1400}; // S (...): dot space dot space dot word-gap
  static const float levels[] = {
      1, 0, 1, 0, 1, 0,
      1, 0, 1, 0, 1, 0,
      1, 0, 1, 0, 1, 0};
  return evalSequence(ms, durations, levels, sizeof(durations) / sizeof(durations[0]));
}

// Custom pattern is provided by main.cpp (patternCustom)
extern float patternCustom(uint32_t ms);
#if ENABLE_MUSIC_MODE
extern float patternMusic(uint32_t ms);
#endif

/// Exported pattern table consumed by the main firmware
const Pattern PATTERNS[] = {
    {"Konstant", patternConstant, 8000},
    {"Atmung", patternBreathing, 15000},
    {"Atmung Warm", patternBreathingWarm, 14000},
    {"Atmung 2", patternBreathing2, 14000},
    {"Sinus", patternSinus, 12000},
    {"Zig-Zag", patternZigZag, 10000},
    {"Saegezahn", patternSawtooth, 9000},
    {"Pulsierend", patternPulse, 12000},
    {"Heartbeat", patternHeartbeat, 12000},
    {"Heartbeat Alarm", patternHeartbeatAlarm, 10000},
    {"Comet", patternComet, 12000},
    {"Aurora", patternAurora, 18000},
    {"Strobo", patternStrobe, 0},
    {"Polizei DE", patternPoliceDE, 8000},
    {"Camera", patternCameraFlash, 8000},
    {"TV Static", patternTVStatic, 8000},
    {"HAL-9000", patternHal9000, 10000},
    {"Funkeln", patternSparkle, 12000},
    {"Kerze Soft", patternCandleSoft, 16000},
    {"Kerze", patternCandle, 16000},
    {"Lagerfeuer", patternCampfire, 18000},
    {"Stufen", patternStepFade, 14000},
    {"Zwinkern", patternTwinkle, 16000},
    {"Gluehwuermchen", patternFireflies, 12000},
    {"Popcorn", patternPopcorn, 10000},
    {"Leuchtstoffroehre", patternFluorescent, 12000},
    {"Weihnacht", patternChristmas, 12000},
    {"Saber Idle", patternSaberIdle, 12000},
    {"Saber Clash", patternSaberClash, 10000},
    {"Emergency Bridge", patternEmergencyBridge, 0},
    {"Arc Reactor", patternArcReactor, 0},
    {"Warp Core", patternWarpCore, 0},
    {"KITT Scanner", patternKittScanner, 0},
    {"Tron Grid", patternTronGrid, 0},
    {"Gewitter", patternThunder, 0},
    {"Distant Storm", patternDistantStorm, 0},
    {"Rolling Thunder", patternRollingThunder, 0},
    {"Heat Lightning", patternHeatLightning, 0},
    {"Strobe Front", patternStrobeFront, 0},
    {"Sheet Lightning", patternSheetLightning, 0},
    {"Mixed Storm", patternMixedStorm, 0},
    {"Sonnenuntergang", patternSunset, 0},
    {"Gamma Probe", patternGammaProbe, 0},
    {"Alert", patternAlert, 0},
    {"SOS", patternSOS, 0},
    {"Custom", patternCustom, 0},
#if ENABLE_MUSIC_MODE
    {"Musik", patternMusic, 0},
#endif
};

const size_t PATTERN_COUNT = sizeof(PATTERNS) / sizeof(PATTERNS[0]);
