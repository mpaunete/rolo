#include "standup.h"
#include "config.h"
#include "console.h"
#include "balance.h"
#include "arm.h"
#include "stream.h"

#include <Arduino.h>
#include <stdio.h>

namespace standup {
    namespace cfg = config::standup;

    namespace {
        enum class Phase {
            Idle,
            Contact,   // arm moving onto the floor
            Lifting,   // arm pushing the body up, waiting for it to stop rising
            Kicking,   // wheels driving under the body
        };

        Phase phase = Phase::Idle;

        // Written by the console (any task), picked up in update() (control loop)
        volatile bool requested = false;
        volatile float wantAccel = 0.0f;
        volatile float wantSpeed = 0.0f;
        volatile float wantAngle = 0.0f;
        volatile int8_t buttonEvent = -1;   // from the web server or gamepad: 1 pressed, 0 released, -1 none

        float kickAccel = 0.0f;
        float kickSpeed = 0.0f;
        float kickAngle = 0.0f;
        float direction = 1.0f;    // +1 lying on its front (drive forward), -1 on its back
        float elapsed = 0.0f;      // seconds in this phase
        float settled = 0.0f;      // seconds the body has been still while lifting
        float lyingTilt = 0.0f;    // where it started
        float speed = 0.0f;        // steps/s, magnitude
        float best = 0.0f;         // closest to upright during the kick, degrees

        // Recorded alongside the samples, and shown on the serial port / terminal
        void note(const char *what, float angle) {
            char text[64];
            snprintf(text, sizeof(text), "stand-up: %s at %.1f deg, %.0f steps/s", what, angle, direction * speed);
            stream::event(text);
            console::out.println(text);
        }

        void finish(const char *how, float angle) {
            note(how, angle);
            phase = Phase::Idle;
            speed = 0.0f;
            arm::setDown(false);
        }

        void begin(float angle) {
            if (balance::getState() != balance::State::Ready) {
                stream::event("stand-up: only while lying down");
                console::out.println("stand-up: only while lying down");
                return;
            }

            kickAccel = wantAccel;
            kickSpeed = fminf(wantSpeed, config::motors::HARDWARE_MAX_SPEED);
            kickAngle = wantAngle;
            direction = angle > 0.0f ? 1.0f : -1.0f;
            lyingTilt = fabsf(angle);
            speed = 0.0f;
            elapsed = 0.0f;
            arm::hold(cfg::ARM_CONTACT);
            phase = Phase::Contact;
            note("arm to the floor", angle);
        }
    }

    void request(float accel, float s, float angle) {
        wantAccel = accel;
        wantSpeed = s;
        wantAngle = angle;
        requested = true;
    }

    void armButton(bool pressed) {
        buttonEvent = pressed ? 1 : 0;
    }

    void update(float angle, float gyroRate, float dt) {
        float tilt = fabsf(angle);

        if (buttonEvent >= 0) {
            bool pressed = buttonEvent == 1;
            buttonEvent = -1;
            if (phase != Phase::Idle) {
                // a stand-up is running: the button doesn't interfere
            } else if (pressed && balance::getState() == balance::State::Ready) {
                request(cfg::KICK_ACCEL, cfg::KICK_SPEED, cfg::KICK_ANGLE);
            } else {
                arm::setDown(pressed);
            }
        }
        
        if (requested) {
            requested = false;
            if (phase == Phase::Idle) {
                begin(angle);
            }
            return;
        }

        if (phase == Phase::Idle) {
            return;
        }

        elapsed += dt;

        if (phase == Phase::Contact) {
            if (elapsed >= cfg::CONTACT_TIME) {
                arm::setDown(true);
                phase = Phase::Lifting;
                elapsed = 0.0f;
                settled = 0.0f;
                note("lifting", angle);
            }
            return;
        }

        if (phase == Phase::Lifting) {
            // Kick as the rising body passes the kick angle, so the wheels add to its
            // momentum; or, if it stops short of that, once it has stopped rising
            bool lifted = tilt < lyingTilt - cfg::MIN_LIFT;
            settled = (lifted && fabsf(gyroRate) < cfg::SETTLED_RATE) ? settled + dt : 0.0f;

            if (tilt < kickAngle || settled >= cfg::SETTLED_TIME) {
                phase = Phase::Kicking;
                elapsed = 0.0f;
                best = tilt;
                note("kick", angle);
            } else if (elapsed > cfg::LIFT_TIMEOUT) {
                finish("arm could not lift it", angle);
            }
            return;
        }

        // Kicking: ramp up to the kick speed, then hold it
        speed = fminf(speed + kickAccel * dt, kickSpeed);
        best = fminf(best, tilt);

        if (tilt < cfg::CATCH_ANGLE) {
            balance::start(direction * speed);
            finish("caught", angle);
        } else if (tilt > best + cfg::FALL_BACK) {
            finish("fell back", angle);
        } else if (elapsed > cfg::KICK_TIMEOUT) {
            finish("timed out", angle);
        }
    }

    bool isDriving() {
        return phase == Phase::Kicking;
    }

    float getSpeed() {
        return direction * speed;
    }
}
