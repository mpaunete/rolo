#pragma once

#include <stdint.h>

// Finds faces in the camera picture and makes the eyes follow the most likely one
namespace face {
    struct Detection {
        bool found;
        int x0, y0, x1, y1;   // the face's box, in camera pixels (320 x 240)
        float score;          // the detector's confidence, 0..1
        uint32_t time;        // millis() when it was found
    };

    void begin();             // from setup(), after camera::begin(): starts the face task
    Detection latest();       // the most recent result (safe from any task)
}
