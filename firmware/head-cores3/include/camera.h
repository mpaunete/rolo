#pragma once

#include <stddef.h>
#include <stdint.h>
#include <esp_camera.h>

// The CoreS3's camera (GC0308, 320 x 240)
namespace camera {
    constexpr int JPEG_QUALITY = 80;   // 0..100: higher looks better and is bigger

    bool begin();   // from setup(), after M5.begin(); false if the camera didn't start

    // Takes a picture and compresses it to JPEG. On success, *jpeg points to the data
    // (the caller frees it with free()) and *length is its size in bytes.
    bool captureJpeg(uint8_t **jpeg, size_t *length);

    // The next raw picture (RGB565). The stream, /capture and the face task share the
    // camera, so only one task at a time gets a picture: always hand it back with
    // giveBack(), quickly. nullptr if the camera has no picture (or never started).
    camera_fb_t *takeFrame();
    void giveBack();
}