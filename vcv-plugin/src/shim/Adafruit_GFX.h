#pragma once
// Self-contained Adafruit_GFX shim. Primitive algorithms are copied verbatim
// from Adafruit_GFX (BSD) so rendering is pixel-identical to the hardware OLED;
// only the drawPixel backend differs (it targets a host framebuffer instead of
// an SSD1306 over I2C). Uses the genuine 5x7 glcd font.
#include "Arduino.h"
#include "glcdfont.h"
#include <cstdarg>

#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#ifndef _swap_int16_t
#define _swap_int16_t(a, b) { int16_t t = a; a = b; b = t; }
#endif

class Adafruit_GFX {
  protected:
    int16_t WIDTH, HEIGHT, _width, _height;
    int16_t cursor_x = 0, cursor_y = 0;
    uint16_t textcolor = 1, textbgcolor = 1;
    uint8_t textsize_x = 1, textsize_y = 1;
    bool wrap = true;
    bool _cp437 = false;

  public:
    Adafruit_GFX(int16_t w, int16_t h) : WIDTH(w), HEIGHT(h), _width(w), _height(h) {}
    virtual ~Adafruit_GFX() {}

    // Backend: implemented by the SSD1306 subclass.
    virtual void drawPixel(int16_t x, int16_t y, uint16_t color) = 0;

    void startWrite() {}
    void endWrite() {}
    void writePixel(int16_t x, int16_t y, uint16_t color) { drawPixel(x, y, color); }

