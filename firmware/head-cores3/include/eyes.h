#pragma once

// The face: two robotic eyes on the CoreS3 screen
namespace eyes {
    void begin();
    void update();   // from loop(): animates and redraws
}