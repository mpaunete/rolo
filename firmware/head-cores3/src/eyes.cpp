#include "eyes.h"

#include "stats.h"

#include <M5Unified.h>

namespace eyes {
    namespace {
        // Everything is drawn into this off-screen image first, then copied to the screen
        // in one go, so the screen never shows a half-drawn frame (no flicker). It only
        // covers the eyes (plus a margin) and moves with them: smaller means faster to draw
        // and to send, and small enough for the fast internal RAM.
        M5Canvas canvas(&M5.Display);
        constexpr int CANVAS_W = 260;       // the eyes and their glow are 244 x 124
        constexpr int CANVAS_H = 140;
        bool useDma = false;                // internal RAM: send with DMA while the CPU moves on

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
        constexpr int LOOK_MAX_X = 55;               // pixels left/right (keeps the glow on screen)
        constexpr int LOOK_MAX_Y = 60;               // pixels up/down
        constexpr uint32_t LOOK_MIN_GAP_MS = 1500;   // time between glances, chosen at random
        constexpr uint32_t LOOK_MAX_GAP_MS = 5000;
        constexpr int LOOK_CENTRE_PERCENT = 40;      // how often a glance goes back to the middle
        constexpr float LOOK_TAU = 0.01f;            // seconds: how lazily they follow (bigger = slower)
        constexpr float FOLLOW_TAU = 0.4f;          // seconds: the same when following a face (smooths
                                                     // out the 5-10 detections a second)

        // Happy eyes (when following a face): a black ellipse cuts into the bottom of each
        // eye and leaves an upward arch
        constexpr float HAPPY_TAU = 0.25f;     // seconds to ease into / out of happy
        constexpr float HAPPY_CUT = 0.45f;     // how far up the cut reaches when fully happy (x eye height)

        float happy = 0.0f;                    // 0 = normal, 1 = fully happy

        int centreX = 0;   // middle of the screen, set in begin()
        int centreY = 0;

        // What's on the screen now: where the canvas was last sent, and what it showed,
        // so a frame that would look the same isn't drawn or sent again
        struct Shown {
            int canvasX, canvasY;   // where the canvas went on the screen
            int x, y;               // eye centre
            int height;             // eye height (blinking)
            int cut;                // happy cut, in pixels (-1: none)
        };
        Shown shown = {0, 0, -1000, -1000, -1, -1};

        float lookX = 0.0f, lookY = 0.0f;       // where the eyes are now, pixels from the centre
        float targetX = 0.0f, targetY = 0.0f;   // where they're heading
        uint32_t nextLook = 0;                  // millis() of the next glance
        uint32_t lastUpdate = 0;

        // Set by the face task (on the other core): follow this point instead of looking around
        volatile bool following = false;
        volatile float followX = 0.0f, followY = 0.0f;

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

        // How far up the happy cut reaches, in pixels (-1: not happy, no cut)
        int happyCut(float happy) {
            return happy > 0.01f ? (int)(happy * HAPPY_CUT * EYE_H) : -1;
        }

        // Clear to black the part of the old canvas area that the new one doesn't cover
        // (the eyes moved away from it): up to four strips around the new rectangle
        void clearUncovered(int oldX, int oldY, int newX, int newY) {
            int oldR = oldX + CANVAS_W, oldB = oldY + CANVAS_H;
            int newR = newX + CANVAS_W, newB = newY + CANVAS_H;
            auto strip = [](int x0, int y0, int x1, int y1) {
                if (x1 > x0 && y1 > y0) {
                    M5.Display.fillRect(x0, y0, x1 - x0, y1 - y0, TFT_BLACK);
                }
            };
            strip(oldX, oldY, oldR, min(oldB, newY));                     // above the new one
            strip(oldX, max(oldY, newB), oldR, oldB);                     // below it
            int top = max(oldY, newY), bottom = min(oldB, newB);         // beside it
            strip(oldX, top, min(oldR, newX), bottom);
            strip(max(oldX, newR), top, oldR, bottom);
        }

