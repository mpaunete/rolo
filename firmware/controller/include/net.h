#pragma once

namespace net {
    enum class Status { Connecting, Connected, AccessPoint };
    
    void begin();
    void update();
    void setCredentials(const char *ssid, const char *password);
    void forgetCredentials();
    void setApName(const char *name);
    void setApPassword(const char *password);
    void printStatus();
    Status getStatus();
}