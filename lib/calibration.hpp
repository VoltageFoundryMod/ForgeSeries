#pragma once

// calibration.hpp — Hardware calibration wizard
//
// Entry point: RunCalibration()
// Called from setup() when the encoder button is held during boot.
//
// Step 1 — Output trim:
//   All 4 outputs driven to MAXDAC (5V after hardware trimming).
//   User adjusts the on-board trimmers with a multimeter until each jack
//   reads 5.00V.  Press encoder to proceed.
//
// Steps 2–5 — CV input 2-point linear capture:
//   For each channel (CV1, CV2) at each reference voltage (1V, 3V):
//     User applies an external reference to the CV input jack.
//     Display shows the live ADC reading as an estimated voltage so the user
//     can confirm the signal is stable before clicking.
//     On click: 256 samples are averaged and stored.
//   Two readings per channel (1V + 3V) define a linear mapping:
//       mv = cvScale * raw_adc + cvOffset
//   This approach is independent of the module's own DAC/outputs, so any
//   trimmer error from Step 1 does not propagate into CV calibration.
//
// CalibrationData lives at EEPROM_CAL_BASE (past all preset slots) and survives
// firmware flashes — EEPROM.commit() is the only thing that writes to flash.

#include <Adafruit_SSD1306.h>
#include <Arduino.h>

#include "boardIO.hpp"
#include "encoder.hpp"
#include "presetManager.hpp"

// Forward declarations from main.cpp
extern Adafruit_SSD1306 display;
extern CalibrationData cal;
extern Encoder encoder;
extern void SaveCalibration(const CalibrationData &);

// ── Calibration constants ────────────────────────────────────────────────────
static const int CAL_ADC_SAMPLES = 256; // samples averaged per capture point per channel

// ── Helpers ──────────────────────────────────────────────────────────────────

static void _CalClear() {
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextWrap(false);
}

static void _CalHeader(const char *title) {
    _CalClear();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(title);
    display.drawLine(0, 9, 127, 9, WHITE);
}

// Flush display buffer — Core 1 owns Wire on RP2040, so signal it.
static void _CalFlush() {
    extern volatile bool _displayFrameReady;
    _displayFrameReady = true;
    delay(35); // ~28ms for 128×64 @ 400kHz I2C
}

// Block until the encoder button is pressed then released (with debounce).
static void _CalWaitClick() {
    while (digitalRead(ENCODER_SW) == LOW)
        delay(10); // wait for release if still held
    delay(20);
    while (digitalRead(ENCODER_SW) == HIGH)
        delay(10); // wait for press
    delay(20);
    while (digitalRead(ENCODER_SW) == LOW)
        delay(10); // wait for release
    delay(20);
}

// Average CAL_ADC_SAMPLES raw ADC readings from one pin.
// 200µs between samples to let the ADC input settle between reads.
static uint16_t _CalReadADC(int pin) {
    uint32_t sum = 0;
    for (int i = 0; i < CAL_ADC_SAMPLES; i++) {
        sum += analogRead(pin);
        delayMicroseconds(200);
    }
    return (uint16_t)(sum / CAL_ADC_SAMPLES);
}

// Return a single raw ADC reading from a pin (used for live display).
static uint16_t _CalReadADCRaw(int pin) { return (uint16_t)analogRead(pin); }

// Convert a raw 12-bit ADC value to an approximate display voltage in mV.
// Uses nominal full-scale (4095 counts = 5000 mV) — good enough for live
// display before calibration coefficients exist.
static uint32_t _CalRawToMVDisplay(uint16_t raw) {
    return (uint32_t)raw * 5000 / 4095;
}

// ── Phase 1: Output trim ──────────────────────────────────────────────────────
// Drive all outputs to MAXDAC and ask the user to adjust each on-board trimmer
// until the jack reads exactly 5.00V.  With the MCP6004 on a 6V rail there is
// enough headroom to reach a clean 5V, so the full DAC range maps linearly to
// 0-5V and no software output scaling is needed.
static void _CalOutputTrim() {
    DACWriteAll(MAXDAC, MAXDAC, MAXDAC, MAXDAC);

    _CalHeader("1/5  OUTPUT TRIM");
    display.setTextSize(1);
    display.setCursor(0, 13);
    display.println("All outputs -> max.");
    display.println("Adjust each trimmer");
    display.println("so each jack reads");
    display.println("5.00V (multimeter).");
    display.println("");
    display.print("Click enc. to proceed");
    _CalFlush();
    _CalWaitClick();

    DACWriteAll(0, 0, 0, 0);
    delay(50);
    Serial.println("  Output trim done (MAXDAC trimmed to 5.00V).");
}

