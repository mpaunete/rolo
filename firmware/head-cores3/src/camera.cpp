#include "camera.h"
#include "stats.h"

#include <M5CoreS3.h>
#include <img_converters.h>
#include <esp_log.h>
#include <esp_timer.h>

namespace camera {
    namespace {
        SemaphoreHandle_t lock = nullptr;  // one user of the camera at a time
        camera_fb_t *current = nullptr;    // the picture someone is using now

        // The library's camera settings (M5CoreS3 GC0308.cpp), except grab_mode: the library
        // queues pictures until someone takes them, so they could be up to a second old.
        // GRAB_LATEST keeps capturing and always hands out the newest picture.
        camera_config_t config() {
            camera_config_t c = {};
            c.pin_pwdn = -1;
            c.pin_reset = -1;
            c.pin_xclk = -1;
            c.pin_sccb_sda = 12;
            c.pin_sccb_scl = 11;
            c.pin_d7 = 47; c.pin_d6 = 48; c.pin_d5 = 16; c.pin_d4 = 15;
            c.pin_d3 = 42; c.pin_d2 = 41; c.pin_d1 = 40; c.pin_d0 = 39;
            c.pin_vsync = 46;
            c.pin_href = 38;
            c.pin_pclk = 45;
            c.xclk_freq_hz = 20000000;
            c.ledc_timer = LEDC_TIMER_0;
            c.ledc_channel = LEDC_CHANNEL_0;
            c.pixel_format = PIXFORMAT_RGB565;
            c.frame_size = FRAMESIZE_QVGA;   // 320 x 240
            c.fb_count = 2;                  // GRAB_LATEST needs at least 2
            c.fb_location = CAMERA_FB_IN_PSRAM;
            c.grab_mode = CAMERA_GRAB_LATEST;
            c.sccb_i2c_port = -1;
            return c;
        }
    }

    bool begin() {
        lock = xSemaphoreCreateMutex();
        M5.In_I2C.release();                       // as the library does: the camera shares that I2C bus
        camera_config_t settings = config();
        if (esp_camera_init(&settings) != ESP_OK) {
            vSemaphoreDelete(lock);
            lock = nullptr;   // no camera: takeFrame() always returns nullptr
            Serial.println("camera: could not start");
            return false;
        }

        // The camera driver's task has a small stack, and printing its messages (even
        // errors, e.g. when no one collects the frames) overflows it: no log at all
        esp_log_level_set("cam_hal", ESP_LOG_NONE);

        Serial.println("camera: ready");
        return true;
    }

    camera_fb_t *takeFrame() {
        if (lock == nullptr) {
            return nullptr;                       // the camera never started
        }
        xSemaphoreTake(lock, portMAX_DELAY);      // wait for our turn
        current = esp_camera_fb_get();            // the newest picture
        if (current == nullptr) {
            xSemaphoreGive(lock);
            return nullptr;
        }
        int64_t captured = (int64_t)current->timestamp.tv_sec * 1000000 + current->timestamp.tv_usec;
        stats::frameTaken((uint32_t)(esp_timer_get_time() - captured));   // how old the picture is
        return current;
    }

    void giveBack() {
        esp_camera_fb_return(current);            // give the frame back to the driver
        current = nullptr;
        xSemaphoreGive(lock);
    }

    bool captureJpeg(uint8_t **jpeg, size_t *length) {
        camera_fb_t *frame = takeFrame();
        if (frame == nullptr) {
            return false;
        }
        bool ok = frame2jpg(frame, JPEG_QUALITY, jpeg, length);
        giveBack();
        return ok;
    }
}
