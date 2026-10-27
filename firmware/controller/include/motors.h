#pragma once

#include <stdint.h>

#include "config.h"

namespace motors {
    bool begin();
    // steps/s, each clamped to limit (MAX_SPEED unless a test asks for more, never
    // more than the step generator can do)
    void setSpeed(float left, float right, float limit = config::motors::MAX_SPEED);
    void enable();
    void disable();
    void stop();
    float getLeftSpeed();
    float getRightSpeed();
    int32_t getPositionDifference();
    int32_t getPosition();
    uint32_t getTimerRestarts();         // times the step timer had to be restarted
    void printStatus();
}