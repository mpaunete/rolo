#include "console.h"

#include <Arduino.h>
#include <net.h>
#include "stats.h"
#include <string.h>

namespace console {
    namespace {
        constexpr size_t MAX_LINE = 128;   // room for "wifi <long ssid> <long password>"
        char line[MAX_LINE];
        size_t length = 0;

        void run(char *text) {
            char *rest = nullptr;
            char *command = strtok_r(text, " ", &rest);
            if (command == nullptr) {
                return;
            }

            if (strcmp(command, "wifi") == 0) {
                char *ssid = strtok_r(nullptr, " ", &rest);
                char *password = strtok_r(nullptr, " ", &rest);
                if (ssid == nullptr || password == nullptr) {
                    Serial.println("usage: wifi <ssid> <password>");
                    return;
                }
                net::setCredentials(ssid, password);
                Serial.println("ok: connecting, use 'net' to check");   // never print the password
            } else if (strcmp(command, "net") == 0) {
                net::printStatus();
            } else if (strcmp(command, "forget") == 0) {
                net::forgetCredentials();
                Serial.println("ok: saved network erased");
            } else if (strcmp(command, "apname") == 0) {
                char *name = strtok_r(nullptr, " ", &rest);
                if (name == nullptr) {
                    Serial.println("usage: apname <name>");
                    return;
                }
                net::setApName(name);
                Serial.println("ok: access point name saved");
            } else if (strcmp(command, "appass") == 0) {
                char *password = strtok_r(nullptr, " ", &rest);
                if (password == nullptr || strlen(password) < 8) {
                    Serial.println("usage: appass <password>, at least 8 characters");
                    return;
                }
                net::setApPassword(password);
                Serial.println("ok: access point password saved");
            } else if (strcmp(command, "stats") == 0) {
                char *onOff = strtok_r(nullptr, " ", &rest);
                if (onOff == nullptr) {
                    stats::print();
                } else if (strcmp(onOff, "on") == 0 || strcmp(onOff, "off") == 0) {
                    stats::printEverySecond(strcmp(onOff, "on") == 0);
                } else {
                    Serial.println("usage: stats [on|off]");
                }
            } else if (strcmp(command, "help") == 0) {
                Serial.println("net, wifi <ssid> <password>, forget, apname <name>, appass <password>");
                Serial.println("stats, stats on|off");
            } else {
                Serial.printf("unknown command: %s (try 'help')\n", command);
            }
        }
    }

    void poll() {
        while (Serial.available() > 0) {
            char c = Serial.read();
            if (c == '\r') {
                continue;
            }
            if (c == '\n') {
                line[length] = '\0';
                run(line);
                length = 0;
                continue;
            }
            if (length < MAX_LINE - 1) {
                line[length++] = c;
            }
        }
    }
}