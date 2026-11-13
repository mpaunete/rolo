#include <M5Unified.h>
#include <M5CoreS3.h>

#include "camera.h"
#include "net.h"
#include "web.h"
#include "console.h"
#include "eyes.h"
#include "face.h"
#include "stats.h"

void setup() {
    Serial.begin(115200);

    // Give the PC up to 2 s to reconnect to the USB serial after a reset
    uint32_t start = millis();
    while (!Serial && millis() - start < 2000) {
        delay(10);
    }
    
    CoreS3.begin(M5.config());
    Serial.println("ROLO head starting");
    stats::begin();   // first: it measures the cores while nothing else runs yet
    camera::begin();
    face::begin();
    net::begin({"rolo", false, &Serial});   // no Bluetooth here: no power saving, faster WiFi
    web::begin();
    eyes::begin();
}

void loop() {
    M5.update();
    console::poll();
    net::update();
    eyes::update();
    stats::update();
    delay(16);   // about 60 frames a second
}