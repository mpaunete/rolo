#include "web.h"
#include "calibration_page.h"
#include "drive.h"
#include "drive_page.h"
#include "stream.h"
#include "gamepad.h"
#include "gamepad_page.h"
#include "params.h"
#include "console.h"
#include "rolo_js.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <stdio.h>
#include <string.h>

namespace web {
    namespace {
        // Everything is sent from the web task on core 0, never from the control loop
        constexpr uint32_t TASK_PERIOD_MS = 10;
        constexpr uint32_t STREAM_INTERVAL_MS = 50;    // recording: a batch 20 times a second
        constexpr uint32_t LIVE_INTERVAL_MS = 100;     // pages: the newest sample, 10 times a second
        constexpr uint32_t CLEANUP_INTERVAL_MS = 1000;

        AsyncWebServer server(80);
        AsyncWebSocket ws("/ws");

        // Who wants what: "stream on" (every sample, the PC tool), "live on" (the newest
        // sample, the pages), "pad on" (gamepad status, the gamepad page), "log on"
        // (everything printed, the terminal on the calibration page)
        enum class Feed { Stream, Live, Pad, Log };
        struct Listener {
            uint32_t id;        // 0 = free slot
            bool stream;
            bool live;
            bool pad;
            bool log;
            uint32_t logFrom;   // how much of the printed text this client has had
        };
        constexpr size_t MAX_LISTENERS = 6;
        Listener listeners[MAX_LISTENERS] = {};
        portMUX_TYPE listenersLock = portMUX_INITIALIZER_UNLOCKED;   // changed by the server, read by the task

        // Batch header in front of the samples: magic 'R', version, sample count, dropped total
        struct __attribute__((packed)) BatchHeader {
            uint8_t magic;
            uint8_t version;
            uint16_t count;
            uint32_t dropped;
        };
        constexpr size_t MAX_BATCH = 40;   // 200 ms of samples per message at most
        constexpr size_t MAX_QUEUED = 2;   // don't pile up messages for a slow client
        constexpr size_t HEAP_RESERVE = 16384;   // always leave this much free (in total) for WiFi/Bluetooth
        constexpr size_t BLOCK_MARGIN = 1024;    // the message's own block, plus this

        void listen(uint32_t id, Feed feed, bool on) {
            bool recording = false;

            portENTER_CRITICAL(&listenersLock);
            Listener *slot = nullptr;
            for (Listener &l : listeners) {
                if (l.id == id) {
                    slot = &l;
                    break;
                }
            }
            if (slot == nullptr && on) {
                for (Listener &l : listeners) {
                    if (l.id == 0) {
                        slot = &l;
                        *slot = {id, false, false, false, false, 0};
                        break;
                    }
                }
            }
            if (slot != nullptr) {
                if (feed == Feed::Stream) slot->stream = on;
                if (feed == Feed::Live) slot->live = on;
                if (feed == Feed::Pad) slot->pad = on;
                if (feed == Feed::Log) {
                    slot->log = on;
                    slot->logFrom = 0;   // starts with the text printed so far
                }
                if (!slot->stream && !slot->live && !slot->pad && !slot->log) {
                    slot->id = 0;   // wants nothing: free the slot
                }
            }
            for (const Listener &l : listeners) {
                recording |= (l.id != 0 && l.stream);
            }
            portEXIT_CRITICAL(&listenersLock);

            stream::setActive(recording);
        }

        void forget(uint32_t id) {
            listen(id, Feed::Stream, false);
            listen(id, Feed::Live, false);
            listen(id, Feed::Pad, false);
            listen(id, Feed::Log, false);
        }

        // Memory for one more message: enough left in total for WiFi/Bluetooth, and one free
        // block big enough for the message itself (free memory is split into many blocks)
        bool memoryFor(size_t length) {
            return ESP.getFreeHeap() >= length + HEAP_RESERVE
                && ESP.getMaxAllocHeap() >= length + BLOCK_MARGIN;
        }

