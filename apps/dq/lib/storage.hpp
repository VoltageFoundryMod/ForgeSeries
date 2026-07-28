#pragma once

// storage.hpp — Platform-specific storage backend
//
// Only responsibility: persist LoadSaveParams and CalibrationData to
// non-volatile storage. Schema and business logic live in presetManager.hpp.
//
// Swap this file for a new platform without touching any other module. The VCV
// Rack port reuses it verbatim against the shim's byte-buffer EEPROM, so a Rack
// patch stores exactly the same blob the hardware writes to flash.

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
    // Erased flash / freshly-initialised EEPROM reads as 0xFF bytes: that decodes
    // to a non-zero `valid` flag AND NaN coefficients, which would otherwise slip
    // past a bare `if (!cal.valid)` check and make CvReadMillivolts() return NaN
    // CV readings (both quantizers appear completely dead). Reject non-finite
    // coefficients too (NaN compares unequal to itself) so an uncalibrated module
    // always falls back to the nominal linear mapping. A magic/version mismatch
    // (older-firmware blob or blank flash) is likewise rejected so the struct is
    // never reinterpreted under a stale layout.
    bool finite = true;
    for (int i = 0; i < NUM_CV_INS; i++) {
        if (cal.cvScale[i] != cal.cvScale[i] || cal.cvOffset[i] != cal.cvOffset[i])
            finite = false;
    }
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        if (cal.dacScale[i] != cal.dacScale[i] || cal.dacOffset[i] != cal.dacOffset[i])
            finite = false;
    }
    if (cal.magic != CAL_MAGIC || !cal.valid || !finite) {
        // No (or invalid) calibration stored yet — populate nominal coefficients:
        // CV inputs use 5000 mV full scale over 4095 counts / zero offset, and the
        // output correction is identity (cmd = desired).
        for (int i = 0; i < NUM_CV_INS; i++) {
            cal.cvScale[i] = 5000.0f / 4095.0f;
            cal.cvOffset[i] = 0.0f;
        }
        for (int i = 0; i < NUM_OUTPUTS; i++) {
            cal.dacScale[i] = 1.0f;
            cal.dacOffset[i] = 0.0f;
        }
        cal.magic = CAL_MAGIC;
        cal.valid = false;
    }
    return cal;
}
