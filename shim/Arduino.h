#pragma once
// Arduino core shim for compiling the ClockForge firmware inside VCV Rack.
// Provides the subset of the Arduino API that lib/ touches, and a HostBridge
// that connects firmware I/O (digitalRead/analogRead, DAC writes, framebuffer)
// to the hosting VCV module. See engine/ for how this is driven.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "WString.h"

// ── Types ────────────────────────────────────────────────────────────────────
typedef uint8_t byte;
typedef bool boolean;

// ── Digital/analog constants ─────────────────────────────────────────────────
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3
#define RISING 1
#define FALLING 2
#define CHANGE 3
#define LED_BUILTIN 13

// Analog pin aliases (XIAO RP2040 GPIO numbers used by pinouts.hpp).
#define A0 26
#define A1 27
#define A2 28
#define A3 29

// Arduino math constants
#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#define EULER 2.718281828459045235360287471352

// Flash-string helpers are no-ops on a hosted build.
#define PROGMEM
#define F(x) (x)
#define PSTR(x) (x)
typedef const char __FlashStringHelper;

// ── Host bridge ──────────────────────────────────────────────────────────────
// One active bridge at a time. The engine wrapper swaps this per module instance
// (context-swap) so each instance sees its own I/O and framebuffer.
struct HostBridge {
    // Firmware inputs (written by the host module before each engine step):
    uint16_t adc[40] = {0};  // analogRead(pin) -> 0..4095, indexed by pin number
    uint8_t gpio[40];        // digitalRead(pin), indexed by pin number

    // Firmware outputs (read by the host after each engine step):
    uint16_t dac[4] = {0, 0, 0, 0}; // captured by the MCP4728 / DACWriteAll shim
    uint8_t fb[1024];               // 128x64 monochrome framebuffer (SSD1306 layout)

    HostBridge() {
        for (auto &g : gpio) g = HIGH; // pull-ups idle high
        for (auto &f : fb) f = 0;
    }
};

extern HostBridge *g_host;

// ── Timing (real wall-clock; the firmware loop runs in real time in Rack) ─────
unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned long us);

// ── GPIO / ADC ───────────────────────────────────────────────────────────────
inline void pinMode(int, int) {}
inline int digitalRead(int pin) {
    return (pin >= 0 && pin < 40 && g_host) ? g_host->gpio[pin] : HIGH;
}
inline void digitalWrite(int, int) {}
inline int analogRead(int pin) {
    return (pin >= 0 && pin < 40 && g_host) ? g_host->adc[pin] : 0;
}
inline void analogReadResolution(int) {}
inline int digitalPinToInterrupt(int pin) { return pin; }
void attachInterrupt(int interrupt, void (*isr)(), int mode);

// Interrupt enable/disable are no-ops in the single-threaded hosted engine.
inline void noInterrupts() {}
inline void interrupts() {}

// ── Math helpers (Arduino macros / functions) ────────────────────────────────
#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif
inline long map(long x, long inMin, long inMax, long outMin, long outMax) {
    if (inMax == inMin) return outMin;
    return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}
using std::min;
using std::max;
using std::abs;

// ── Random ───────────────────────────────────────────────────────────────────
inline long random(long howbig) { return howbig > 0 ? (long)(::rand() % howbig) : 0; }
inline long random(long howsmall, long howbig) {
    return howbig > howsmall ? howsmall + (long)(::rand() % (howbig - howsmall)) : howsmall;
}
inline void randomSeed(unsigned long seed) { ::srand((unsigned)seed); }

// ── Serial (routed to stdout; cheap and harmless inside Rack) ────────────────
struct SerialShim {
    void begin(unsigned long) {}
    operator bool() const { return true; }
    template <typename T> void print(const T &) {}
    template <typename T> void print(const T &, int) {}
    void print(const String &) {}
    void print(const char *) {}
    template <typename T> void println(const T &) {}
    template <typename T> void println(const T &, int) {}
    void println(const String &) {}
    void println(const char *) {}
    void println() {}
    template <typename... A> void printf(const char *, A...) {}
    void write(uint8_t) {}
    void flush() {}
};
extern SerialShim Serial;
