#include "arm.h"
#include "config.h"
#include "params.h"

#include <Arduino.h>

namespace arm {
    namespace cfg = config::arm;

    namespace {
        constexpr uint32_t PWM_HZ = 50;          // standard servo frame: 20 ms
        constexpr uint8_t PWM_BITS = 16;
        constexpr float FRAME_US = 1000000.0f / PWM_HZ;

        volatile bool wantDown = false;          // written by the web server (core 0)
        volatile float holdAt = -1.0f;           // a specific angle (stand-up), or < 0 for up/down
        float angle = 0.0f;                      // current angle, degrees
        float velocity = 0.0f;                   // degrees/s

        void writeAngle(float degrees) {
            float us = cfg::MIN_US + (degrees / 180.0f) * (cfg::MAX_US - cfg::MIN_US);
            uint32_t duty = (uint32_t)(us / FRAME_US * ((1 << PWM_BITS) - 1));
            ledcWrite(cfg::CHANNEL, duty);
        }
    }

    void begin() {
        ledcSetup(cfg::CHANNEL, PWM_HZ, PWM_BITS);
        ledcAttachPin(cfg::PIN, cfg::CHANNEL);

        angle = params::get().armUp;             // start up, no sweep
        writeAngle(angle);
    }

    void setDown(bool down) {
        holdAt = -1.0f;
        wantDown = down;
    }

    void hold(float degrees) {
        holdAt = constrain(degrees, 0.0f, 180.0f);
    }

    void update(float dt) {
        const params::Params &p = params::get();
        float target = holdAt >= 0.0f ? holdAt : (wantDown ? p.armDown : p.armUp);

        // Settled: nothing to send
        if (fabsf(target - angle) < 0.1f && fabsf(velocity) < 1.0f) {
            return;
        }

        // A spring pulls the arm towards the target, damping slows it down.
        // That gives a smooth start, a fast swing, and a small overshoot at the end.
        float omega = 2.0f * PI * p.armFreq;
        float accel = omega * omega * (target - angle) - 2.0f * p.armDamping * omega * velocity;

        velocity += accel * dt;
        angle += velocity * dt;

        // Never push the servo past its range
        if (angle < 0.0f || angle > 180.0f) {
            angle = constrain(angle, 0.0f, 180.0f);
            velocity = 0.0f;
        }

        writeAngle(angle);
    }
    
    bool isDown() {
        return wantDown;
    }
}