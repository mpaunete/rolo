#include "gamepad.h"
#include "console.h"
#include "config.h"
#include "drive.h"
#include "arm.h"
#include "standup.h"

#include <Arduino.h>
#include <Bluepad32.h>
#include <string.h>

namespace gamepad {
    namespace cfg = config::gamepad;

    namespace {
        ControllerPtr pad = nullptr;      // the one controller we use
        char modelName[32] = "";          // copied once at connect (getModelName() allocates)

        // Requests from the web page (core 0), carried out in update() (core 1)
        volatile int pairingRequest = -1; // -1 = none, 0 = off, 1 = on
        volatile bool forgetRequest = false;
        volatile bool rumbleRequest = false;
        bool pairingAllowed = true;

        bool wasDriving = false;
        bool wasArmDown = false;

        // Copy of the controller's state for the gamepad page. Taken in update() (core 1),
        // formatted in statusJson() (web task, core 0), so the web task never touches
        // Bluepad32's objects while Bluetooth is changing them.
        struct Status {
            bool connected;
            bool pairing;
            char model[32];
            uint8_t battery;
            int32_t lx, ly, rx, ry, brake, throttle;
            uint16_t buttons;
            uint16_t misc;
            uint8_t dpad;
            int32_t gyro[3];
            int32_t accel[3];
        };
        Status status = {};
        portMUX_TYPE statusLock = portMUX_INITIALIZER_UNLOCKED;

        void takeStatus() {
            Status now = {};
            now.pairing = pairingAllowed;
            now.connected = pad != nullptr && pad->isConnected();
            if (now.connected) {
                memcpy(now.model, modelName, sizeof(now.model));
                now.battery = pad->battery();
                now.lx = pad->axisX();
                now.ly = pad->axisY();
                now.rx = pad->axisRX();
                now.ry = pad->axisRY();
                now.brake = pad->brake();
                now.throttle = pad->throttle();
                now.buttons = pad->buttons();
                now.misc = pad->miscButtons();
                now.dpad = pad->dpad();
                now.gyro[0] = pad->gyroX();
                now.gyro[1] = pad->gyroY();
                now.gyro[2] = pad->gyroZ();
                now.accel[0] = pad->accelX();
                now.accel[1] = pad->accelY();
                now.accel[2] = pad->accelZ();
            }

            portENTER_CRITICAL(&statusLock);
            status = now;
            portEXIT_CRITICAL(&statusLock);
        }

        void onConnected(ControllerPtr controller) {
            if (pad == nullptr) {
                pad = controller;
                strncpy(modelName, controller->getModelName().c_str(), sizeof(modelName) - 1);
                console::out.printf("Gamepad: connected (%s)\n", controller->getModelName().c_str());
                controller->setPlayerLEDs(1);
            } else {
                // Only one controller drives the robot
                controller->disconnect();
            }
        }

        void onDisconnected(ControllerPtr controller) {
            if (controller == pad) {
                pad = nullptr;
                console::out.println("Gamepad: disconnected");
            }
        }

        // -512..511 -> -1..1, with a dead zone and a squared curve for fine control
        float stick(int32_t raw) {
            float v = constrain(raw / 512.0f, -1.0f, 1.0f);
            float a = fabsf(v);
            if (a < cfg::DEADZONE) {
                return 0.0f;
            }
            a = (a - cfg::DEADZONE) / (1.0f - cfg::DEADZONE);
            return copysignf(a * a, v);
        }

        void handleRequests() {
            if (pairingRequest >= 0) {
                pairingAllowed = (pairingRequest == 1);
                BP32.enableNewBluetoothConnections(pairingAllowed);
                pairingRequest = -1;
            }
            if (forgetRequest) {
                BP32.forgetBluetoothKeys();
                forgetRequest = false;
            }
            if (rumbleRequest && pad != nullptr) {
                pad->playDualRumble(0, 250, 0x80, 0x40);
                rumbleRequest = false;
            }
        }
    }

    void begin() {
        BP32.setup(&onConnected, &onDisconnected);
        BP32.enableVirtualDevice(false);
        BP32.enableNewBluetoothConnections(pairingAllowed);
    }

    void update() {
        BP32.update();
        handleRequests();

        // For the gamepad page, 10 times a second is plenty
        static uint32_t lastStatus = 0;
        if ((uint32_t)(millis() - lastStatus) >= 100) {
            lastStatus = millis();
            takeStatus();
        }

        if (pad == nullptr || !pad->isConnected() || !pad->hasData()) {
            return;
        }

        // Left stick drives. Up is negative on the stick, forward is positive for drive.
        float x = stick(pad->axisX());
        float y = -stick(pad->axisY());
        bool driving = (x != 0.0f || y != 0.0f);

        // Send while the stick is moved, plus once when it returns to the centre,
        // so the web joystick still works while the gamepad is idle
        if (driving || wasDriving) {
            drive::command(x, y);
        }
        wasDriving = driving;

        // Bottom face button (B on a Switch layout) holds the arm down
        bool armDown = pad->a();
        if (armDown != wasArmDown) {
            standup::armButton(armDown);
            wasArmDown = armDown;
        }
    }

    bool isConnected() {
        return pad != nullptr && pad->isConnected();
    }

    void allowPairing(bool allow) {
        pairingRequest = allow ? 1 : 0;
    }

    void forgetPaired() {
        forgetRequest = true;
    }

    void rumble() {
        rumbleRequest = true;
    }

    size_t statusJson(char *buf, size_t size) {
        Status st;
        portENTER_CRITICAL(&statusLock);
        st = status;
        portEXIT_CRITICAL(&statusLock);

        if (!st.connected) {
            return snprintf(buf, size, "{\"type\":\"pad\",\"connected\":false,\"pairing\":%s}",
                            st.pairing ? "true" : "false");
        }

        return snprintf(buf, size,
            "{\"type\":\"pad\",\"connected\":true,\"pairing\":%s,\"model\":\"%s\",\"battery\":%u,"
            "\"lx\":%ld,\"ly\":%ld,\"rx\":%ld,\"ry\":%ld,\"brake\":%ld,\"throttle\":%ld,"
            "\"buttons\":%u,\"misc\":%u,\"dpad\":%u,"
            "\"gyro\":[%ld,%ld,%ld],\"accel\":[%ld,%ld,%ld]}",
            st.pairing ? "true" : "false", st.model, st.battery,
            (long)st.lx, (long)st.ly, (long)st.rx, (long)st.ry,
            (long)st.brake, (long)st.throttle,
            st.buttons, st.misc, st.dpad,
            (long)st.gyro[0], (long)st.gyro[1], (long)st.gyro[2],
            (long)st.accel[0], (long)st.accel[1], (long)st.accel[2]);
    }
}
