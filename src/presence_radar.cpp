#include "presence_radar.h"

#if ENABLE_RD03

#include <math.h>

#include "comms.h"
#include "inputs.h"
#include "lamp_state.h"
#include "utils.h"

// ---------- Runtime state ----------
bool radarEnabled = Settings::RD03_DEFAULT_ENABLED;
bool radarPresent = false;
float radarDistanceCm = 0.0f;
float radarSpeedCmS = 0.0f;
uint8_t radarTargetCount = 0;
uint32_t radarLastFrameMs = 0;
bool radarDebugRaw = RD03_DEBUG_RAW;

bool radarDimEnabled = Settings::RD03_DIM_DEFAULT_ENABLED;
float radarDimNearCm = Settings::RD03_DIM_NEAR_CM_DEFAULT;
float radarDimFarCm = Settings::RD03_DIM_FAR_CM_DEFAULT;

bool radarMotionOnEnabled = Settings::RD03_MOTION_ON_DEFAULT_ENABLED;
float radarMotionSpeedThreshold = Settings::RD03_MOTION_SPEED_THR_DEFAULT;
uint32_t radarMotionHoldMs = Settings::RD03_MOTION_HOLD_MS_DEFAULT;

bool radarOffDistanceEnabled = Settings::RD03_OFF_DISTANCE_DEFAULT_ENABLED;
float radarOffDistanceCm = Settings::RD03_OFF_DISTANCE_CM_DEFAULT;
uint32_t radarOffGraceMs = Settings::RD03_OFF_GRACE_MS_DEFAULT;
bool radarHwOverride = Settings::RD03_HW_OVERRIDE_DEFAULT;

float radarVelocityFactor = Settings::RD03_VELOCITY_FACTOR_DEFAULT;
float radarVelocityScale = 1.0f;

// ---------- UART framing ----------
static HardwareSerial RadarSerial(RD03_UART_NUM);

static const uint8_t RD03_HDR[4] = {0xAA, 0xFF, 0x03, 0x00};
static const uint8_t RD03_TRAILER[2] = {0x55, 0xCC};
static const size_t RD03_BLOCK_BYTES = 8;
#if RD03_MULTI_TARGET
static const size_t RD03_BLOCK_COUNT = 3;
#else
static const size_t RD03_BLOCK_COUNT = 1;
#endif
static const size_t RD03_FRAME_BYTES = sizeof(RD03_HDR) + RD03_BLOCK_COUNT * RD03_BLOCK_BYTES + sizeof(RD03_TRAILER);

static const size_t RD03_BUF_CAP = 96;
static uint8_t radarBuf[RD03_BUF_CAP];
static size_t radarBufLen = 0;

// Feature-local state
static float radarDimFiltered = -1.0f;
static float radarDimLastApplied = -1.0f;
static const float RADAR_DIM_ALPHA = 0.3f;
static const float RADAR_DIM_DELTA_MIN = 0.02f;

// Reference speed at which the velocity brightness modifier reaches its full radarVelocityFactor;
// scales linearly (clamped) between 0 and this speed. Not exposed as a setting, keeps the
// feature to the single "factor" knob the user configures.
static const float RADAR_VELOCITY_REF_SPEED_CMS = 60.0f;

static uint32_t radarMotionLastQualifyMs = 0;
static uint32_t radarOffGraceDeadline = 0;

// LD2450-style signed-magnitude decode: bit15 set => positive magnitude in low 15 bits,
// bit15 clear => negative. Community-documented, unverified against this hardware.
static int16_t radarDecodeSigned(uint16_t raw)
{
    if (raw & 0x8000)
        return (int16_t)(raw & 0x7FFF);
    return -(int16_t)(0x8000 - raw);
}

static void radarBufferConsume(size_t n)
{
    if (n >= radarBufLen)
    {
        radarBufLen = 0;
        return;
    }
    memmove(radarBuf, radarBuf + n, radarBufLen - n);
    radarBufLen -= n;
}

static void radarReadIncoming()
{
    while (RadarSerial.available() && radarBufLen < RD03_BUF_CAP)
    {
        radarBuf[radarBufLen++] = (uint8_t)RadarSerial.read();
    }
}

static void radarDumpRawIfEnabled(const uint8_t *data, size_t len)
{
    if (!radarDebugRaw)
        return;
    String hex = F("[Radar] raw:");
    for (size_t i = 0; i < len; ++i)
    {
        if (data[i] < 0x10)
            hex += '0';
        hex += String(data[i], HEX);
        hex += ' ';
    }
    sendFeedback(hex);
}

