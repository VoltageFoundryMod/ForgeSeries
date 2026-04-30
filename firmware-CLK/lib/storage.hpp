#pragma once

// storage.hpp — Platform-specific storage backend
//
// Only responsibility: persist LoadSaveParams and CalibrationData to
// non-volatile storage. Schema and business logic live in presetManager.hpp.
//
// Swap this file for a new platform (e.g. std::fstream for VCVRack) without
// touching any other module.

#include "presetManager.hpp"

#if defined(ARDUINO_ARCH_RP2040)
// ── RP2040: arduino-pico EEPROM emulation ────────────────────────────
#include <EEPROM.h>

// EEPROM layout:
//   [0 .. NUM_SLOTS×sizeof(LoadSaveParams))  — preset slots
//   [EEPROM_CAL_BASE ..)                     — CalibrationData (never moved by slot ops)
#define EEPROM_PRESET_BASE 0
#define EEPROM_CAL_BASE (NUM_SLOTS * (int)sizeof(LoadSaveParams))
#define EEPROM_TOTAL_SIZE (EEPROM_CAL_BASE + (int)sizeof(CalibrationData))

void EEPROMInit() {
    EEPROM.begin(EEPROM_TOTAL_SIZE);
}

void Save(const LoadSaveParams &p, int slot) {
    if (slot < 0 || slot >= NUM_SLOTS)
        return;
    LoadSaveParams ps = p;
    ps.valid = VALID_MAGIC;
    EEPROM.put(EEPROM_PRESET_BASE + slot * sizeof(LoadSaveParams), ps);
    EEPROM.commit();
}

LoadSaveParams Load(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS)
        return LoadDefaultParams();
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
            for (int p = 0; p < CAL_LUT_POINTS; p++) {
                uint32_t mv = CAL_LUT_MV[p];
                cal.cvLUT[i][p] = (uint16_t)((uint32_t)mv * 33 * 4095 / (51 * 3300));
            }
        }
        cal.valid = false;
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
    if (slot < 0 || slot >= NUM_SLOTS)
        return;
    LoadSaveParams ps = p;
    ps.valid = VALID_MAGIC;
    delay(100);
    noInterrupts();
    switch (slot) {
    case 0:
        slot0.write(ps);
        break;
    case 1:
        slot1.write(ps);
        break;
    case 2:
        slot2.write(ps);
        break;
    case 3:
        slot3.write(ps);
        break;
    }
    interrupts();
}

LoadSaveParams Load(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS)
        return LoadDefaultParams();
    LoadSaveParams p;
    noInterrupts();
    switch (slot) {
    case 0:
        p = slot0.read();
        break;
    case 1:
        p = slot1.read();
        break;
    case 2:
        p = slot2.read();
        break;
    case 3:
        p = slot3.read();
        break;
    default:
        p = LoadDefaultParams();
        break;
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
        for (int p = 0; p < CAL_LUT_POINTS; p++) {
            uint32_t mv = CAL_LUT_MV[p];
            cal.cvLUT[i][p] = (uint16_t)((uint32_t)mv * 33 * 4095 / (51 * 3300));
        }
    }
    return cal;
}

#endif // ARDUINO_ARCH_RP2040
