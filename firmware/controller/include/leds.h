#pragma once

#include <stdint.h>

namespace leds {
    // What the LEDs show. Running shows network / robot state / effort,
    // the others are start-up phases.
    enum class Mode {
        Startup,       // white
        LayDown,       // yellow blink: lay the robot down to calibrate
        Calibrating,   // cyan progress bar across all three LEDs
        Running,       // LED 0 network, LED 1 robot state, LED 2 effort (or battery warning)
        Fault,         // solid red: hardware not responding
    };

    // Colours as 0xRRGGBB
    constexpr uint32_t RED = 0xFF0000;
    constexpr uint32_t GREEN = 0x00FF00;

    void begin();                       // starts the LED task
    void setMode(Mode mode);
    void setProgress(float fraction);   // calibration progress, 0..1
    void setEffort(float fraction);     // |speed| / MAX_SPEED, 0..1
    void flash(uint32_t colour);        // all LEDs flash 3 times, then back to the mode
}