#include <Arduino.h>

#include "settings.h"
#include "comms.h"
#include "utils.h"
#include "inputs.h"
#include "lamp_state.h"
#include "command.h"

#if ENABLE_MUSIC_MODE
float patternMusicDirect(uint32_t) { return 1.0f; }
float patternMusicBeat(uint32_t) { return 1.0f; }
bool musicEnabled = Settings::MUSIC_DEFAULT_ENABLED;
float musicFiltered = 0.0f;
float musicDc = 0.5f;
float musicEnv = 0.0f;
float musicRawLevel = 0.0f;
uint32_t lastMusicSampleMs = 0;
float musicGain = Settings::MUSIC_GAIN_DEFAULT;
float musicSmoothing = 0.4f; // 0..1 low-pass for direct mode
bool musicAutoLamp = false;
float musicAutoThr = 0.4f;
uint8_t musicMode = 0; // 0=direct,1=beat
float musicModScale = 1.0f;
float musicBeatEnv = 0.0f;
float musicBeatIntervalMs = 600.0f;
uint32_t musicLastBeatMs = 0;
uint32_t musicLastKickMs = 0;
float clapPrevEnv = 0.0f;
bool clapEnabled = Settings::CLAP_DEFAULT_ENABLED;
float clapThreshold = Settings::CLAP_THRESHOLD_DEFAULT;
uint32_t clapCooldownMs = Settings::CLAP_COOLDOWN_MS_DEFAULT;
uint32_t clapLastMs = 0;
bool clapAbove = false;
String clapCmd1 = F("");
String clapCmd2 = F("toggle");
String clapCmd3 = F("");
uint8_t clapCount = 0;
uint32_t clapWindowStartMs = 0;
constexpr uint32_t CLAP_WINDOW_MS = 900;
constexpr float CLAP_RISE_MIN = 0.02f;
bool clapTraining = false;
uint32_t clapTrainLastLog = 0;
bool musicPatternActive = false; // true when a Music pattern is selected
#endif


#if ENABLE_MUSIC_MODE
void executeClapCommand(uint8_t count)
{
    String cmd = (count == 1) ? clapCmd1 : (count == 2) ? clapCmd2
                                                        : clapCmd3;
    cmd.trim();
    if (cmd.isEmpty())
        return;
    sendFeedback(String(F("[Clap] ")) + count + F("x -> ") + cmd);
    if (cmd.startsWith(F("clap ")))
        return; // avoid recursion
    handleCommand(cmd);
}
#else
void executeClapCommand(uint8_t)
{
    // Music/clap disabled at build; ignore
}
#endif

