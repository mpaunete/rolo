#include <M5Unified.h>
#include <net.h>

#include "console.h"
#include "eyes.h"

void setup() {
    Serial.begin(115200);

    // Give the PC up to 2 s to reconnect to the USB serial after a reset
    uint32_t start = millis();
    while (!Serial && millis() - start < 2000) {
        delay(10);
    }
    
    M5.begin();
    Serial.println("ROLO head starting");
    net::begin({"rolo", false, &Serial});   // no Bluetooth here: no power saving, faster WiFi
    eyes::begin();
}

void loop() {
    M5.update();
    console::poll();
    net::update();
    eyes::update();
    delay(16);   // about 60 frames a second
}