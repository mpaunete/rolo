#include "eyes.h"
#include "lcd.h"

#include <Arduino.h>
#include <string.h>

namespace eyes {
    namespace {
        Lcd display;

        // Everything is drawn into this off-screen image first, then copied to the screen
        // in one go, so the screen never shows a half-drawn frame (no flicker). It only
        // covers the eyes (plus a margin) and moves with them: smaller means faster to draw
        // and to send, and small enough for the C3's internal RAM, which DMA needs.
        LGFX_Sprite canvas(&display);
        constexpr int CANVAS_W = 260;       // the eyes and their glow: up to 260 x 142 (surprised)
        constexpr int CANVAS_H = 150;
        bool useDma = false;

        constexpr int EYE_W = 80;           // pixels, at size 1
        constexpr int EYE_H = 100;
        constexpr int EYE_GAP = 60;         // between the two eyes
        constexpr int EYE_OFFSET = (EYE_W + EYE_GAP) / 2;
        constexpr int RADIUS = 18;          // corner rounding

        // The CRT effects
        constexpr int GLOW_LAYERS = 8;      // dim halos around each eye: many thin ones, for a smooth fade
        constexpr int GLOW_STEP = 2;        // pixels each halo is bigger than the last
        constexpr int GLOW = GLOW_LAYERS * GLOW_STEP;
        // The innermost halo's brightness; the outer ones fade evenly to dark. Tune it live
        // with the 'glow' command.
        float glowBrightness = 1.0f;
        // Every brightness goes through this curve first, to suit the panel: below 1 the
        // in-between shades get darker (a glow that falls off quickly), above 1 brighter.
        // 1 = no change. Tune it live with the 'curve' command. Glow 1 with curve 0.5
        // looks closest to the CoreS3 on the Waveshare 2".
        float glowCurve = 0.5f;
        constexpr float CORE_BRIGHTNESS = 1.7f;   // the hot core: above 1 turns whiter
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

        // Looking around
        constexpr int LOOK_MAX_X = 55;               // pixels left/right (keeps the glow on screen)
        constexpr int LOOK_MAX_Y = 45;               // pixels up/down
        constexpr uint32_t LOOK_MIN_GAP_MS = 1500;   // time between glances, chosen at random
        constexpr uint32_t LOOK_MAX_GAP_MS = 5000;
        constexpr int LOOK_CENTRE_PERCENT = 40;      // how often a glance goes back to the mood's own direction
        constexpr float LOOK_TAU = 0.01f;            // seconds: how lazily they follow (bigger = slower)
        constexpr float FOLLOW_TAU = 0.1f;           // seconds: the same with lookAt()
        constexpr float DIZZY_TURNS_PER_S = 0.7f;    // how fast dizzy eyes roll round

        constexpr float MOOD_TAU = 0.12f;            // seconds to ease from one mood's shape to the next
        constexpr float HAPPY_CUT = 0.45f;           // how far up the smile cut reaches when fully happy (x eye height)

        // What an expression looks like. The eyes ease from one shape to the next.
        struct Shape {
            float open;               // 0..1 of the full height (before blinking)
            float lidInner;           // upper lid at the inner corner (towards the other eye), 0..1 of the height
            float lidOuter;           // ...and at the outer corner
            float smile;              // 0..1: the happy cut from below
            float size;               // 1 = normal
            float gazeX, gazeY;       // where it looks when not glancing, -1..1
            float red, green, blue;   // 0..255
        };

        struct MoodInfo {
            const char *name;
            Shape shape;
            float glanceEvery;   // x the normal time between glances (smaller = more restless)
            float blinkSlower;   // x the normal blink time (bigger = slower)
        };

        const MoodInfo MOODS[] = {
            //                 open   lidIn  lidOut smile  size   gazeX  gazeY   red  green  blue    glance blink
            {"neutral",      {1.00f, 0.00f, 0.00f, 0.0f, 1.00f,  0.0f,  0.0f,    0, 255, 255},  1.00f, 1.0f},
            {"happy",        {1.00f, 0.00f, 0.00f, 1.0f, 1.00f,  0.0f, -0.2f,    0, 255, 255},  1.00f, 1.0f},
            {"sad",          {0.80f, 0.00f, 0.40f, 0.0f, 0.95f,  0.0f,  0.6f,    0, 140, 255},  2.00f, 1.5f},
            {"angry",        {0.75f, 0.45f, 0.05f, 0.0f, 0.90f,  0.0f, -0.1f,  255,  50,  20},  0.60f, 1.0f},
            {"surprised",    {1.00f, 0.00f, 0.00f, 0.0f, 1.10f,  0.0f, -0.2f,    0, 255, 255},  0.25f, 1.0f},
            {"sleepy",       {0.35f, 0.30f, 0.30f, 0.0f, 1.00f,  0.0f,  0.4f,    0, 150, 200},  3.00f, 3.0f},
            {"suspicious",   {0.55f, 0.15f, 0.15f, 0.0f, 0.90f,  0.8f,  0.0f,    0, 255, 255},  2.00f, 1.0f},
            {"dizzy",        {0.70f, 0.10f, 0.10f, 0.0f, 0.95f,  0.0f,  0.0f,  255, 200,   0},  1.00f, 1.0f},
        };
        static_assert(sizeof(MOODS) / sizeof(MOODS[0]) == (size_t)Mood::Count, "one entry per mood");

