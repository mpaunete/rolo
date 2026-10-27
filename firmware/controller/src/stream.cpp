#include "stream.h"

#include <Arduino.h>
#include <string.h>

namespace stream {
    namespace {
        // Samples: written by the control loop (core 1), read by the web task (core 0).
        // The lock is only held for copies, a few microseconds at most.
        constexpr size_t SAMPLE_CAPACITY = 100;   // 0.5 s of buffering (7 kB): emptied every 50 ms
        Sample *samples = nullptr;                // allocated the first time someone records
        size_t head = 0;                          // next write
        size_t count = 0;
        uint32_t droppedCount = 0;
        volatile bool active = false;
        Sample newest;
        bool haveNewest = false;
        portMUX_TYPE sampleLock = portMUX_INITIALIZER_UNLOCKED;

        // Events: written by the web server (core 0) and the console (core 1)
        constexpr size_t EVENT_CAPACITY = 16;
        constexpr size_t EVENT_LENGTH = 64;
        struct Event {
            uint32_t timeUs;
            char text[EVENT_LENGTH];
        };
        Event events[EVENT_CAPACITY];
        size_t eventHead = 0;
        size_t eventCount = 0;
        portMUX_TYPE eventLock = portMUX_INITIALIZER_UNLOCKED;
    }

    void setActive(bool on) {
        if (on && samples == nullptr) {
            // From the heap, not static RAM: Bluetooth leaves too little static RAM
            samples = (Sample *)malloc(SAMPLE_CAPACITY * sizeof(Sample));
            if (samples == nullptr) {
                return;                           // no memory: stay inactive
            }
        }

        Sample *unused = nullptr;

        portENTER_CRITICAL(&sampleLock);
        if (on && !active) {
            count = 0;                            // start fresh, no stale samples
            droppedCount = 0;
        }
        active = on;
        if (!on && samples != nullptr) {
            // Nobody records any more: give the memory back. Taken out of reach here,
            // inside the lock, so no other task can still be using it when it's freed.
            unused = samples;
            samples = nullptr;
            count = 0;
        }
        portEXIT_CRITICAL(&sampleLock);

        free(unused);                             // outside the lock (free(nullptr) does nothing)
    }

    bool isActive() {
        return active;
    }

    void add(const Sample &sample) {
        portENTER_CRITICAL(&sampleLock);
        newest = sample;
        haveNewest = true;

        if (active) {
            samples[head] = sample;
            head = (head + 1) % SAMPLE_CAPACITY;
            if (count < SAMPLE_CAPACITY) {
                count++;
            } else {
                droppedCount++;                   // overwrote the oldest unsent sample
            }
        }
        portEXIT_CRITICAL(&sampleLock);
    }

    bool latest(Sample &out) {
        portENTER_CRITICAL(&sampleLock);
        bool have = haveNewest;
        out = newest;
        portEXIT_CRITICAL(&sampleLock);
        return have;
    }

    size_t take(Sample *out, size_t maxSamples) {
        portENTER_CRITICAL(&sampleLock);
        if (samples == nullptr) {
            portEXIT_CRITICAL(&sampleLock);
            return 0;                             // not recording: nothing to take
        }

        size_t n = count < maxSamples ? count : maxSamples;
        size_t start = (head + SAMPLE_CAPACITY - count) % SAMPLE_CAPACITY;

        for (size_t i = 0; i < n; i++) {
            out[i] = samples[(start + i) % SAMPLE_CAPACITY];
        }
        count -= n;
        portEXIT_CRITICAL(&sampleLock);
        return n;
    }

    uint32_t dropped() {
        return droppedCount;
    }

    void event(const char *text) {
        portENTER_CRITICAL(&eventLock);
        Event &e = events[eventHead];
        e.timeUs = micros();
        strncpy(e.text, text, EVENT_LENGTH - 1);
        e.text[EVENT_LENGTH - 1] = '\0';
        for (char *c = e.text; *c; c++) {
            if (*c == '"' || *c == '\\') {
                *c = '\'';                         // keep the JSON message valid
            }
        }
        eventHead = (eventHead + 1) % EVENT_CAPACITY;
        if (eventCount < EVENT_CAPACITY) {
            eventCount++;
        }
        portEXIT_CRITICAL(&eventLock);
    }

    bool takeEvent(char *out, size_t size, uint32_t &timeUs) {
        bool got = false;

        portENTER_CRITICAL(&eventLock);
        if (eventCount > 0) {
            size_t index = (eventHead + EVENT_CAPACITY - eventCount) % EVENT_CAPACITY;
            timeUs = events[index].timeUs;
            strncpy(out, events[index].text, size - 1);
            out[size - 1] = '\0';
            eventCount--;
            got = true;
        }
        portEXIT_CRITICAL(&eventLock);

        return got;
    }
}