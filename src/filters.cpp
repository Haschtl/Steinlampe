#include "settings.h"
#include "filters.h"

#if ENABLE_FILTERS

#include <math.h>
#include <esp_random.h>

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

  st.compEnabled = false;
  st.compThr = Settings::FILTER_COMP_THR_DEFAULT;
  st.compRatio = Settings::FILTER_COMP_RATIO_DEFAULT;
  st.compAttackMs = Settings::FILTER_COMP_ATTACK_DEFAULT;
  st.compReleaseMs = Settings::FILTER_COMP_RELEASE_DEFAULT;
  st.compGain = 1.0f;
  st.compLastMs = millis();

  st.envEnabled = false;
  st.envAttackMs = Settings::FILTER_ENV_ATTACK_DEFAULT;
  st.envReleaseMs = Settings::FILTER_ENV_RELEASE_DEFAULT;
  st.envValue = -1.0f;
  st.envLastMs = millis();

  st.foldEnabled = false;
  st.foldAmt = Settings::FILTER_FOLD_AMT_DEFAULT;

  st.delayEnabled = false;
  st.delayMs = Settings::FILTER_DELAY_MS_DEFAULT;
  st.delayFeedback = Settings::FILTER_DELAY_FB_DEFAULT;
  st.delayMix = Settings::FILTER_DELAY_MIX_DEFAULT;
}

