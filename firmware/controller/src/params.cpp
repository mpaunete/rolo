#include "params.h"
#include "config.h"

#include <Arduino.h>
#include <string.h>
#include <Preferences.h>

namespace params {
    namespace {
        Params current;

        const Entry table[] = {
            {"kp",             &current.kp,             0.0f,    10000.0f},
            {"kd",             &current.kd,             0.0f,    3000.0f},
            {"dFilterTau",     &current.dFilterTau,     0.0f,    0.1f},
            {"kpSpeed",        &current.kpSpeed,        0.0f,    0.01f},
            {"brakeSpeed",     &current.brakeSpeed,     50.0f,   3000.0f},
            {"kiSpeed",        &current.kiSpeed,        0.0f,    0.01f},
            {"maxTargetAngle", &current.maxTargetAngle, 0.0f,    20.0f},
            {"accelLean",      &current.accelLean,      0.0f,    0.002f},
            {"driveSpeed",     &current.driveSpeed,     0.0f,    9000.0f},
            {"driveTurn",      &current.driveTurn,      0.0f,    3000.0f},
            {"driveAccel",     &current.driveAccel,     200.0f,  20000.0f},
            {"armUp",          &current.armUp,          0.0f,    180.0f},
            {"armDown",        &current.armDown,        0.0f,    180.0f},
            {"armFreq",        &current.armFreq,        0.5f,    10.0f},
            {"armDamping",     &current.armDamping,     0.2f,    1.5f},
        };

        constexpr size_t TABLE_SIZE = sizeof(table) / sizeof(table[0]);
        constexpr const char *STORAGE = "tuning";  // new name: old saved values are ignored

        volatile uint32_t changeCount = 0;         // so the web pages know to refresh
    }

    Params &get() {
        return current;
    }

    bool set(const char *name, float value) {
        for (size_t i = 0; i < TABLE_SIZE; i++) {
            if (strcmp(table[i].name, name) == 0) {
                *table[i].value = constrain(value, table[i].min, table[i].max);
                changeCount++;
                return true;
            }
        }
        return false;
    }

    size_t count() {
        return TABLE_SIZE;
    }

    const Entry &entry(size_t index) {
        return table[index];
    }

    void load() {
        Preferences prefs;

        if (!prefs.begin(STORAGE, true)) {
            return;  // nothing saved yet
        }

        for (size_t i = 0; i < TABLE_SIZE; i++) {
            const Entry &e = table[i];
            float stored = prefs.getFloat(e.name, *e.value);
            *e.value = constrain(stored, e.min, e.max);
        }

        prefs.end();
        changeCount++;
    }

    void save() {
        Preferences prefs;
        prefs.begin(STORAGE, false);
        prefs.clear();  // drop values of parameters that no longer exist

        for (size_t i = 0; i < TABLE_SIZE; i++) {
            prefs.putFloat(table[i].name, *table[i].value);
        }

        prefs.end();
    }

    uint32_t changes() {
        return changeCount;
    }

    void resetToDefaults() {
        namespace cfg = config::balance;
        changeCount++;

        current.kp = cfg::KP;
        current.kd = cfg::KD;
        current.dFilterTau = cfg::D_FILTER_TAU;

        current.kpSpeed = cfg::KP_SPEED;
        current.brakeSpeed = cfg::BRAKE_SPEED;
        current.kiSpeed = cfg::KI_SPEED;
        current.maxTargetAngle = cfg::MAX_TARGET_ANGLE;
        current.accelLean = cfg::ACCEL_LEAN;

        current.driveSpeed = config::drive::MAX_SPEED;
        current.driveTurn = config::drive::MAX_TURN;
        current.driveAccel = config::drive::ACCEL;

        current.armUp = config::arm::UP_DEG;
        current.armDown = config::arm::DOWN_DEG;
        current.armFreq = config::arm::FREQ_HZ;
        current.armDamping = config::arm::DAMPING;
    }
}