#if ENABLE_MUSIC_MODE
void updateMusicSensor()
{
    bool active = musicEnabled || clapEnabled || musicAutoLamp;
    if (!active)
    {
        // Reset any lingering modulation state and skip ADC when audio features are off
        musicModScale = 1.0f;
        musicBeatEnv = 0.0f;
        musicBeatIntervalMs = 600.0f;
        musicLastBeatMs = 0;
        musicLastKickMs = 0;
        return;
    }
    uint32_t now = millis();
    if (now - lastMusicSampleMs < Settings::MUSIC_SAMPLE_MS)
        return;
    lastMusicSampleMs = now;
    int raw = analogRead(Settings::MUSIC_PIN);
    // clamp ADC to valid range (some boards return negative when floating)
    if (raw < 0)
        raw = 0;
    if (raw > 4095)
        raw = 4095;
    // envelope with DC removal: track slow baseline, then rectify delta
    float val = (float)raw / 4095.0f;
    musicRawLevel = clamp01(val);
    static bool musicEnvInit = false;
    if (!musicEnvInit)
    {
        musicDc = val;
        musicEnv = 0.0f;
        musicEnvInit = true;
    }
    const float dcAlpha = 0.01f; // slow baseline for bias removal
    musicDc = (1.0f - dcAlpha) * musicDc + dcAlpha * val;
    float delta = fabsf(val - musicDc);
    const float envAlpha = Settings::MUSIC_ALPHA; // faster attack/decay for percussive signals
    musicEnv = (1.0f - envAlpha) * musicEnv + envAlpha * delta;
    // Boost envelope a bit for visibility
    musicFiltered = clamp01(musicEnv * musicGain * 1.5f);
    bool kickDetected = false;
    // If no feature needs audio, reset modifiers and bail after updating status fields
    // Modulate brightness scale based on mode
    if (musicEnabled)
    {
        if (musicMode == 0)
        {
            // Direct mode: map envelope to a wider modulation range for visibility
            float target = 0.25f + 2.2f * musicFiltered; // up to ~1.5 before clamp
            target = target > 1.5f ? 1.5f : target;
            musicModScale = 0.6f * musicModScale + 0.4f * target;
            if (musicFiltered > musicAutoThr * 1.2f)
                kickDetected = true;
        }
        else // beat mode
        {
            bool rising = (musicFiltered > musicAutoThr) && (musicBeatEnv <= musicAutoThr);
            musicBeatEnv = musicFiltered;
            uint32_t nowMs = millis();
            if (rising)
            {
                if (musicLastBeatMs > 0)
                {
                    float interval = (float)(nowMs - musicLastBeatMs);
                    if (interval > 200.0f && interval < 2000.0f)
                        musicBeatIntervalMs = 0.8f * musicBeatIntervalMs + 0.2f * interval;
                }
                musicLastBeatMs = nowMs;
                musicModScale = 0.8f; // kick on beat but avoid full current spike
                kickDetected = true;
            }
            else
            {
                float decayMs = fmaxf(250.0f, musicBeatIntervalMs * 0.6f);
                float k = expf(-(Settings::MUSIC_SAMPLE_MS) / decayMs);
                // decay toward a low floor; keep a neutral baseline so patterns remain visible
                const float floor = 0.15f;
                musicModScale = floor + (musicModScale - floor) * k;
            }
        }
    }
    // hard clamp modulation to avoid overdriving LEDs (helps prevent brownouts)
    musicModScale = clamp01(musicModScale);
    // Apply additional smoothing in direct mode (after modulation) to emulate afterglow
    if (musicEnabled && musicMode == 0 && musicSmoothing > 0.0f)
    {
        musicModScale = (1.0f - musicSmoothing) * musicModScale + musicSmoothing * musicFiltered;
    }
    if (kickDetected)
        musicLastKickMs = now;
    // Auto lamp on when switch is ON and audio crosses threshold (rising edge)
    static bool musicAutoAbove = false;
    const bool above = musicFiltered >= musicAutoThr;
    if (musicAutoLamp && switchDebouncedState && above && !musicAutoAbove)
    {
        setLampEnabled(true, "music auto");
        lastActivityMs = now;
    }
    musicAutoAbove = above && musicAutoLamp;
    if (clapTraining && (now - clapTrainLastLog) >= 200)
    {
        clapTrainLastLog = now;
        sendFeedback(String(F("[ClapTrain] env=")) + String(musicFiltered, 3) + F(" thr=") + String(clapThreshold, 2) +
                     F(" above=") + (musicFiltered >= clapThreshold ? F("1") : F("0")));
    }
    if (clapEnabled)
    {
        float clapDelta = musicEnv - clapPrevEnv;
        clapPrevEnv = musicEnv;
        float riseNeeded = std::max(CLAP_RISE_MIN, clapThreshold * 0.3f);
        bool risingEdge = (musicEnv >= clapThreshold) && (clapDelta >= riseNeeded);
        if (risingEdge && (now - clapLastMs) >= clapCooldownMs)
        {
            clapLastMs = now;
            if (clapWindowStartMs == 0)
                clapWindowStartMs = now;
            ++clapCount;
            clapAbove = true;
        }
        else
        {
            // reset latch when sufficiently below threshold
            if (musicEnv < clapThreshold * 0.3f)
                clapAbove = false;
        }

        if (clapWindowStartMs && (now - clapWindowStartMs) >= CLAP_WINDOW_MS)
        {
            if (clapCount > 0)
            {
                uint8_t count = clapCount > 3 ? 3 : clapCount;
                executeClapCommand(count);
                sendFeedback(String(F("[Clap] detected ")) + String(count) + F("x"));
            }
            clapCount = 0;
            clapWindowStartMs = 0;
        }
    }
}
#endif