float filtersApply(float in, uint32_t nowMs)
{
  float out = in;
  static uint32_t delayTs[256];
  static float delayVal[256];
  static bool delayInit = false;
  static uint16_t delayHead = 0;
  if (!delayInit)
  {
    for (auto &d : delayTs)
      d = 0;
    for (auto &v : delayVal)
      v = 0.0f;
    delayInit = true;
  }

  // Attack/Release envelope shaper
  if (st.envEnabled)
  {
    uint32_t dt = nowMs - st.envLastMs;
    st.envLastMs = nowMs;
    if (st.envValue < 0.0f)
      st.envValue = out;
    if (dt > 1000) dt = 1000;
    float attack = st.envAttackMs < 1 ? 1.0f : (float)st.envAttackMs;
    float release = st.envReleaseMs < 1 ? 1.0f : (float)st.envReleaseMs;
    float alpha = out > st.envValue
                      ? 1.0f - expf(-(float)dt / attack)
                      : 1.0f - expf(-(float)dt / release);
    st.envValue = st.envValue + (out - st.envValue) * alpha;
    out = st.envValue;
  }

  // Compressor
  if (st.compEnabled && st.compRatio > 1.0f)
  {
    float level = out <= 0.0001f ? 0.0001f : out;
    float thr = st.compThr;
    float ratio = st.compRatio;
    float targetGain = 1.0f;
    if (level > thr)
    {
      float compOut = thr + (level - thr) / ratio;
      targetGain = compOut / level;
    }
    uint32_t dt = nowMs - st.compLastMs;
    st.compLastMs = nowMs;
    if (dt > 1000) dt = 1000;
    float aMs = st.compAttackMs < 1 ? 1.0f : (float)st.compAttackMs;
    float rMs = st.compReleaseMs < 1 ? 1.0f : (float)st.compReleaseMs;
    float alpha = targetGain < st.compGain
                      ? 1.0f - expf(-(float)dt / aMs)
                      : 1.0f - expf(-(float)dt / rMs);
    st.compGain = st.compGain + (targetGain - st.compGain) * alpha;
    out *= st.compGain;
  }

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

  // Wavefolder
  if (st.foldEnabled && st.foldAmt > 0.001f)
  {
    float k = 1.0f + st.foldAmt * 6.0f;
    float y = fabsf(fmodf(out * k, 2.0f) - 1.0f); // triangle fold 0..1
    out = y;
  }

  // Delay (simple tap with feedback)
  if (st.delayEnabled && st.delayMs > 0)
  {
    // push current sample with feedback contribution
    uint16_t idx = delayHead;
    delayTs[idx] = nowMs;
    delayVal[idx] = out; // base value; feedback added via delayed sample use
    delayHead = (delayHead + 1) % 256;

    float delayed = 0.0f;
    // find last sample older than delayMs
    for (int i = 0; i < 256; ++i)
    {
      int j = (delayHead - 1 - i);
      if (j < 0) j += 256;
      if (delayTs[j] == 0)
        continue;
      if ((nowMs - delayTs[j]) >= st.delayMs)
      {
        delayed = delayVal[j];
        break;
      }
    }
    // apply feedback and mix
    float fbSample = delayed * st.delayFeedback;
    out = (1.0f - st.delayMix) * out + st.delayMix * (delayed + fbSample);
    // store feedback into current slot for future reads
    delayVal[(delayHead + 255) % 256] = out;
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
  // sanitize to avoid NaN/inf leaking into status/log
  if (!isfinite(out.iirAlpha)) out.iirAlpha = Settings::FILTER_IIR_ALPHA_DEFAULT;
  if (!isfinite(out.clipAmount)) out.clipAmount = Settings::FILTER_CLIP_AMT_DEFAULT;
  if (out.clipCurve > 1) out.clipCurve = 0;
  if (!isfinite(out.tremRateHz) || out.tremRateHz < 0.0f) out.tremRateHz = Settings::FILTER_TREM_RATE_DEFAULT;
  if (!isfinite(out.tremDepth) || out.tremDepth < 0.0f || out.tremDepth > 1.0f) out.tremDepth = Settings::FILTER_TREM_DEPTH_DEFAULT;
  if (out.tremWave > 1) out.tremWave = 0;
  if (!isfinite(out.sparkDensity) || out.sparkDensity < 0.0f) out.sparkDensity = Settings::FILTER_SPARK_DENS_DEFAULT;
  if (!isfinite(out.sparkIntensity) || out.sparkIntensity < 0.0f) out.sparkIntensity = Settings::FILTER_SPARK_INT_DEFAULT;
  if (out.sparkDecayMs > 10000U) out.sparkDecayMs = Settings::FILTER_SPARK_DECAY_DEFAULT;
  if (!isfinite(out.compThr) || out.compThr < 0.0f) out.compThr = Settings::FILTER_COMP_THR_DEFAULT;
  if (!isfinite(out.compRatio) || out.compRatio < 1.0f) out.compRatio = Settings::FILTER_COMP_RATIO_DEFAULT;
  if (out.compAttackMs > 10000U) out.compAttackMs = Settings::FILTER_COMP_ATTACK_DEFAULT;
  if (out.compReleaseMs > 10000U) out.compReleaseMs = Settings::FILTER_COMP_RELEASE_DEFAULT;
  if (out.envAttackMs > 10000U) out.envAttackMs = Settings::FILTER_ENV_ATTACK_DEFAULT;
  if (out.envReleaseMs > 10000U) out.envReleaseMs = Settings::FILTER_ENV_RELEASE_DEFAULT;
  if (!isfinite(out.foldAmt) || out.foldAmt < 0.0f) out.foldAmt = Settings::FILTER_FOLD_AMT_DEFAULT;
  if (out.foldAmt > 1.0f) out.foldAmt = 1.0f;
  if (out.delayMs > 10000U) out.delayMs = Settings::FILTER_DELAY_MS_DEFAULT;
  if (!isfinite(out.delayFeedback) || out.delayFeedback < 0.0f || out.delayFeedback > 0.95f)
    out.delayFeedback = Settings::FILTER_DELAY_FB_DEFAULT;
  if (!isfinite(out.delayMix) || out.delayMix < 0.0f || out.delayMix > 1.0f)
    out.delayMix = Settings::FILTER_DELAY_MIX_DEFAULT;
}

void filtersSetComp(bool en, float thr, float ratio, uint32_t attackMs, uint32_t releaseMs)
{
  st.compEnabled = en;
  st.compThr = thr;
  st.compRatio = ratio;
  st.compAttackMs = attackMs;
  st.compReleaseMs = releaseMs;
  st.compGain = 1.0f;
  st.compLastMs = millis();
}

void filtersSetEnv(bool en, uint32_t attackMs, uint32_t releaseMs)
{
  st.envEnabled = en;
  st.envAttackMs = attackMs;
  st.envReleaseMs = releaseMs;
  st.envValue = -1.0f;
  st.envLastMs = millis();
}

void filtersSetFold(bool en, float amt)
{
  st.foldEnabled = en;
  st.foldAmt = amt;
}

void filtersSetDelay(bool en, uint32_t delayMs, float feedback, float mix)
{
  st.delayEnabled = en;
  st.delayMs = delayMs;
  st.delayFeedback = feedback;
  st.delayMix = mix;
}

#endif // ENABLE_FILTERS
