#include "imu.h"
#include "console.h"
#include "config.h"
#include "leds.h"

#include <Arduino.h>    
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

namespace imu {
    namespace cfg = config::imu;

    static Adafruit_MPU6050 mpu;

    // State
    static float gyroBias = 0.0f;
    static float angleAccel = 0.0f;

    static float angle = 0.0f;
    static float gyroRate = 0.0f;


    // Private helper function
    static float calculateAccelAngle(const sensors_event_t &accel) {
        return atan2f(
            -accel.acceleration.x,
            accel.acceleration.z
        ) * RAD_TO_DEG;
    }

    bool begin() {
        Wire.begin(cfg::SDA_PIN, cfg::SCL_PIN);
        Wire.setClock(cfg::I2C_FREQUENCY);

        if (!mpu.begin()) {
            return false;
        }

        mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
        mpu.setGyroRange(MPU6050_RANGE_1000_DEG);   // falls and stand-ups go past 250 deg/s
        mpu.setFilterBandwidth(MPU6050_BAND_44_HZ);

        return true;
    }

    void waitForCalibrationPosition() {
        bool warned = false;
        uint32_t lyingSince = 0;

        while (true) {
            sensors_event_t accel, gyro, temp;
            bool lying = mpu.getEvent(&accel, &gyro, &temp) &&
                         fabsf(calculateAccelAngle(accel)) > cfg::CALIBRATION_MIN_TILT;

            uint32_t now = millis();

            if (lying) {
                leds::setMode(leds::Mode::Calibrating);
                leds::setProgress(0.0f);

                if (lyingSince == 0) {
                    lyingSince = now;
                }
                if ((uint32_t)(now - lyingSince) >= cfg::CALIBRATION_SETTLE_MS) {
                    return;
                }
            } else {
                leds::setMode(leds::Mode::LayDown);

                lyingSince = 0;

                if (!warned) {
                    console::out.println("Lay the robot down to calibrate.");
                    warned = true;
                }
            }

            delay(10);
        }
    }

    bool calibrate() {
        console::out.println();
        console::out.println("ROLO: Starting 3-second calibration");
        console::out.println("Keep the robot lying down and still.");

        double gyroSum = 0.0;
        double gyroSumSq = 0.0;
        double angleSum = 0.0;
        double angleSumSq = 0.0;

        uint32_t samples = 0;
        uint32_t start = millis();
        uint32_t lastTick = 0;

        while ((uint32_t)(millis() - start) < cfg::CALIBRATION_MS) {
            sensors_event_t accel, gyro, temp;

            leds::setProgress((float)(millis() - start) / cfg::CALIBRATION_MS);

            if (!mpu.getEvent(&accel, &gyro, &temp)) {
                console::out.println("Calibration failed: sensor read error");
                return false;
            }

            float rawGyro = cfg::GYRO_SIGN * gyro.gyro.y * RAD_TO_DEG;
            float rawAngle = calculateAccelAngle(accel);

            gyroSum += rawGyro;
            gyroSumSq += (double)rawGyro * rawGyro;

            angleSum += rawAngle;
            angleSumSq += (double)rawAngle * rawAngle;

            samples++;
            delay(cfg::CALIBRATION_SAMPLE_MS);
        }

        if (samples < 100) {
            console::out.println("Calibration failed: insufficient samples");
            return false;
        }

        double meanGyro = gyroSum / samples;
        double meanAngle = angleSum / samples;

        double gyroVariance = fmax(
            0.0,
            gyroSumSq / samples - meanGyro * meanGyro
        );

        double angleVariance = fmax(
            0.0,
            angleSumSq / samples - meanAngle * meanAngle
        );

        double gyroStdDev = sqrt(gyroVariance);
        double angleStdDev = sqrt(angleVariance);

        console::out.printf("Samples: %lu\n", (unsigned long)samples);
        console::out.printf(
            "Gyro mean: %.3f deg/s | stddev: %.3f\n",
            meanGyro, gyroStdDev
        );
        console::out.printf(
            "Angle mean: %.3f deg | stddev: %.3f\n",
            meanAngle, angleStdDev
        );

        if (gyroStdDev > cfg::MAX_CALIBRATION_GYRO_STDDEV ||
            angleStdDev > cfg::MAX_CALIBRATION_ANGLE_STDDEV) {
            console::out.println("Calibration rejected: robot moved");
            return false;
        }

        if (fabs(meanAngle) < cfg::CALIBRATION_MIN_TILT) {
            console::out.println("Calibration rejected: robot not lying down");
            return false;
        }

        gyroBias = meanGyro;

        angle = meanAngle;
        angleAccel = angle;
        gyroRate = 0.0f;

        console::out.printf("Gyro bias: %.3f deg/s\n", gyroBias);
        console::out.printf("Start angle: %.3f deg\n", angle);

        return true;
    }

    bool update(float dt) {
        sensors_event_t accel, gyro, temp;

        if (!mpu.getEvent(&accel, &gyro, &temp)) {
            return false;
        }

        // Accelerometer tilt angle
        angleAccel = calculateAccelAngle(accel);

        // Gyroscope angular velocity, corrected for bias
        gyroRate = cfg::GYRO_SIGN
                * gyro.gyro.y * RAD_TO_DEG
                - gyroBias;

        // Complementary filter

        float alpha = cfg::FILTER_TAU
                    / (cfg::FILTER_TAU + dt);

        angle = alpha * (angle + gyroRate * dt)
            + (1.0f - alpha) * angleAccel;

        return true;
    }

    float getAngle() {
        return angle;
    }

    float getGyroRate() {
        return gyroRate;
    }

    float getAccelAngle() {
        return angleAccel;
    }
}
