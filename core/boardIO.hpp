#pragma once

// Converter resolutions (DAC_RESOLUTION / MAXDAC / MAXADC) are board facts and
// live in boardPinouts.hpp, included below, so code that only
// needs to scale a reading does not have to include the whole DAC driver.

// MCP4728 I2C address — the base board's DAC, outputs 1-4.
#define MCP4728_ADDR 0x60

// The expander's MCP4728, outputs 5-8. Same part, same bus (Wire1), so its
// address has to be moved off the 0x60 default before it can be fitted. The
// MCP4728 stores its address in EEPROM and changing it needs LDAC toggled
// during the write, which no pin here can do — it is done out-of-circuit
// through the expander's program header.
#define MCP4728_EXP_ADDR 0x61

#include <Adafruit_MCP4728.h>
#include <Arduino.h>
#include <Wire.h>
#include <math.h> // lroundf (output calibration remap)

#include "calibrationData.hpp" // CalibrationData (full definition)
// boardPinouts.hpp, not the app-level pinouts.hpp: this layer needs the board
// wiring only, never an app's jack semantics. Depending on the app header
// would make core/ unbuildable without one.
#include "boardPinouts.hpp"

// MCP4728 quad 12-bit I2C DAC (channels A=out1, B=out2, C=out3, D=out4)
inline Adafruit_MCP4728 dac4;

// Current per-channel shadow values for fast single-channel updates.
// Indices 4-7 are the expander's, written only when one is fitted.
inline uint16_t _dacShadow[NUM_MAX_OUTPUTS] = {0, 0, 0, 0, 0, 0, 0, 0};

// Set once at boot by ProbeExpander(). Nothing writes the expander DAC unless
// this is true, so a module with no expander pays one I2C probe and nothing
// more.
inline bool expanderDacPresent = false;

// Physical channel mapping: DAC channel A-D drive outputs 1-4 in order.
//
// This used to swap B and C, compensating for the GY-MCP4728 breakout's wiring
// on the older board. The board no longer uses that breakout — the DAC is on
// the PCB with its outputs wired in order, and the expander is wired the same
// way — so the mapping is the identity on both devices and the swap is gone.
inline const MCP4728_channel_t _chanMap[4] = {
    MCP4728_CHANNEL_A, // Output 1 → DACA → Jack 1 (CV 1)
    MCP4728_CHANNEL_B, // Output 2 → DACB → Jack 2 (CV 2)
    MCP4728_CHANNEL_C, // Output 3 → DACC → Jack 3 (GATE 1)
    MCP4728_CHANNEL_D, // Output 4 → DACD → Jack 4 (GATE 2)
};

inline void InitIO() {
    analogReadResolution(12); // RP2040 supports 12-bit ADC

    pinMode(CLK_IN_PIN, INPUT_PULLDOWN); // TRIG in — pull low so a floating
                                         // input doesn't fire spurious triggers
    for (int i = 0; i < NUM_MAX_CV_INS; i++) {
        pinMode(CV_IN_PINS[i], INPUT);
    }
    pinMode(ENCODER_SW, INPUT_PULLUP);
    // No GPIO gate output pins — all outputs via MCP4728
}

// Display bus speed, platform-wide.
//
// The SSD1306 datasheet rates the bus at 400 kHz, but the panel on this board
// runs reliably at 1 MHz — ForgeView has shipped that way to hit its scope
// refresh rate. Since a display.display() flush blocks the calling core for the
// whole transfer, there is no upside to clocking it slower on the other
// modules: 1 MHz simply makes every UI more responsive.
//
// This applies to Wire ONLY. Do NOT raise Wire1 to match — see InitWire().
//
// Left overridable so an app can drop back if a future panel revision needs it.
#ifndef FORGE_DISPLAY_I2C_HZ
#define FORGE_DISPLAY_I2C_HZ 1000000
#endif

