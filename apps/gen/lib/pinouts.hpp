#pragma once

// ── RP2040 / Seeed XIAO RP2040 ────────────────────────────────────────
// Shared Forge Series hardware platform — identical wiring to ClockForge and
// NoteForge. Only the jack *meanings* differ between modules.
//
// I2C — display bus (SSD1306 OLED) on Wire
// NOTE: Wire.setSDA/SCL on RP2040 takes GPIO numbers, not Arduino pin numbers.
// XIAO RP2040 physical pad D4 = GPIO6 (I2C1 SDA), D5 = GPIO7 (I2C1 SCL).
#define I2C_SDA_PIN 6 // GPIO6 = D4
#define I2C_SCL_PIN 7 // GPIO7 = D5

// I2C — DAC bus (MCP4728) on Wire1
// XIAO RP2040 physical pad D6 = GPIO0 (I2C0 SDA), D7 = GPIO1 (I2C0 SCL).
// Separate bus from display so DACWriteAll and display.display() never conflict.
#define I2C_DAC_SDA_PIN 0 // GPIO0 = D6
#define I2C_DAC_SCL_PIN 1 // GPIO1 = D7

// IN 1 — gate/trigger input, interrupt-driven only, not read as analog CV.
// Its job is menu-selectable (clock / reset / kick / spawn) — see cvInputs.hpp.
#define CLK_IN_PIN A0 // GPIO26

// IN 2 / IN 3 — assignable modulation CV.
//
// CV RANGE: the current hardware revision accepts 0–5 V only. A later revision
// moves to ±5 V. Nothing downstream of CvNorm() in cvInputs.hpp assumes a
// polarity, so that swap is a one-function change — do not sprinkle
// range assumptions through the modulation targets.
#define CV_1_IN_PIN A1 // GPIO27 — IN 2
#define CV_2_IN_PIN A2 // GPIO28 — IN 3

// Encoder — arduino-pico pin numbers = GPIO numbers (not silkscreen D numbers)
// XIAO RP2040: D8=GPIO2, D9=GPIO4, D10=GPIO3
#define ENC_PIN_1 4  // GPIO4 = D9
#define ENC_PIN_2 2  // GPIO2 = D8
#define ENCODER_SW 3 // GPIO3 = D10

// No GPIO output pins — all outputs via MCP4728
#define NUM_CV_INS 2   // Modulation CV inputs (IN1/CLK_IN_PIN is trigger only)
#define NUM_OUTPUTS 4  // All 4 outputs are DAC
#define NUM_CHANNELS 2 // Two containers, each a complete voice

// Output jack assignment (index into the DAC/output arrays).
// Deliberately identical to NoteForge: Jack 1/2 = pitch, Jack 3/4 = gate.
#define OUT_CV(ch) (ch)                  // ch 0 -> jack 1, ch 1 -> jack 2
#define OUT_GATE(ch) (NUM_CHANNELS + ch) // ch 0 -> jack 3, ch 1 -> jack 4

static int CV_IN_PINS[] = {CV_1_IN_PIN, CV_2_IN_PIN};
