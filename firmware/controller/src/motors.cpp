#include "motors.h"
#include "console.h"
#include "config.h"

#include <Arduino.h>
#include <TMCStepper.h>
#include <soc/gpio_struct.h>

namespace motors {
    namespace cfg = config::motors;

    namespace {
        HardwareSerial &driverSerial = Serial2;

        constexpr uint8_t TMC2209_VERSION = 0x21;

        // Timer runs at 1 MHz, so the alarm is the tick period in microseconds
        constexpr uint32_t TIMER_DIVIDER = 80;
        constexpr uint64_t TICK_US = 1000000 / cfg::STEP_TICK_HZ;

        // Phase accumulator: one step every time the 32-bit phase wraps around
        constexpr float PHASE_PER_STEP = 4294967296.0f;  // 2^32
        constexpr uint32_t MAX_INCREMENT = 0x80000000;  // one step every 2 ticks

        struct Motor {
            uint8_t stepPin;
            uint8_t dirPin;
            bool inverted;

            TMC2209Stepper driver;

            // Shared with the timer interrupt
            volatile uint32_t increment = 0;  // phase added per tick (0 = stopped)
            volatile int32_t position = 0;
            volatile int8_t direction = 1;    // +1 or -1
            uint32_t phase = 0;               // only touched by the interrupt
            bool stepHigh = false;            // only touched by the interrupt

            float currentSpeed = 0.0f;        // signed steps/s actually running

            Motor(uint8_t step, uint8_t dir, uint8_t address, bool inv)
                : stepPin(step),
                  dirPin(dir),
                  inverted(inv),
                  driver(&driverSerial, cfg::R_SENSE, address) {}

            // Runs in the timer interrupt, every tick
            void IRAM_ATTR tick() {
                if (stepHigh) {
                    GPIO.out_w1tc = (1UL << stepPin);   // end the pulse from last tick
                    stepHigh = false;
                }

                uint32_t before = phase;
                phase += increment;

                if (phase < before) {                   // wrapped: time for a step
                    GPIO.out_w1ts = (1UL << stepPin);
                    stepHigh = true;
                    position += direction;
                }
            }

            bool begin() {
                pinMode(stepPin, OUTPUT);
                pinMode(dirPin, OUTPUT);
                digitalWrite(stepPin, LOW);
                setDirection(1);

                driver.begin();
                if (driver.version() != TMC2209_VERSION) {
                    return false;
                }

                driver.toff(4);
                driver.I_scale_analog(false);   // current set only by rms_current(), not the module's potentiometer
                driver.rms_current(cfg::RUN_CURRENT_MA);
                driver.microsteps(cfg::MICROSTEPS);
                driver.en_spreadCycle(true);
                driver.pwm_autoscale(true);
                return true;
            }

            void setDirection(int8_t dir) {
                direction = dir;
                bool high = (dir > 0) != inverted;
                digitalWrite(dirPin, high ? HIGH : LOW);
            }

            void setSpeed(float stepsPerSec) {
                float speed = fabsf(stepsPerSec);

                if (speed < cfg::MIN_SPEED) {
                    increment = 0;
                    currentSpeed = 0.0f;
                    return;
                }

                int8_t dir = stepsPerSec > 0.0f ? 1 : -1;
                if (dir != direction) {
                    // Pause stepping while DIR and the step count's sign change,
                    // so no step lands in between with the wrong direction
                    increment = 0;
                    setDirection(dir);
                }

                float stepsPerTick = speed / cfg::STEP_TICK_HZ;
                uint32_t inc = (uint32_t)fminf(stepsPerTick * PHASE_PER_STEP, (float)MAX_INCREMENT);

                increment = inc;
                currentSpeed = dir * (inc / PHASE_PER_STEP) * cfg::STEP_TICK_HZ;
            }

            float actualSpeed() const {
                return currentSpeed;
            }

            int32_t getPosition() const {
                return position;
            }
        };

        Motor left  {cfg::LEFT_STEP_PIN,  cfg::LEFT_DIR_PIN,  cfg::LEFT_DRIVER_ADDRESS,  true};
        Motor right {cfg::RIGHT_STEP_PIN, cfg::RIGHT_DIR_PIN, cfg::RIGHT_DRIVER_ADDRESS, true};

        hw_timer_t *timer = nullptr;
        volatile uint32_t tickCount = 0;
        uint32_t lastTickCount = 0;
        uint32_t timerRestarts = 0;

        void IRAM_ATTR onTick() {
            left.tick();
            right.tick();
            tickCount++;
        }

        // If the interrupt ever misses its alarm, the timer would count on
        // forever without firing. Spot that (no ticks since last time) and restart.
        void checkTimer() {
            if (tickCount == lastTickCount) {
                timerWrite(timer, 0);
                timerRestarts++;
            }
            lastTickCount = tickCount;
        }
    }

    bool begin() {
        disable();
        pinMode(cfg::ENABLE_PIN, OUTPUT);

        driverSerial.begin(cfg::UART_BAUD, SERIAL_8N1, cfg::UART_RX_PIN, cfg::UART_TX_PIN);

        bool leftOk = left.begin();
        bool rightOk = right.begin();

        if (!leftOk) {
            console::out.println("Left driver not responding");
        }
        if (!rightOk) {
            console::out.println("Right driver not responding");
        }

        // Set up the alarm completely before the interrupt can fire
        timer = timerBegin(cfg::STEP_TIMER, TIMER_DIVIDER, true);
        timerAlarmWrite(timer, TICK_US, true);
        timerAttachInterrupt(timer, onTick, true);
        timerAlarmEnable(timer);

        return leftOk && rightOk;
    }

    void setSpeed(float l, float r, float limit) {
        if (timer != nullptr) {
            checkTimer();
        }

        limit = fminf(limit, cfg::HARDWARE_MAX_SPEED);
        left.setSpeed(constrain(l, -limit, limit));
        right.setSpeed(constrain(r, -limit, limit));
    }

    void enable() {
        digitalWrite(cfg::ENABLE_PIN, LOW);
    }

    void disable() {
        digitalWrite(cfg::ENABLE_PIN, HIGH);
    }

    void stop() {
        setSpeed(0.0f, 0.0f);
    }

    float getLeftSpeed()  { return left.actualSpeed(); }
    float getRightSpeed() { return right.actualSpeed(); }
    int32_t getPositionDifference() { return left.getPosition() - right.getPosition(); }
    int32_t getPosition() { return (left.getPosition() + right.getPosition()) / 2; }
    uint32_t getTimerRestarts() { return timerRestarts; }

    // Driver diagnostics: what each TMC2209 reports about its current setting
    void printStatus() {
        for (Motor *m : {&left, &right}) {
            console::out.printf("%s: analog scaling %s, current scale %u/31, set %u mA, %s%s\n",
                m == &left ? "left" : "right",
                m->driver.I_scale_analog() ? "ON (pot)" : "off",
                m->driver.cs_actual(),
                m->driver.rms_current(),
                m->driver.otpw() ? "HOT " : "",
                m->driver.ot() ? "OVERHEATED" : "");
        }
    }
}