// Initialize Wire (display) and Wire1 (DAC) I2C buses.
// Called from setup() BEFORE display.begin() and InitDAC().
// Wire  (GPIO6/7, I2C1) → SSD1306 display only.
// Wire1 (GPIO0/1, I2C0) → MCP4728 DAC only.
// Independent hardware blocks — can run simultaneously, no conflicts.
// Free a slave left mid-byte by the previous run.
//
// A reset can land in the middle of a display transaction — the reset button, or
// the reboot that returns to the module selector. Neither the SSD1306 nor the
// MCP4728 has a reset line on this board, so the slave survives with its state
// intact: if it was in the middle of a byte it keeps SDA pulled low, waiting for
// the clocks that would finish it. The RP2040 then cannot issue a START, so
// display.begin() gets nowhere and the panel sits on its old frame until power
// is removed — pressing reset again does not help, which is the tell.
//
// Up to nine bit-banged clocks let the slave finish that byte and release SDA,
// and a STOP then leaves the bus idle. Must run BEFORE Wire.begin() takes the
// pins. Costs nothing when the bus is already free, which is the normal case.
//
// Pins are driven open-drain, as I2C requires: LOW is actively driven, HIGH is
// released to the pull-up. Never drive SCL or SDA high.
inline void RecoverI2CBus(uint8_t sda, uint8_t scl) {
    pinMode(sda, INPUT_PULLUP);
    pinMode(scl, INPUT_PULLUP);
    delayMicroseconds(5);

    if (digitalRead(sda) == HIGH)
        return; // bus idle — nothing to recover

    for (int i = 0; i < 9 && digitalRead(sda) == LOW; i++) {
        pinMode(scl, OUTPUT);
        digitalWrite(scl, LOW);
        delayMicroseconds(5);
        pinMode(scl, INPUT_PULLUP); // release; the pull-up raises it
        delayMicroseconds(5);
    }

    // STOP: SDA rising while SCL is high.
    pinMode(sda, OUTPUT);
    digitalWrite(sda, LOW);
    delayMicroseconds(5);
    pinMode(scl, INPUT_PULLUP);
    delayMicroseconds(5);
    pinMode(sda, INPUT_PULLUP); // the rise
    delayMicroseconds(5);
}

