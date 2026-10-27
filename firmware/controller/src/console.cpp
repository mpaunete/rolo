#include "console.h"
#include "arm.h"
#include "balance.h"
#include "config.h"
#include "gamepad.h"
#include "motors.h"
#include "motortest.h"
#include "net.h"
#include "params.h"
#include "standup.h"
#include "stream.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

namespace console {
    Output out;

    namespace {
        constexpr size_t MAX_LINE = 64;

        // Serial input, collected until Enter
        char serialLine[MAX_LINE];
        size_t serialLength = 0;

        // The last few kB printed, for the web terminal. 'total' counts every character
        // ever printed, so a reader can tell what it has already seen.
        constexpr size_t KEPT = 2048;
        char kept[KEPT];
        uint32_t total = 0;
        portMUX_TYPE keptLock = portMUX_INITIALIZER_UNLOCKED;

        // Commands arrive from the loop (serial) and the web server: one at a time
        SemaphoreHandle_t runLock = xSemaphoreCreateMutex();

        void keep(const uint8_t *text, size_t size) {
            portENTER_CRITICAL(&keptLock);
            for (size_t i = 0; i < size; i++) {
                kept[total % KEPT] = (char)text[i];
                total++;
            }
            portEXIT_CRITICAL(&keptLock);
        }

        // Commands that carry a password: never shown or recorded in full
        bool secret(const char *line) {
            return strncmp(line, "wifi ", 5) == 0 || strncmp(line, "appass ", 7) == 0;
        }

        // The command itself, in the web terminal only (the serial monitor echoes it already)
        void echo(const char *line) {
            char text[MAX_LINE + 16];
            if (secret(line)) {
                const char *space = strchr(line, ' ');
                snprintf(text, sizeof(text), "> %.*s ****\n", (int)(space - line), line);
            } else {
                snprintf(text, sizeof(text), "> %s\n", line);
            }
            keep((const uint8_t *)text, strlen(text));
        }

        void handleSet(char *&rest) {
            char *name = strtok_r(nullptr, " ", &rest);
            char *valueText = strtok_r(nullptr, " ", &rest);

            if (name == nullptr || valueText == nullptr) {
                out.println("usage: set <name> <value>");
                return;
            }

            float value = strtof(valueText, nullptr);

            if (params::set(name, value)) {
                out.printf("ok: %s\n", name);
            } else {
                out.printf("unknown parameter: %s\n", name);
            }
        }

        void handleList() {
            for (size_t i = 0; i < params::count(); i++) {
                const params::Entry &e = params::entry(i);
                out.printf("%-16s %10.4f   [%g .. %g]\n", e.name, *e.value, e.min, e.max);
            }
        }

        void handleSave() {
            if (!balance::isTripped()) {
                out.println("refused: lay the robot down before saving");
                return;
            }

            params::save();
            out.println("ok: saved");
        }

        void handleWifi(char *&rest) {
            char *ssid = strtok_r(nullptr, " ", &rest);
            char *password = strtok_r(nullptr, " ", &rest);

            if (ssid == nullptr || password == nullptr) {
                out.println("usage: wifi <ssid> <password>");
                return;
            }

            net::setCredentials(ssid, password);
            out.println("ok: connecting, use 'net' to check");
        }

        void handleApName(char *&rest) {
            char *name = strtok_r(nullptr, " ", &rest);

            if (name == nullptr) {
                out.println("usage: apname <name>");
                return;
            }

            net::setApName(name);
            out.println("ok: access point name saved");
        }

        void handleApPass(char *&rest) {
            char *password = strtok_r(nullptr, " ", &rest);

            if (password == nullptr) {
                out.println("usage: appass <password>");
                return;
            }

            if (strlen(password) < 8) {
                out.println("refused: password must be at least 8 characters");
                return;
            }

            net::setApPassword(password);
            out.println("ok: access point password saved");
        }

        void handleArm(char *&rest) {
            char *where = strtok_r(nullptr, " ", &rest);

            if (where != nullptr && strcmp(where, "down") == 0) {
                arm::setDown(true);
            } else if (where != nullptr && strcmp(where, "up") == 0) {
                arm::setDown(false);
            } else {
                out.println("usage: arm up|down");
            }
        }

        void handleKick(char *&rest) {
            char *accelText = strtok_r(nullptr, " ", &rest);
            char *speedText = strtok_r(nullptr, " ", &rest);
            char *angleText = strtok_r(nullptr, " ", &rest);
            float accel = accelText != nullptr ? strtof(accelText, nullptr) : config::standup::KICK_ACCEL;
            float speed = speedText != nullptr ? strtof(speedText, nullptr) : config::standup::KICK_SPEED;
            float angle = angleText != nullptr ? strtof(angleText, nullptr) : config::standup::KICK_ANGLE;

            if (accel < 1000.0f || speed < 500.0f || angle <= 0.0f || angle >= 90.0f) {
                out.println("usage: kick [accel steps/s^2] [speed steps/s] [angle deg], e.g. kick 44000 9000 60");
                return;
            }

            standup::request(accel, speed, angle);
            out.printf("ok: stand-up, kick at %.0f deg, %.0f steps/s^2 up to %.0f steps/s\n", angle, accel, speed);
        }

        void handlePad(char *&rest) {
            char *what = strtok_r(nullptr, " ", &rest);
            char *onOff = strtok_r(nullptr, " ", &rest);

            if (what != nullptr && strcmp(what, "pair") == 0 && onOff != nullptr) {
                bool on = strcmp(onOff, "on") == 0;
                gamepad::allowPairing(on);
                out.printf("ok: pairing %s\n", on ? "on" : "off");
            } else if (what != nullptr && strcmp(what, "forget") == 0) {
                gamepad::forgetPaired();
                out.println("ok: paired controllers forgotten");
            } else if (what != nullptr && strcmp(what, "rumble") == 0) {
                gamepad::rumble();
            } else {
                out.println("usage: pad pair on|off, pad forget, pad rumble");
            }
        }

