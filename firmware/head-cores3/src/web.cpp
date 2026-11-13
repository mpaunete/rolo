#include "web.h"
#include "camera.h"
#include "face.h"
#include "stats.h"

#include <Arduino.h>
#include <esp_http_server.h>
#include <img_converters.h>

namespace web {
    namespace {
        httpd_handle_t server = nullptr;
        httpd_handle_t streamServer = nullptr;   // port 81: the stream gets a server of its own

        // "/": a small page saying the head is alive
        esp_err_t handleStatus(httpd_req_t *request) {
            static char page[4096];
            int n = snprintf(page, sizeof(page),
                "<!doctype html><meta charset='utf-8'><title>ROLO</title>"
                "<h1>ROLO</h1>"
                "<p>M5Stack CoreS3, running for %lu s</p>"
                "<p>free memory: %lu bytes, free PSRAM: %lu bytes</p>"
                // The stream, with a transparent drawing layer on top for the face box
                "<div style='position:relative;width:320px;height:240px'>"
                "<img id='video' width='320' height='240'>"
                "<canvas id='boxes' width='320' height='240' style='position:absolute;left:0;top:0'></canvas>"
                "</div>"
                // The stream costs most of a core (software JPEG on the head) and slows the face
                // and the eyes: off until asked for
                "<p><button id='cam'>camera on</button></p>"
                "<table id='st' style='font:14px monospace;border-spacing:12px 2px'></table>"
                "<script>"
                "const url='http://'+location.hostname+':81/stream';"
                "let on=false;"
                "cam.onclick=()=>{on=!on;if(on){video.src=url;}else{video.removeAttribute('src');video.src='';}"
                  "cam.textContent=on?'camera off':'camera on';};"
                // The stats, once a second
                "const row=(a,b)=>'<tr><td>'+a+'</td><td>'+b+'</td></tr>';"
                "setInterval(async()=>{"
                  "const s=await (await fetch('/stats')).json();"
                  "st.innerHTML="
                    "row('core 0',s.core0.toFixed(0)+'%%  (estimated)')+"
                    "row('core 1',s.core1.toFixed(0)+'%%  (estimated)')+"
                    "row('face',s.faceRate.toFixed(1)+'/s  detect '+s.faceDetectMs.toFixed(1)+' ms  copy '+s.faceCopyMs.toFixed(1)+' ms  = '+s.faceCore.toFixed(0)+'%% of core 0')+"
                    "row('eyes',s.eyesRate.toFixed(1)+'/s  draw '+s.eyesDrawMs.toFixed(1)+' ms  push '+s.eyesPushMs.toFixed(1)+' ms  = '+s.eyesCore.toFixed(0)+'%% of core 1')+"
                    "row('camera','frame age '+s.frameAgeMs.toFixed(0)+' ms')+"
                    "row('stream',s.streamRate.toFixed(1)+'/s  encode '+s.streamEncodeMs.toFixed(1)+' ms  send '+s.streamSendMs.toFixed(1)+' ms  '+s.streamKB.toFixed(1)+' kB  = '+s.streamCore.toFixed(0)+'%% of a core')+"
                    "row('memory',(s.heap/1024).toFixed(0)+' kB free, PSRAM '+(s.psram/1024).toFixed(0)+' kB free');"
                "},1000);"
                "const g=boxes.getContext('2d');"
                "setInterval(async()=>{"
                  "const f=await (await fetch('/face')).json();"
                  "g.clearRect(0,0,320,240);"
                  "if(!f.found||f.age>500)return;"
                  "g.strokeStyle='#0f0';g.lineWidth=2;g.strokeRect(f.x0,f.y0,f.x1-f.x0,f.y1-f.y0);"
                  "g.fillStyle='#0f0';g.font='14px sans-serif';"
                  "g.fillText('face '+Math.round(f.score*100)+'%%',f.x0,f.y0-4);"
                "},200);"
                "</script>",
                millis() / 1000, (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getFreePsram());

            if (n >= (int)sizeof(page)) {
                Serial.println("web: status page too long for its buffer");
            }

            httpd_resp_set_type(request, "text/html");
            return httpd_resp_send(request, page, HTTPD_RESP_USE_STRLEN);
        }

        // "/face": the latest face detection, as JSON
        esp_err_t handleFace(httpd_req_t *request) {
            face::Detection d = face::latest();
            char json[160];
            snprintf(json, sizeof(json),
                "{\"found\":%s,\"x0\":%d,\"y0\":%d,\"x1\":%d,\"y1\":%d,\"score\":%.2f,\"age\":%lu}",
                d.found ? "true" : "false", d.x0, d.y0, d.x1, d.y1, d.score,
                (unsigned long)(millis() - d.time));
            httpd_resp_set_type(request, "application/json");
            return httpd_resp_send(request, json, HTTPD_RESP_USE_STRLEN);
        }

        // "/stats": how busy the head is (see stats.h), as JSON
        esp_err_t handleStats(httpd_req_t *request) {
            stats::Snapshot s = stats::latest();
            char json[480];
            snprintf(json, sizeof(json),
                "{\"core0\":%.1f,\"core1\":%.1f,"
                "\"faceRate\":%.1f,\"faceCopyMs\":%.1f,\"faceDetectMs\":%.1f,\"faceCore\":%.1f,"
                "\"eyesRate\":%.1f,\"eyesDrawMs\":%.1f,\"eyesPushMs\":%.1f,\"eyesCore\":%.1f,"
                "\"frameAgeMs\":%.1f,\"streamRate\":%.1f,\"streamEncodeMs\":%.1f,\"streamSendMs\":%.1f,"
                "\"streamKB\":%.1f,\"streamCore\":%.1f,"
                "\"heap\":%lu,\"psram\":%lu}",
                s.coreLoad[0], s.coreLoad[1],
                s.faceRate, s.faceCopyMs, s.faceDetectMs, s.faceCore,
                s.eyesRate, s.eyesDrawMs, s.eyesPushMs, s.eyesCore,
                s.frameAgeMs, s.streamRate, s.streamEncodeMs, s.streamSendMs, s.streamKB, s.streamCore,
                (unsigned long)s.freeHeap, (unsigned long)s.freePsram);
            httpd_resp_set_type(request, "application/json");
            return httpd_resp_send(request, json, HTTPD_RESP_USE_STRLEN);
        }

        // "/capture": one picture from the camera, as JPEG
        esp_err_t handleCapture(httpd_req_t *request) {
            uint8_t *jpeg = nullptr;
            size_t length = 0;
            if (!camera::captureJpeg(&jpeg, &length)) {
                httpd_resp_send_500(request);
                return ESP_FAIL;
            }
            httpd_resp_set_type(request, "image/jpeg");
            esp_err_t result = httpd_resp_send(request, (const char *)jpeg, length);
            free(jpeg);   // frame2jpg() allocated it
            return result;
        }

        // "/stream" (port 81): one picture after another, until the viewer closes it
        esp_err_t handleStream(httpd_req_t *request) {
            httpd_resp_set_type(request, "multipart/x-mixed-replace;boundary=frame");

            while (true) {
                // Take a picture and compress it, timed: the JPEG is made in software here
                camera_fb_t *frame = camera::takeFrame();
                if (frame == nullptr) {
                    return ESP_FAIL;
                }
                uint8_t *jpeg = nullptr;
                size_t length = 0;
                uint32_t start = micros();
                bool ok = frame2jpg(frame, camera::JPEG_QUALITY, &jpeg, &length);
                uint32_t encodeUs = micros() - start;
                camera::giveBack();
                if (!ok) {
                    return ESP_FAIL;
                }

                // Each picture: a separator, its type and size, then the JPEG itself
                char header[96];
                int n = snprintf(header, sizeof(header),
                    "\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                    (unsigned)length);
                start = micros();
                esp_err_t result = httpd_resp_send_chunk(request, header, n);
                if (result == ESP_OK) {
                    result = httpd_resp_send_chunk(request, (const char *)jpeg, length);
                }
                stats::streamDone(encodeUs, micros() - start, length);
                free(jpeg);

                if (result != ESP_OK) {
                    return ESP_OK;   // the viewer closed the stream: that's the normal end
                }
            }
        }
    }

    void begin() {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        if (httpd_start(&server, &config) != ESP_OK) {
            Serial.println("web: could not start the server");
            return;
        }

        httpd_uri_t status = {};
        status.uri = "/";
        status.method = HTTP_GET;
        status.handler = handleStatus;
        httpd_register_uri_handler(server, &status);

        httpd_uri_t capture = {};
        capture.uri = "/capture";
        capture.method = HTTP_GET;
        capture.handler = handleCapture;
        httpd_register_uri_handler(server, &capture);

        httpd_uri_t statsUri = {};
        statsUri.uri = "/stats";
        statsUri.method = HTTP_GET;
        statsUri.handler = handleStats;
        httpd_register_uri_handler(server, &statsUri);

        httpd_uri_t faceUri = {};
        faceUri.uri = "/face";
        faceUri.method = HTTP_GET;
        faceUri.handler = handleFace;
        httpd_register_uri_handler(server, &faceUri);

        // A second server on port 81 for the stream, so the pages on port 80 still answer
        httpd_config_t streamConfig = HTTPD_DEFAULT_CONFIG();
        streamConfig.server_port = 81;
        streamConfig.ctrl_port = config.ctrl_port + 1;   // each server needs its own internal port
        if (httpd_start(&streamServer, &streamConfig) == ESP_OK) {
            httpd_uri_t stream = {};
            stream.uri = "/stream";
            stream.method = HTTP_GET;
            stream.handler = handleStream;
            httpd_register_uri_handler(streamServer, &stream);
        }

        Serial.println("web: server started on port 80");
    }
}