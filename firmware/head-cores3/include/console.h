#pragma once

// Commands typed over USB serial: the WiFi setup and the stats
namespace console {
    void poll();   // from loop(): reads and runs command lines
}