        void handleMotorTest(char *&rest) {
            char *first = strtok_r(nullptr, " ", &rest);

            if (first != nullptr && strcmp(first, "stop") == 0) {
                motortest::stop();
                out.println("ok: motor test stopping");
                return;
            }

            if (first == nullptr) {
                out.println("usage: motortest <steps/s> [seconds] [accel], or motortest stop");
                out.println("  lay the robot down, then hold it with the wheels off the ground");
                return;
            }

            char *secondsText = strtok_r(nullptr, " ", &rest);
            char *accelText = strtok_r(nullptr, " ", &rest);
            float speed = strtof(first, nullptr);
            float seconds = secondsText != nullptr ? strtof(secondsText, nullptr) : config::motors::TEST_SECONDS;
            float accel = accelText != nullptr ? strtof(accelText, nullptr) : config::motors::TEST_ACCEL;

            if (seconds <= 0.0f || seconds > 30.0f || accel < 100.0f) {
                out.println("refused: seconds 0..30, accel at least 100");
                return;
            }

            motortest::request(speed, seconds, accel);
            out.printf("ok: motor test %.0f steps/s for %.1f s, ramp %.0f steps/s^2\n", speed, seconds, accel);
        }

        void handleHelp() {
            out.println("set <name> <value>, list, save, load, reset, mem");
            out.println("arm up|down, kick [accel] [speed] [angle], pad pair on|off, pad forget, pad rumble");
            out.println("motors, motortest <steps/s> [seconds] [accel], motortest stop");
            out.println("net, wifi <ssid> <password>, forget, apname <name>, appass <password>");
        }
    }

    size_t Output::write(uint8_t c) {
        return write(&c, 1);
    }

    size_t Output::write(const uint8_t *buffer, size_t size) {
        keep(buffer, size);
        return Serial.write(buffer, size);
    }

    size_t readOutput(uint32_t &from, char *text, size_t size) {
        portENTER_CRITICAL(&keptLock);
        uint32_t oldest = total > KEPT ? total - KEPT : 0;
        if (from < oldest) {
            from = oldest;
        }
        size_t n = total - from;
        if (n > size - 1) {
            n = size - 1;
        }
        for (size_t i = 0; i < n; i++) {
            text[i] = kept[(from + i) % KEPT];
        }
        from += n;
        portEXIT_CRITICAL(&keptLock);

        text[n] = '\0';
        return n;
    }

    void run(const char *text) {
        if (text[0] == '\0') {
            return;
        }

        xSemaphoreTake(runLock, portMAX_DELAY);

        echo(text);
        if (!secret(text)) {
            stream::event(text);   // recorded alongside the samples, never WiFi passwords
        }

        char line[MAX_LINE];
        strncpy(line, text, sizeof(line) - 1);
        line[sizeof(line) - 1] = '\0';

        char *rest = nullptr;
        char *command = strtok_r(line, " ", &rest);

        if (command == nullptr) {
            // only spaces
        } else if (strcmp(command, "set") == 0) {
            handleSet(rest);
        } else if (strcmp(command, "list") == 0) {
            handleList();
        } else if (strcmp(command, "save") == 0) {
            handleSave();
        } else if (strcmp(command, "load") == 0) {
            params::load();
            out.println("ok: saved values loaded");
        } else if (strcmp(command, "reset") == 0) {
            params::resetToDefaults();
            out.println("ok: defaults restored (not saved)");
        } else if (strcmp(command, "arm") == 0) {
            handleArm(rest);
        } else if (strcmp(command, "armbutton") == 0) {
            char *state = strtok_r(nullptr, " ", &rest);
            standup::armButton(state != nullptr && strcmp(state, "down") == 0);
        } else if (strcmp(command, "kick") == 0) {
            handleKick(rest);
        } else if (strcmp(command, "pad") == 0) {
            handlePad(rest);
        } else if (strcmp(command, "net") == 0) {
            net::printStatus();
        } else if (strcmp(command, "wifi") == 0) {
            handleWifi(rest);
        } else if (strcmp(command, "forget") == 0) {
            net::forgetCredentials();
            out.println("ok: saved network erased");
        } else if (strcmp(command, "apname") == 0) {
            handleApName(rest);
        } else if (strcmp(command, "appass") == 0) {
            handleApPass(rest);
        } else if (strcmp(command, "motortest") == 0) {
            handleMotorTest(rest);
        } else if (strcmp(command, "mem") == 0) {
            out.printf("memory: %lu free, largest block %lu, lowest since start %lu\n",
                       (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap(),
                       (unsigned long)ESP.getMinFreeHeap());
        } else if (strcmp(command, "motors") == 0) {
            motors::printStatus();
        } else if (strcmp(command, "help") == 0) {
            handleHelp();
        } else {
            out.printf("unknown command: %s (try 'help')\n", command);
        }

        xSemaphoreGive(runLock);
    }

    void poll() {
        while (Serial.available() > 0) {
            char c = Serial.read();

            if (c == '\r') {
                continue;
            }

            if (c == '\n') {
                serialLine[serialLength] = '\0';
                run(serialLine);
                serialLength = 0;
                continue;
            }

            if (serialLength < MAX_LINE - 1) {
                serialLine[serialLength++] = c;
            }
        }
    }
}
