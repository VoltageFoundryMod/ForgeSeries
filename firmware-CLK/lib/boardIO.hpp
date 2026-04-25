#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "pinouts.hpp"

// DAC resolution constants — shared across all platforms
#define DAC_RESOLUTION (12)
#define MAXDAC         4095

#if defined(ARDUINO_ARCH_RP2040)
// ── RP2040 I/O ────────────────────────────────────────────────────────
#include <Adafruit_MCP4728.h>

// MCP4728 quad 12-bit I2C DAC (channels A=out1, B=out2, C=out3, D=out4)
Adafruit_MCP4728 dac4;

// Current per-channel shadow values for fast single-channel updates
static uint16_t _dacShadow[4] = {0, 0, 0, 0};

// Physical channel mapping: board wiring has DACB and DACC swapped relative
// to the expected output order (Out1=A, Out2=C, Out3=B, Out4=D).
static const MCP4728_channel_t _chanMap[4] = {
    MCP4728_CHANNEL_A,  // Output 1 → DACA → Jack 1
    MCP4728_CHANNEL_C,  // Output 2 → DACC → Jack 2  (B/C swapped in hardware)
    MCP4728_CHANNEL_B,  // Output 3 → DACB → Jack 3  (B/C swapped in hardware)
    MCP4728_CHANNEL_D,  // Output 4 → DACD → Jack 4
};

// Write all 4 DAC channels.
// Uses MCP4728 Multi-Write command in a single I2C transaction (one START/STOP),
// which is ~3x faster than four separate setChannelValue() calls.
// Hardware channel mapping: DACA=sw0, DACB=sw2, DACC=sw1, DACD=sw3 (B/C wired swapped).
// Each 3-byte block: [CMD: 0x40|(hwCh<<2)] [VREF=0,PD=00,GAIN=0,D11:8] [D7:0]
void DACWriteAll(uint16_t ch0, uint16_t ch1, uint16_t ch2, uint16_t ch3) {
    _dacShadow[0] = ch0;
    _dacShadow[1] = ch1;
    _dacShadow[2] = ch2;
    _dacShadow[3] = ch3;
    // hw order A,B,C,D maps to sw[0,2,1,3]
    const uint16_t hwVals[4] = {ch0, ch2, ch1, ch3};
    Wire1.beginTransmission(MCP4728_ADDR);
    for (int i = 0; i < 4; i++) {
        Wire1.write(0x40 | (i << 1));         // Multi-Write cmd: cmd=010, channel=i (bits [2:1]), UDAC=0
        Wire1.write((hwVals[i] >> 8) & 0x0F); // VREF=VDD, PD=normal, GAIN=1x, D[11:8]
        Wire1.write(hwVals[i] & 0xFF);         // D[7:0]
    }
    Wire1.endTransmission();
}

// Write a single DAC channel; keeps other channels at their last value.
void DACWrite(int channel, uint32_t value) {
    if (channel < 0 || channel > 3) return;
    _dacShadow[channel] = (uint16_t)value;
    dac4.setChannelValue(_chanMap[channel], _dacShadow[channel], MCP4728_VREF_VDD, MCP4728_GAIN_1X);
}

// Calibration helpers ────────────────────────────────────────────────
// Loaded at boot; updated by CV calibration routine (Phase 8B).
struct CalibrationData;  // forward-declared here; defined in loadsave.hpp
extern CalibrationData cal;

static int32_t _adcAccum[NUM_CV_INS] = {0};
static uint8_t _adcCount[NUM_CV_INS] = {0};
static float   _channelADC[NUM_CV_INS] = {0.0f, 0.0f};

// Accumulate 8 samples then apply 2-point calibration.
// Call once per CV channel each loop iteration.
void AdjustADCReadings(int pin, int ch);  // body below (needs CalibrationData)

// Returns the latest calibrated ADC reading for a CV channel (0–4095).
float GetCVReading(int ch) {
    return _channelADC[ch];
}

void InitIO() {
    analogReadResolution(12);  // RP2040 supports 12-bit ADC

    pinMode(CLK_IN_PIN, INPUT_PULLDOWN);  // CLK in — pull low so floating input doesn't trigger spurious interrupts
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
    Wire.setClock(400000);   // SSD1306 rated 400kHz — keep conservative

    Wire1.setSDA(I2C_DAC_SDA_PIN);
    Wire1.setSCL(I2C_DAC_SCL_PIN);
    Wire1.begin();
    Wire1.setClock(1000000); // MCP4728 datasheet max=400kHz but silicon handles 1MHz;
                             // reduces DACWriteAll from ~400µs to ~150µs.
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
        ok &= dac4.setChannelValue(_chanMap[i], 0, MCP4728_VREF_VDD, MCP4728_GAIN_1X);
    }
    Serial.printf("MCP4728 init: %s\n", ok ? "OK" : "FAILED");
    return ok;
}