        Mood current = Mood::Neutral;
        Shape shape = MOODS[0].shape;   // what's shown now, easing towards the mood's shape

        int centreX = 0;   // middle of the screen, set in begin()
        int centreY = 0;

        // What's on the screen now, in pixels, so a frame that would look the same isn't
        // drawn or sent again. Only ints, so two frames can be compared with memcmp.
        struct Frame {
            int canvasX, canvasY;     // where the canvas went on the screen
            int x, y;                 // eye centre
            int width, height;        // eye size (height includes blinking)
            int lidInner, lidOuter;   // pixels down from the top of the eye
            int cut;                  // happy cut in pixels (-1: none)
            int colour;               // the eye's RGB565 colour
            int glow, curve;          // glow brightness and curve, x 100 (so changing them redraws)
        };
        Frame shown = {};
        bool anythingShown = false;

        uint32_t nextBlink = 0;                 // millis() when the next blink starts
        float lookX = 0.0f, lookY = 0.0f;       // where the eyes are now, pixels from the centre
        float targetX = 0.0f, targetY = 0.0f;   // where they're heading
        uint32_t nextLook = 0;                  // millis() of the next glance
        uint32_t lastUpdate = 0;

        // Set by lookAt() (possibly from another task): look there instead of around
        volatile bool following = false;
        volatile float followX = 0.0f, followY = 0.0f;

        const MoodInfo &info() {
            return MOODS[(int)current];
        }

        // The eye colour at a brightness from 0 (off) to 1 (full); above 1 it turns
        // whiter, like a phosphor driven hard
        uint16_t shade(float level) {
            float white = level > 1.0f ? level - 1.0f : 0.0f;   // how much to blend towards white
            float base = level > 1.0f ? 1.0f : level;
            base = powf(base, 1.0f / glowCurve);                // the panel's curve
            auto channel = [&](float c) { return (uint8_t)min(255.0f, c * base + 255.0f * white); };
            return canvas.color565(channel(shape.red), channel(shape.green), channel(shape.blue));
        }

        // A rounded rectangle centred on (x, y), with corners that grow with its size so
        // the halos stay the same shape as the eye
        void rounded(int x, int y, int w, int h, int r, uint16_t colour) {
            r = min(r, min(w, h) / 2);   // a flat shape can't have big round corners
            canvas.fillRoundRect(x - w / 2, y - h / 2, w, h, r, colour);
        }

        // Clear to black the part of the old canvas area that the new one doesn't cover
        // (the eyes moved away from it): up to four strips around the new rectangle
        void clearUncovered(int oldX, int oldY, int newX, int newY) {
            int oldR = oldX + CANVAS_W, oldB = oldY + CANVAS_H;
            int newR = newX + CANVAS_W, newB = newY + CANVAS_H;
            auto strip = [](int x0, int y0, int x1, int y1) {
                if (x1 > x0 && y1 > y0) {
                    display.fillRect(x0, y0, x1 - x0, y1 - y0, TFT_BLACK);
                }
            };
            strip(oldX, oldY, oldR, min(oldB, newY));                     // above the new one
            strip(oldX, max(oldY, newB), oldR, oldB);                     // below it
            int top = max(oldY, newY), bottom = min(oldB, newB);         // beside it
            strip(oldX, top, min(oldR, newX), bottom);
            strip(max(oldX, newR), top, oldR, bottom);
        }