/**
 * @brief Decode one 8-byte target block (X, Y, Speed, Distance-resolution, all int16 LE).
 * X/Y assumed millimeters, speed assumed cm/s directly (unverified units - check with
 * RD03_DEBUG_RAW). A block that decodes to X=Y=Speed=0 is treated as an empty slot.
 */
static bool radarDecodeBlock(const uint8_t *block, float &outDistanceCm, float &outSpeedCmS)
{
    uint16_t rawX = (uint16_t)block[0] | ((uint16_t)block[1] << 8);
    uint16_t rawY = (uint16_t)block[2] | ((uint16_t)block[3] << 8);
    uint16_t rawSpeed = (uint16_t)block[4] | ((uint16_t)block[5] << 8);

    int16_t xMm = radarDecodeSigned(rawX);
    int16_t yMm = radarDecodeSigned(rawY);
    int16_t speed = radarDecodeSigned(rawSpeed);

    if (xMm == 0 && yMm == 0 && speed == 0)
        return false;

    outDistanceCm = sqrtf((float)xMm * (float)xMm + (float)yMm * (float)yMm) / 10.0f;
#if RD03_MULTI_TARGET
    outSpeedCmS = (float)speed; // native speed field, only trusted on the multi-target protocol
#else
    outSpeedCmS = 0.0f; // single-target: derived from distance delta instead, see radarTryParseFrame()
#endif
    return true;
}

/**
 * @brief Look for one complete frame in the buffer, parse it, and consume it.
 * @return true if a frame was found and processed (whether or not a target was present).
 */
static bool radarTryParseFrame()
{
    if (radarBufLen < sizeof(RD03_HDR))
        return false;

    // Resync: find the header anywhere in the buffer, drop leading garbage.
    size_t hdrIdx = radarBufLen; // sentinel: "not found"
    for (size_t i = 0; i + sizeof(RD03_HDR) <= radarBufLen; ++i)
    {
        if (memcmp(radarBuf + i, RD03_HDR, sizeof(RD03_HDR)) == 0)
        {
            hdrIdx = i;
            break;
        }
    }
    if (hdrIdx == radarBufLen)
    {
        // No header anywhere: keep only the last few bytes (could be a split header).
        if (radarBufLen > sizeof(RD03_HDR))
            radarBufferConsume(radarBufLen - sizeof(RD03_HDR));
        return false;
    }
    if (hdrIdx > 0)
        radarBufferConsume(hdrIdx);

    if (radarBufLen < RD03_FRAME_BYTES)
        return false; // wait for more bytes

    const uint8_t *trailer = radarBuf + sizeof(RD03_HDR) + RD03_BLOCK_COUNT * RD03_BLOCK_BYTES;
    if (memcmp(trailer, RD03_TRAILER, sizeof(RD03_TRAILER)) != 0)
    {
        // Corrupt/misaligned frame: drop just the header byte and try to resync next call.
        radarDumpRawIfEnabled(radarBuf, RD03_FRAME_BYTES);
        radarBufferConsume(1);
        return true;
    }

    radarDumpRawIfEnabled(radarBuf, RD03_FRAME_BYTES);

    float bestDistanceCm = -1.0f;
    float bestSpeedCmS = 0.0f;
    uint8_t targets = 0;
    for (size_t t = 0; t < RD03_BLOCK_COUNT; ++t)
    {
        const uint8_t *block = radarBuf + sizeof(RD03_HDR) + t * RD03_BLOCK_BYTES;
        float distanceCm, speedCmS;
        if (!radarDecodeBlock(block, distanceCm, speedCmS))
            continue;
        targets++;
        if (bestDistanceCm < 0.0f || distanceCm < bestDistanceCm)
        {
            bestDistanceCm = distanceCm;
            bestSpeedCmS = speedCmS;
        }
    }

    uint32_t now = millis();
    radarTargetCount = targets;
    radarPresent = targets > 0;
    if (radarPresent)
    {
#if !RD03_MULTI_TARGET
        // No native speed field on the single-target protocol: derive it from distance delta.
        if (radarLastFrameMs > 0 && radarDistanceCm >= 0.0f)
        {
            float dtS = (float)(now - radarLastFrameMs) / 1000.0f;
            if (dtS > 0.01f)
                bestSpeedCmS = (bestDistanceCm - radarDistanceCm) / dtS;
        }
#endif
        radarDistanceCm = bestDistanceCm;
        radarSpeedCmS = bestSpeedCmS;
    }
    else
    {
        radarSpeedCmS = 0.0f;
    }
    radarLastFrameMs = now;

    radarBufferConsume(RD03_FRAME_BYTES);
    return true;
}

