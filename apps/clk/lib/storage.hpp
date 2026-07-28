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

// The arduino-pico EEPROM emulation is ONE 4096-byte flash sector, and its
// begin() clamps anything larger without telling you:
//     if ((size <= 0) || (size > 4096)) { size = 4096; }
// Past the clamp, EEPROM::get() leaves its argument untouched and EEPROM::put()
// writes nothing — both without an error. Growing LoadSaveParams or NUM_SLOTS
// far enough therefore silently disables the top preset slots and, once
// EEPROM_CAL_BASE crosses the line, calibration entirely. Fail the build here
// instead of shipping that.
static_assert(EEPROM_TOTAL_SIZE <= 4096,
              "EEPROM layout exceeds the 4096-byte emulated sector: reduce "
              "NUM_SLOTS or shrink LoadSaveParams. Silently loses presets "
              "and calibration at runtime otherwise.");

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
    // past a bare `if (!cal.valid)` check and make AdjustADCReadings() compute NaN
    // CV readings (CV inputs appear completely dead). Reject non-finite
    // coefficients too (NaN compares unequal to itself) so an uncalibrated module
    // always falls back to the nominal linear mapping.  A magic/version mismatch
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
        // output correction is identity (cmd = desired).  AdjustADCReadings() and
        // the DAC write path also fall back to nominal when !cal.valid, so these
        // values are a consistent, NaN-free starting point.
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
