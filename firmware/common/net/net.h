#pragma once

#include <Print.h>

namespace net {
    enum class Status { Connecting, Connected, AccessPoint };

    struct Config {
        const char *apName;   // prefix of the access-point name, e.g. "rolo" -> "rolo-3F2A91"
        bool powerSave;       // true on the body (Bluetooth shares the radio), false on a head
        Print *out;           // where status messages go (console::out, or Serial)
    };

    void begin(const Config &config);
    void update();
    void setCredentials(const char *ssid, const char *password);
    void forgetCredentials();
    void setApName(const char *name);
    void setApPassword(const char *password);
    void printStatus();
    Status getStatus();
}