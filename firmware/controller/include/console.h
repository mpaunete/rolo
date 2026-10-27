#pragma once

#include <Print.h>
#include <stddef.h>
#include <stdint.h>

// Commands and messages, from and to both the serial port and the web terminal.
namespace console {
    // Everything the robot prints: goes to the serial port, and is kept for the web
    // terminal. Use it like Serial: console::out.printf(...), console::out.println(...).
    // Safe from any task.
    class Output : public Print {
    public:
        size_t write(uint8_t c) override;
        size_t write(const uint8_t *buffer, size_t size) override;
    };
    extern Output out;

    void poll();                    // from loop(): reads command lines from the serial port
    void run(const char *line);     // runs one command line (serial or web terminal)

    // For the web terminal: the text printed since position 'from', at most size - 1
    // characters, '\0'-terminated. Moves 'from' on. If 'from' is older than the text
    // still kept, it starts at the oldest kept text. Returns the number of characters.
    size_t readOutput(uint32_t &from, char *text, size_t size);
}
