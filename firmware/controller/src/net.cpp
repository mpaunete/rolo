#include "net.h"
#include "console.h"

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_wifi.h>

namespace net {
    namespace {
        constexpr const char *STORAGE = "net";
        constexpr const char *DEFAULT_AP_NAME = "rolo";
        // First-boot access point. The password is public (it's in this file and the README):
        // change it with the console commands "apname <name>" and "appass <password>"
        constexpr const char *DEFAULT_AP_PASSWORD = "rolorolo";

        constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;
        constexpr uint32_t RETRY_INTERVAL_MS = 60000;

        enum class State {
            Connecting,
            Connected,
            AccessPoint,
        };

        State state = State::Connecting;
        uint32_t stateSince = 0;
        uint32_t lastRetry = 0;

        void enter(State next) {
            state = next;
            stateSince = millis();
        }

        bool hasStoredCredentials() {
            wifi_config_t conf;
            if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) {
                return false;
            }
            return conf.sta.ssid[0] != '\0';
        }

        void startAccessPoint() {
            Preferences prefs;
            String name = DEFAULT_AP_NAME;
            String password = DEFAULT_AP_PASSWORD;
            if (prefs.begin(STORAGE, true)) {
                name = prefs.getString("apName", DEFAULT_AP_NAME);
                password = prefs.getString("apPassword", DEFAULT_AP_PASSWORD);
                prefs.end();
            }

            WiFi.setAutoReconnect(false);
            WiFi.disconnect();
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAP(name.c_str(), password.c_str());
            lastRetry = millis();
            enter(State::AccessPoint);

            console::out.printf("WiFi: access point '%s' at %s\n", name.c_str(), WiFi.softAPIP().toString().c_str());
        }

        void stopAccessPoint() {
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            WiFi.setAutoReconnect(true);
        }
    }

    void begin() {
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(true);
        WiFi.setAutoReconnect(true);

        if (hasStoredCredentials()) {
            WiFi.begin();
            enter(State::Connecting);
        } else {
            console::out.println("WiFi: no network configured");
            startAccessPoint();
        }
    }

    void update() {
        bool connected = WiFi.status() == WL_CONNECTED;
        uint32_t now = millis();

        switch (state) {
            case State::Connecting:
                if (connected) {
                    enter(State::Connected);
                    printStatus();
                } else if ((uint32_t)(now - stateSince) > CONNECT_TIMEOUT_MS) {
                    console::out.println("WiFi: could not connect");
                    startAccessPoint();
                }
                break;

            case State::Connected:
                if (connected) {
                    stateSince = now;
                } else if ((uint32_t)(now - stateSince) > CONNECT_TIMEOUT_MS) {
                    console::out.println("WiFi: connection lost");
                    startAccessPoint();
                }
                break;

            case State::AccessPoint:
                if (connected) {
                    stopAccessPoint();
                    enter(State::Connected);
                    printStatus();
                } else if (hasStoredCredentials() && (uint32_t)(now - lastRetry) > RETRY_INTERVAL_MS) {
                    lastRetry = now;
                    WiFi.begin();
                }
                break;
        }
    }

    void setCredentials(const char *ssid, const char *password) {
        WiFi.begin(ssid, password);
        lastRetry = millis();
        if (state != State::AccessPoint) {
            enter(State::Connecting);
        }
    }

    void forgetCredentials() {
        WiFi.disconnect(false, true);  // keep WiFi on, erase the saved network
        startAccessPoint();
    }

    void setApName(const char *name) {
        Preferences prefs;
        prefs.begin(STORAGE, false);
        prefs.putString("apName", name);
        prefs.end();
    }

    void setApPassword(const char *password) {
        Preferences prefs;
        prefs.begin(STORAGE, false);
        prefs.putString("apPassword", password);
        prefs.end();
    }

    void printStatus() {
        if (state == State::AccessPoint) {
            console::out.printf("WiFi: access point at %s\n", WiFi.softAPIP().toString().c_str());
        } else if (WiFi.status() == WL_CONNECTED) {
            console::out.printf("WiFi: connected to '%s', IP %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        } else {
            console::out.println("WiFi: connecting...");
        }
    }

    Status getStatus() {
        switch (state) {
            case State::Connected:   return Status::Connected;
            case State::AccessPoint: return Status::AccessPoint;
            default:                 return Status::Connecting;
        }
    }
}