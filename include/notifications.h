
#include "Arduino.h"
#include <vector>

// Notify blink
extern std::vector<uint32_t> notifySeq;
extern uint8_t notifyIdx;
extern uint32_t notifyStageStartMs;
extern bool notifyInvert;
extern bool notifyRestoreLamp;
extern bool notifyPrevLampOn;
extern bool notifyActive;
extern uint32_t notifyFadeMs;
extern float notifyMinBrightness; // 0..1 floor for notify output
