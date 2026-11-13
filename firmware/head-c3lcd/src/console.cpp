#include "console.h"
#include "eyes.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

namespace console {
    namespace {
        constexpr size_t MAX_LINE = 64;
        char line[MAX_LINE];
        size_t length = 0;

        // The demo: every few seconds, the next mood
        constexpr uint32_t DEMO_MS = 3000;
        bool demo = false;
        uint32_t lastDemo = 0;

        void listMoods() {
            Serial.print("moods:");
            for (int i = 0; i < (int)eyes::Mood::Count; i++) {
                Serial.printf(" %s", eyes::name((eyes::Mood)i));
            }
            Serial.printf("\nnow: %s\n", eyes::name(eyes::mood()));
        }

        void run(char *text) {
            char *rest = nullptr;
            char *command = strtok_r(text, " ", &rest);
            if (command == nullptr) {
                return;
            }

            if (strcmp(command, "mood") == 0) {
                char *which = strtok_r(nullptr, " ", &rest);
                eyes::Mood mood;
                if (which == nullptr) {
                    listMoods();
                } else if (eyes::fromName(which, mood)) {
                    demo = false;
                    eyes::setMood(mood);
                    Serial.printf("ok: %s\n", which);
                } else {
                    Serial.printf("unknown mood: %s\n", which);
                    listMoods();
                }
            } else if (strcmp(command, "demo") == 0) {
                char *onOff = strtok_r(nullptr, " ", &rest);
                demo = onOff == nullptr || strcmp(onOff, "off") != 0;
                lastDemo = millis();
                Serial.printf("ok: demo %s\n", demo ? "on" : "off");
            } else if (strcmp(command, "glow") == 0) {
                char *value = strtok_r(nullptr, " ", &rest);
                if (value != nullptr) {
                    eyes::setGlow(strtof(value, nullptr));
                }
                Serial.printf("glow: %.2f (0..1.5)\n", eyes::glow());
            } else if (strcmp(command, "curve") == 0) {
                char *value = strtok_r(nullptr, " ", &rest);
                if (value != nullptr) {
                    eyes::setCurve(strtof(value, nullptr));
                }
                Serial.printf("curve: %.2f (0.5..4, 1 = off)\n", eyes::curve());
            } else if (strcmp(command, "gamma") == 0) {
                char *which = strtok_r(nullptr, " ", &rest);
                if (which != nullptr && strcmp(which, "waveshare") == 0) {
                    eyes::tuneScreen(true);
                    Serial.println("ok: Waveshare's screen settings");
                } else if (which != nullptr && strcmp(which, "lovyan") == 0) {
                    eyes::tuneScreen(false);
                    Serial.println("ok: LovyanGFX's screen settings");
                } else {
                    Serial.println("usage: gamma waveshare|lovyan");
                }
            } else if (strcmp(command, "help") == 0) {
                Serial.println("mood, mood <name>, demo [on|off], glow [brightness], curve [value], gamma waveshare|lovyan");
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

        if (demo && millis() - lastDemo >= DEMO_MS) {
            lastDemo = millis();
            int next = ((int)eyes::mood() + 1) % (int)eyes::Mood::Count;
            eyes::setMood((eyes::Mood)next);
            Serial.printf("mood: %s\n", eyes::name((eyes::Mood)next));
        }
    }
}
