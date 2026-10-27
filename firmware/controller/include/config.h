
#pragma once

#include <stdint.h>

namespace config {

    // ========================================
    // Hardware - MPU6050
    // ========================================
    namespace imu {
        // Wiring
        constexpr uint8_t SDA_PIN = 21;
        constexpr uint8_t SCL_PIN = 22;
        constexpr uint32_t I2C_FREQUENCY = 400000;

        // Calibration (done lying down)
        constexpr float CALIBRATION_MIN_TILT = 80.0f;      // degrees: lying down means tilted more than this
        constexpr uint32_t CALIBRATION_SETTLE_MS = 1000;   // must stay lying down this long before starting
        constexpr uint32_t CALIBRATION_MS = 3000;
        constexpr uint32_t CALIBRATION_SAMPLE_MS = 5;
        constexpr float MAX_CALIBRATION_GYRO_STDDEV = 1.5f;  // deg/s
        constexpr float MAX_CALIBRATION_ANGLE_STDDEV = 1.0f; // degrees

        // Filtering
        constexpr float FILTER_TAU = 1.0f;   // seconds: complementary filter, gyro vs accelerometer
        constexpr float GYRO_SIGN = 1.0f;
    }

    // ========================================
    // Hardware - TMC2209 stepper drivers
    // ========================================
    namespace motors {
        constexpr uint8_t LEFT_STEP_PIN = 14;
        constexpr uint8_t LEFT_DIR_PIN = 27;

        constexpr uint8_t RIGHT_STEP_PIN = 26;
        constexpr uint8_t RIGHT_DIR_PIN = 25;

        constexpr uint8_t ENABLE_PIN = 19;

        constexpr uint8_t UART_RX_PIN = 16;
        constexpr uint8_t UART_TX_PIN = 17;
        constexpr uint32_t UART_BAUD = 115200;

        constexpr uint8_t LEFT_DRIVER_ADDRESS = 0;
        constexpr uint8_t RIGHT_DRIVER_ADDRESS = 1;

        constexpr uint16_t RUN_CURRENT_MA = 900;  // RMS
        constexpr uint16_t MICROSTEPS = 16;

        constexpr float R_SENSE = 0.11f;

        constexpr float MAX_SPEED = 12000.0f;   // steps/s
        constexpr float MIN_SPEED = 2.0f;       // steps/s: below this, the motor stops
        constexpr float MAX_ACCEL = 111000.0f;  // steps/s^2: above this the motors stall

        // Step generation: one fixed-rate timer interrupt drives both motors
        constexpr uint8_t STEP_TIMER = 0;
        constexpr uint32_t STEP_TICK_HZ = 40000;  // ticks/s; max speed is half of this
        constexpr float HARDWARE_MAX_SPEED = STEP_TICK_HZ / 2.0f;   // steps/s: one step every 2 ticks

        // "motortest": spinning the wheels with the robot held up, to find the real top speed
        constexpr float TEST_ACCEL = 20000.0f;    // steps/s^2 by default: gentle, well below a stall
        constexpr float TEST_SECONDS = 3.0f;      // how long it holds the speed by default
    }

    // ========================================
    // Battery monitor (divider on GPIO 35)
    // ========================================
    namespace battery {
        constexpr uint8_t PIN = 35;

        constexpr float R_TOP = 100000.0f;      // ohms, battery + to the pin
        constexpr float R_BOTTOM = 22000.0f;    // ohms, pin to battery -
        constexpr float DIVIDER_RATIO = (R_TOP + R_BOTTOM) / R_BOTTOM;
        constexpr float CALIBRATION = 0.983f;    // multimeter reading / displayed reading

        constexpr float WARN_V = 10.8f;         // 3S LiPo: 3.6 V per cell
        constexpr float CRITICAL_V = 10.2f;     // 3S LiPo: 3.4 V per cell
        constexpr float HYSTERESIS_V = 0.2f;    // a warning clears only this far above its threshold

        constexpr float SAMPLE_INTERVAL_S = 0.1f;
        constexpr float FILTER_TAU_S = 2.0f;
    }

    // ========================================
    // Hardware - WS2812 status LEDs (J4)
    // ========================================
    namespace leds {
        constexpr uint8_t PIN = 18;
        constexpr uint8_t COUNT = 3;
        constexpr uint8_t BRIGHTNESS = 25;          // out of 255: WS2812s are very bright

        constexpr uint8_t NETWORK = 0;              // LED index: WiFi state
        constexpr uint8_t STATE = 1;                // LED index: robot state
        constexpr uint8_t EFFORT = 2;               // LED index: speed vs MAX_SPEED

        constexpr uint32_t UPDATE_INTERVAL_MS = 20; // 50 Hz
    }