    void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
        for (int16_t i = 0; i < h; i++) drawPixel(x, y + i, color);
    }
    void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
        for (int16_t i = 0; i < w; i++) drawPixel(x + i, y, color);
    }
    void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        for (int16_t j = 0; j < h; j++) writeFastHLine(x, y + j, w, color);
    }
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) { writeFastVLine(x, y, h, color); }
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) { writeFastHLine(x, y, w, color); }
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) { writeFillRect(x, y, w, h, color); }
    void fillScreen(uint16_t color) { fillRect(0, 0, _width, _height, color); }

    void writeLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
        int16_t steep = abs(y1 - y0) > abs(x1 - x0);
        if (steep) { _swap_int16_t(x0, y0); _swap_int16_t(x1, y1); }
        if (x0 > x1) { _swap_int16_t(x0, x1); _swap_int16_t(y0, y1); }
        int16_t dx = x1 - x0, dy = abs(y1 - y0);
        int16_t err = dx / 2, ystep = (y0 < y1) ? 1 : -1;
        for (; x0 <= x1; x0++) {
            if (steep) writePixel(y0, x0, color); else writePixel(x0, y0, color);
            err -= dy;
            if (err < 0) { y0 += ystep; err += dx; }
        }
    }
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
        if (x0 == x1) { if (y0 > y1) _swap_int16_t(y0, y1); writeFastVLine(x0, y0, y1 - y0 + 1, color); }
        else if (y0 == y1) { if (x0 > x1) _swap_int16_t(x0, x1); writeFastHLine(x0, y0, x1 - x0 + 1, color); }
        else writeLine(x0, y0, x1, y1, color);
    }

    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        writeFastHLine(x, y, w, color);
        writeFastHLine(x, y + h - 1, w, color);
        writeFastVLine(x, y, h, color);
        writeFastVLine(x + w - 1, y, h, color);
    }

    void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
        int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r;
        writePixel(x0, y0 + r, color); writePixel(x0, y0 - r, color);
        writePixel(x0 + r, y0, color); writePixel(x0 - r, y0, color);
        while (x < y) {
            if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
            x++; ddF_x += 2; f += ddF_x;
            writePixel(x0 + x, y0 + y, color); writePixel(x0 - x, y0 + y, color);
            writePixel(x0 + x, y0 - y, color); writePixel(x0 - x, y0 - y, color);
            writePixel(x0 + y, y0 + x, color); writePixel(x0 - y, y0 + x, color);
            writePixel(x0 + y, y0 - x, color); writePixel(x0 - y, y0 - x, color);
        }
    }
    void drawCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t cornername, uint16_t color) {
        int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r;
        while (x < y) {
            if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
            x++; ddF_x += 2; f += ddF_x;
            if (cornername & 0x4) { writePixel(x0 + x, y0 + y, color); writePixel(x0 + y, y0 + x, color); }
            if (cornername & 0x2) { writePixel(x0 + x, y0 - y, color); writePixel(x0 + y, y0 - x, color); }
            if (cornername & 0x8) { writePixel(x0 - y, y0 + x, color); writePixel(x0 - x, y0 + y, color); }
            if (cornername & 0x1) { writePixel(x0 - y, y0 - x, color); writePixel(x0 - x, y0 - y, color); }
        }
    }
    void fillCircleHelper(int16_t x0, int16_t y0, int16_t r, uint8_t corners, int16_t delta, uint16_t color) {
        int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r, px = x, py = y;
        delta++;
        while (x < y) {
            if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
            x++; ddF_x += 2; f += ddF_x;
            if (x < (y + 1)) {
                if (corners & 1) writeFastVLine(x0 + x, y0 - y, 2 * y + delta, color);
                if (corners & 2) writeFastVLine(x0 - x, y0 - y, 2 * y + delta, color);
            }
            if (y != py) {
                if (corners & 1) writeFastVLine(x0 + py, y0 - px, 2 * px + delta, color);
                if (corners & 2) writeFastVLine(x0 - py, y0 - px, 2 * px + delta, color);
                py = y;
            }
            px = x;
        }
    }
    void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
        writeFastVLine(x0, y0 - r, 2 * r + 1, color);
        fillCircleHelper(x0, y0, r, 3, 0, color);
    }

    void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
        int16_t max_radius = ((w < h) ? w : h) / 2;
        if (r > max_radius) r = max_radius;
        writeFastHLine(x + r, y, w - 2 * r, color);
        writeFastHLine(x + r, y + h - 1, w - 2 * r, color);
        writeFastVLine(x, y + r, h - 2 * r, color);
        writeFastVLine(x + w - 1, y + r, h - 2 * r, color);
        drawCircleHelper(x + r, y + r, r, 1, color);
        drawCircleHelper(x + w - r - 1, y + r, r, 2, color);
        drawCircleHelper(x + w - r - 1, y + h - r - 1, r, 4, color);
        drawCircleHelper(x + r, y + h - r - 1, r, 8, color);
    }
    void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
        int16_t max_radius = ((w < h) ? w : h) / 2;
        if (r > max_radius) r = max_radius;
        writeFillRect(x + r, y, w - 2 * r, h, color);
        fillCircleHelper(x + w - r - 1, y + r, r, 1, h - 2 * r - 1, color);
        fillCircleHelper(x + r, y + r, r, 2, h - 2 * r - 1, color);
    }

    void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
        drawLine(x0, y0, x1, y1, color);
        drawLine(x1, y1, x2, y2, color);
        drawLine(x2, y2, x0, y0, color);
    }
    void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
        int16_t a, b, y, last;
        if (y0 > y1) { _swap_int16_t(y0, y1); _swap_int16_t(x0, x1); }
        if (y1 > y2) { _swap_int16_t(y2, y1); _swap_int16_t(x2, x1); }
        if (y0 > y1) { _swap_int16_t(y0, y1); _swap_int16_t(x0, x1); }
        if (y0 == y2) {
            a = b = x0;
            if (x1 < a) a = x1; else if (x1 > b) b = x1;
            if (x2 < a) a = x2; else if (x2 > b) b = x2;
            writeFastHLine(a, y0, b - a + 1, color);
            return;
        }
        int16_t dx01 = x1 - x0, dy01 = y1 - y0, dx02 = x2 - x0, dy02 = y2 - y0,
                dx12 = x2 - x1, dy12 = y2 - y1;
        int32_t sa = 0, sb = 0;
        last = (y1 == y2) ? y1 : y1 - 1;
        for (y = y0; y <= last; y++) {
            a = x0 + sa / dy01; b = x0 + sb / dy02; sa += dx01; sb += dx02;
            if (a > b) _swap_int16_t(a, b);
            writeFastHLine(a, y, b - a + 1, color);
        }
        sa = (int32_t)dx12 * (y - y1); sb = (int32_t)dx02 * (y - y0);
        for (; y <= y2; y++) {
            a = x1 + sa / dy12; b = x0 + sb / dy02; sa += dx12; sb += dx02;
            if (a > b) _swap_int16_t(a, b);
            writeFastHLine(a, y, b - a + 1, color);
        }
    }

    void drawBitmap(int16_t x, int16_t y, const uint8_t bitmap[], int16_t w, int16_t h, uint16_t color) {
        int16_t byteWidth = (w + 7) / 8;
        uint8_t b = 0;
        for (int16_t j = 0; j < h; j++, y++)
            for (int16_t i = 0; i < w; i++) {
                if (i & 7) b <<= 1; else b = pgm_read_byte(&bitmap[j * byteWidth + i / 8]);
                if (b & 0x80) writePixel(x + i, y, color);
            }
    }

    // ── Text ─────────────────────────────────────────────────────────────────
    void drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color, uint16_t bg, uint8_t size_x, uint8_t size_y) {
        if ((x >= _width) || (y >= _height) || ((x + 6 * size_x - 1) < 0) || ((y + 8 * size_y - 1) < 0)) return;
        if (!_cp437 && (c >= 176)) c++;
        for (int8_t i = 0; i < 5; i++) {
            uint8_t line = pgm_read_byte(&font[c * 5 + i]);
            for (int8_t j = 0; j < 8; j++, line >>= 1) {
                if (line & 1) {
                    if (size_x == 1 && size_y == 1) writePixel(x + i, y + j, color);
                    else writeFillRect(x + i * size_x, y + j * size_y, size_x, size_y, color);
                } else if (bg != color) {
                    if (size_x == 1 && size_y == 1) writePixel(x + i, y + j, bg);
                    else writeFillRect(x + i * size_x, y + j * size_y, size_x, size_y, bg);
                }
            }
        }
        if (bg != color) {
            if (size_x == 1 && size_y == 1) writeFastVLine(x + 5, y, 8, bg);
            else writeFillRect(x + 5 * size_x, y, size_x, 8 * size_y, bg);
        }
    }
    void drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color, uint16_t bg, uint8_t size) {
        drawChar(x, y, c, color, bg, size, size);
    }

    size_t write(uint8_t c) {
        if (c == '\n') { cursor_x = 0; cursor_y += textsize_y * 8; }
        else if (c != '\r') {
            if (wrap && ((cursor_x + textsize_x * 6) > _width)) { cursor_x = 0; cursor_y += textsize_y * 8; }
            drawChar(cursor_x, cursor_y, c, textcolor, textbgcolor, textsize_x, textsize_y);
            cursor_x += textsize_x * 6;
        }
        return 1;
    }

    void setCursor(int16_t x, int16_t y) { cursor_x = x; cursor_y = y; }
    int16_t getCursorX() const { return cursor_x; }
    int16_t getCursorY() const { return cursor_y; }
    void setTextSize(uint8_t s) { setTextSize(s, s); }
    void setTextSize(uint8_t sx, uint8_t sy) { textsize_x = sx ? sx : 1; textsize_y = sy ? sy : 1; }
    void setTextColor(uint16_t c) { textcolor = textbgcolor = c; }
    void setTextColor(uint16_t c, uint16_t bg) { textcolor = c; textbgcolor = bg; }
    void setTextWrap(bool w) { wrap = w; }
    void cp437(bool x = true) { _cp437 = x; }
    int16_t width() const { return _width; }
    int16_t height() const { return _height; }

    // ── Print-style helpers ───────────────────────────────────────────────────
    void print(const char *s) { if (s) while (*s) write((uint8_t)*s++); }
    void print(const String &s) { print(s.c_str()); }
    void print(char c) { write((uint8_t)c); }
    void print(int v, int base = 10) { print(String(v, base)); }
    void print(unsigned int v, int base = 10) { print(String(v, base)); }
    void print(long v, int base = 10) { print(String(v, base)); }
    void print(unsigned long v, int base = 10) { print(String(v, base)); }
    void print(double v, int digits = 2) { print(String(v, digits)); }

    void println() { write((uint8_t)'\n'); }
    void println(const char *s) { print(s); println(); }
    void println(const String &s) { print(s); println(); }
    void println(char c) { print(c); println(); }
    void println(int v, int base = 10) { print(v, base); println(); }
    void println(unsigned int v, int base = 10) { print(v, base); println(); }
    void println(long v, int base = 10) { print(v, base); println(); }
    void println(unsigned long v, int base = 10) { print(v, base); println(); }
    void println(double v, int digits = 2) { print(v, digits); println(); }

    void printf(const char *fmt, ...) {
        char buf[160];
        va_list ap; va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        print(buf);
    }
};
