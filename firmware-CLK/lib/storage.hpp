#pragma once

// storage.hpp — Platform-specific storage backend
//
// Only responsibility: persist LoadSaveParams and CalibrationData to
// non-volatile storage. Schema and business logic live in presetManager.hpp.
//
// Swap this file for a new platform (e.g. std::fstream for VCVRack) without
// touching any other module.

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
