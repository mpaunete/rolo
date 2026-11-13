#pragma once

// The face: two glowing robot eyes on the 2" LCD
namespace eyes {
    // What the eyes express. They ease from one mood to the next.
    enum class Mood {
        Neutral,
        Happy,
        Sad,
        Angry,       // also the effort face, when standing up
        Surprised,
        Sleepy,
        Suspicious,
        Dizzy,       // after a fall: the eyes roll around
        Count        // not a mood: how many there are
    };

    void begin();
    void update();   // from loop(): animates and redraws

    void setMood(Mood mood);
    Mood mood();
    const char *name(Mood mood);
    bool fromName(const char *name, Mood &mood);   // false if there's no mood with that name

    // The screen's voltages and gamma curves: Waveshare's (the default) or LovyanGFX's
    void tuneScreen(bool waveshare);

    // How bright the glow around the eyes is (the innermost halo, 0..1.5; 1 by default)
    void setGlow(float brightness);
    float glow();

    // How the in-between shades are shown, to suit the panel (0.5..4: below 1 darker, above 1
    // brighter, 1 = unchanged; 0.5 by default)
    void setCurve(float curve);
    float curve();

    // Look at this point, -1..1 each way (left and up negative), instead of glancing
    // around at random (e.g. where the robot is driving)
    void lookAt(float x, float y);
    void lookAround();   // back to glancing around at random
}
