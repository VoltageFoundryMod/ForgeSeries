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

// Reference voltages (mV) used during CV input calibration capture.
#define CAL_REF1_MV 1000 // first reference: 1 V
#define CAL_REF2_MV 3000 // second reference: 3 V

// Output calibration is a two-point linear correction of the DAC command code:
//   - Full-scale anchor: all outputs driven to MAXDAC and the user trims each
//     on-board trimmer so the jack reads 5.00V (command 4095 → 5.000V).
//   - Low point: the DAC is driven to the ideal-1V code and the user measures
//     the actual jack voltage, capturing the op-amp offset the trimmer leaves.
// The two points define a per-channel remap  cmd = dacScale*desired + dacOffset
// applied on every DAC write (see boardIO.hpp).  Identity (1,0) = uncalibrated.

// Magic/version stamp for the stored blob. Bump whenever this struct's layout
// changes so an older-firmware blob is rejected rather than misinterpreted.
#define CAL_MAGIC 0x434C4B32UL // 'CLK2'

// ── Calibration data struct ─────────────────────────────────────────────────
// Stored at EEPROM_CAL_BASE, past all preset slots, so it survives firmware
// flashes and preset load/save operations.
struct CalibrationData {
    uint32_t magic; // must equal CAL_MAGIC for a valid, current-layout blob
    boolean valid;
    // Per-channel CV input linear coefficients derived from two external refs.
    // Conversion: mv = cvScale[ch] * raw_adc + cvOffset[ch]
    float cvScale[NUM_CV_INS];
    float cvOffset[NUM_CV_INS];
    // Per-channel output (DAC) correction: cmd = dacScale*desired + dacOffset,
    // where desired/cmd are DAC counts (0..MAXDAC == 0..5V).
    float dacScale[NUM_OUTPUTS];
    float dacOffset[NUM_OUTPUTS];
};
