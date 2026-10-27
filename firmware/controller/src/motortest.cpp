#include "motortest.h"
#include "balance.h"
#include "config.h"
#include "console.h"
#include "stream.h"

#include <Arduino.h>
#include <stdio.h>

namespace motortest {
    namespace cfg = config::motors;

    namespace {
        enum class Phase { Idle, Up, Hold, Down };

        Phase phase = Phase::Idle;

        // Written by the console (any task), picked up in update() (control loop)
        volatile bool requested = false;
        volatile bool stopRequested = false;
        volatile float wantSpeed = 0.0f;
        volatile float wantSeconds = 0.0f;
        volatile float wantAccel = 0.0f;

        float target = 0.0f;
        float holdSeconds = 0.0f;
        float accel = 0.0f;
        float speed = 0.0f;
        float elapsed = 0.0f;

        void note(const char *text) {
            char line[64];
            snprintf(line, sizeof(line), "motor test: %s (%.0f steps/s)", text, speed);
            stream::event(line);          // in the recording
            console::out.println(line);   // and on the serial port / terminal
        }
    }

    void request(float s, float seconds, float a) {
        wantSpeed = s;
        wantSeconds = seconds;
        wantAccel = a;
        requested = true;
    }

    void stop() {
        stopRequested = true;
    }

    void update(float dt) {
        if (requested) {
            requested = false;
            if (!balance::isTripped()) {
                stream::event("motor test: refused, the robot is balancing");
                console::out.println("motor test: refused, the robot is balancing");
            } else {
                target = constrain(wantSpeed, -cfg::HARDWARE_MAX_SPEED, cfg::HARDWARE_MAX_SPEED);
                holdSeconds = wantSeconds;
                accel = wantAccel;
                speed = 0.0f;
                phase = Phase::Up;
                note("starting");
            }
        }

        if (phase == Phase::Idle) {
            return;
        }

        // Balancing has started (robot stood up in your hands): it owns the wheels now
        if (!balance::isTripped()) {
            phase = Phase::Idle;
            speed = 0.0f;
            note("stopped, the robot started balancing");
            return;
        }

        if (stopRequested && phase != Phase::Down) {
            stopRequested = false;
            phase = Phase::Down;
            note("stopping");
        }

        float step = accel * dt;
        if (phase == Phase::Up) {
            speed = target > speed ? fminf(speed + step, target) : fmaxf(speed - step, target);
            if (speed == target) {
                phase = Phase::Hold;
                elapsed = 0.0f;
                note("at speed");
            }
        } else if (phase == Phase::Hold) {
            elapsed += dt;
            if (elapsed >= holdSeconds) {
                phase = Phase::Down;
            }
        } else {
            speed = speed > 0.0f ? fmaxf(speed - step, 0.0f) : fminf(speed + step, 0.0f);
            if (speed == 0.0f) {
                phase = Phase::Idle;
                stopRequested = false;
                note("done");
            }
        }
    }

    bool isRunning() {
        return phase != Phase::Idle;
    }

    float getSpeed() {
        return speed;
    }
}
