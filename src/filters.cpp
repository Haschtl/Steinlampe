#include "filters.h"

#include <math.h>
#include <esp_random.h>

#include "settings.h"
#include "utils.h"

namespace
{
FilterState st;

float random01()
{
  return (float)esp_random() / (float)UINT32_MAX;
}

float softsign(float x)
{
  return x / (1.0f + fabsf(x));
}

float waveValue(uint8_t wave, float phase)
{
  // phase in radians
  switch (wave)
  {
  case 1: // triangle
  {
    float norm = fmodf(phase / (2.0f * (float)M_PI), 1.0f);
    float tri = norm < 0.5f ? (norm * 4.0f - 1.0f) : (3.0f - norm * 4.0f);
    return tri * 0.5f + 0.5f; // 0..1
  }
  default: // sine
    return sinf(phase) * 0.5f + 0.5f; // 0..1
  }
}

} // namespace

void filtersInit()
{
  st.iirEnabled = false;
  st.iirAlpha = 0.2f;
  st.iirValue = -1.0f;

  st.clipEnabled = false;
  st.clipAmount = 0.0f;
  st.clipCurve = 0;

  st.tremEnabled = false;
  st.tremRateHz = 1.5f;
  st.tremDepth = 0.3f;
  st.tremWave = 0;
  st.tremStartMs = millis();

  st.sparkEnabled = false;
  st.sparkDensity = 1.0f;
  st.sparkIntensity = 0.3f;
  st.sparkDecayMs = 200;
  st.sparkValue = 0.0f;
  st.sparkLastMs = millis();
}

float filtersApply(float in, uint32_t nowMs)
{
  float out = in;

  // IIR low-pass on the final output
  if (st.iirEnabled)
  {
    float a = clamp01(st.iirAlpha);
    if (st.iirValue < 0.0f)
      st.iirValue = out;
    st.iirValue = st.iirValue + (out - st.iirValue) * a;
    out = st.iirValue;
  }

  // Soft-clip
  if (st.clipEnabled && st.clipAmount > 0.001f)
  {
    float amt = clamp01(st.clipAmount);
    float shaped;
    float x = out * (1.0f + 4.0f * amt); // drive up with amount
    if (st.clipCurve == 1)
      shaped = softsign(x);
    else
      shaped = tanhf(x);
    // mix original and shaped
    out = (1.0f - amt) * out + amt * shaped;
  }

  // Tremolo AM
  if (st.tremEnabled && st.tremDepth > 0.001f && st.tremRateHz > 0.01f)
  {
    float depth = clamp01(st.tremDepth);
    float phase = 2.0f * (float)M_PI * st.tremRateHz * ((nowMs - st.tremStartMs) / 1000.0f);
    float m = waveValue(st.tremWave, phase);
    float mod = (1.0f - depth) + depth * m;
    out *= mod;
  }

  // Sparkle overlay
  if (st.sparkEnabled)
  {
    uint32_t dt = nowMs - st.sparkLastMs;
    st.sparkLastMs = nowMs;
    // decay current sparkle
    if (st.sparkValue > 0.0f && st.sparkDecayMs > 0)
    {
      float k = expf(-(float)dt / (float)st.sparkDecayMs);
      st.sparkValue *= k;
    }
    // random trigger
    float p = st.sparkDensity * ((float)dt / 1000.0f);
    if (p > 0 && random01() < p)
    {
      st.sparkValue += st.sparkIntensity;
      if (st.sparkValue > 1.0f)
        st.sparkValue = 1.0f;
    }
    out *= (1.0f + st.sparkValue);
  }

  if (out < 0.0f)
    out = 0.0f;
  if (out > 1.5f)
    out = 1.5f; // allow slight over for clip/trem, clamp downstream
  return out;
}

void filtersSetIir(bool en, float alpha)
{
  st.iirEnabled = en;
  st.iirAlpha = alpha;
  if (!en)
    st.iirValue = -1.0f;
}

void filtersSetClip(bool en, float amt, uint8_t curve)
{
  st.clipEnabled = en;
  st.clipAmount = amt;
  st.clipCurve = curve;
}

void filtersSetTrem(bool en, float rateHz, float depth, uint8_t wave)
{
  st.tremEnabled = en;
  st.tremRateHz = rateHz;
  st.tremDepth = depth;
  st.tremWave = wave;
  st.tremStartMs = millis();
}

void filtersSetSpark(bool en, float density, float intensity, uint32_t decayMs)
{
  st.sparkEnabled = en;
  st.sparkDensity = density;
  st.sparkIntensity = intensity;
  st.sparkDecayMs = decayMs;
  if (!en)
  {
    st.sparkValue = 0.0f;
  }
}

void filtersGetState(FilterState &out)
{
  out = st;
}
