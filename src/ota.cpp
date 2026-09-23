#include "ota.h"

#if ENABLE_OTA

#include <Update.h>
#include "libb64/cdecode.h"

#include "comms.h"

bool otaActive = false;
size_t otaExpectedSize = 0;
size_t otaReceivedSize = 0;

static String otaExpectedMd5;
static String otaVersion;
static uint32_t otaLastChunkMs = 0;
static uint32_t otaRebootAtMs = 0;

static const uint32_t OTA_TIMEOUT_MS = 15000;

// Sized for the largest chunk we accept (Serial/USB chunks are the bigger of the two transports,
// see frontend/src/lib/ota.ts). Static (not stack) so a big chunk can't blow the loop task's stack.
static const size_t OTA_MAX_B64_CHARS = 6100;
static const size_t OTA_DECODE_BUF_SIZE = 4608;
static uint8_t otaDecodeBuf[OTA_DECODE_BUF_SIZE];

static void otaReset()
{
    otaActive = false;
    otaExpectedSize = 0;
    otaReceivedSize = 0;
    otaExpectedMd5 = "";
    otaVersion = "";
    otaLastChunkMs = 0;
}

static void otaFail(const String &reason)
{
    if (otaActive)
        Update.abort();
    otaReset();
    sendFeedback(String(F("[OTA] ")) + reason);
}

bool otaBegin(size_t size, const String &md5Hex, const String &version)
{
    if (otaActive)
    {
        sendFeedback(F("[OTA] Already in progress, send 'ota abort' first"));
        return false;
    }
    if (size == 0 || md5Hex.length() != 32)
    {
        sendFeedback(F("[OTA] Usage: ota begin <size> <md5hex32> <version>"));
        return false;
    }
    if (!Update.begin(size))
    {
        sendFeedback(String(F("[OTA] begin failed: ")) + Update.errorString());
        return false;
    }
    if (!Update.setMD5(md5Hex.c_str()))
    {
        Update.abort();
        sendFeedback(F("[OTA] Invalid MD5 string"));
        return false;
    }
    otaActive = true;
    otaExpectedSize = size;
    otaReceivedSize = 0;
    otaExpectedMd5 = md5Hex;
    otaVersion = version;
    otaLastChunkMs = millis();
    sendFeedback(String(F("[OTA] ready size=")) + String(size) + F(" version=") + version);
    return true;
}

bool otaChunk(const String &base64Data)
{
    if (!otaActive)
    {
        sendFeedback(F("[OTA] Not active, send 'ota begin' first"));
        return false;
    }
    int inLen = base64Data.length();
    if (inLen == 0)
        return true;
    if ((size_t)inLen > OTA_MAX_B64_CHARS)
    {
        otaFail(F("Chunk too large, aborted"));
        return false;
    }

    int outLen = base64_decode_chars(base64Data.c_str(), inLen, (char *)otaDecodeBuf);
    if (outLen <= 0 || (size_t)outLen > OTA_DECODE_BUF_SIZE)
    {
        otaFail(F("Bad base64 chunk, aborted"));
        return false;
    }
    if (otaReceivedSize + (size_t)outLen > otaExpectedSize)
    {
        otaFail(F("Received more than expected size, aborted"));
        return false;
    }
    size_t written = Update.write(otaDecodeBuf, (size_t)outLen);
    if (written != (size_t)outLen)
    {
        otaFail(String(F("Flash write failed: ")) + Update.errorString());
        return false;
    }
    otaReceivedSize += written;
    otaLastChunkMs = millis();
    return true;
}

bool otaEnd()
{
    if (!otaActive)
    {
        sendFeedback(F("[OTA] Not active"));
        return false;
    }
    if (otaReceivedSize != otaExpectedSize)
    {
        otaFail(String(F("Size mismatch: got ")) + String(otaReceivedSize) + F(" expected ") + String(otaExpectedSize));
        return false;
    }
    String version = otaVersion;
    if (!Update.end(true))
    {
        otaFail(String(F("Verify failed: ")) + Update.errorString());
        return false;
    }
    sendFeedback(String(F("[OTA] Success, rebooting to ")) + version);
    otaReset();
    otaRebootAtMs = millis() + 1500;
    return true;
}

void otaAbort()
{
    if (!otaActive)
        return;
    otaFail(F("Aborted"));
}

void otaLoop()
{
    uint32_t now = millis();
    if (otaActive && otaLastChunkMs > 0 && (now - otaLastChunkMs) > OTA_TIMEOUT_MS)
    {
        otaFail(F("Timed out waiting for next chunk, aborted"));
    }
    if (otaRebootAtMs > 0 && (int32_t)(now - otaRebootAtMs) >= 0)
    {
        otaRebootAtMs = 0;
        ESP.restart();
    }
}

#endif // ENABLE_OTA
