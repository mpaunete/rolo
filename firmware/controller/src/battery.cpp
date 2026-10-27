#include "battery.h"
#include "config.h"

#include <Arduino.h>

namespace battery {
    namespace cfg = config::battery;

    namespace {
        float voltage = 0.0f;
        float sinceSample = 0.0f;

        // Read by the LED task (core 0) as well
        volatile bool low = false;
        volatile bool critical = false;

        float readVoltage() {
            // analogReadMilliVolts uses the chip's factory ADC calibration
            float pinVolts = analogReadMilliVolts(cfg::PIN) / 1000.0f;
            return pinVolts * cfg::DIVIDER_RATIO * cfg::CALIBRATION;
        }

        // Latched: motor current pulls the voltage down while driving, so a warning
        // only clears once the battery is clearly back above the threshold
        bool below(bool was, float threshold) {
            if (voltage < threshold) {
                return true;
            }
            if (voltage > threshold + cfg::HYSTERESIS_V) {
                return false;
            }
            return was;
        }

        void checkLevels() {
            low = below(low, cfg::WARN_V);
            critical = below(critical, cfg::CRITICAL_V);
        }
    }

    void begin() {
        analogSetPinAttenuation(cfg::PIN, ADC_11db);   // pin range up to about 3.1 V
        voltage = readVoltage();                       // start from a real reading
        checkLevels();
    }

    void update(float dt) {
        sinceSample += dt;
        if (sinceSample < cfg::SAMPLE_INTERVAL_S) {
            return;
        }

        // Smooth over a few seconds: motor current makes the voltage dip briefly
        float alpha = sinceSample / (cfg::FILTER_TAU_S + sinceSample);
        voltage += alpha * (readVoltage() - voltage);
        sinceSample = 0.0f;
        checkLevels();
    }

    float getVoltage() {
        return voltage;
    }

    bool isLow() {
        return low;
    }

    bool isCritical() {
        return critical;
    }
}