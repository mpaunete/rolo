#pragma once

// Commands over USB serial, for trying the eyes without the body
namespace console {
    void poll();   // from loop(): reads command lines, and runs the mood demo if it's on
}
