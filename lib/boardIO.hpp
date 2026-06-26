#pragma once

// DAC resolution constants — shared across all platforms
#define DAC_RESOLUTION (12)
#define MAXDAC 4095 // This is the maximum value for a 12-bit DAC, which is 2^12 - 1 = 4095.
#define MAXADC 4095 // This is the maximum value for a 12-bit ADC, which is also 4095 which RP2040 supports.
// MCP4728 I2C address (all 4 outputs go through this DAC)
#define MCP4728_ADDR 0x60

#include <Adafruit_MCP4728.h>
#include <Arduino.h>
#include <Wire.h>

#include "calibrationData.hpp" // CalibrationData (full definition)
#include "pinouts.hpp"

// MCP4728 quad 12-bit I2C DAC (channels A=out1, B=out2, C=out3, D=out4)
Adafruit_MCP4728 dac4;

// Current per-channel shadow values for fast single-channel updates
static uint16_t _dacShadow[4] = {0, 0, 0, 0};

// Physical channel mapping: board wiring has DACB and DACC swapped relative
// to the expected output order (Out1=A, Out2=C, Out3=B, Out4=D).
static const MCP4728_channel_t _chanMap[4] = {
    MCP4728_CHANNEL_A, // Output 1 → DACA → Jack 1
    MCP4728_CHANNEL_C, // Output 2 → DACC → Jack 2  (B/C swapped in hardware)
    MCP4728_CHANNEL_B, // Output 3 → DACB → Jack 3  (B/C swapped in hardware)
    MCP4728_CHANNEL_D, // Output 4 → DACD → Jack 4
};

void InitIO() {
    analogReadResolution(12); // RP2040 supports 12-bit ADC

    pinMode(CLK_IN_PIN, INPUT_PULLDOWN); // CLK in — pull low so floating input
                                         // doesn't trigger spurious interrupts
    for (int i = 0; i < NUM_CV_INS; i++) {
        pinMode(CV_IN_PINS[i], INPUT);
    }
    pinMode(ENCODER_SW, INPUT_PULLUP);
    // No GPIO gate output pins — all outputs via MCP4728
}

// Initialize Wire (display) and Wire1 (DAC) I2C buses.
// Called from setup() BEFORE display.begin() and InitDAC().
// Wire  (GPIO6/7, I2C1) → SSD1306 display only.
// Wire1 (GPIO0/1, I2C0) → MCP4728 DAC only.
// Independent hardware blocks — can run simultaneously, no conflicts.
void InitWire() {
    Wire.setSDA(I2C_SDA_PIN);
    Wire.setSCL(I2C_SCL_PIN);
    Wire.begin();
    Wire.setClock(400000); // SSD1306 rated 400kHz — keep conservative

    Wire1.setSDA(I2C_DAC_SDA_PIN);
    Wire1.setSCL(I2C_DAC_SCL_PIN);
    Wire1.begin();
    Wire1.setClock(
        400000); // MCP4728 rated max=400kHz (Fm). 1MHz caused silent I2C
                 // data corruption: chip ACKs but misinterprets data due to
                 // tLOW timing violation (required 1300ns, 1MHz gives 500ns).
}

// Initialize the MCP4728 DAC. Returns false if not found.
// Called from setup() AFTER display.begin() so errors can be shown on screen.
bool InitDAC() {
    if (!dac4.begin(MCP4728_ADDR, &Wire1)) {
        Serial.println("MCP4728 not found! Check I2C wiring and address.");
        return false;
    }
    Serial.println("MCP4728 found. Configuring channels (VDD ref, Gain 1x)...");
    // setChannelValue with udac=false uses the Multi-Write command (UDAC=0).
    // This updates the OUTPUT register immediately -- no LDAC pulse needed.
    // fastWrite only updates the INPUT register; output changes only when LDAC
    // goes low. Since LDAC is unconnected on the GY-MCP4728 board, fastWrite
    // must NOT be used.
    bool ok = true;
    for (int i = 0; i < 4; i++) {
        ok &=
            dac4.setChannelValue(_chanMap[i], 0, MCP4728_VREF_VDD, MCP4728_GAIN_1X);
    }
    Serial.printf("MCP4728 init: %s\n", ok ? "OK" : "FAILED");
    return ok;
}

// Calibration helpers ────────────────────────────────────────────────
extern CalibrationData cal;

// Write all 4 DAC channels.
// Uses MCP4728 Multi-Write command in a single I2C transaction (one
// START/STOP), which is ~3x faster than four separate setChannelValue() calls.
// Hardware channel mapping: DACA=sw0, DACB=sw2, DACC=sw1, DACD=sw3 (B/C wired
// swapped). Each 3-byte block: [CMD: 0x40|(hwCh<<2)]
// [VREF=0,PD=00,GAIN=0,D11:8] [D7:0]
void DACWriteAll(uint16_t ch0, uint16_t ch1, uint16_t ch2, uint16_t ch3) {
    _dacShadow[0] = ch0;
    _dacShadow[1] = ch1;
    _dacShadow[2] = ch2;
    _dacShadow[3] = ch3;
    // hw order A,B,C,D maps to sw[0,2,1,3]
    const uint16_t hwVals[4] = {ch0, ch2, ch1, ch3};
    Wire1.beginTransmission(MCP4728_ADDR);
    for (int i = 0; i < 4; i++) {
        Wire1.write(
            0x40 |
            (i << 1)); // Multi-Write cmd: cmd=010, channel=i (bits [2:1]), UDAC=0
        Wire1.write((hwVals[i] >> 8) &
                    0x0F);             // VREF=VDD, PD=normal, GAIN=1x, D[11:8]
        Wire1.write(hwVals[i] & 0xFF); // D[7:0]
    }
    uint8_t result = Wire1.endTransmission();
    (void)result;
}

// Write a single DAC channel; keeps other channels at their last value.
void DACWrite(int channel, uint32_t value) {
    if (channel < 0 || channel > 3)
        return;
    _dacShadow[channel] = (uint16_t)value;
    dac4.setChannelValue(_chanMap[channel], _dacShadow[channel], MCP4728_VREF_VDD,
                         MCP4728_GAIN_1X);
}
