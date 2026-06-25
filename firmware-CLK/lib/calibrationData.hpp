#pragma once

// calibrationData.hpp — CalibrationData struct
//
// Kept separate from presetManager.hpp to break the include cycle:
//   presetManager.hpp → cvInputs.hpp (for CVInputTarget)
//   cvInputs.hpp      → calibrationData.hpp (for CalibrationData)
//
// Two-point linear calibration: user supplies an external 1V and 3V reference
// to each CV input.  The ADC readings at those two voltages are used to derive
// per-channel linear coefficients:  mv = cvScale * raw_adc + cvOffset
// This is independent of the module's own DAC/outputs, avoiding any trimmer
// error propagating into the CV calibration.

#include "pinouts.hpp" // NUM_CV_INS
#include <Arduino.h>

// Reference voltages (mV) used during calibration capture.
#define CAL_REF1_MV 1000 // first reference: 1 V
#define CAL_REF2_MV 3000 // second reference: 3 V
// Output calibration is purely a hardware trimmer adjustment: all outputs are
// driven to MAXDAC and the user trims each jack to 5.00V.  With the MCP6004 on
// a 6V rail the full DAC range is linear, so there is no software output
// scaling — only the CV inputs require stored calibration coefficients.
// ── Calibration data struct ─────────────────────────────────────────────────
// Stored at EEPROM_CAL_BASE, past all preset slots, so it survives firmware
// flashes and preset load/save operations.
struct CalibrationData {
    boolean valid;
    // Per-channel linear coefficients derived from two external references.
    // Conversion: mv = cvScale[ch] * raw_adc + cvOffset[ch]
    float cvScale[NUM_CV_INS];
    float cvOffset[NUM_CV_INS];
};
