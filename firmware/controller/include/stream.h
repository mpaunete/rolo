#pragma once

#include <stddef.h>
#include <stdint.h>

// Live data stream: one sample per control loop (200 Hz). Written by the control loop
// (core 1), read by the web task (core 0), sent in binary batches: all of them to the
// PC recording tool, only the newest few times a second to the web pages.
namespace stream {
    // One control-loop pass. Packed: this exact byte layout is what the PC tool decodes,
    // so change tools/rolo_stream.py and include/rolo_js.h whenever this changes (and bump VERSION).
    struct __attribute__((packed)) Sample {
        uint32_t timeUs;        // micros() at this loop pass
        uint16_t dtUs;          // time since the previous pass
        uint8_t flags;          // bits 0-1 balance state, bit 2 arm down, bit 3 gamepad connected,
                                // bit 4 battery low, bit 5 battery critical
        uint8_t reserved;

        float angle;            // degrees
        float gyroRate;         // degrees/s
        float accelAngle;       // degrees, accelerometer only
        float targetAngle;      // degrees, from the speed loop
        float speed;            // steps/s, balance output
        float driveSpeed;       // steps/s, speed setpoint from the driver
        float driveAccel;       // steps/s^2, setpoint acceleration
        float turn;             // steps/s
        float joyX;             // -1..1, last drive command
        float joyY;             // -1..1
        float leftSpeed;        // steps/s, actually sent to the motors
        float rightSpeed;
        float battery;          // volts

        int32_t position;       // steps, average of both wheels
        int32_t positionDiff;   // steps, left minus right
    };

    static_assert(sizeof(Sample) == 68, "Sample layout changed: update tools/rolo_stream.py and rolo_js.h too");

    constexpr uint8_t VERSION = 1;

    // Recording (the PC tool): every sample is kept until it is taken
    void setActive(bool active);         // only collect while someone is recording
    bool isActive();

    // From the control loop, every pass. Only a copy: the sending happens in another task.
    void add(const Sample &sample);

    // The newest sample (for the web pages' live view). False until the first add().
    bool latest(Sample &out);

    // Copies up to maxSamples recorded samples into out. Returns how many were copied.
    size_t take(Sample *out, size_t maxSamples);
    uint32_t dropped();                  // samples lost because nobody read them in time

    // Commands and other events: recorded alongside the samples, and shown in the
    // calibration page's terminal. Always queued (the web task empties the queue).
    void event(const char *text);        // safe to call from any task
    bool takeEvent(char *out, size_t size, uint32_t &timeUs);
}