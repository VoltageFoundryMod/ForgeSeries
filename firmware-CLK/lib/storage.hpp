#pragma once

// storage.hpp — Platform-specific storage backend
//
// Only responsibility: persist LoadSaveParams and CalibrationData to
// non-volatile storage. Schema and business logic live in presetManager.hpp.
//
// Swap this file for a new platform (e.g. std::fstream for VCVRack) without
// touching any other module.

#include "boardIO.hpp"
#include "presetManager.hpp"
#include <EEPROM.h>

// ── RP2040: arduino-pico EEPROM emulation ────────────────────────────
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
        // No calibration stored yet — populate nominal linear coefficients
        // (5000 mV full scale over 4095 counts, zero offset).
        // AdjustADCReadings() also falls back to nominal when !cal.valid,
        // so these values are a consistent starting point that avoids NaN.
        for (int i = 0; i < NUM_CV_INS; i++) {
            cal.cvScale[i] = 5000.0f / 4095.0f;
            cal.cvOffset[i] = 0.0f;
        }
        cal.valid = false;
    }
    return cal;
}
