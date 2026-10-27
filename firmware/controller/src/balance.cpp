#include "balance.h"
#include "params.h"
#include "config.h"
#include "motors.h"

#include <Arduino.h>

namespace balance {
    namespace cfg = config::balance;
    namespace motorsCfg = config::motors;

    namespace {
        bool tripped = true;
        bool wasDown = false;

        float speed = 0.0f;
        float speedFiltered = 0.0f;
        float gyroFiltered = 0.0f;

        float distance = 0.0f;
        int32_t lastPosition = 0;
        float targetAngle = 0.0f;

        float runawayTime = 0.0f;

        // Just taken over from wheels already running (a stand-up kick): catching the body
        // may need more than MAX_SPEED, until the speed is back under it
        bool catching = false;

        // ...and braking after it, with a deeper lean allowed
        bool braking = false;
        float brakeTime = 0.0f;
    }

    float update(float angle, float gyroRate, int32_t position, float dt, float targetSpeed, float targetAccel) {
        const params::Params &p = params::get();

        if (fabsf(angle) > cfg::MAX_TILT) {
            tripped = true;
            wasDown = true;
        } else if (tripped && wasDown && fabsf(angle) < cfg::REARM_TILT) {
            // Stood up: start from whatever the wheels are doing (a motor test or a kick may
            // still be driving them). Starting from 0 would stop them dead and throw the body.
            start((motors::getLeftSpeed() + motors::getRightSpeed()) / 2.0f);
        }

        if (tripped) {
            speed = 0.0f;
            speedFiltered = 0.0f;
            gyroFiltered = 0.0f;
            distance = 0.0f;
            lastPosition = position;
            return 0.0f;
        }

        // Speed of the centre of mass: wheel speed plus the body's tilting motion
        float alpha = dt / (cfg::SPEED_FILTER_TAU + dt);
        float comSpeed = speed + cfg::COM_GAIN * gyroRate;
        speedFiltered += alpha * (comSpeed - speedFiltered);

        float speedError = speedFiltered - targetSpeed;

        int32_t moved = position - lastPosition;
        lastPosition = position;

        // I term: learns the balance offset from slow drift while standing, not from pushes
        // (only below brakeSpeed). Frozen while driving: otherwise it tries to keep the robot
        // on the planned path, slowing it down after an overshoot and pulling it back after a stop.
        bool driving = fabsf(targetSpeed) > 1.0f || fabsf(targetAccel) > 1.0f;
        if (!driving && fabsf(speedError) < p.brakeSpeed) {
            distance += moved;
        }

        if (p.kiSpeed > 0.0f) {
            float maxDistance = p.maxTargetAngle / p.kiSpeed;
            distance = constrain(distance, -maxDistance, maxDistance);
        }

        // No speed gain at standstill (the I term holds it there); full gain from brakeSpeed up
        float moving = fmaxf(fabsf(speedError), fabsf(targetSpeed));
        float blend = fminf(moving / p.brakeSpeed, 1.0f);
        float kpSpeedNow = p.kpSpeed * blend;

        // Feed-forward: lean into the commanded acceleration instead of waiting for a speed error
        targetAngle = -(kpSpeedNow * speedError + p.kiSpeed * distance)
                    + p.accelLean * targetAccel;
        // Deepest lean: maxTargetAngle, or more while braking after a stand-up
        if (braking) {
            brakeTime += dt;
            if (fabsf(speed) < config::standup::BRAKE_DONE_SPEED || brakeTime > config::standup::BRAKE_TIME) {
                braking = false;
            }
        }
        float maxLean = braking ? fmaxf(p.maxTargetAngle, config::standup::BRAKE_LEAN) : p.maxTargetAngle;
        targetAngle = constrain(targetAngle, -maxLean, maxLean);

        // D term on a smoothed gyro: damps the sway without feeding the
        // ~18 Hz motor resonance
        float gyroAlpha = dt / (p.dFilterTau + dt);
        gyroFiltered += gyroAlpha * (gyroRate - gyroFiltered);

        float accel = p.kp * (angle - targetAngle) + p.kd * gyroFiltered;
        accel = constrain(accel, -motorsCfg::MAX_ACCEL, motorsCfg::MAX_ACCEL);
        // Never faster than MAX_SPEED, except while catching after a stand-up kick: then up
        // to what the step generator can do
        float limit = catching ? motorsCfg::HARDWARE_MAX_SPEED : motorsCfg::MAX_SPEED;
        speed += accel * dt;
        speed = constrain(speed, -limit, limit);

        if (catching && fabsf(speed) < motorsCfg::MAX_SPEED) {
            catching = false;              // caught: normal limits from here
        }

        // Runaway: at or above MAX_SPEED for too long (also ends a catch that never slows down)
        if (fabsf(speed) >= motorsCfg::MAX_SPEED) {
            runawayTime += dt;
            if (runawayTime > cfg::RUNAWAY_TIME) {
                tripped = true;
                wasDown = false;
                catching = false;
                runawayTime = 0.0f;
                speed = 0.0f;
            }
        } else {
            runawayTime = 0.0f;
        }

        return speed;
    }

    void start(float wheelSpeed) {
        tripped = false;
        wasDown = false;
        speed = wheelSpeed;           // carry on from the kick's speed: a jump to 0 would stall the motors
        speedFiltered = wheelSpeed;
        gyroFiltered = 0.0f;
        distance = 0.0f;
        runawayTime = 0.0f;
        catching = true;              // may go past MAX_SPEED until it is back under it
        braking = true;               // and lean deeper to brake, until nearly stopped
        brakeTime = 0.0f;
    }

    bool isTripped() {
        return tripped;
    }

    float getTargetAngle() {
        return targetAngle;
    }

    State getState() {
        if (!tripped) {
            return State::Balancing;
        }
        return wasDown ? State::Ready : State::LayDown;
    }
}