// ── CV capture step: wait for stable reference, then average ─────────────────
// ch      : 0-based CV channel index
// refMV   : expected reference voltage in mV (CAL_REF1_MV or CAL_REF2_MV)
// step    : 1-based step number shown in header (2–5)
// Returns : averaged raw ADC reading captured on click
static uint16_t _CalCaptureCV(int ch, int refMV, int step) {
    char header[20];
    snprintf(header, sizeof(header), "%d/5  CV%d INPUT %dV", step, ch + 1,
             refMV / 1000);

    // Loop: update display with live reading until encoder click
    while (true) {
        uint16_t liveRaw = _CalReadADCRaw(CV_IN_PINS[ch]);
        uint32_t liveMV = _CalRawToMVDisplay(liveRaw);

        _CalHeader(header);
        display.setTextSize(1);
        display.setCursor(0, 13);
        display.printf("Apply %dV to CV%d.\n\n", refMV / 1000, ch + 1);
        display.printf("Reading: %d.%03dV\n", (int)(liveMV / 1000),
                       (int)(liveMV % 1000));
        display.println("");
        display.print("Click when stable.");
        _CalFlush();

        // Poll button without blocking; _CalFlush already adds ~35 ms per frame
        if (digitalRead(ENCODER_SW) == LOW) {
            delay(20); // debounce
            if (digitalRead(ENCODER_SW) == LOW) {
                // Capture: average CAL_ADC_SAMPLES readings
                uint16_t averaged = _CalReadADC(CV_IN_PINS[ch]);
                uint32_t capturedMV = _CalRawToMVDisplay(averaged);
                Serial.printf("  CV%d ref=%dmV -> ADC=%d (~%dmV display)\n", ch + 1,
                              refMV, averaged, (int)capturedMV);
                // Wait for release
                while (digitalRead(ENCODER_SW) == LOW)
                    delay(10);
                delay(20);
                return averaged;
            }
        }
    }
}

// ── Main entry point ──────────────────────────────────────────────────────────
void RunCalibration() {
    Serial.println("Entering calibration mode...");

    // Welcome screen
    _CalHeader("CALIBRATION");
    display.setTextSize(1);
    display.setCursor(0, 13);
    display.println("5-step wizard:");
    display.println("1) Trim outputs to 5V");
    display.println("2-5) Apply 1V & 3V to");
    display.println("     each CV input.");
    display.println("");
    display.print("Click enc. to start");
    _CalFlush();
    _CalWaitClick();

    // ── Step 1: output trim (hardware trimmer → 5.00V at MAXDAC) ───────────────
    _CalOutputTrim();

    // ── Steps 2–5: per-channel, per-reference capture ─────────────────────────
    CalibrationData newCal;
    newCal.valid = true;

    // For each channel capture readings at the two reference voltages,
    // then derive linear coefficients: mv = scale * raw + offset
    const int refs[2] = {CAL_REF1_MV, CAL_REF2_MV};
    for (int ch = 0; ch < NUM_CV_INS; ch++) {
        uint16_t rawRef[2];
        for (int r = 0; r < 2; r++) {
            int step = 2 + ch * 2 + r; // steps 2, 3, 4, 5
            rawRef[r] = _CalCaptureCV(ch, refs[r], step);
        }
        // Linear fit: two points (rawRef[0], CAL_REF1_MV) and (rawRef[1], CAL_REF2_MV)
        float scale = (float)(CAL_REF2_MV - CAL_REF1_MV) / (float)(rawRef[1] - rawRef[0]);
        float offset = CAL_REF1_MV - scale * rawRef[0];
        newCal.cvScale[ch] = scale;
        newCal.cvOffset[ch] = offset;
        Serial.printf("  CV%d: scale=%.4f  offset=%.2f\n", ch + 1, scale, offset);
    }

    // ── Summary — user confirms before saving ─────────────────────────────────
    _CalHeader("CALIBRATION DONE");
    display.setTextSize(1);
    display.setCursor(0, 13);
    display.println("Calibration done!");
    for (int ch = 0; ch < NUM_CV_INS; ch++) {
        display.printf("CV%d: x%.4f+%.1f\n", ch + 1, newCal.cvScale[ch],
                       newCal.cvOffset[ch]);
    }
    display.println("");
    display.println("Click enc. to save");
    display.print("and reboot.");
    _CalFlush();

    _CalWaitClick();

    // ── Save and apply ────────────────────────────────────────────────────────
    SaveCalibration(newCal);
    cal = newCal;
    Serial.println("Calibration saved. Rebooting...");

    _CalHeader("SAVED");
    display.setTextSize(2);
    display.setCursor(10, 25);
    display.println("Calibration");
    display.setCursor(30, 43);
    display.println("Saved!");
    _CalFlush();
    delay(1500);

    rp2040.reboot();
}
