#pragma once

namespace web {
    // Starts the web server and the web task (core 0), which does all the sending:
    // the recording stream, the pages' live view and the gamepad status
    void begin();
}
