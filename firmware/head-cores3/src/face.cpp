#include "face.h"
#include "camera.h"
#include "eyes.h"
#include "stats.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <vector>
#include <human_face_detect_msr01.hpp>
#include <human_face_detect_mnp01.hpp>

namespace face {
    namespace {
        constexpr uint32_t LOST_AFTER_MS = 2000;   // no face for this long: look around again
        constexpr uint32_t FOLLOW_INTERVAL_MS = 200;   // following a face: 5 detections a second
        constexpr uint32_t SEARCH_INTERVAL_MS = 500;   // nobody in view: 2 a second, still quick to notice someone

        // The latest result, for the web page: written by the face task, read by the web server
        Detection last = {};
        portMUX_TYPE lastLock = portMUX_INITIALIZER_UNLOCKED;

        constexpr bool MIRROR = false;              // flip left/right so the eyes look at you, not away (try both)
        constexpr float GAIN_X = 1.0f;   // how strongly the eyes follow sideways
        constexpr float GAIN_Y = 2.5f;   // ...and up/down: faces move less vertically in the picture

        // Where in the picture "straight ahead" is, in pixels from its centre (negative = left / up).
        // The camera sits above the screen and isn't aimed exactly straight out, so a face in
        // front of the eyes doesn't land in the middle of the picture.
        constexpr int OFFSET_X = -35;
        constexpr int OFFSET_Y = 10;

        // A position in the picture (pixels) as -1..1 around "straight ahead" (centre): each side
        // is scaled to its own edge, so -1 and +1 are always the picture's edges, even when the
        // centre is off-middle
        float relative(float position, float centre, float size) {
            if (position < centre) {
                return (position - centre) / centre;            // centre..0    ->  0..-1
            }
            return (position - centre) / (size - centre);       // centre..size ->  0..+1
        }

        void task(void *) {
            // Espressif's two-stage face detector, as in their CameraWebServer example
            HumanFaceDetectMSR01 stage1(0.1F, 0.5F, 10, 0.3F);
            HumanFaceDetectMNP01 stage2(0.35F, 0.3F, 5);

            uint8_t *copy = nullptr;     // our own copy of the picture, so the camera is free again quickly
            size_t copySize = 0;
            uint32_t lastSeen = 0;
            bool following = false;
            TickType_t lastWake = xTaskGetTickCount();   // for the pace below

            for (;;) {
                camera_fb_t *frame = camera::takeFrame();
                if (frame == nullptr) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                    continue;
                }
                if (copy == nullptr || copySize != frame->len) {
                    free(copy);
                    copySize = frame->len;
                    copy = (uint8_t *)heap_caps_malloc(copySize, MALLOC_CAP_SPIRAM);   // 150 kB: PSRAM
                }
                uint32_t copyStart = micros();
                if (copy != nullptr) {
                    memcpy(copy, frame->buf, copySize);
                }
                uint32_t copyUs = micros() - copyStart;
                int width = frame->width;
                int height = frame->height;
                camera::giveBack();
                if (copy == nullptr) {
                    vTaskDelay(pdMS_TO_TICKS(1000));   // no memory for the copy: try again later
                    continue;
                }

                // The slow part, about 100 ms, on our copy
                uint32_t detectStart = micros();
                std::vector<int> shape = {height, width, 3};
                auto &candidates = stage1.infer((uint16_t *)copy, shape);
                auto &faces = stage2.infer((uint16_t *)copy, shape, candidates);
                stats::faceDone(copyUs, micros() - detectStart);

                Detection d = {};
                if (!faces.empty()) {
                    auto &best = faces.front();
                    d = {true, best.box[0], best.box[1], best.box[2], best.box[3], best.score, millis()};
                }
                portENTER_CRITICAL(&lastLock);
                last = d;
                portEXIT_CRITICAL(&lastLock);

                if (!faces.empty()) {
                    // The most likely face; box = [left, top, right, bottom] in pixels
                    auto &box = faces.front().box;
                    // Face centre, relative to "straight ahead", as -1..1 of the picture
                    float x = relative((box[0] + box[2]) / 2.0f, width / 2.0f + OFFSET_X, width);
                    float y = relative((box[1] + box[3]) / 2.0f, height / 2.0f + OFFSET_Y, height);
                    eyes::lookAt((MIRROR ? -x : x) * GAIN_X, y * GAIN_Y);
                    lastSeen = millis();
                    following = true;
                } else if (following && (uint32_t)(millis() - lastSeen) > LOST_AFTER_MS) {
                    eyes::lookAround();
                    following = false;
                }

                // Fast while following someone, slower while nobody is there: the eyes smooth
                // the movement anyway, and the rest of core 0 is left for WiFi and the web server
                uint32_t interval = following ? FOLLOW_INTERVAL_MS : SEARCH_INTERVAL_MS;
                vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(interval));
            }
        }
    }

    Detection latest() {
        portENTER_CRITICAL(&lastLock);
        Detection d = last;
        portEXIT_CRITICAL(&lastLock);
        return d;
    }

    void begin() {
        xTaskCreatePinnedToCore(task, "face", 16384, nullptr, 1, nullptr, 0);   // core 0: away from the eyes
    }
}
