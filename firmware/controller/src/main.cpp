
#include <Arduino.h>

#include "config.h"
#include "params.h"
#include "imu.h"
#include "motors.h"
#include "balance.h"
#include "console.h"
#include "net.h"
#include "web.h"
#include "stream.h"
#include "leds.h"
#include "drive.h"
#include "arm.h"
#include "gamepad.h"
#include "battery.h"
#include "standup.h"
#include "motortest.h"


uint32_t lastUpdate = 0;
uint32_t lastPrint = 0;


void setup() {
    params::resetToDefaults();
    params::load();

    Serial.begin(115200);
    delay(500);

    console::out.println("ROLO controller starting...");

    if (!motors::begin()) {          // first: pins defined, drivers held off
        leds::setMode(leds::Mode::Fault);
        console::out.println("ERROR: motor drivers not responding!");
        while (true) { 
            delay(100); }
    }
    console::out.println("Motor drivers OK");

    leds::begin();
    arm::begin();
    battery::begin();
    net::begin();
    gamepad::begin();
    web::begin();

    if (!imu::begin()) {
        leds::setMode(leds::Mode::Fault);
        console::out.println("ERROR: MPU6050 not found!");
        while (true) {
            delay(100);
        }
    }
    console::out.println("IMU OK");

    // Calibrate lying down; on failure, try again
    while (true) {
        imu::waitForCalibrationPosition();

        if (imu::calibrate()) {
            leds::flash(leds::GREEN);
            break;
        }

        leds::flash(leds::RED);
        delay(1000);
    }
    console::out.println("IMU calibration complete");

    motors::enable();  
    console::out.println("Motors ENABLED");

    lastUpdate = micros();
    lastPrint = millis();

    leds::setMode(leds::Mode::Running);
    console::out.println("Controller ready");
}

void loop() {
    console::poll();
    net::update();

    uint32_t now = micros();
    uint32_t elapsed = (uint32_t)(now - lastUpdate);

    if (elapsed < config::timing::LOOP_INTERVAL_US) {
        return;
    }

    float dt = elapsed * 1e-6f;
    lastUpdate = now;

    gamepad::update();
    battery::update(dt);

    if (dt <= 0.0f || dt > 0.1f || !imu::update(dt)) {
        motors::stop();
        return;
    }

    float angle = imu::getAngle();
    float gyroRate = imu::getGyroRate();
    float angleAccel = imu::getAccelAngle();

    drive::update(dt, balance::getState() == balance::State::Balancing);

    arm::update(dt);

    standup::update(angle, gyroRate, dt);   // first: a successful kick hands over to balance this pass
    motortest::update(dt);

    float speed = balance::update(angle, gyroRate, motors::getPosition(), dt, drive::getSpeed(), drive::getAccel());
    if (standup::isDriving()) {
        speed = standup::getSpeed();           // while lying down, the stand-up kick drives the wheels
    }
    float turn = drive::getTurn();
    if (motortest::isRunning()) {
        speed = motortest::getSpeed();         // robot down and held up: both wheels, past MAX_SPEED
        motors::setSpeed(speed, speed, config::motors::HARDWARE_MAX_SPEED);
    } else {
        // The balance loop keeps itself within MAX_SPEED, except when it takes over from
        // wheels already running faster (then it only slows down): don't clamp that here
        motors::setSpeed(speed - turn, speed + turn, config::motors::HARDWARE_MAX_SPEED);
    }
    leds::setEffort(fabsf(speed) / config::motors::MAX_SPEED);

    // Every pass: the web task (core 0) sends it to the recorder and the pages
    stream::Sample s{};
    s.timeUs = now;
    s.dtUs = (uint16_t)min(elapsed, (uint32_t)65535);
    s.flags = (uint8_t)balance::getState()
            | (arm::isDown() ? 0x04 : 0)
            | (gamepad::isConnected() ? 0x08 : 0)
            | (battery::isLow() ? 0x10 : 0)
            | (battery::isCritical() ? 0x20 : 0);
    s.angle = angle;
    s.gyroRate = gyroRate;
    s.accelAngle = angleAccel;
    s.targetAngle = balance::getTargetAngle();
    s.speed = speed;
    s.driveSpeed = drive::getSpeed();
    s.driveAccel = drive::getAccel();
    s.turn = turn;
    s.joyX = drive::getJoyX();
    s.joyY = drive::getJoyY();
    s.leftSpeed = motors::getLeftSpeed();
    s.rightSpeed = motors::getRightSpeed();
    s.battery = battery::getVoltage();
    s.position = motors::getPosition();
    s.positionDiff = motors::getPositionDifference();
    stream::add(s);

    static uint32_t lastRestarts = 0;
    if (motors::getTimerRestarts() != lastRestarts) {
        lastRestarts = motors::getTimerRestarts();
        console::out.printf("WARNING: step timer restarted (%lu times)\n", (unsigned long)lastRestarts);
    }
    // if ((uint32_t)(millis() - lastPrint) >= config::timing::PRINT_INTERVAL_MS) {
    //     lastPrint = millis();

    //     Serial.printf(
    //         "Accel: %+7.2f | "
    //         "Angle: %+7.2f | "
    //         "Gyro: %+7.2f deg/s | "
    //         "Speed: %+7.0f steps/s | "
    //         "Target: %+7.2f deg | "
    //         "L: %+6.0f | R: %+6.0f | dPos: %+ld | "
    //         "Pos: %+6ld | "
    //         "%s\n",
    //         angleAccel,
    //         angle,
    //         gyroRate,
    //         speed,
    //         balance::getTargetAngle(),
    //         motors::getLeftSpeed(),
    //         motors::getRightSpeed(),
    //         (long)motors::getPositionDifference(),
    //         (long)motors::getPosition(),
    //         balance::isTripped() ? "TILT LIMIT" : "OK"
    //     );
    // }
}
