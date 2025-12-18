
#include "Arduino.h"
#include <vector>

// Notify blink
std::vector<uint32_t> notifySeq = {120, 60, 120, 200};
uint8_t notifyIdx = 0;
uint32_t notifyStageStartMs = 0;
bool notifyInvert = false;
bool notifyRestoreLamp = false;
bool notifyPrevLampOn = false;
bool notifyActive = false;
uint32_t notifyFadeMs = 0;