        // True if this client has room for another message, and memory is available
        bool canSend(uint32_t id, size_t length) {
            if (!memoryFor(length)) {
                return false;
            }
            AsyncWebSocketClient *c = ws.client(id);
            return c != nullptr && c->queueLen() < MAX_QUEUED;
        }

        // Batches held back: say why, once a second (it reaches the recorder as an event)
        void reportHeld(size_t length) {
            static uint32_t lastReport = 0;
            if ((uint32_t)(millis() - lastReport) < 1000) {
                return;
            }
            lastReport = millis();

            char text[64];
            if (!memoryFor(length)) {
                snprintf(text, sizeof(text), "stream held: memory %lu free, largest block %lu",
                         (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());
            } else {
                snprintf(text, sizeof(text), "stream held: client queue full");
            }
            stream::event(text);
        }

        // Recording: the samples collected since last time, and any events
        void sendStream(const Listener *list) {
            bool anyone = false;
            for (size_t i = 0; i < MAX_LISTENERS; i++) {
                anyone |= (list[i].id != 0 && list[i].stream);
            }
            if (!anyone || !stream::isActive()) {
                return;
            }

            // Allocated once, on the heap (static RAM is tight with Bluetooth)
            static uint8_t *buffer = (uint8_t *)malloc(sizeof(BatchHeader) + MAX_BATCH * sizeof(stream::Sample));
            if (buffer == nullptr) {
                return;
            }
            stream::Sample *samples = (stream::Sample *)(buffer + sizeof(BatchHeader));

            // At most two batches per call. If a client or the memory can't keep up,
            // samples wait in the stream buffer (and are counted as dropped if it fills).
            for (int batch = 0; batch < 2; batch++) {
                bool room = true;
                for (size_t i = 0; i < MAX_LISTENERS; i++) {
                    if (list[i].id != 0 && list[i].stream) {
                        room &= canSend(list[i].id, sizeof(BatchHeader) + MAX_BATCH * sizeof(stream::Sample));
                    }
                }
                if (!room) {
                    reportHeld(sizeof(BatchHeader) + MAX_BATCH * sizeof(stream::Sample));
                    break;
                }
                size_t n = stream::take(samples, MAX_BATCH);
                if (n == 0) {
                    break;
                }

                BatchHeader header = {'R', stream::VERSION, (uint16_t)n, stream::dropped()};
                memcpy(buffer, &header, sizeof(header));
                size_t length = sizeof(header) + n * sizeof(stream::Sample);

                for (size_t i = 0; i < MAX_LISTENERS; i++) {
                    if (list[i].id != 0 && list[i].stream) {
                        ws.binary(list[i].id, buffer, length);
                    }
                }
            }

            char text[64];
            uint32_t timeUs;
            while (stream::takeEvent(text, sizeof(text), timeUs)) {
                char json[128];
                snprintf(json, sizeof(json), "{\"type\":\"event\",\"t\":%lu,\"text\":\"%s\"}",
                         (unsigned long)timeUs, text);
                for (size_t i = 0; i < MAX_LISTENERS; i++) {
                    if (list[i].id != 0 && list[i].stream) {
                        ws.text(list[i].id, json);
                    }
                }
            }
        }

        // Pages: the newest sample, as a batch of one (same format as the recording)
        void sendLive(const Listener *list) {
            struct __attribute__((packed)) {
                BatchHeader header;
                stream::Sample sample;
            } message;

            if (!stream::latest(message.sample)) {
                return;
            }
            message.header = {'R', stream::VERSION, 1, 0};

            for (size_t i = 0; i < MAX_LISTENERS; i++) {
                if (list[i].id != 0 && list[i].live && canSend(list[i].id, sizeof(message))) {
                    ws.binary(list[i].id, (uint8_t *)&message, sizeof(message));
                }
            }
        }

        void sendPad(const Listener *list) {
            char json[384];
            bool formatted = false;

            for (size_t i = 0; i < MAX_LISTENERS; i++) {
                if (list[i].id != 0 && list[i].pad && canSend(list[i].id, sizeof(json))) {
                    if (!formatted) {
                        gamepad::statusJson(json, sizeof(json));
                        formatted = true;
                    }
                    ws.text(list[i].id, json);
                }
            }
        }

        // {"type":"params","items":[{"name":"kp","value":3000,"min":0,"max":10000},...]}
        void buildParamsJson(char *buf, size_t size) {
            size_t used = snprintf(buf, size, "{\"type\":\"params\",\"items\":[");

            for (size_t i = 0; i < params::count() && used < size; i++) {
                const params::Entry &e = params::entry(i);
                used += snprintf(buf + used, size - used,
                    "%s{\"name\":\"%s\",\"value\":%g,\"min\":%g,\"max\":%g}",
                    i == 0 ? "" : ",", e.name, *e.value, e.min, e.max);
            }

            if (used < size) {
                snprintf(buf + used, size - used, "]}");
            }
        }

        void sendParams(AsyncWebSocketClient *client) {
            char json[1536];   // about 60 bytes per parameter
            buildParamsJson(json, sizeof(json));

            if (client != nullptr) {
                client->text(json);
            } else {
                ws.textAll(json);
            }
        }

        // Text for JSON: quotes, backslashes and control characters escaped
        size_t escapeJson(const char *text, char *out, size_t size) {
            size_t used = 0;
            for (const char *c = text; *c && used + 7 < size; c++) {
                if (*c == '"' || *c == '\\') {
                    out[used++] = '\\';
                    out[used++] = *c;
                } else if (*c == '\n') {
                    out[used++] = '\\';
                    out[used++] = 'n';
                } else if ((uint8_t)*c < 0x20) {
                    used += snprintf(out + used, size - used, "\\u%04x", (uint8_t)*c);
                } else {
                    out[used++] = *c;
                }
            }
            out[used] = '\0';
            return used;
        }

        // Terminal: whatever was printed since each client's last message
        void sendLog(const Listener *list) {
            for (size_t i = 0; i < MAX_LISTENERS; i++) {
                if (list[i].id == 0 || !list[i].log) {
                    continue;
                }

                char text[256];
                char json[600];
                if (!canSend(list[i].id, sizeof(json))) {
                    continue;
                }

                uint32_t from = list[i].logFrom;
                if (console::readOutput(from, text, sizeof(text)) == 0) {
                    continue;
                }
                int used = snprintf(json, sizeof(json), "{\"type\":\"log\",\"text\":\"");
                used += escapeJson(text, json + used, sizeof(json) - used - 3);
                snprintf(json + used, sizeof(json) - used, "\"}");
                ws.text(list[i].id, json);

                // Remember how far this client got (in the real list, not the copy)
                portENTER_CRITICAL(&listenersLock);
                for (Listener &l : listeners) {
                    if (l.id == list[i].id) {
                        l.logFrom = from;
                    }
                }
                portEXIT_CRITICAL(&listenersLock);
            }
        }

        void task(void *) {
            uint32_t lastStream = 0;
            uint32_t lastLive = 0;
            uint32_t lastCleanup = 0;
            uint32_t paramsSeen = params::changes();

            for (;;) {
                vTaskDelay(pdMS_TO_TICKS(TASK_PERIOD_MS));
                uint32_t now = millis();

                if ((uint32_t)(now - lastCleanup) >= CLEANUP_INTERVAL_MS) {
                    lastCleanup = now;
                    ws.cleanupClients();
                }

                // Work from a copy, so a page connecting or leaving can't change the list mid-send
                Listener list[MAX_LISTENERS];
                portENTER_CRITICAL(&listenersLock);
                memcpy(list, listeners, sizeof(list));
                portEXIT_CRITICAL(&listenersLock);

                if ((uint32_t)(now - lastStream) >= STREAM_INTERVAL_MS) {
                    lastStream = now;
                    sendStream(list);
                }
                if ((uint32_t)(now - lastLive) >= LIVE_INTERVAL_MS) {
                    lastLive = now;
                    sendLive(list);
                    sendPad(list);
                    sendLog(list);
                }

                // Parameters changed (from any page, the terminal or the serial port): show everyone
                if (params::changes() != paramsSeen) {
                    paramsSeen = params::changes();
                    sendParams(nullptr);
                }
            }
        }

        // From a page or the PC tool. "drive <x> <y>" and the feeds ("stream", "live", "pad",
        // "log" + on|off) are handled here; every other line is a console command, the same
        // as typed on the serial port.
        void handleCommand(AsyncWebSocketClient *client, const char *text) {
            float x, y;

            // Sent ~20 times a second while the joystick is held: no reply
            if (sscanf(text, "drive %f %f", &x, &y) == 2) {
                drive::command(x, y);
                return;
            }

            // Feeds: what this client wants sent to it
            char feed[8];
            char onOff[4];
            if (sscanf(text, "%7s %3s", feed, onOff) == 2 && (strcmp(onOff, "on") == 0 || strcmp(onOff, "off") == 0)) {
                bool on = strcmp(onOff, "on") == 0;
                if (strcmp(feed, "stream") == 0) {
                    listen(client->id(), Feed::Stream, on);
                    if (on) {
                        // How much room there is: recorded at the start of every recording
                        char note[64];
                        snprintf(note, sizeof(note), "memory: %lu free, largest block %lu, lowest %lu",
                                 (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMaxAllocHeap(),
                                 (unsigned long)ESP.getMinFreeHeap());
                        stream::event(note);
                    }
                    return;
                }
                if (strcmp(feed, "live") == 0) {
                    listen(client->id(), Feed::Live, on);
                    return;
                }
                if (strcmp(feed, "pad") == 0) {
                    listen(client->id(), Feed::Pad, on);
                    return;
                }
                if (strcmp(feed, "log") == 0) {
                    listen(client->id(), Feed::Log, on);
                    return;
                }
            }

            console::run(text, console::Source::Web);
        }

        void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
            if (type == WS_EVT_CONNECT) {
                sendParams(client);
                return;
            }

            if (type == WS_EVT_DISCONNECT) {
                forget(client->id());
                return;
            }

            if (type != WS_EVT_DATA) {
                return;
            }

            // Only handle complete, single-frame text messages
            AwsFrameInfo *info = (AwsFrameInfo *)arg;
            if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) {
                return;
            }

            char text[64];
            size_t n = len < sizeof(text) - 1 ? len : sizeof(text) - 1;
            memcpy(text, data, n);
            text[n] = '\0';

            handleCommand(client, text);
        }
    }

