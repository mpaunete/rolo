#pragma once

namespace drive {
    // From the web page: joystick position, each -1..1 (x = turn right, y = forward)
    void command(float x, float y);

    // From the control loop: ramps towards the joystick, or back to a stop
    // if commands stop arriving. Pass false when not balancing.
    void update(float dt, bool enabled);

    float getSpeed();   // steps/s, forward positive: the speed the balance loop holds
    float getTurn();    // steps/s, added to the left wheel and taken off the right
    float getAccel();   // steps/s^2: how fast the speed setpoint is changing right now
    float getJoyX();
    float getJoyY();
}