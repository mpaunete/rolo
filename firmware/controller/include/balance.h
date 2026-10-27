#pragma once

#include <stdint.h>

namespace balance {
    enum class State {
        Balancing,   // upright and running
        Ready,       // lying down: stand it up to start
        LayDown,     // tripped while upright (runaway, or never laid down): lay it down to re-arm
    };

    float update(float angle, float gyroRate, int32_t position, float dt, float targetSpeed, float targetAccel);
    bool isTripped();                           // tilt cutoff active

    // Take over now, with the wheels already turning at wheelSpeed (from the stand-up kick)
    void start(float wheelSpeed);
    float getTargetAngle();
    State getState();
}