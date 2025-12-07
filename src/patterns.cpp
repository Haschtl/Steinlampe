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
  const float peak = 0.92f;
  const float risePortion = 0.6f; // percent of cycle spent rising
  const uint32_t period = 8200;
  float phase = (ms % period) / (float)period;
  float t = 0.0f;
  if (phase < risePortion)
  {
    t = phase / risePortion;
    t = t * t * (3.0f - 2.0f * t); // smooth ease
  }
  else
  {
    t = 1.0f - ((phase - risePortion) / (1.0f - risePortion));
    t = 1.0f - t * t; // faster fall
  }
  return clamp01(base + (peak - base) * t);
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
  // Layered smooth noise for non-repeating flicker
  float base = 0.35f;
  float slow = (smoothNoise(ms, 900, 0x11) - 0.5f) * 0.25f;   // very slow drift
  float mid = (smoothNoise(ms, 180, 0x22) - 0.5f) * 0.30f;   // main flicker
  float fast = (smoothNoise(ms, 65, 0x33) - 0.5f) * 0.18f;   // fast shimmer
  float spark = (smoothNoise(ms, 35, 0x44) - 0.5f) * 0.10f;  // micro jitter
  return clamp01(base + slow + mid + fast + spark);
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
  float embers = (smoothNoise(ms, 1400, 0x88) - 0.5f) * 0.25f; // slow glowing bed
  float tongues = (smoothNoise(ms, 320, 0x99) - 0.5f) * 0.30f; // moving flames
  float sparks = (smoothNoise(ms, 120, 0xAA) - 0.5f) * 0.18f;  // flicker
  float crackle = (smoothNoise(ms, 45, 0xBB) - 0.5f) * 0.10f;  // fast crackle
  return clamp01(base + embers + tongues + sparks + crackle);
}

/// Linear interpolation between preset levels
float patternStepFade(uint32_t ms)
{
  static const float stops[] = {0.15f, 0.45f, 0.9f, 0.35f};
  static const uint32_t segmentMs = 2500;
  size_t seg = (ms / segmentMs) % (sizeof(stops) / sizeof(stops[0]));
  float progress = (ms % segmentMs) / (float)segmentMs;
  float start = stops[seg];
  float end = stops[(seg + 1) % (sizeof(stops) / sizeof(stops[0]))];
  return start + (end - start) * progress;
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
  float base = 0.05f + (smoothNoise(ms, 900, 0xD1) - 0.5f) * 0.04f;
  const uint32_t window = 1100;
  uint32_t idx = ms / window;
  uint32_t start = idx * window;
  float flash = 0.0f;
  // multiple small pulses possible per window
  for (int k = 0; k < 2; ++k)
  {
    uint32_t salt = idx * 0x9E37u + (uint32_t)k * 0x45u;
    float chance = hash11(salt);
    if (chance < 0.35f)
      continue;
    uint32_t offset = (uint32_t)(hash11(salt ^ 0xAAu) * (window - 280));
    uint32_t t = ms - start;
    if (t >= offset && t < offset + 280)
    {
      uint32_t dt = t - offset;
      float env = expf(-(float)dt / 140.0f);
      flash += 0.75f * env * (0.7f + 0.3f * hash11(salt ^ 0x11u));
    }
  }
  return clamp01(base + flash);
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
  float wave = 0.25f + 0.25f * sinf(t * 0.35f * TWO_PI);
  float shimmer = (smoothNoise(ms, 120, 0xD4) - 0.5f) * 0.12f;
  float burst = 0.0f;
  const uint32_t window = 1600;
  uint32_t idx = ms / window;
  if (hash11(idx * 0xABu) > 0.6f)
  {
    uint32_t start = idx * window;
    uint32_t off = (uint32_t)(hash11(idx * 0x37u) * (window - 400));
    uint32_t dt = ms - start;
    if (dt >= off && dt < off + 400)
    {
      float x = (dt - off) / 400.0f;
      burst = (1.0f - (x - 0.5f) * (x - 0.5f) * 4.0f) * 0.5f; // parabola-ish pulse
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
  float base = 0.12f + (smoothNoise(ms, 1400, 0x42) - 0.5f) * 0.03f;
  const uint32_t window = 1600;
  uint32_t idx = ms / window;
  uint32_t start = idx * window;
  float flare = 0.0f;
  if (hash11(idx * 0x1337u) > 0.55f)
  {
    uint32_t off = (uint32_t)(hash11(idx * 0x51u) * (window - 320));
    uint32_t dt = ms - start;
    if (dt >= off && dt < off + 320)
    {
      float x = (float)(dt - off);
      float rise = x < 40.0f ? (x / 40.0f) : 1.0f;
      float decay = expf(-(x > 40.0f ? (x - 40.0f) : 0.0f) / 110.0f);
      float spark = (smoothNoise(ms, 25, 0xA5) - 0.5f) * 0.12f;
      flare = (0.9f + spark) * rise * decay;
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

/// Thunder: low ambient light, occasional sharp flashes with afterglow
float patternThunder(uint32_t ms)
{
  const float base = 0.06f;
  float ambient = (smoothNoise(ms, 420, 0xC1) - 0.5f) * 0.05f;

  // One flash window per ~7s
  const uint32_t windowMs = 7000;
  uint32_t winIdx = ms / windowMs;
  uint32_t winStart = winIdx * windowMs;
  float chance = hash11(winIdx * 0x9E3779B9u + 0x77);
  float flash = 0.0f;
  if (chance > 0.35f)
  {
    uint32_t offset = (uint32_t)(hash11(winIdx * 0xA5B35705u + 0x44) * (windowMs - 900));
    uint32_t t = ms - winStart;
    if (t >= offset && t < offset + 900)
    {
      uint32_t dt = t - offset;
      if (dt < 120)
      {
        flash = 1.0f; // hard flash
      }
      else
      {
        float decay = expf(-(float)(dt - 120) / 260.0f);
        float micro = (sinf((float)dt * 0.09f) + 1.0f) * 0.08f;
        flash = 0.8f * decay + micro * decay;
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
    {"Sinus", patternSinus, 12000},
    {"Pulsierend", patternPulse, 12000},
    {"Funkeln", patternSparkle, 12000},
    {"Kerze Soft", patternCandleSoft, 16000},
    {"Kerze", patternCandle, 16000},
    {"Lagerfeuer", patternCampfire, 18000},
    {"Stufen", patternStepFade, 14000},
    {"Zwinkern", patternTwinkle, 16000},
    {"Gluehwuermchen", patternFireflies, 12000},
    {"Popcorn", patternPopcorn, 10000},
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
    {"Alert", patternAlert, 0},
    {"SOS", patternSOS, 0},
    {"Custom", patternCustom, 0},
#if ENABLE_MUSIC_MODE
    {"Musik", patternMusic, 0},
#endif
};

const size_t PATTERN_COUNT = sizeof(PATTERNS) / sizeof(PATTERNS[0]);
