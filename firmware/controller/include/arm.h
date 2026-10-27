#pragma once

namespace arm {
    void begin();
    void setDown(bool down);   // from the web page (or a gamepad later)
    void hold(float degrees);  // move to this angle instead (the stand-up); setDown() ends it
    void update(float dt);     // from the control loop: moves smoothly towards the target
    bool isDown();  
}