    // ========================================
    // Balance controller
    // ========================================
    namespace balance {
        // Tuned defaults (adjustable on the calibration page)
        constexpr float KP = 4500.0f;             // steps/s^2 per degree
        constexpr float KD = 814.0f;              // steps/s^2 per deg/s, on the smoothed gyro
        constexpr float D_FILTER_TAU = 0.015f;    // seconds: gyro smoothing for the D term

        constexpr float KP_SPEED = 0.004f;        // degrees per step/s, when moving (braking)
        constexpr float BRAKE_SPEED = 400.0f;     // steps/s: below this the robot is at rest (no speed gain, I term learns).
        constexpr float KI_SPEED = 0.003f;        // degrees per step of drift
        constexpr float MAX_TARGET_ANGLE = 12.0f; // degrees: deepest lean the speed loop asks for
        constexpr float ACCEL_LEAN = 0.0008f;     // degrees of lean per steps/s^2 (physics says 0.000534; wheel inertia needs more).

        // Fixed
        constexpr float SPEED_FILTER_TAU = 0.05f;  // seconds
        constexpr float COM_GAIN = 18.0f;          // steps/s per deg/s: body tilt rate -> centre-of-mass speed

        constexpr float MAX_TILT = 30.0f;      // degrees: trip above this
        constexpr float REARM_TILT = 5.0f;     // degrees: re-arm below this
        constexpr float RUNAWAY_TIME = 1.0f;   // seconds at max speed before tripping
    }

    // ========================================
    // Hardware - arm servo (J3)
    // ========================================
    namespace arm {
        constexpr uint8_t PIN = 13;
        constexpr uint8_t CHANNEL = 0;           // LEDC (PWM) channel

        constexpr float MIN_US = 500.0f;         // pulse width at 0 degrees
        constexpr float MAX_US = 2500.0f;        // pulse width at 180 degrees

        constexpr float UP_DEG = 90.0f;           // resting position (arm vertical)
        constexpr float DOWN_DEG = 0.0f;        // when the button is held
        constexpr float FREQ_HZ = 2.5f;          // how fast it swings (spring stiffness)
        constexpr float DAMPING = 0.6f;          // 1 = no overshoot, lower = more bounce
    }

    // ========================================
    // Driving (from the web joystick)
    // ========================================
    namespace drive {
        constexpr float MAX_SPEED = 6000.0f;        // steps/s at full stick: the default for driveSpeed, which
                                                     // tops out at 9000 (params.cpp), keeping the rest of
                                                     // motors::MAX_SPEED (12000) for balancing
        constexpr float MAX_TURN = 800.0f;           // steps/s wheel difference at full stick
        constexpr float ACCEL = 8000.0f;             // steps/s^2: how fast the speed setpoint changes
        constexpr uint32_t TIMEOUT_MS = 500;         // stop if no command arrives for this long
    }

    // ========================================
    // Bluetooth gamepad
    // ========================================
    namespace gamepad {
        constexpr float DEADZONE = 0.10f;   // fraction of the stick travel ignored around the centre
    }

    // ========================================
    // Standing up from lying down: arm onto the floor, arm pushes the body up,
    // then a wheel kick until the balance loop can take over
    // ========================================
    namespace standup {
        constexpr float ARM_CONTACT = 84.0f;     // arm angle (degrees) where it rests on the floor
        constexpr float CONTACT_TIME = 0.4f;     // seconds to get there and settle
        constexpr float MIN_LIFT = 10.0f;        // degrees the arm must raise the body before the kick
        constexpr float SETTLED_RATE = 15.0f;    // deg/s: slower than this, the body has stopped rising
        constexpr float SETTLED_TIME = 0.1f;     // ...for this long
        constexpr float LIFT_TIMEOUT = 2.0f;     // seconds for the arm to lift the body and settle

        constexpr float KICK_ANGLE = 63.0f;      // degrees: kick as the rising body passes this (keeps its momentum)
        constexpr float KICK_ACCEL = 70000.0f;   // steps/s^2: 40000+ slipped in tests
        constexpr float KICK_SPEED = 13000.0f;   // steps/s: 11000+ fell back in tests
        constexpr float CATCH_ANGLE = 20.0f;     // degrees: the balance loop takes over from here
        constexpr float KICK_TIMEOUT = 1.0f;     // seconds of kick before giving up
        constexpr float FALL_BACK = 3.0f;        // degrees back down from the best angle: the kick failed

        // Braking after the catch: a deeper lean than maxTargetAngle stops it in about half
        // the distance (20 deg: ~0.3 m, 12 deg: ~0.6 m), but would over-react to pushes in
        // normal use, so only until it has nearly stopped
        constexpr float BRAKE_LEAN = 20.0f;      // degrees
        constexpr float BRAKE_DONE_SPEED = 500.0f;   // steps/s: below this, normal limits again
        constexpr float BRAKE_TIME = 2.0f;       // seconds at most
    }

    // ========================================
    // Main control loop
    // ========================================
    namespace timing {
        constexpr uint32_t LOOP_INTERVAL_US = 5000; // 200 Hz
    }

}
