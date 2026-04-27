#pragma once

// calibration.hpp — Hardware calibration wizard
//
// Entry point: RunCalibration()
// Called from setup() when the encoder button is held during boot.
//
// Phase 1 — Output trim:
//   All 4 outputs driven to MAXDAC (5V after hardware trimming).
//   User adjusts the on-board trimmers with a multimeter until each jack reads 5.00V.
//   Press encoder to proceed.
//
// Phase 2 — CV input LUT capture (both channels simultaneously):
//   User connects Out1 → CV1 and Out2 → CV2 with two patch cables.
//   Firmware drives Out1 and Out2 together through 7 voltage steps (0–5V).
//   At each step: waits 200ms for settling, then averages 256 ADC samples per channel.
//   Both channels are captured in a single pass — no second connection needed.
//   When done: user sees a summary and presses the encoder to confirm save → reboot.
//
// CalibrationData lives at EEPROM_CAL_BASE (past all preset slots) and survives
// firmware flashes — EEPROM.commit() is the only thing that writes to flash.

#include <Arduino.h>
#include <Adafruit_SSD1306.h>

#include "boardIO.hpp"
#include "presetManager.hpp"

// Forward declarations from main.cpp
extern Adafruit_SSD1306 display;
extern CalibrationData  cal;
extern void SaveCalibration(const CalibrationData &);

// ── Calibration constants ────────────────────────────────────────────────────
static const int CAL_ADC_SAMPLES = 256;  // samples averaged per LUT point per channel
static const int CAL_SETTLE_MS   = 200;  // ms after setting DAC before reading ADC

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
#if defined(ARDUINO_ARCH_RP2040)
    extern volatile bool _displayFrameReady;
    _displayFrameReady = true;
    delay(35);  // ~28ms for 128×64 @ 400kHz I2C
#else
    display.display();
#endif
}

// Block until the encoder button is pressed then released (with debounce).
static void _CalWaitClick() {
    while (digitalRead(ENCODER_SW) == LOW)  delay(10);  // wait for release if still held
    delay(20);
    while (digitalRead(ENCODER_SW) == HIGH) delay(10);  // wait for press
    delay(20);
    while (digitalRead(ENCODER_SW) == LOW)  delay(10);  // wait for release
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

// Set Out1 and Out2 to the same DAC value, zero Out3/Out4, wait for settling.
static void _CalSetOutputs(uint16_t dacVal) {
#if defined(ARDUINO_ARCH_RP2040)
    DACWriteAll(dacVal, dacVal, 0, 0);
#endif
    delay(CAL_SETTLE_MS);
}

// ── Phase 1: Output trim ──────────────────────────────────────────────────────
static void _CalOutputTrim() {
#if defined(ARDUINO_ARCH_RP2040)
    DACWriteAll(MAXDAC, MAXDAC, MAXDAC, MAXDAC);
#endif

    _CalHeader("1/3  OUTPUT TRIM");
    display.setTextSize(1);
    display.setCursor(0, 13);
    display.println("All 4 outputs -> 5V.");
    display.println("Adjust each trimmer");
    display.println("until its jack reads");
    display.println("5.00V (multimeter).");
    display.println("");
    display.print("Click enc. when done");
    _CalFlush();

    _CalWaitClick();

#if defined(ARDUINO_ARCH_RP2040)
    DACWriteAll(0, 0, 0, 0);
#endif
    delay(50);
}

// ── Phase 2: Patch cable instruction ─────────────────────────────────────────
static void _CalPatchInstruction() {
    _CalHeader("2/3  PATCH CABLES");
    display.setTextSize(1);
    display.setCursor(0, 13);
    display.println("Connect cables:");
    display.println("  OUT 1  ->  CV IN 1");
    display.println("  OUT 2  ->  CV IN 2");
    display.println("Keep connected during");
    display.println("the next step.");
    display.print("Click enc. to start");
    _CalFlush();

    _CalWaitClick();
}

// ── Phase 3: Simultaneous dual-channel LUT capture ───────────────────────────
static void _CalCaptureBothChannels(CalibrationData &newCal) {
    for (int p = 0; p < CAL_LUT_POINTS; p++) {
        uint16_t dacVal = CAL_LUT_DAC[p];
        uint16_t mv     = CAL_LUT_MV[p];

        _CalSetOutputs(dacVal);  // drives Out1+Out2; also waits CAL_SETTLE_MS

        // Progress screen
        _CalHeader("3/3  CV INPUT CAL");
        display.setTextSize(1);
        display.setCursor(0, 13);
        display.printf("Step %d/%d:  %d.%02dV\n",
            p + 1, CAL_LUT_POINTS, mv / 1000, (mv % 1000) / 10);
        display.println("Reading inputs...");
        display.println("");
        // Progress bar
        int barW = (int)((float)(p + 1) / CAL_LUT_POINTS * 110);
        display.drawRect(9, 48, 110, 8, WHITE);
        display.fillRect(9, 48, barW, 8, WHITE);
        _CalFlush();

        // Sample both channels simultaneously
        for (int ch = 0; ch < NUM_CV_INS; ch++) {
            newCal.cvLUT[ch][p] = _CalReadADC(CV_IN_PINS[ch]);
            Serial.printf("  CV%d LUT[%d]: %dmV -> DAC=%d -> ADC=%d\n",
                ch + 1, p, mv, dacVal, newCal.cvLUT[ch][p]);
        }
    }

    // Zero outputs when done
#if defined(ARDUINO_ARCH_RP2040)
    DACWriteAll(0, 0, 0, 0);
#endif
    delay(50);
}

// ── Main entry point ──────────────────────────────────────────────────────────
void RunCalibration() {
    Serial.println("Entering calibration mode...");

    // Welcome screen
    _CalHeader("CALIBRATION");
    display.setTextSize(1);
    display.setCursor(0, 13);
    display.println("3-step wizard:");
    display.println("1) Trim outputs to 5V");
    display.println("2) Patch OUT->CV INs");
    display.println("3) CV input capture");
    display.println("");
    display.print("Click enc. to start");
    _CalFlush();
    _CalWaitClick();

    // ── Step 1: output trim ───────────────────────────────────────────────────
    _CalOutputTrim();

    // ── Step 2 + 3: patch instruction + simultaneous capture ─────────────────
    _CalPatchInstruction();

    CalibrationData newCal;
    newCal.valid = true;
    _CalCaptureBothChannels(newCal);

    // ── Summary — user confirms before saving ─────────────────────────────────
    _CalHeader("CALIBRATION DONE");
    display.setTextSize(1);
    display.setCursor(0, 13);
    display.println("Calibration done!");
    for (int ch = 0; ch < NUM_CV_INS; ch++) {
        display.printf("CV%d: 0V=%4d 5V=%4d\n", ch + 1,
            newCal.cvLUT[ch][0],
            newCal.cvLUT[ch][CAL_LUT_POINTS - 1]);
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

#if defined(ARDUINO_ARCH_RP2040)
    rp2040.reboot();
#else
    NVIC_SystemReset();
#endif
}
