#pragma once

#include "lamp_config.h"

#if ENABLE_BLE_MIDI

#include <Arduino.h>

/**
 * @brief Feed one raw MIDI byte into the MIDI Show Control (MSC) SysEx
 * reassembler/dispatcher.
 *
 * Transport-agnostic: the caller is responsible for stripping anything
 * transport-specific (e.g. BLE-MIDI's timestamp bytes) before calling this -
 * everything handed in here is treated as a plain MIDI byte stream. Bytes
 * outside of an 0xF0..0xF7 SysEx span are ignored, so it's safe to feed it
 * every byte unconditionally alongside normal channel-voice message parsing.
 */
void feedMscByte(uint8_t b);

#endif
