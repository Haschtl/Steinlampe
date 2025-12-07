#pragma once

#include "lamp_config.h"

#if ENABLE_BT_SERIAL && ENABLE_BT_MIDI

// Parse classic BT (SPP) MIDI bytes; call per incoming byte.
void processBtMidiByte(uint8_t b);

#endif
