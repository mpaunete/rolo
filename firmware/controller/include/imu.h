#pragma once

namespace imu {
    bool begin();
    void waitForCalibrationPosition();
    bool calibrate();
    bool update(float dt);

    float getAngle();
    float getGyroRate();
    float getAccelAngle(); 
}