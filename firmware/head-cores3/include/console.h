#pragma once

// Commands typed over USB serial: for now only the WiFi setup
namespace console {
    void poll();   // from loop(): reads and runs command lines
}