#pragma once

// appStorage.hpp — preset + calibration persistence, shared by every app.
//
// Each app's lib/storage.hpp is a four-line shim over this: it defines
// FORGE_APP_SLUG, includes its own presetManager.hpp, then includes this. That
// is all that differed between the three copies this replaces.
//
// REQUIRES, from the app's presetManager.hpp, included first:
//     LoadSaveParams        the preset payload type
//     LoadDefaultParams()   factory defaults
//     NUM_SLOTS             how many slots this module offers
//     VALID_MAGIC           schema stamp written into every saved payload
// and FORGE_APP_SLUG, a short filesystem-safe name ("clk").
//
// ── Backends ────────────────────────────────────────────────────────────────
// FORGE_USE_FS  → core/fsStore.hpp, one file per slot. Every hardware build.
// otherwise     → the Arduino EEPROM API, which is what the VCV Rack port uses:
//                 its shim is a growable byte buffer that Rack persists into the
//                 patch, so there is no filesystem and none is wanted.
//
// The EEPROM path is kept solely for Rack. On hardware it is a dead end: the
// emulation is ONE 4096-byte sector, and ClockForge's ten slots alone need
// 5776 bytes.

#ifndef FORGE_APP_SLUG
#error "define FORGE_APP_SLUG before including appStorage.hpp"
#endif

#ifdef FORGE_USE_FS

#include "fsStore.hpp"

// The shell mounts the filesystem before any app starts.
inline void EEPROMInit() {}

inline void Save(const LoadSaveParams &p, int slot) {
    if (slot < 0 || slot >= NUM_SLOTS)
        return;
    LoadSaveParams ps = p;
    ps.valid = VALID_MAGIC;
    forge::fs::SavePreset(FORGE_APP_SLUG, slot, ps);
}

inline LoadSaveParams Load(int slot) {
    LoadSaveParams p;
    if (slot < 0 || slot >= NUM_SLOTS ||
        !forge::fs::LoadPreset(FORGE_APP_SLUG, slot, p))
        return LoadDefaultParams();
    // VALID_MAGIC still guards the payload: the filesystem verifies a blob came
    // back intact and the right size, not that this firmware's schema wrote it.
    return (p.valid == VALID_MAGIC) ? p : LoadDefaultParams();
}

// Calibration describes the board, so it is one shared file rather than a copy
// per app. A wizard run in any module is picked up by all of them.
inline void SaveCalibration(const CalibrationData &c) {
    forge::fs::SaveCalibrationFs(c);
}
inline CalibrationData LoadCalibration() { return forge::fs::LoadCalibrationFs(); }

#else // ── EEPROM (VCV Rack only) ────────────────────────────────────────────

#include <EEPROM.h>

#define EEPROM_PRESET_BASE 0
#define EEPROM_CAL_BASE (NUM_SLOTS * (int)sizeof(LoadSaveParams))
#define EEPROM_TOTAL_SIZE (EEPROM_CAL_BASE + (int)sizeof(CalibrationData))

// Deliberately no 4096-byte static_assert here any more. It guarded the
// hardware sector, and hardware no longer takes this path; Rack's shim is a
// std::vector that grows on demand. Re-adding it would cap the slot count for
// no reason.

inline void EEPROMInit() { EEPROM.begin(EEPROM_TOTAL_SIZE); }

inline void Save(const LoadSaveParams &p, int slot) {
    if (slot < 0 || slot >= NUM_SLOTS)
        return;
    LoadSaveParams ps = p;
    ps.valid = VALID_MAGIC;
    EEPROM.put(EEPROM_PRESET_BASE + slot * sizeof(LoadSaveParams), ps);
    EEPROM.commit();
}

inline LoadSaveParams Load(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS)
        return LoadDefaultParams();
    LoadSaveParams p;
    EEPROM.get(EEPROM_PRESET_BASE + slot * sizeof(LoadSaveParams), p);
    return (p.valid == VALID_MAGIC) ? p : LoadDefaultParams();
}

inline void SaveCalibration(const CalibrationData &cal) {
    EEPROM.put(EEPROM_CAL_BASE, cal);
    EEPROM.commit();
}

// Rejects anything that is not a current, finite, valid blob and substitutes
// nominal coefficients. Erased storage reads as 0xFF, which decodes to a
// non-zero `valid` with NaN coefficients — that would slip past a bare
// `if (!valid)` and make every CV reading NaN, i.e. inputs that look dead.
// NaN compares unequal to itself, which is what the finite test relies on.
inline CalibrationData LoadCalibration() {
    CalibrationData cal;
    EEPROM.get(EEPROM_CAL_BASE, cal);

    bool finite = true;
    for (int i = 0; i < NUM_MAX_CV_INS; i++)
        if (cal.cvScale[i] != cal.cvScale[i] || cal.cvOffset[i] != cal.cvOffset[i])
            finite = false;
    for (int i = 0; i < NUM_MAX_OUTPUTS; i++)
        if (cal.dacScale[i] != cal.dacScale[i] || cal.dacOffset[i] != cal.dacOffset[i])
            finite = false;

    if (cal.magic != CAL_MAGIC || !cal.valid || !finite) {
        for (int i = 0; i < NUM_MAX_CV_INS; i++) {
            cal.cvScale[i] = 5000.0f / (float)MAXADC;
            cal.cvOffset[i] = 0.0f;
        }
        for (int i = 0; i < NUM_MAX_OUTPUTS; i++) {
            cal.dacScale[i] = 1.0f;
            cal.dacOffset[i] = 0.0f;
        }
        cal.magic = CAL_MAGIC;
        cal.valid = false;
    }
    return cal;
}

#endif // FORGE_USE_FS
