#pragma once

/**
 * @file presence_radar.h
 * @brief RD-03/RD-03D 24GHz mmWave presence radar (UART), fully independent from PresenceBLE.
 *
 * Build flags (see lamp_config.h for the ENABLE_RD03 default):
 *   ENABLE_RD03       - master compile switch (0 = zero footprint, default)
 *   RD03_MULTI_TARGET - 0 = RD-03 single-target parser (default), 1 = RD-03D multi-target parser
 *   RD03_BAUD         - UART baud rate (default 256000)
 *   RD03_UART_NUM     - HardwareSerial index to use (default 2)
 *   RD03_RX_PIN/RD03_TX_PIN - UART pins (default 16/17)
 *   RD03_DEBUG_RAW    - 1 = compile in a raw-hex frame dump for protocol bring-up (default 0)
 *
 * Protocol confidence: the multi-target (RD-03D) frame format implemented here is the
 * community-documented LD2450-style protocol (AA FF 03 00 header, 3x8-byte target blocks,
 * 55 CC trailer). It is unverified against this specific module and should be checked with
 * RD03_DEBUG_RAW before trusting the three behaviors below. The single-target (RD-03, non-D)
 * parser reuses the same outer framing with a single 8-byte block as a best-effort guess and
 * is the least certain part of this module - use RD03_DEBUG_RAW to capture real frames and
 * correct field offsets if the reported distance/speed don't look right.
 */

#include <Arduino.h>

#include "settings.h"

#ifndef ENABLE_RD03
#define ENABLE_RD03 0
#endif

#if ENABLE_RD03

#ifndef RD03_MULTI_TARGET
#define RD03_MULTI_TARGET 0
#endif
#ifndef RD03_BAUD
#define RD03_BAUD 256000
#endif
#ifndef RD03_UART_NUM
#define RD03_UART_NUM 2
#endif
#ifndef RD03_RX_PIN
#define RD03_RX_PIN 16
#endif
#ifndef RD03_TX_PIN
#define RD03_TX_PIN 17
#endif
#ifndef RD03_DEBUG_RAW
#define RD03_DEBUG_RAW 0
#endif

// ---------- Runtime state ----------
extern bool radarEnabled;
extern bool radarPresent;
extern float radarDistanceCm;
extern float radarSpeedCmS;
extern uint8_t radarTargetCount;
extern uint32_t radarLastFrameMs;
extern bool radarDebugRaw;

// Feature 1: touchless distance dimming
extern bool radarDimEnabled;
extern float radarDimNearCm;
extern float radarDimFarCm;

// Feature 2: motion-gated auto-on (velocity threshold)
extern bool radarMotionOnEnabled;
extern float radarMotionSpeedThreshold;
extern uint32_t radarMotionHoldMs;

// Feature 3: distance-based auto-off
extern bool radarOffDistanceEnabled;
extern float radarOffDistanceCm;
extern uint32_t radarOffGraceMs;

void radarSetup();
void updateRadar();

#endif // ENABLE_RD03
