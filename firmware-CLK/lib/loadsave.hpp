#pragma once

#include <Arduino.h>

#include "outputs.hpp"

// Number of preset slots.
// Slot 0 = auto-load/save on boot; slots 1–(NUM_SLOTS-1) = user presets.
// Increasing this shifts EEPROM_CAL_BASE — re-run CV calibration after changing.
#define NUM_SLOTS 5

// Struct to hold params that are saved/loaded to/from EEPROM/flash
// VALID_MAGIC: written to the `valid` field on save; checked on load.
// Using 0xA5 avoids false-positives from erased flash (0xFF) or zeroed RAM.
#define VALID_MAGIC 0xA5

struct LoadSaveParams {
    uint8_t valid;  // VALID_MAGIC (0xA5) = valid data; any other value = use defaults
    unsigned int BPM;
    unsigned int externalClockDivIdx;
    int divIdx[NUM_OUTPUTS];
    int dutyCycle[NUM_OUTPUTS];
    bool outputState[NUM_OUTPUTS];
    uint32_t outputLevel[NUM_OUTPUTS];
    int outputOffset[NUM_OUTPUTS];
    int swingIdx[NUM_OUTPUTS];
    int swingEvery[NUM_OUTPUTS];
    int pulseProbability[NUM_OUTPUTS];
    EuclideanParams euclideanParams[NUM_OUTPUTS];
    int phaseShift[NUM_OUTPUTS];
    int waveformType[NUM_OUTPUTS];
    byte CVInputTarget[NUM_CV_INS];
    int CVInputAttenuation[NUM_CV_INS];
    int CVInputOffset[NUM_CV_INS];
    EnvelopeParams envParams[NUM_OUTPUTS];
    QuantizerParams quantizerParams[NUM_OUTPUTS];
};

// CV input calibration — stored past the preset block so preset operations
// never clobber it. Only updated by the calibration routine (Phase 8B).
struct CalibrationData {
    boolean valid;
    float cvCalOffset[NUM_CV_INS];  // back-calculated intercept from 1V anchor
    float cvCalScale[NUM_CV_INS];   // 1638 / (reading_3v - reading_1v)
};

// ── Load default setting data ─────────────────────────────────────────
LoadSaveParams LoadDefaultParams() {
    LoadSaveParams p;
    p.valid = VALID_MAGIC;
    p.BPM = 120;
    p.externalClockDivIdx = 0;
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        p.divIdx[i] = 9;
        p.dutyCycle[i] = 50;
        p.outputState[i] = true;
        p.outputLevel[i] = 100;
        p.outputOffset[i] = 0;
        p.swingIdx[i] = 0;
        p.swingEvery[i] = 2;
        p.pulseProbability[i] = 100;
        p.euclideanParams[i] = {false, 10, 6, 1, 0};
        p.phaseShift[i] = 0;
        p.waveformType[i] = 0;
        p.envParams[i] = {200.0f, 200.0f, 70.0f, 250.0f, 0.5f, 0.5f, 0.5f, false};
        p.quantizerParams[i] = {false, 3, 4, 1, 0};
    }
    for (int i = 0; i < NUM_CV_INS; i++) {
        p.CVInputTarget[i] = 0;
        p.CVInputAttenuation[i] = 0;
        p.CVInputOffset[i] = 0;
    }
    return p;
}

#if defined(ARDUINO_ARCH_RP2040)
// ── RP2040: arduino-pico EEPROM emulation ────────────────────────────
#include <EEPROM.h>

// EEPROM layout:
//   [0 .. NUM_SLOTS×sizeof(LoadSaveParams))  — preset slots
//   [EEPROM_CAL_BASE ..)                     — CalibrationData (never moved by slot ops)
#define EEPROM_PRESET_BASE  0
#define EEPROM_CAL_BASE     (NUM_SLOTS * (int)sizeof(LoadSaveParams))
#define EEPROM_TOTAL_SIZE   (EEPROM_CAL_BASE + (int)sizeof(CalibrationData))

void EEPROMInit() {
    EEPROM.begin(EEPROM_TOTAL_SIZE);
}

void Save(const LoadSaveParams &p, int slot) {
    if (slot < 0 || slot >= NUM_SLOTS) return;
    LoadSaveParams ps = p;
    ps.valid = VALID_MAGIC;
    EEPROM.put(EEPROM_PRESET_BASE + slot * sizeof(LoadSaveParams), ps);
    EEPROM.commit();
}

LoadSaveParams Load(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS) return LoadDefaultParams();
    LoadSaveParams p;
    EEPROM.get(EEPROM_PRESET_BASE + slot * sizeof(LoadSaveParams), p);
    return (p.valid == VALID_MAGIC) ? p : LoadDefaultParams();
}

void SaveCalibration(const CalibrationData &cal) {
    EEPROM.put(EEPROM_CAL_BASE, cal);
    EEPROM.commit();
}

CalibrationData LoadCalibration() {
    CalibrationData cal;
    EEPROM.get(EEPROM_CAL_BASE, cal);
    if (!cal.valid) {
        for (int i = 0; i < NUM_CV_INS; i++) {
            cal.cvCalOffset[i] = 0.0f;
            cal.cvCalScale[i]  = 1.0f;
        }
    }
    return cal;
}

#else
// ── SAMD21: FlashStorage library ─────────────────────────────────────
#include <FlashStorage.h>

// Create 4 flash slots (legacy NUM_SLOTS was 4; keeping the explicit declarations)
FlashStorage(slot0, LoadSaveParams);
FlashStorage(slot1, LoadSaveParams);
FlashStorage(slot2, LoadSaveParams);
FlashStorage(slot3, LoadSaveParams);

void EEPROMInit() {
    // Nothing needed for FlashStorage
}

void Save(const LoadSaveParams &p, int slot) {
    if (slot < 0 || slot >= NUM_SLOTS) return;
    LoadSaveParams ps = p;
    ps.valid = VALID_MAGIC;
    delay(100);
    noInterrupts();
    switch (slot) {
    case 0: slot0.write(ps); break;
    case 1: slot1.write(ps); break;
    case 2: slot2.write(ps); break;
    case 3: slot3.write(ps); break;
    }
    interrupts();
}

LoadSaveParams Load(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS) return LoadDefaultParams();
    LoadSaveParams p;
    noInterrupts();
    switch (slot) {
    case 0: p = slot0.read(); break;
    case 1: p = slot1.read(); break;
    case 2: p = slot2.read(); break;
    case 3: p = slot3.read(); break;
    default: p = LoadDefaultParams(); break;
    }
    interrupts();
    return (p.valid == VALID_MAGIC) ? p : LoadDefaultParams();
}

// Stubs — no independent calibration storage on SAMD21 build
void SaveCalibration(const CalibrationData &) {}
CalibrationData LoadCalibration() {
    CalibrationData cal;
    cal.valid = false;
    for (int i = 0; i < NUM_CV_INS; i++) {
        cal.cvCalOffset[i] = 0.0f;
        cal.cvCalScale[i]  = 1.0f;
    }
    return cal;
}

#endif  // ARDUINO_ARCH_RP2040
