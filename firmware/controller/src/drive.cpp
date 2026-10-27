#include "drive.h"
#include "config.h"
#include "params.h"

#include <Arduino.h>

namespace drive {
    namespace cfg = config::drive;

    namespace {
        // Written by the web server (core 0), read by the control loop (core 1)
        volatile float joyX = 0.0f;
        volatile float joyY = 0.0f;
        volatile uint32_t lastCommand = 0;

        float speed = 0.0f;
        float turn = 0.0f;
        float accel = 0.0f;

        // Move value towards target by at most maxStep
        float approach(float value, float target, float maxStep) {
            if (value < target) {
                return fminf(value + maxStep, target);
            }
            return fmaxf(value - maxStep, target);
        }
    }

    void command(float x, float y) {
        joyX = constrain(x, -1.0f, 1.0f);
        joyY = constrain(y, -1.0f, 1.0f);
        lastCommand = millis();
    }

    void update(float dt, bool enabled) {
        float targetSpeed = 0.0f;
        float targetTurn = 0.0f;

        // No command for a while (page closed, WiFi dropped): stop
        bool fresh = (uint32_t)(millis() - lastCommand) < cfg::TIMEOUT_MS;

        const params::Params &p = params::get();

        if (enabled && fresh) {
            targetSpeed = joyY * p.driveSpeed;
            targetTurn = joyX * p.driveTurn;
        }

        if (!enabled) {
            speed = 0.0f;   // start from rest next time it's stood up
            turn = 0.0f;
            accel = 0.0f;
            return;
        }

        // Same rate for speeding up and braking
        float previous = speed;
        speed = approach(speed, targetSpeed, p.driveAccel * dt);
        accel = (speed - previous) / dt;   // for the balance loop's lean feed-forward

        turn = approach(turn, targetTurn, p.driveAccel * dt);
    }

    float getSpeed() {
        return speed;
    }

    float getTurn() {
        return turn;
    }

    float getAccel() {
        return accel;
    }
    float getJoyX() {
        return joyX;
    }

    float getJoyY() {
        return joyY;
    }
}