        // One eye, centred on (x, y) in canvas pixels. innerSide is +1 when the other eye
        // is to the right (the left eye), -1 when it's to the left: the lids slope from there.
        void drawEye(int x, int y, const Frame &f, int innerSide) {
            int w = f.width, h = f.height;
            int r = RADIUS * w / EYE_W;

            // Glow: from the biggest, dimmest halo inwards
            for (int i = GLOW_LAYERS; i >= 1; i--) {
                int grow = i * GLOW_STEP;
                float level = glowBrightness * (GLOW_LAYERS + 1 - i) / GLOW_LAYERS;
                rounded(x, y, w + 2 * grow, h + 2 * grow, r + grow, shade(level));
            }
            rounded(x, y, w, h, r, f.colour);                        // the eye
            if (h > 2 * CORE_INSET) {
                rounded(x, y, w - 2 * CORE_INSET, h - 2 * CORE_INSET,
                        r - CORE_INSET, shade(CORE_BRIGHTNESS));      // its hot core
            }

            // Happy: cut the bottom away with a black ellipse, leaving an upward arch
            if (f.cut >= 0) {
                int cutTop = y + h / 2 - f.cut;   // where the arch ends
                int rx = w * 3 / 4;               // wider than the eye and its glow
                int ry = EYE_H / 2;
                canvas.fillEllipse(x, cutTop + ry, rx, ry, TFT_BLACK);
            }

            // Upper lid: black above a straight line from the inner corner to the outer
            // one, reaching past the glow on both sides
            if (f.lidInner > 0 || f.lidOuter > 0) {
                int top = y - h / 2;
                int half = w / 2 + GLOW;
                int innerX = x + innerSide * half, outerX = x - innerSide * half;
                int innerY = top + f.lidInner, outerY = top + f.lidOuter;
                int above = top - GLOW - 1;
                canvas.fillTriangle(innerX, above, outerX, above, outerX, outerY, TFT_BLACK);
                canvas.fillTriangle(innerX, above, outerX, outerY, innerX, innerY, TFT_BLACK);
            }
        }

        // How open the eyes are right now: 1 = open, 0 = closed
        float openness(uint32_t now) {
            uint32_t blinkMs = (uint32_t)(BLINK_MS * info().blinkSlower);
            int32_t t = (int32_t)(now - nextBlink);   // ms since the blink started (negative: not yet)
            if (t < 0) {
                return 1.0f;
            }
            if (t >= (int32_t)blinkMs) {
                // This blink is over: schedule the next one
                bool twice = random(100) < DOUBLE_BLINK_PERCENT;
                nextBlink = now + (twice ? DOUBLE_BLINK_GAP_MS : random(BLINK_MIN_GAP_MS, BLINK_MAX_GAP_MS));
                return 1.0f;
            }
            float phase = (float)t / blinkMs;     // 0 .. 1 through the blink
            return fabsf(2.0f * phase - 1.0f);    // 1 -> 0 (closed halfway through) -> 1
        }

        // Ease what's shown towards the mood's shape
        void ease(float dt) {
            const Shape &to = info().shape;
            float k = dt / (MOOD_TAU + dt);
            auto step = [k](float &value, float target) { value += k * (target - value); };
            step(shape.open, to.open);
            step(shape.lidInner, to.lidInner);
            step(shape.lidOuter, to.lidOuter);
            step(shape.smile, to.smile);
            step(shape.size, to.size);
            step(shape.gazeX, to.gazeX);
            step(shape.gazeY, to.gazeY);
            step(shape.red, to.red);
            step(shape.green, to.green);
            step(shape.blue, to.blue);
        }

        // Look where we're told; dizzy, roll round; otherwise, now and then pick a new
        // place to look around the mood's own direction. Either way, drift towards it.
        void look(uint32_t now, float dt) {
            if (following) {
                targetX = followX * LOOK_MAX_X;
                targetY = followY * LOOK_MAX_Y;
            } else if (current == Mood::Dizzy) {
                float angle = now / 1000.0f * DIZZY_TURNS_PER_S * 2.0f * PI;
                targetX = 0.6f * LOOK_MAX_X * cosf(angle);
                targetY = 0.5f * LOOK_MAX_Y * sinf(angle);
            } else if ((int32_t)(now - nextLook) >= 0) {
                targetX = shape.gazeX * LOOK_MAX_X;
                targetY = shape.gazeY * LOOK_MAX_Y;
                if (random(100) >= LOOK_CENTRE_PERCENT) {
                    targetX += random(-LOOK_MAX_X, LOOK_MAX_X + 1) / 2;
                    targetY += random(-LOOK_MAX_Y, LOOK_MAX_Y + 1) / 2;
                }
                targetX = constrain(targetX, (float)-LOOK_MAX_X, (float)LOOK_MAX_X);
                targetY = constrain(targetY, (float)-LOOK_MAX_Y, (float)LOOK_MAX_Y);
                float gap = random(LOOK_MIN_GAP_MS, LOOK_MAX_GAP_MS) * info().glanceEvery;
                nextLook = now + (uint32_t)gap;
            }

            float tau = following ? FOLLOW_TAU : LOOK_TAU;
            float k = dt / (tau + dt);
            lookX += k * (targetX - lookX);
            lookY += k * (targetY - lookY);
        }
    }

