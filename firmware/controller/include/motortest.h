#pragma once

// Spins both wheels at a set speed while the robot is down and held up (wheels free),
// to find how fast the motors can really go. Ramps up gently, holds, ramps down.
namespace motortest {
    // From the console: speed in steps/s (up to the step generator's limit), how long to
    // hold it, and how fast to ramp (steps/s^2)
    void request(float speed, float seconds, float accel);
    void stop();

    void update(float dt);   // from the control loop, every pass
    bool isRunning();
    float getSpeed();        // steps/s for both wheels while isRunning()
}
