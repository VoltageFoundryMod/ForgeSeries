#pragma once

// ── RP2040 / Seeed XIAO RP2040 ────────────────────────────────────────
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

// External clock trigger (IN1) — interrupt-driven only, not analog CV
#define CLK_IN_PIN A0 // GPIO26

// CV modulation inputs (IN2, IN3)
#define CV_1_IN_PIN A1 // GPIO27
#define CV_2_IN_PIN A2 // GPIO28

// Encoder — arduino-pico pin numbers = GPIO numbers (not silkscreen D numbers)
// XIAO RP2040: D8=GPIO2, D9=GPIO4, D10=GPIO3
#define ENC_PIN_1 4  // GPIO4 = D9
#define ENC_PIN_2 2  // GPIO2 = D8
#define ENCODER_SW 3 // GPIO3 = D10

// MCP4728 I2C address (all 4 outputs go through this DAC)
#define MCP4728_ADDR 0x60

// No GPIO output pins — all outputs via MCP4728
#define NUM_CV_INS 2 // Modulation CVs (IN1/CLK_IN_PIN is clock-trigger only)
#define NUM_OUTPUTS 4
#define NUM_DAC_OUTS 4 // All 4 outputs are DAC

int CV_IN_PINS[] = {CV_1_IN_PIN, CV_2_IN_PIN};