    void begin() {
        display.init();
        display.tune(Lcd::Tuning::Waveshare);
        display.setRotation(1);   // landscape, 320 x 240 (3 if it's upside down)
        display.fillScreen(TFT_BLACK);
        centreX = display.width() / 2;
        centreY = display.height() / 2;

        // Internal RAM (the C3 has no PSRAM): DMA can feed the screen from it
        canvas.setColorDepth(16);
        useDma = canvas.createSprite(CANVAS_W, CANVAS_H) != nullptr;
        if (!useDma) {
            Serial.println("eyes: no room for the canvas, no eyes");
            return;
        }
        display.startWrite();   // keep the screen's bus for ourselves, so DMA can run between frames

        shape.open = 0.0f;      // start closed: the eyes open as they ease to neutral, like waking up
        uint32_t now = millis();
        nextBlink = now + BLINK_MIN_GAP_MS;
        nextLook = now + LOOK_MIN_GAP_MS;
        lastUpdate = now;
    }

    void update() {
        if (!useDma) {
            return;   // no canvas
        }
        uint32_t now = millis();
        float dt = (now - lastUpdate) / 1000.0f;
        lastUpdate = now;
        ease(dt);
        look(now, dt);

        // This frame, in pixels
        Frame f = {};
        f.width = (int)(EYE_W * shape.size);
        int fullHeight = max(CLOSED_H, (int)(EYE_H * shape.size * shape.open));
        f.height = CLOSED_H + (int)((fullHeight - CLOSED_H) * openness(now));
        f.lidInner = (int)(shape.lidInner * f.height);
        f.lidOuter = (int)(shape.lidOuter * f.height);
        f.cut = shape.smile > 0.01f ? (int)(shape.smile * HAPPY_CUT * EYE_H * shape.size) : -1;
        f.colour = shade(1.0f);
        f.glow = (int)(glowBrightness * 100.0f);
        f.curve = (int)(glowCurve * 100.0f);
        f.x = centreX + (int)lookX;
        f.y = centreY + (int)lookY;
        // The canvas goes around the eyes, kept on the screen
        f.canvasX = constrain(f.x - CANVAS_W / 2, 0, (int)display.width() - CANVAS_W);
        f.canvasY = constrain(f.y - CANVAS_H / 2, 0, (int)display.height() - CANVAS_H);

        // Nothing would change on the screen: don't draw or send anything
        if (anythingShown && memcmp(&f, &shown, sizeof(f)) == 0) {
            return;
        }

        // The previous frame may still be on its way to the screen: the canvas is ours
        // again once it has gone (usually it has, during loop()'s delay)
        display.waitDMA();

        int ex = f.x - f.canvasX;   // the eyes' centre, in canvas pixels
        int ey = f.y - f.canvasY;
        canvas.fillScreen(TFT_BLACK);
        drawEye(ex - EYE_OFFSET, ey, f, +1);
        drawEye(ex + EYE_OFFSET, ey, f, -1);

        // Scanlines: a dark band every few rows, lined up with the screen's rows (not the
        // canvas's), so they stay still while the canvas moves
        for (int row = -(f.canvasY % SCANLINE_EVERY); row < CANVAS_H; row += SCANLINE_EVERY) {
            canvas.fillRect(0, row, CANVAS_W, SCANLINE_THICK, TFT_BLACK);
        }

        // Where the eyes were but the canvas no longer reaches: back to black
        if (anythingShown) {
            clearUncovered(shown.canvasX, shown.canvasY, f.canvasX, f.canvasY);
        }

        // Start sending and return: the CPU is free while it goes
        display.pushImageDMA(f.canvasX, f.canvasY, CANVAS_W, CANVAS_H,
                             (const lgfx::swap565_t *)canvas.getBuffer());

        shown = f;
        anythingShown = true;
    }

    void setMood(Mood mood) {
        if (mood == Mood::Count) {
            return;
        }
        current = mood;
        nextLook = millis();   // glance the new mood's way straight away
    }

    Mood mood() {
        return current;
    }

    const char *name(Mood mood) {
        return mood < Mood::Count ? MOODS[(int)mood].name : "?";
    }

    bool fromName(const char *text, Mood &mood) {
        for (int i = 0; i < (int)Mood::Count; i++) {
            if (strcmp(text, MOODS[i].name) == 0) {
                mood = (Mood)i;
                return true;
            }
        }
        return false;
    }

    void tuneScreen(bool waveshare) {
        display.waitDMA();   // not in the middle of sending a frame
        display.tune(waveshare ? Lcd::Tuning::Waveshare : Lcd::Tuning::Lovyan);
    }

    void setGlow(float brightness) {
        glowBrightness = constrain(brightness, 0.0f, 1.5f);
    }

    float glow() {
        return glowBrightness;
    }

    void setCurve(float curve) {
        glowCurve = constrain(curve, 0.5f, 4.0f);
    }

    float curve() {
        return glowCurve;
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