    // Pages are sent straight from flash in small pieces. Sending them as a plain string
    // would first copy the whole page into memory (and run out with Bluetooth on).
    static void sendFromFlash(AsyncWebServerRequest *request, const char *type, const char *content, size_t length) {
        request->send(200, type, (const uint8_t *)content, length);
    }

    void begin() {
        ws.onEvent(onEvent);
        server.addHandler(&ws);

        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
            request->redirect("/drive");
        });

        server.on("/drive", HTTP_GET, [](AsyncWebServerRequest *request) {
            sendFromFlash(request, "text/html", DRIVE_PAGE, sizeof(DRIVE_PAGE) - 1);
        });

        server.on("/gamepad", HTTP_GET, [](AsyncWebServerRequest *request) {
            sendFromFlash(request, "text/html", GAMEPAD_PAGE, sizeof(GAMEPAD_PAGE) - 1);
        });

        server.on("/rolo.js", HTTP_GET, [](AsyncWebServerRequest *request) {
            sendFromFlash(request, "text/javascript", ROLO_JS, sizeof(ROLO_JS) - 1);
        });

        server.on("/calibration", HTTP_GET, [](AsyncWebServerRequest *request) {
            sendFromFlash(request, "text/html", CALIBRATION_PAGE, sizeof(CALIBRATION_PAGE) - 1);
        });

        server.begin();

        // Core 0, with WiFi: sending never holds up the control loop on core 1
        xTaskCreatePinnedToCore(task, "web", 6144, nullptr, 1, nullptr, 0);
    }
}