inline void InitWire() {
    // Before either bus is claimed: a previous run may have been reset mid-byte.
    RecoverI2CBus(I2C_SDA_PIN, I2C_SCL_PIN);
    RecoverI2CBus(I2C_DAC_SDA_PIN, I2C_DAC_SCL_PIN);

    Wire.setSDA(I2C_SDA_PIN);
    Wire.setSCL(I2C_SCL_PIN);
    Wire.begin();
    Wire.setClock(FORGE_DISPLAY_I2C_HZ);

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
inline bool InitDAC() {
    if (!dac4.begin(MCP4728_ADDR, &Wire1)) {
        Serial.println("MCP4728 not found! Check I2C wiring and address.");
        return false;
    }
    Serial.println("MCP4728 found. Configuring channels (VDD ref, Gain 1x)...");
    // Multi-Write (UDAC=0), and it is used here for one reason: it is the only
    // frame that carries the VREF and gain bits. The hot path is Fast Write,
    // which does not, and relies on the device retaining what this sets.
    // Changing the reference or gain means changing it here.
    bool ok = true;
    for (int i = 0; i < 4; i++) {
        ok &=
            dac4.setChannelValue(_chanMap[i], 0, MCP4728_VREF_VDD, MCP4728_GAIN_1X);
    }
    Serial.printf("MCP4728 init: %s\n", ok ? "OK" : "FAILED");
    return ok;
}

// ── Expander DAC (outputs 5-8) ───────────────────────────────────────────────
//
// Is an expander on the bus? A bare address probe: START, address, look for
// the ACK. Cheap, and the only way to tell — the expander has no ID register
// and nothing else on the header reports back.
//
// Hardware only. Under VCV Rack the Wire shim ACKs every address, so this
// answers true in every patch; the Rack port decides an expander is present
// from module adjacency instead and never calls this.
inline bool ProbeExpander() {
    Wire1.beginTransmission(MCP4728_EXP_ADDR);
    expanderDacPresent = (Wire1.endTransmission() == 0);
    return expanderDacPresent;
}

// Configure the expander's four channels. Same reference and gain as the base
// DAC, because the output stage behind it is the same circuit — 10k into the
// non-inverting input, 6k8 to ground, 9k1 plus a 2k trimmer in the feedback.
// The channel map is the identity here and on the base board alike.
inline bool InitExpDAC() {
    if (!ProbeExpander()) {
        Serial.println("No expander DAC at 0x61.");
        return false;
    }
    bool ok = true;
    for (int i = 0; i < NUM_EXP_OUTPUTS; i++) {
        Wire1.beginTransmission(MCP4728_EXP_ADDR);
        Wire1.write(0x40 | (i << 1)); // Multi-Write, channel i, UDAC=0
        Wire1.write(0x00);            // VREF=VDD, PD=normal, GAIN=1x, D[11:8]=0
        Wire1.write(0x00);            // D[7:0]
        ok &= (Wire1.endTransmission() == 0);
    }
    Serial.printf("MCP4728 expander init: %s\n", ok ? "OK" : "FAILED");
    return ok;
}

// Calibration helpers ────────────────────────────────────────────────
extern CalibrationData cal;

// Output (DAC) calibration ───────────────────────────────────────────
// Per-channel two-point correction remaps a desired output value (counts,
// 0..MAXDAC == 0..5V) to the code actually commanded so the jack voltage
// matches the ideal mapping.  Bypassed during the calibration wizard so the
// user trims and measures true, uncorrected hardware.
inline bool _dacCalBypass = false;
inline void SetDACCalBypass(bool bypass) { _dacCalBypass = bypass; }

// Apply the stored per-channel output correction. Returns `desired` unchanged
// (clamped) when calibration is invalid or bypassed, so an uncalibrated module
// behaves exactly as before.
inline uint16_t _CalibrateDACValue(int channel, uint32_t desired) {
    if (channel < 0 || channel >= NUM_MAX_OUTPUTS)
        return (uint16_t)constrain((int)desired, 0, MAXDAC);
    if (_dacCalBypass || !cal.valid)
        return (uint16_t)constrain((int)desired, 0, MAXDAC);
    float cmd = cal.dacScale[channel] * (float)desired + cal.dacOffset[channel];
    return (uint16_t)constrain((int)lroundf(cmd), 0, MAXDAC);
}

// Write all 4 DAC channels in a single I2C transaction (one START/STOP).
// Hardware channels A,B,C,D drive outputs 1..4 in order — see _chanMap above.
//
// FRAME FORMAT: MCP4728 Fast Write — two bytes per channel, channels A-D in
// order, no command byte:
//
//     [0 0 PD1 PD0 D11 D10 D9 D8] [D7..D0]
//
// Nine bytes a frame against Multi-Write's thirteen, which at 400 kHz is about
// 210 us against 300. With two devices on the bus that is the difference
// between a ~600 us and a ~415 us frame, straight off the Tick0 rate.
//
// Two conditions make this legal, and both are hardware facts rather than
// anything the firmware arranges:
//
//   * Fast Write updates the INPUT register; the output follows it only while
//     LDAC is low. LDAC IS low here — grounded on the base board, pulled down
//     on the shared node the expander joins — so it is transparent. Nothing
//     drives it and nothing needs to; the pin would only be wanted for
//     latching both DACs at the same instant, which no pad is left for.
//   * Fast Write carries no VREF or gain bits, so those must already be set.
//     InitDAC()/InitExpDAC() write them once with Multi-Write and the device
//     retains them.
//
// If LDAC ever came off ground, the failure is loud rather than subtle: the
// input registers would fill and the outputs would never move at all.
inline void DACWriteAll(uint16_t ch0, uint16_t ch1, uint16_t ch2, uint16_t ch3) {
    _dacShadow[0] = ch0;
    _dacShadow[1] = ch1;
    _dacShadow[2] = ch2;
    _dacShadow[3] = ch3;
    // Apply per-channel output calibration (desired counts → command code).
    const uint16_t hwVals[4] = {
        _CalibrateDACValue(0, ch0),
        _CalibrateDACValue(1, ch1),
        _CalibrateDACValue(2, ch2),
        _CalibrateDACValue(3, ch3),
    };
    Wire1.beginTransmission(MCP4728_ADDR);
    for (int i = 0; i < 4; i++) {
        Wire1.write((hwVals[i] >> 8) & 0x0F); // PD=normal, D[11:8]
        Wire1.write(hwVals[i] & 0xFF);        // D[7:0]
    }
    uint8_t result = Wire1.endTransmission();
    (void)result;
}

// Write the expander's four channels (outputs 5-8). Same Fast Write frame as
// DACWriteAll — see the note there — addressed to the second device.
//
// The two banks are NOT updated simultaneously and cannot be: latching them
// together needs LDAC driven, and no pad is left to drive it. Bank 2 trails
// bank 1 by one transaction, every frame. That is a fixed offset rather than
// jitter — both values come from the same engine pass — and at the rates
// these outputs run it is not observable.
//
// Measure with metrics.BeginDACMeasurement() before tuning further; the
// byte counts above are arithmetic, not readings.
inline void DACWriteAllExp(uint16_t ch4, uint16_t ch5, uint16_t ch6, uint16_t ch7) {
    if (!expanderDacPresent)
        return;
    _dacShadow[4] = ch4;
    _dacShadow[5] = ch5;
    _dacShadow[6] = ch6;
    _dacShadow[7] = ch7;
    // The expander's four trimmers are its own, so its calibration entries are
    // separate: channels 4-7 of the same per-channel tables.
    const uint16_t hwVals[4] = {
        _CalibrateDACValue(4, ch4),
        _CalibrateDACValue(5, ch5),
        _CalibrateDACValue(6, ch6),
        _CalibrateDACValue(7, ch7),
    };
    Wire1.beginTransmission(MCP4728_EXP_ADDR);
    for (int i = 0; i < 4; i++) {
        Wire1.write((hwVals[i] >> 8) & 0x0F); // PD=normal, D[11:8]
        Wire1.write(hwVals[i] & 0xFF);        // D[7:0]
    }
    uint8_t result = Wire1.endTransmission();
    (void)result;
}

// Write a single DAC channel; keeps other channels at their last value.
inline void DACWrite(int channel, uint32_t value) {
    if (channel < 0 || channel > 3)
        return;
    _dacShadow[channel] = (uint16_t)value;
    uint16_t cmd = _CalibrateDACValue(channel, value);
    dac4.setChannelValue(_chanMap[channel], cmd, MCP4728_VREF_VDD,
                         MCP4728_GAIN_1X);
}
