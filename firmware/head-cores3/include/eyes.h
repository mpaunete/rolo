#pragma once

// The face: two robotic eyes on the CoreS3 screen
namespace eyes {
    void begin();
    void update();   // from loop(): animates and redraws

    // From another task (e.g. the face task): follow this point, -1..1 each way
    // (left and up negative), instead of glancing around at random
    void lookAt(float x, float y);
    void lookAround();   // back to glancing around at random
}