// ── Legacy SetPin shim (keeps outputs.hpp calling convention working) ──
// Maps the old 4-output index to MCP4728 channels via _chanMap.
void SetPin(int pin, uint32_t value) {
    if (pin < 0 || pin > 3) return;
    _dacShadow[pin] = (uint16_t)value;
    dac4.setChannelValue(_chanMap[pin], _dacShadow[pin], MCP4728_VREF_VDD, MCP4728_GAIN_1X);
}

#else
// ── SAMD21 I/O ────────────────────────────────────────────────────────
#include <Adafruit_MCP4725.h>

// Add prototypes for functions defined in this file
void InitIO();
void InternalDAC(uint32_t intDAC_OUT);
void MCP(uint32_t MCP_OUT);
void DACWrite(int pin, uint32_t value);
void PWM1(uint32_t duty1);
void PWM2(uint32_t duty2);
void PWMWrite(int pin, uint32_t value);
void SetPin(int pin, uint32_t value);

// Create the MCP4725 object
Adafruit_MCP4725 dac;
// Handle IO devices initialization
void InitIO() {
    // ADC settings. These increase ADC reading stability but at the cost of cycle time. Takes around 0.7ms for one reading with these
    REG_ADC_AVGCTRL |= ADC_AVGCTRL_SAMPLENUM_1;
    ADC->AVGCTRL.reg = ADC_AVGCTRL_SAMPLENUM_128 | ADC_AVGCTRL_ADJRES(4);

    analogWriteResolution(10);
    analogReadResolution(12);

    pinMode(LED_BUILTIN, OUTPUT); // LED
    pinMode(CLK_IN_PIN, INPUT);   // CLK in
    for (int i = 0; i < NUM_CV_INS; i++) {
        pinMode(CV_IN_PINS[i], INPUT); // CV in
    }
    pinMode(ENCODER_SW, INPUT_PULLUP); // push sw
    for (int i = 0; i < NUM_GATE_OUTS; i++) {
        pinMode(OUT_PINS[i], OUTPUT); // Gate out
    }

    // Initialize the DAC
    if (!dac.begin(0x60)) { // 0x60 is the default I2C address for MCP4725
        Serial.println("MCP4725 not found!");
        while (1)
            ;
    }
    Serial.println("MCP4725 initialized.");
    MCP(0);                       // Set the DAC output to 0
    InternalDAC(0);               // Set the internal DAC output to 0
    digitalWrite(OUT_PIN_1, LOW); // Initialize the output pins to low
    digitalWrite(OUT_PIN_2, LOW); // Initialize the output pins to low
}

// Handle DAC Outputs
void InternalDAC(uint32_t value) {
    analogWrite(DAC_INTERNAL_PIN, value / 4); // "/4" -> 12bit to 10bit
}

void MCP(uint32_t value) {
    dac.setVoltage(value, false);
}

// Write to DAC pins indexed by 0
void DACWrite(int pin, uint32_t value) {
    switch (pin) {
    case 0: // Internal DAC
        InternalDAC(value);
        break;
    case 1: // MCP DAC
        MCP(value);
        break;
    default:
        break;
    }
}

// Write to PWM pins indexed by 0
void PWMWrite(int pin, uint32_t value) {
    switch (pin) {
    case 0:
        pwm(OUT_PINS[0], 46000, value);
        break;
    case 1:
        pwm(OUT_PINS[1], 46000, value);
        break;
    default:
        break;
    }
}

// Set the output pins. Value can be from 0 to 4095 (12bit).
// For pins 0 and 1, 0 is low and anything else is high (inverted hardware logic)
void SetPin(int pin, uint32_t value) {
    switch (pin) {
    case 0: // Gate Output 1 (inverted: LOW = HIGH output)
        digitalWrite(OUT_PINS[0], value > 0 ? LOW : HIGH);
        break;
    case 1: // Gate Output 2 (inverted: LOW = HIGH output)
        digitalWrite(OUT_PINS[1], value > 0 ? LOW : HIGH);
        break;
    case 2: // Internal DAC Output
        DACWrite(0, value);
        break;
    case 3: // MCP DAC Output
        DACWrite(1, value);
        break;
    default:
        break;
    }
}

#endif  // ARDUINO_ARCH_RP2040
