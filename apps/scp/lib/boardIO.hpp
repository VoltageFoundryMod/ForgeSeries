#pragma once

// ── ForgeView (Scope) board IO — Seeed XIAO RP2040 ───────────────────────────
// Mirrors the shared Forge Series hardware used by ClockForge: dual I2C
// (SSD1306 on Wire, MCP4728 quad DAC on Wire1) plus the 12-bit ADC inputs.
// The scope reads CV1/CV2 (+ the CLK trigger) and buffers the two CV inputs
// straight to DAC outputs 1/2 (oscilloscope through), leaving 3/4 idle.

// DAC/ADC resolution constants — shared across platforms
#define DAC_RESOLUTION (12)
#define MAXDAC 4095 // 12-bit DAC full scale (2^12 - 1)
#define MAXADC 4095 // RP2040 12-bit ADC full scale
// MCP4728 I2C address (all 4 outputs go through this DAC)
#define MCP4728_ADDR 0x60

#include <Adafruit_MCP4728.h>
#include <Arduino.h>
#include <Wire.h>

#include "pinouts.hpp"

// MCP4728 quad 12-bit I2C DAC (channels A=out1, B=out2, C=out3, D=out4)
Adafruit_MCP4728 dac4;

// Current per-channel shadow values
static uint16_t _dacShadow[4] = {0, 0, 0, 0};

// Physical channel mapping: board wiring has DACB and DACC swapped relative to
// the expected output order (Out1=A, Out2=C, Out3=B, Out4=D). Identical to the
// ClockForge board.
static const MCP4728_channel_t _chanMap[4] = {
    MCP4728_CHANNEL_A, // Output 1 -> DACA -> Jack 1
    MCP4728_CHANNEL_C, // Output 2 -> DACC -> Jack 2 (B/C swapped in hardware)
    MCP4728_CHANNEL_B, // Output 3 -> DACB -> Jack 3 (B/C swapped in hardware)
    MCP4728_CHANNEL_D, // Output 4 -> DACD -> Jack 4
};

void InitIO() {
    analogReadResolution(12); // RP2040 supports 12-bit ADC

    pinMode(CLK_IN_PIN, INPUT_PULLDOWN); // CLK/trigger in — pulled low so a
                                         // floating input doesn't self-trigger
    for (int i = 0; i < NUM_CV_INS; i++) {
        pinMode(CV_IN_PINS[i], INPUT);
    }
    pinMode(ENCODER_SW, INPUT_PULLUP);
    // No GPIO gate output pins — all outputs via MCP4728
}

// Initialize Wire (display) and Wire1 (DAC) I2C buses.
// Wire  (GPIO6/7, I2C1) -> SSD1306 display only.
// Wire1 (GPIO0/1, I2C0) -> MCP4728 DAC only.
void InitWire() {
    Wire.setSDA(I2C_SDA_PIN);
    Wire.setSCL(I2C_SCL_PIN);
    Wire.begin();
    Wire.setClock(1000000); // SSD1306 — fast bus for high scope refresh rate

    Wire1.setSDA(I2C_DAC_SDA_PIN);
    Wire1.setSCL(I2C_DAC_SCL_PIN);
    Wire1.begin();
    Wire1.setClock(400000); // MCP4728 rated max 400kHz (Fm)
}

// Initialize the MCP4728 DAC. Returns false if not found.
bool InitDAC() {
    if (!dac4.begin(MCP4728_ADDR, &Wire1)) {
        Serial.println("MCP4728 not found! Check I2C wiring and address.");
        return false;
    }
    bool ok = true;
    for (int i = 0; i < 4; i++) {
        ok &= dac4.setChannelValue(_chanMap[i], 0, MCP4728_VREF_VDD, MCP4728_GAIN_1X);
    }
    Serial.printf("MCP4728 init: %s\n", ok ? "OK" : "FAILED");
    return ok;
}

// Write all 4 DAC channels in a single MCP4728 Multi-Write I2C transaction.
// Hardware channel mapping: DACA=sw0, DACB=sw2, DACC=sw1, DACD=sw3 (B/C swapped).
void DACWriteAll(uint16_t ch0, uint16_t ch1, uint16_t ch2, uint16_t ch3) {
    _dacShadow[0] = ch0;
    _dacShadow[1] = ch1;
    _dacShadow[2] = ch2;
    _dacShadow[3] = ch3;
    const uint16_t hwVals[4] = {ch0, ch2, ch1, ch3}; // hw A,B,C,D <- sw 0,2,1,3
    Wire1.beginTransmission(MCP4728_ADDR);
    for (int i = 0; i < 4; i++) {
        Wire1.write(0x40 | (i << 1));          // Multi-Write cmd, channel i, UDAC=0
        Wire1.write((hwVals[i] >> 8) & 0x0F);  // VREF=VDD, PD=normal, GAIN=1x, D[11:8]
        Wire1.write(hwVals[i] & 0xFF);         // D[7:0]
    }
    Wire1.endTransmission();
}
