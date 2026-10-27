#pragma once

#include <stddef.h>

namespace gamepad {
    void begin();
    void update();   // call from loop(): reads the controller and drives the robot

    bool isConnected();

    // From the web page (handled on the next update(), in the loop task)
    void allowPairing(bool allow);
    void forgetPaired();
    void rumble();

    // State for the gamepad debug page, as JSON (safe from any task: it formats a copy
    // taken in update()). Returns the length written.
    size_t statusJson(char *buf, size_t size);
}