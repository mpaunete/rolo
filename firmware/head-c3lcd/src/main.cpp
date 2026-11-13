#include <Arduino.h>

#include "console.h"
#include "eyes.h"

void setup() {
    Serial.begin(115200);

    // Give the PC up to 2 s to reconnect to the USB serial after a reset
    uint32_t start = millis();
    while (!Serial && millis() - start < 2000) {
        delay(10);
    }

    Serial.println("ROLO head (C3 + LCD) starting, try 'help'");
    eyes::begin();
}

void loop() {
    console::poll();
    eyes::update();
    delay(16);   // about 60 frames a second
}