void radarSetup()
{
    RadarSerial.begin(RD03_BAUD, SERIAL_8N1, RD03_RX_PIN, RD03_TX_PIN);
}

void updateRadar()
{
    radarReadIncoming();
    // Drain any backlog, but cap iterations so a stuck resync can't loop forever.
    for (int i = 0; i < 4 && radarTryParseFrame(); ++i)
    {
    }

    if (!radarEnabled)
    {
        radarVelocityScale = 1.0f; // don't leave a stale multiplier applied once radar is disabled
        return;
    }

    uint32_t now = millis();
    const uint32_t STALE_MS = 1000;
    bool present = radarPresent && radarLastFrameMs > 0 && (now - radarLastFrameMs) <= STALE_MS;

    // Optional brightness modifier scaling with target velocity (factor 1.0 = disabled).
    {
        float target = 1.0f;
        if (radarVelocityFactor != 1.0f && present)
        {
            float normSpeed = clamp01(fabsf(radarSpeedCmS) / RADAR_VELOCITY_REF_SPEED_CMS);
            target = 1.0f + (radarVelocityFactor - 1.0f) * normSpeed;
        }
        radarVelocityScale += (target - radarVelocityScale) * RADAR_DIM_ALPHA;
    }

    // Feature 1: touchless distance dimming.
    if (radarDimEnabled && present)
    {
        float span = radarDimFarCm - radarDimNearCm;
        float level = (span > 1.0f) ? (1.0f - clamp01((radarDistanceCm - radarDimNearCm) / span)) : 1.0f;
        level = clamp01(level);
        radarDimFiltered = (radarDimFiltered < 0.0f) ? level : radarDimFiltered + (level - radarDimFiltered) * RADAR_DIM_ALPHA;
        if (radarDimLastApplied < 0.0f || fabsf(radarDimFiltered - radarDimLastApplied) >= RADAR_DIM_DELTA_MIN)
        {
            radarDimLastApplied = radarDimFiltered;
            setBrightnessPercent(radarDimFiltered * 100.0f, false, false, true);
        }
    }
    else
    {
        radarDimFiltered = -1.0f;
        radarDimLastApplied = -1.0f;
    }

    // Feature 2: motion-gated auto-on (only reacts to targets moving faster than the threshold).
    // Actions are gated by hardwareWantsOn() unless radarHwOverride allows overriding it in
    // both directions (see inputs.h).
    if (radarMotionOnEnabled)
    {
        if (present && fabsf(radarSpeedCmS) >= radarMotionSpeedThreshold)
        {
            radarMotionLastQualifyMs = now;
            if (!lampEnabled && (radarHwOverride || hardwareWantsOn()))
            {
                setLampEnabled(true, "radar-motion");
                sendFeedback(F("[Radar] Motion -> Lamp ON"));
            }
        }
        else if (radarMotionLastQualifyMs > 0 && (now - radarMotionLastQualifyMs) >= radarMotionHoldMs)
        {
            if (!lampEnabled)
            {
                radarMotionLastQualifyMs = 0;
            }
            else if (radarHwOverride || !hardwareWantsOn())
            {
                setLampEnabled(false, "radar-motion-hold");
                sendFeedback(F("[Radar] Motion hold expired -> Lamp OFF"));
                radarMotionLastQualifyMs = 0;
            }
            // else: hardware still wants the lamp on, keep pending and retry next tick.
        }
    }

    // Feature 3: distance-based auto-off (target absent or farther than the threshold).
    if (radarOffDistanceEnabled)
    {
        bool farOrGone = !present || radarDistanceCm > radarOffDistanceCm;
        if (farOrGone)
        {
            if (radarOffGraceDeadline == 0)
            {
                radarOffGraceDeadline = now + radarOffGraceMs;
            }
            else if (now >= radarOffGraceDeadline)
            {
                if (lampEnabled && (radarHwOverride || !hardwareWantsOn()))
                {
                    setLampEnabled(false, "radar-offdist");
                    sendFeedback(F("[Radar] Off-distance exceeded -> Lamp OFF"));
                }
                radarOffGraceDeadline = 0;
            }
        }
        else
        {
            radarOffGraceDeadline = 0;
        }
    }
}

#endif // ENABLE_RD03
