#include <M5Unified.h>

void setup() {
    M5.begin();
    M5.Display.setTextSize(2);
    M5.Display.println("ROLO head X");
    Serial.println("ROLO head starting");
}

void loop() {
    M5.update();
    if (M5.Touch.getCount() > 0) {
        auto t = M5.Touch.getDetail();
        M5.Display.fillCircle(t.x, t.y, 4, TFT_GREEN);
    }
}