        // One eye, centred on (x, y) in canvas pixels. A smaller height is a (partly)
        // closed eye, and cut (pixels, -1: none) curves its bottom upwards into a smile.
        void drawEye(int x, int y, int height, int cut) {
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

            // Happy: cut the bottom away with a black ellipse, rising as happy goes to 1
            if (cut >= 0) {
                int cutTop = y + height / 2 - cut;                                 // where the arch ends
                int rx = EYE_W * 3 / 4;                                           // wider than the eye and its glow
                int ry = EYE_H / 2;
                canvas.fillEllipse(x, cutTop + ry, rx, ry, TFT_BLACK);
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

        // Follow a face if there is one; otherwise, now and then pick a new place to look.
        // Either way, drift towards it, and ease the happy look in or out.
        void look(uint32_t now) {
            if (following) {
                targetX = followX * LOOK_MAX_X;
                targetY = followY * LOOK_MAX_Y;
            } else if ((int32_t)(now - nextLook) >= 0) {
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
            float tau = following ? FOLLOW_TAU : LOOK_TAU;
            float k = dt / (tau + dt);
            lookX += k * (targetX - lookX);
            lookY += k * (targetY - lookY);

            // Happy while following a face, eased in and out the same way
            float kHappy = dt / (HAPPY_TAU + dt);
            happy += kHappy * ((following ? 1.0f : 0.0f) - happy);
        }
    }

    void begin() {
        centreX = M5.Display.width() / 2;
        centreY = M5.Display.height() / 2;

        // Internal RAM if there's room (fast, and the screen can be fed from it by DMA),
        // otherwise PSRAM and plain sending
        canvas.setPsram(false);
        useDma = canvas.createSprite(CANVAS_W, CANVAS_H) != nullptr;
        if (!useDma) {
            Serial.println("eyes: no room in internal RAM for the canvas, using PSRAM (slower)");
            canvas.setPsram(true);
            canvas.createSprite(CANVAS_W, CANVAS_H);
        }

        M5.Display.fillScreen(TFT_BLACK);
        if (useDma) {
            M5.Display.startWrite();   // keep the screen's bus for ourselves, so DMA can run between frames
        }
        nextBlink = millis() + BLINK_MIN_GAP_MS;
        lastUpdate = millis();
        nextLook = millis() + LOOK_MIN_GAP_MS;
    }

    void update() {
        uint32_t now = millis();
        look(now);

        int height = CLOSED_H + (int)((EYE_H - CLOSED_H) * openness(now));
        int x = centreX + (int)lookX;
        int y = centreY + (int)lookY;
        int cut = happyCut(happy);

        // Nothing would change on the screen: don't draw or send anything
        if (x == shown.x && y == shown.y && height == shown.height && cut == shown.cut) {
            return;
        }

        // The previous frame may still be on its way to the screen: the canvas is ours
        // again once it has gone (usually it has, during loop()'s delay)
        uint32_t waitStart = micros();
        if (useDma) {
            M5.Display.waitDMA();
        }
        uint32_t waitUs = micros() - waitStart;

        // Put the canvas around the eyes, kept on the screen
        int canvasX = constrain(x - CANVAS_W / 2, 0, (int)M5.Display.width() - CANVAS_W);
        int canvasY = constrain(y - CANVAS_H / 2, 0, (int)M5.Display.height() - CANVAS_H);
        int ex = x - canvasX;   // the eye centre, in canvas pixels
        int ey = y - canvasY;

        uint32_t drawStart = micros();
        canvas.fillScreen(TFT_BLACK);
        drawEye(ex - EYE_OFFSET, ey, height, cut);
        drawEye(ex + EYE_OFFSET, ey, height, cut);

        // Scanlines: a dark band every few rows, lined up with the screen's rows (not the
        // canvas's), so they stay still while the canvas moves
        for (int row = -(canvasY % SCANLINE_EVERY); row < CANVAS_H; row += SCANLINE_EVERY) {
            canvas.fillRect(0, row, CANVAS_W, SCANLINE_THICK, TFT_BLACK);
        }
        uint32_t drawUs = micros() - drawStart;

        // Where the eyes were but the canvas no longer reaches: back to black
        uint32_t pushStart = micros();
        if (shown.height >= 0) {
            clearUncovered(shown.canvasX, shown.canvasY, canvasX, canvasY);
        }
        if (useDma) {
            // Start sending and return: the CPU is free while it goes
            M5.Display.pushImageDMA(canvasX, canvasY, CANVAS_W, CANVAS_H,
                                    (const m5gfx::swap565_t *)canvas.getBuffer());
        } else {
            canvas.pushSprite(canvasX, canvasY);
        }
        stats::eyesDone(drawUs, waitUs + (micros() - pushStart));

        shown = {canvasX, canvasY, x, y, height, cut};
    }

    void lookAt(float x, float y) {
        followX = constrain(x, -1.0f, 1.0f);
        followY = constrain(y, -1.0f, 1.0f);
        following = true;
    }

    void lookAround() {
        following = false;
    }
}