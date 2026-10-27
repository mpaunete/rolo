#include "leds.h"
#include "config.h"
#include "net.h"
#include "balance.h"
#include "battery.h"

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

namespace leds {
    namespace cfg = config::leds;

    namespace {
        Adafruit_NeoPixel strip(cfg::COUNT, cfg::PIN, NEO_GRB + NEO_KHZ800);

        // Written by the main code, read by the LED task
        volatile Mode mode = Mode::Startup;
        volatile float progress = 0.0f;
        volatile float effort = 0.0f;
        volatile uint32_t flashColour = 0;
        volatile uint32_t flashUntil = 0;

        constexpr uint32_t FLASH_MS = 600;   // 3 flashes of 100 ms on, 100 ms off

        constexpr uint32_t WHITE = 0xFFFFFF;
        constexpr uint32_t YELLOW = 0xFFB000;
        constexpr uint32_t ORANGE = 0xFF4000;
        constexpr uint32_t CYAN = 0x00FFFF;
        constexpr uint32_t BLUE = 0x0000FF;
        constexpr uint32_t PURPLE = 0xA000FF;
        constexpr uint32_t OFF = 0x000000;

        // Scale a colour's brightness, 0..1
        uint32_t dim(uint32_t colour, float level) {
            uint8_t r = ((colour >> 16) & 0xFF) * level;
            uint8_t g = ((colour >> 8) & 0xFF) * level;
            uint8_t b = (colour & 0xFF) * level;
            return Adafruit_NeoPixel::Color(r, g, b);
        }

        bool blink(uint32_t now, uint32_t periodMs) {
            return (now % periodMs) < periodMs / 2;
        }

        float breathe(uint32_t now) {
            float phase = (now % 3000) / 3000.0f;         // 3 s per breath
            return 0.15f + 0.85f * 0.5f * (1.0f - cosf(phase * 2.0f * PI));
        }

        uint32_t networkColour(uint32_t now) {
            switch (net::getStatus()) {
                case net::Status::Connected:   return BLUE;
                case net::Status::AccessPoint: return PURPLE;
                default:                       return blink(now, 1000) ? dim(BLUE, 0.3f) : OFF;
            }
        }

        uint32_t stateColour(uint32_t now) {
            switch (balance::getState()) {
                case balance::State::Balancing: return GREEN;
                case balance::State::Ready:     return dim(GREEN, breathe(now));
                default:                        return blink(now, 500) ? ORANGE : OFF;
            }
        }

        // Green at rest, through yellow, to red at MAX_SPEED; flashing red at the limit.
        // A battery warning takes this LED over: slow orange blink when low, red when critical.
        uint32_t effortColour(uint32_t now) {
            if (battery::isCritical()) {
                return blink(now, 1000) ? RED : OFF;
            }
            if (battery::isLow()) {
                return blink(now, 1000) ? ORANGE : OFF;
            }

            if (balance::getState() != balance::State::Balancing) {
                return OFF;
            }

            float e = constrain(effort, 0.0f, 1.0f);
            if (e > 0.95f) {
                return blink(now, 200) ? RED : OFF;
            }

            uint8_t r = e < 0.5f ? 255 * (e * 2.0f) : 255;
            uint8_t g = e < 0.5f ? 255 : 255 * (1.0f - (e - 0.5f) * 2.0f);
            return dim((r << 16) | (g << 8), 0.2f + 0.8f * e);
        }

        void draw(uint32_t now) {
            // A flash overrides everything until it ends
            if ((int32_t)(flashUntil - now) > 0) {
                uint32_t c = blink(now, 200) ? flashColour : OFF;
                strip.fill(c);
                return;
            }

            switch (mode) {
                case Mode::Startup:
                    strip.fill(dim(WHITE, 0.5f));
                    break;

                case Mode::LayDown:
                    strip.fill(blink(now, 500) ? YELLOW : OFF);
                    break;

                case Mode::Calibrating: {
                    // Progress bar: each LED fills in turn
                    float lit = progress * cfg::COUNT;
                    for (uint8_t i = 0; i < cfg::COUNT; i++) {
                        float level = constrain(lit - i, 0.0f, 1.0f);
                        strip.setPixelColor(i, dim(CYAN, 0.1f + 0.9f * level));
                    }
                    break;
                }

                case Mode::Running:
                    strip.setPixelColor(cfg::NETWORK, networkColour(now));
                    strip.setPixelColor(cfg::STATE, stateColour(now));
                    strip.setPixelColor(cfg::EFFORT, effortColour(now));
                    break;

                case Mode::Fault:
                    strip.fill(RED);
                    break;
            }
        }

        // Runs on core 0 (with WiFi), so blinking never waits on the balance loop
        void task(void *) {
            while (true) {
                draw(millis());
                strip.show();
                vTaskDelay(pdMS_TO_TICKS(cfg::UPDATE_INTERVAL_MS));
            }
        }
    }

    void begin() {
        strip.begin();
        strip.setBrightness(cfg::BRIGHTNESS);
        strip.clear();
        strip.show();

        xTaskCreatePinnedToCore(task, "leds", 4096, nullptr, 1, nullptr, 0);
    }

    void setMode(Mode m) {
        mode = m;
    }

    void setProgress(float fraction) {
        progress = fraction;
    }

    void setEffort(float fraction) {
        effort = fraction;
    }

    void flash(uint32_t colour) {
        flashColour = colour;
        flashUntil = millis() + FLASH_MS;
    }
}