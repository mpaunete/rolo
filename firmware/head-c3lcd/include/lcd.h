#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// The Waveshare 2" LCD (ST7789, 240 x 320) on the ESP32-C3 SuperMini, over SPI with DMA
class Lcd : public lgfx::LGFX_Device {
public:
    Lcd() {
        auto bus = spi.config();
        bus.spi_host = SPI2_HOST;
        bus.spi_mode = 0;
        bus.freq_write = 40000000;         // 40 MHz: a full canvas takes about 15 ms
        bus.freq_read = 16000000;
        bus.pin_sclk = 3;
        bus.pin_mosi = 4;
        bus.pin_miso = -1;                 // the screen is never read
        bus.pin_dc = 1;
        bus.dma_channel = SPI_DMA_CH_AUTO; // send frames while the CPU moves on
        spi.config(bus);
        panel.setBus(&spi);

        auto p = panel.config();
        p.pin_cs = 2;
        p.pin_rst = 0;
        p.pin_busy = -1;
        p.panel_width = 240;
        p.panel_height = 320;
        p.readable = false;
        p.invert = true;                   // the Waveshare panel shows inverted colours without it
        p.rgb_order = false;
        p.bus_shared = false;              // nothing else on this SPI bus
        panel.config(p);

        setPanel(&panel);
    }

    // How the panel shows in-between shades: its voltages and gamma curves. LovyanGFX's
    // defaults are for another panel ("JLX240") and crush dark shades to black here, which
    // flattens the glow. The Waveshare values are from their demo code for this screen.
    enum class Tuning { Lovyan, Waveshare };

    void tune(Tuning tuning) {
        static const uint8_t lovyanPositive[] = {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32, 0x44, 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17};
        static const uint8_t lovyanNegative[] = {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31, 0x54, 0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E};
        static const uint8_t wavesharePositive[] = {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23};
        static const uint8_t waveshareNegative[] = {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23};
        bool waveshare = tuning == Tuning::Waveshare;

        startWrite();
        command(0xBB, waveshare ? 0x1F : 0x28);   // VCOMS: common voltage
        command(0xC3, waveshare ? 0x12 : 0x10);   // VRHS: gamma reference voltage
        command(0xE0, waveshare ? wavesharePositive : lovyanPositive, 14);   // gamma curves
        command(0xE1, waveshare ? waveshareNegative : lovyanNegative, 14);
        endWrite();
    }

private:
    void command(uint8_t cmd, uint8_t value) {
        command(cmd, &value, 1);
    }

    void command(uint8_t cmd, const uint8_t *data, size_t length) {
        writeCommand(cmd);
        for (size_t i = 0; i < length; i++) {
            writeData(data[i]);
        }
    }

    lgfx::Bus_SPI spi;
    lgfx::Panel_ST7789 panel;
};
