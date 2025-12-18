#include "Arduino.h"
#include "vector"

#include "quickmode.h"
#include "comms.h"

// Demo cycling
bool demoActive = false;
uint32_t demoDwellMs = 6000;
std::vector<uint8_t> demoList;
size_t demoIndex = 0;
uint32_t demoLastSwitchMs = 0;

/**
 * @brief Build and start a demo cycle through quick-enabled modes with fixed dwell.
 */
void startDemo(uint32_t dwellMs)
{
    demoList.clear();
    size_t total = quickModeCount();
    for (size_t i = 0; i < total; ++i)
    {
        if (isQuickEnabled(i))
            demoList.push_back((uint8_t)i);
    }
    if (demoList.empty())
    {
        demoActive = false;
        sendFeedback(F("[Demo] Quick list empty"));
        return;
    }
    demoDwellMs = dwellMs;
    if (demoDwellMs < 500)
        demoDwellMs = 500;
    if (demoDwellMs > 600000)
        demoDwellMs = 600000;
    demoIndex = 0;
    demoLastSwitchMs = millis();
    demoActive = true;
    applyQuickMode(demoList[demoIndex]);
    sendFeedback(String(F("[Demo] Start dwell=")) + String(demoDwellMs) + F("ms list=") + quickMaskToCsv());
}

void stopDemo()
{
    demoActive = false;
    sendFeedback(F("[Demo] Stopped"));
}