#pragma once

// boardPinouts.hpp — the ForgeSeries hardware, and nothing else.
//
// Every module in the series is the same board: a Seeed XIAO RP2040, an SSD1306
// OLED, an MCP4728 quad DAC, one rotary encoder with a push switch, three input
// jacks and four output jacks. That wiring is fixed and identical across every
// firmware, so it lives here exactly once.
//

// Needed for A0/A1/A2 below. This used to be left to the caller, which happened
// to work only because main.cpp included <Arduino.h> first; including a pinout
// header before Arduino.h was a compile error waiting to happen.
#include <Arduino.h>

// ── I2C — display bus (SSD1306 OLED) on Wire ─────────────────────────────────
// NOTE: Wire.setSDA/SCL on RP2040 takes GPIO numbers, not Arduino pin numbers.
// XIAO RP2040 physical pad D4 = GPIO6 (I2C1 SDA), D5 = GPIO7 (I2C1 SCL).
#define I2C_SDA_PIN 6 // GPIO6 = D4
#define I2C_SCL_PIN 7 // GPIO7 = D5

// ── I2C — DAC bus (MCP4728) on Wire1 ─────────────────────────────────────────
// XIAO RP2040 physical pad D6 = GPIO0 (I2C0 SDA), D7 = GPIO1 (I2C0 SCL).
// A separate bus from the display so DAC writes and display.display() never
// contend; combined with the core split (Core 0 = DAC, Core 1 = GFX) that
// removes the need for a mutex on either bus.
#define I2C_DAC_SDA_PIN 0 // GPIO0 = D6
#define I2C_DAC_SCL_PIN 1 // GPIO1 = D7

// ── IN 1 — gate/trigger input ────────────────────────────────────────────────
// Interrupt-driven only, never read as analog CV. Its meaning (clock / reset /
// kick / ...) is menu-selectable per app.
#define CLK_IN_PIN A0 // GPIO26

// ── IN 2 / IN 3 — analog CV inputs ───────────────────────────────────────────
// CV RANGE: the current hardware revision accepts 0-5 V only. A later revision
// moves to +/-5 V. Nothing downstream of each app's CV normalisation assumes a
// polarity, so that swap stays a one-function change — do not sprinkle range
// assumptions through modulation targets.
#define CV_1_IN_PIN A1 // GPIO27 — IN 2
#define CV_2_IN_PIN A2 // GPIO28 — IN 3

// ── Encoder ──────────────────────────────────────────────────────────────────
// arduino-pico pin numbers are GPIO numbers, not the silkscreen D numbers.
// XIAO RP2040: D8=GPIO2, D9=GPIO4, D10=GPIO3.
#define ENC_PIN_1 4  // GPIO4 = D9
#define ENC_PIN_2 2  // GPIO2 = D8
#define ENCODER_SW 3 // GPIO3 = D10

// ── Counts ───────────────────────────────────────────────────────────────────
// No GPIO output pins — all four outputs go through the MCP4728.
#define NUM_CV_INS 2   // analog CV inputs (IN 1 is trigger-only)
#define NUM_OUTPUTS 4  // output jacks
#define NUM_DAC_OUTS 4 // ...all of which are DAC channels

// ── Converter resolution ─────────────────────────────────────────────────────
// The MCP4728 is 12-bit and so is the RP2040's ADC. These are board facts, so
// they live here rather than in boardIO.hpp: scaling a CV reading should not
// require pulling in the DAC driver.
#define DAC_RESOLUTION (12)
#define MAXDAC 4095 // Maximum value for a 12-bit DAC: 2^12 - 1
#define MAXADC 4095 // Maximum value for the RP2040's 12-bit ADC

// `inline` matters: this is a definition in a header. Without it, every TU that
// includes the header emits the symbol and the unified firmware (shell + one TU
// per app) fails to link. `inline` rather than `static` so all TUs share ONE
// array instead of each carrying a private copy.
inline int CV_IN_PINS[] = {CV_1_IN_PIN, CV_2_IN_PIN};
