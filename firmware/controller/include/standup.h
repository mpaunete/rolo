#pragma once

// Standing up from lying down: the arm goes onto the floor, pushes the body up, and once
// the body has stopped rising the wheels drive under it until the balance loop can catch
// it (or the kick fails: then the motors stop and the arm goes back up).
namespace standup {
    // From the console: try to stand up (only while lying down), kicking at accel
    // (steps/s^2) up to speed (steps/s), as the rising body passes angle (degrees
    // from upright; or once it stops rising, if it doesn't get that far)
    void request(float accel, float speed, float angle);

    // From the control loop, every pass, before the balance update
    void update(float angle, float gyroRate, float dt);

    bool isDriving();   // true while the kick drives the wheels
    float getSpeed();   // steps/s for both wheels while isDriving()

    // The arm button (drive page or gamepad): while lying down it starts a stand-up,
    // otherwise it moves the arm. Ignored while a stand-up is running.
    void armButton(bool pressed);
}
