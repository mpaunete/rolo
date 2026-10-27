#pragma once

namespace battery {
    void begin();
    void update(float dt);   // call from the control loop; samples every few loops

    float getVoltage();      // smoothed battery voltage, volts
    bool isLow();            // below WARN_V: time to charge (safe to call from any task)
    bool isCritical();       // below CRITICAL_V: stop using it
}