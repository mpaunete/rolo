#pragma once

#include <stdint.h>

// How busy the head is: the load on each core (estimated from idle time) and the
// measured cost of our own work (face detection on core 0, the eyes on core 1)
namespace stats {
    // From setup(), before the camera, the face task and WiFi start: measures how often
    // each core is idle when nothing is running yet (takes 1 s)
    void begin();
    void update();   // from loop(): recomputes once a second (and prints, if switched on)

    // Reported by the work itself, from its own task
    void faceDone(uint32_t copyUs, uint32_t detectUs);
    void eyesDone(uint32_t drawUs, uint32_t pushUs);
    void frameTaken(uint32_t ageUs);   // a picture handed out by the camera, this old
    void streamDone(uint32_t encodeUs, uint32_t sendUs, uint32_t bytes);   // one JPEG of the stream

    struct Snapshot {
        float coreLoad[2];    // % per core, estimated: includes WiFi, the web server, the camera driver
        float faceRate;       // face detections per second
        float faceCopyMs;     // average time to copy a picture
        float faceDetectMs;   // average time for the detector
        float faceCore;       // % of core 0 spent on face detection (measured)
        float eyesRate;       // eye frames per second
        float eyesDrawMs;     // average time to draw a frame
        float eyesPushMs;     // average time to send it to the screen
        float eyesCore;       // % of core 1 spent on the eyes (measured)
        float frameAgeMs;     // average age of a picture when someone takes it
        float streamRate;     // stream frames per second
        float streamEncodeMs; // average time to compress a picture to JPEG
        float streamSendMs;   // average time for WiFi to take it
        float streamKB;       // average JPEG size
        float streamCore;     // % of a core spent compressing (measured)
        uint32_t freeHeap;    // bytes
        uint32_t freePsram;   // bytes
    };
    Snapshot latest();

    void print();               // the latest numbers, one line on the serial port
    void printEverySecond(bool on);
}
