#include "eyes.h"

#include <M5Unified.h>

namespace eyes {
    namespace {
        // Everything is drawn into this off-screen image first, then copied to the screen
        // in one go, so the screen never shows a half-drawn frame (no flicker)
        M5Canvas canvas(&M5.Display);

        constexpr int EYE_W = 80;           // pixels
        constexpr int EYE_H = 100;
        constexpr int EYE_GAP = 60;         // between the two eyes
        constexpr int EYE_OFFSET = (EYE_W + EYE_GAP) / 2; 
        constexpr int RADIUS = 18;          // corner rounding

        // Phosphor colour (cyan), and the CRT effects
        constexpr uint8_t RED = 0, GREEN = 255, BLUE = 255;
        constexpr int GLOW_LAYERS = 4;      // dim halos around each eye
        constexpr int GLOW_STEP = 3;        // pixels each halo is bigger than the last
        constexpr int CORE_INSET = 10;      // the brighter middle starts this far inside the edge
        constexpr int SCANLINE_EVERY = 4;   // a dark band every 4 rows...
        constexpr int SCANLINE_THICK = 2;   // ...this many rows thick

        // Blinking
        constexpr uint32_t BLINK_MS = 160;            // closing and opening again
        constexpr uint32_t BLINK_MIN_GAP_MS = 4000;   // time between blinks, chosen at random
        constexpr uint32_t BLINK_MAX_GAP_MS = 10000;
        constexpr uint32_t DOUBLE_BLINK_GAP_MS = 120; // a quick second blink...
        constexpr int DOUBLE_BLINK_PERCENT = 20;      // ...this often
        constexpr int CLOSED_H = 6;                   // pixels: a thin line when closed

        uint32_t nextBlink = 0;   // millis() when the next blink starts

        // Looking around
        constexpr int LOOK_MAX_X = 35;               // pixels left/right (keeps the glow on screen)
        constexpr int LOOK_MAX_Y = 40;               // pixels up/down
        constexpr uint32_t LOOK_MIN_GAP_MS = 1500;   // time between glances, chosen at random
        constexpr uint32_t LOOK_MAX_GAP_MS = 5000;
        constexpr int LOOK_CENTRE_PERCENT = 40;      // how often a glance goes back to the middle
        constexpr float LOOK_TAU = 0.04f;            // seconds: how lazily they follow (bigger = slower)

        int centreX = 0;   // middle of the screen, set in begin()
        int centreY = 0;

        float lookX = 0.0f, lookY = 0.0f;       // where the eyes are now, pixels from the centre
        float targetX = 0.0f, targetY = 0.0f;   // where they're heading
        uint32_t nextLook = 0;                  // millis() of the next glance
        uint32_t lastUpdate = 0;

        // The phosphor colour at a brightness from 0 (off) to 1 (full); above 1 it turns
        // whiter, like a phosphor driven hard
        uint16_t shade(float level) {
            float white = level > 1.0f ? level - 1.0f : 0.0f;   // how much to blend towards white
            float base = level > 1.0f ? 1.0f : level;
            auto channel = [&](uint8_t c) { return (uint8_t)min(255.0f, c * base + 255.0f * white); };
            return canvas.color565(channel(RED), channel(GREEN), channel(BLUE));
        }

        // A rounded rectangle centred on (x, y), with corners that grow with its size so
        // the halos stay the same shape as the eye
        void rounded(int x, int y, int w, int h, int r, uint16_t colour) {
            r = min(r, min(w, h) / 2);   // a flat shape can't have big round corners
            canvas.fillRoundRect(x - w / 2, y - h / 2, w, h, r, colour);
        }

        // One eye, centred on (x, y). A smaller height is a (partly) closed eye.
        void drawEye(int x, int y, int height) {
            // Glow: from the biggest, dimmest halo inwards
            for (int i = GLOW_LAYERS; i >= 1; i--) {
                int grow = i * GLOW_STEP;
                rounded(x, y, EYE_W + 2 * grow, height + 2 * grow, RADIUS + grow, shade(0.35f / i));
            }
            rounded(x, y, EYE_W, height, RADIUS, shade(1.0f));       // the eye
            if (height > 2 * CORE_INSET) {
                rounded(x, y, EYE_W - 2 * CORE_INSET, height - 2 * CORE_INSET,
                        RADIUS - CORE_INSET, shade(1.4f));            // its hot core
            }
        }

        // How open the eyes are right now: 1 = open, 0 = closed
        float openness(uint32_t now) {
            int32_t t = (int32_t)(now - nextBlink);   // ms since the blink started (negative: not yet)
            if (t < 0) {
                return 1.0f;
            }
            if (t >= (int32_t)BLINK_MS) {
                // This blink is over: schedule the next one
                bool twice = random(100) < DOUBLE_BLINK_PERCENT;
                nextBlink = now + (twice ? DOUBLE_BLINK_GAP_MS : random(BLINK_MIN_GAP_MS, BLINK_MAX_GAP_MS));
                return 1.0f;
            }
            float phase = (float)t / BLINK_MS;    // 0 .. 1 through the blink
            return fabsf(2.0f * phase - 1.0f);    // 1 -> 0 (closed halfway through) -> 1
        }

        // Now and then pick a new place to look, and drift towards it
        void look(uint32_t now) {
            if ((int32_t)(now - nextLook) >= 0) {
                if (random(100) < LOOK_CENTRE_PERCENT) {
                    targetX = 0.0f;
                    targetY = 0.0f;
                } else {
                    targetX = random(-LOOK_MAX_X, LOOK_MAX_X + 1);
                    targetY = random(-LOOK_MAX_Y, LOOK_MAX_Y + 1);
                }
                nextLook = now + random(LOOK_MIN_GAP_MS, LOOK_MAX_GAP_MS);
            }

            // Ease towards the target: the same smoothing as the body's filters
            float dt = (now - lastUpdate) / 1000.0f;
            lastUpdate = now;
            float k = dt / (LOOK_TAU + dt);
            lookX += k * (targetX - lookX);
            lookY += k * (targetY - lookY);
        }
    }

    void begin() {
        centreX = M5.Display.width() / 2;
        centreY = M5.Display.height() / 2;

        canvas.setPsram(true);   // 320 x 240 x 2 bytes = 150 kB: put it in the 8 MB PSRAM
        canvas.createSprite(M5.Display.width(), M5.Display.height());
        nextBlink = millis() + BLINK_MIN_GAP_MS;
        lastUpdate = millis();
        nextLook = millis() + LOOK_MIN_GAP_MS;
    }

    void update() {
        canvas.fillScreen(TFT_BLACK);
        uint32_t now = millis();
        look(now);

        int height = CLOSED_H + (int)((EYE_H - CLOSED_H) * openness(now));
        int x = centreX + (int)lookX;
        int y = centreY + (int)lookY;
        drawEye(x - EYE_OFFSET, y, height);
        drawEye(x + EYE_OFFSET, y, height);

        // Scanlines: a dark band every few rows, over everything
        for (int y = 0; y < canvas.height(); y += SCANLINE_EVERY) {
            canvas.fillRect(0, y, canvas.width(), SCANLINE_THICK, TFT_BLACK);
        }

        canvas.pushSprite(0, 0);              // copy the finished frame to the screen
    }
}