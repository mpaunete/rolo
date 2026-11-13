#include "stats.h"

#include <Arduino.h>
#include <esp_freertos_hooks.h>

namespace stats {
    namespace {
        constexpr uint32_t PERIOD_MS = 1000;

        // Idle hooks: each core calls its hook whenever it has nothing else to do, then
        // sleeps until the next interrupt (at least every 1 ms tick). Fewer calls per
        // second than at start-up means the core was busy. It's an estimate: busy WiFi
        // also means more interrupts, so more wake-ups, which reads as a little less load.
        volatile uint32_t idleCalls[2] = {0, 0};
        uint32_t idleWhenFree[2] = {1, 1};   // calls per second with nothing running (calibrated)
        uint32_t lastIdle[2] = {0, 0};

        bool idleCore0() { idleCalls[0]++; return true; }   // true: go back to sleep
        bool idleCore1() { idleCalls[1]++; return true; }

        // Our own work, added up between updates (written by the face and eye tasks)
        portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
        uint32_t faceCount = 0, faceCopyUs = 0, faceDetectUs = 0;
        uint32_t eyesCount = 0, eyesDrawUs = 0, eyesPushUs = 0;
        uint32_t frameCount = 0, frameAgeUs = 0;
        uint32_t streamCount = 0, streamEncodeUs = 0, streamSendUs = 0, streamBytes = 0;

        Snapshot current = {};
        uint32_t lastUpdate = 0;
        bool printing = false;
    }

    void begin() {
        esp_register_freertos_idle_hook_for_cpu(idleCore0, 0);
        esp_register_freertos_idle_hook_for_cpu(idleCore1, 1);

        // Calibrate: one second with (almost) nothing running on either core
        uint32_t start0 = idleCalls[0], start1 = idleCalls[1];
        delay(PERIOD_MS);
        idleWhenFree[0] = max<uint32_t>(1, idleCalls[0] - start0);
        idleWhenFree[1] = max<uint32_t>(1, idleCalls[1] - start1);
        lastIdle[0] = idleCalls[0];
        lastIdle[1] = idleCalls[1];
        lastUpdate = millis();
        Serial.printf("stats: idle calls per second when free: core 0 %lu, core 1 %lu\n",
                      (unsigned long)idleWhenFree[0], (unsigned long)idleWhenFree[1]);
    }

    void faceDone(uint32_t copyUs, uint32_t detectUs) {
        portENTER_CRITICAL(&lock);
        faceCount++;
        faceCopyUs += copyUs;
        faceDetectUs += detectUs;
        portEXIT_CRITICAL(&lock);
    }

    void eyesDone(uint32_t drawUs, uint32_t pushUs) {
        portENTER_CRITICAL(&lock);
        eyesCount++;
        eyesDrawUs += drawUs;
        eyesPushUs += pushUs;
        portEXIT_CRITICAL(&lock);
    }

    void frameTaken(uint32_t ageUs) {
        portENTER_CRITICAL(&lock);
        frameCount++;
        frameAgeUs += ageUs;
        portEXIT_CRITICAL(&lock);
    }

    void streamDone(uint32_t encodeUs, uint32_t sendUs, uint32_t bytes) {
        portENTER_CRITICAL(&lock);
        streamCount++;
        streamEncodeUs += encodeUs;
        streamSendUs += sendUs;
        streamBytes += bytes;
        portEXIT_CRITICAL(&lock);
    }

    void update() {
        uint32_t now = millis();
        uint32_t elapsed = now - lastUpdate;
        if (elapsed < PERIOD_MS) {
            return;
        }
        lastUpdate = now;

        // Take this period's totals and start the next one
        portENTER_CRITICAL(&lock);
        uint32_t fN = faceCount, fCopy = faceCopyUs, fDetect = faceDetectUs;
        uint32_t eN = eyesCount, eDraw = eyesDrawUs, ePush = eyesPushUs;
        uint32_t frN = frameCount, frAge = frameAgeUs;
        uint32_t sN = streamCount, sEncode = streamEncodeUs, sSend = streamSendUs, sBytes = streamBytes;
        faceCount = faceCopyUs = faceDetectUs = 0;
        eyesCount = eyesDrawUs = eyesPushUs = 0;
        frameCount = frameAgeUs = 0;
        streamCount = streamEncodeUs = streamSendUs = streamBytes = 0;
        portEXIT_CRITICAL(&lock);

        Snapshot s = {};
        for (int core = 0; core < 2; core++) {
            uint32_t calls = idleCalls[core] - lastIdle[core];
            lastIdle[core] = idleCalls[core];
            float perSecond = calls * 1000.0f / elapsed;
            s.coreLoad[core] = constrain(100.0f * (1.0f - perSecond / idleWhenFree[core]), 0.0f, 100.0f);
        }

        float seconds = elapsed / 1000.0f;
        s.faceRate = fN / seconds;
        s.faceCopyMs = fN ? fCopy / 1000.0f / fN : 0.0f;
        s.faceDetectMs = fN ? fDetect / 1000.0f / fN : 0.0f;
        s.faceCore = (fCopy + fDetect) / 10.0f / elapsed;   // us / (ms * 1000) * 100 %
        s.eyesRate = eN / seconds;
        s.eyesDrawMs = eN ? eDraw / 1000.0f / eN : 0.0f;
        s.eyesPushMs = eN ? ePush / 1000.0f / eN : 0.0f;
        s.eyesCore = (eDraw + ePush) / 10.0f / elapsed;
        s.frameAgeMs = frN ? frAge / 1000.0f / frN : 0.0f;
        s.streamRate = sN / seconds;
        s.streamEncodeMs = sN ? sEncode / 1000.0f / sN : 0.0f;
        s.streamSendMs = sN ? sSend / 1000.0f / sN : 0.0f;
        s.streamKB = sN ? sBytes / 1024.0f / sN : 0.0f;
        s.streamCore = sEncode / 10.0f / elapsed;
        s.freeHeap = ESP.getFreeHeap();
        s.freePsram = ESP.getFreePsram();

        portENTER_CRITICAL(&lock);
        current = s;
        portEXIT_CRITICAL(&lock);

        if (printing) {
            print();
        }
    }

    Snapshot latest() {
        portENTER_CRITICAL(&lock);
        Snapshot s = current;
        portEXIT_CRITICAL(&lock);
        return s;
    }

    void print() {
        Snapshot s = latest();
        Serial.printf("core0 %3.0f%% core1 %3.0f%% | face %4.1f/s copy %4.1f ms detect %5.1f ms (%3.0f%% of core0)"
                      " | eyes %4.1f/s draw %4.1f ms push %4.1f ms (%3.0f%% of core1)"
                      " | frame age %4.1f ms | stream %4.1f/s encode %4.1f ms send %4.1f ms %4.1f kB (%3.0f%%)"
                      " | heap %lu psram %lu\n",
                      s.coreLoad[0], s.coreLoad[1], s.faceRate, s.faceCopyMs, s.faceDetectMs, s.faceCore,
                      s.eyesRate, s.eyesDrawMs, s.eyesPushMs, s.eyesCore,
                      s.frameAgeMs, s.streamRate, s.streamEncodeMs, s.streamSendMs, s.streamKB, s.streamCore,
                      (unsigned long)s.freeHeap, (unsigned long)s.freePsram);
    }

    void printEverySecond(bool on) {
        printing = on;
    }
}
