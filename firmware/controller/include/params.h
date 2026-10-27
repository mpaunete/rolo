#pragma once

#include <stddef.h>
#include <stdint.h>

namespace params {
    struct Params {
        // Balance (inner loop)
        float kp;
        float kd;
        float dFilterTau;

        // Speed (outer loop)
        float kpSpeed;
        float brakeSpeed;
        float kiSpeed;
        float maxTargetAngle;
        float accelLean;

        // Driving
        float driveSpeed;
        float driveTurn;
        float driveAccel;

        // Arm servo
        float armUp;
        float armDown;
        float armFreq;
        float armDamping;
    };

    struct Entry {
        const char *name;
        float *value;
        float min;
        float max;
    };

    Params &get();
    bool set(const char *name, float value);

    size_t count();
    const Entry &entry(size_t index);

    void load();
    void save();

    void resetToDefaults();

    uint32_t changes();   // goes up whenever a value changes (set, load, reset)

}