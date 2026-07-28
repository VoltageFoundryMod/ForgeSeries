#pragma once
// Adafruit_SSD1306 shim: an Adafruit_GFX whose drawPixel targets an in-RAM
// monochrome buffer. display() packs that buffer into the active HostBridge
// framebuffer (1bpp, row-major, MSB-first) for the VCV display widget to blit.
#include "Adafruit_GFX.h"
#include "Wire.h"

// Color constants (match Adafruit_SSD1306).
#define BLACK 0
#define WHITE 1
#define INVERSE 2
#define SSD1306_BLACK 0
#define SSD1306_WHITE 1
#define SSD1306_INVERSE 2
#define SSD1306_SWITCHCAPVCC 0x02
#define SSD1306_EXTERNALVCC 0x01

class Adafruit_SSD1306 : public Adafruit_GFX {
    uint8_t _px[64 * 128]; // 0/1 per pixel
  public:
    Adafruit_SSD1306(int16_t w, int16_t h, TwoWire * = nullptr, int8_t = -1)
        : Adafruit_GFX(w, h) { clearDisplay(); }

    bool begin(uint8_t = SSD1306_SWITCHCAPVCC, uint8_t = 0x3C, bool = true, bool = true) {
        clearDisplay();
        return true;
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || x >= _width || y < 0 || y >= _height) return;
        uint8_t &p = _px[y * _width + x];
        switch (color) {
            case WHITE: p = 1; break;
            case BLACK: p = 0; break;
            case INVERSE: p ^= 1; break;
            default: p = 1; break;
        }
    }

    void clearDisplay() {
        for (auto &p : _px) p = 0;
    }

    // Pack the pixel buffer into the host framebuffer (1bpp row-major, MSB first).
    void display() {
        if (!g_host) return;
        for (int i = 0; i < 1024; i++) g_host->fb[i] = 0;
        for (int y = 0; y < _height; y++)
            for (int x = 0; x < _width; x++)
                if (_px[y * _width + x])
                    g_host->fb[(y * _width + x) >> 3] |= (uint8_t)(0x80 >> (x & 7));
    }

    void invertDisplay(bool) {}
    void dim(bool) {}
    void ssd1306_command(uint8_t) {}
    uint8_t *getBuffer() { return _px; }
};
