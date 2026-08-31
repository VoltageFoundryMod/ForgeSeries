#pragma once

// calibrationData.hpp — the CalibrationData struct, shared by every firmware.
//
// Calibration describes the *board's* analog front and back ends, not anything
// about a particular module, so all apps use the same struct and (unlike the
// per-app schemas this file used to be duplicated into) the same magic. That is
// what lets the unified firmware calibrate once in the shell and have every app
// read the result.
//
// Kept separate from presetManager.hpp so the low-level IO layer (boardIO.hpp)
// can see the struct without pulling in an app's preset schema, and to break the
// include cycle presetManager -> cvInputs -> calibrationData.
//
// CV INPUTS — two-point linear calibration. The user feeds an external 1 V and
// 3 V reference to each CV input; the ADC readings at those two voltages derive
// per-channel coefficients:  mv = cvScale * raw_adc + cvOffset.  This is
// independent of the module's own DAC/outputs, so no trimmer error propagates
// into the CV calibration.
//
// OUTPUTS — two-point linear correction of the DAC command code:
//   * Full-scale anchor: all outputs driven to MAXDAC while the user trims each
//     on-board trimmer until the jack reads 5.00 V (command 4095 -> 5.000 V).
//   * Low point: the DAC is driven to the ideal-1V code and the user measures
//     the actual jack voltage, capturing the op-amp offset the trimmer leaves.
// The two points define a per-channel remap  cmd = dacScale*desired + dacOffset
// applied on every DAC write (see boardIO.hpp). Identity (1,0) = uncalibrated.
//
// Accuracy matters more on some modules than others — a quantizer skewed by an
// uncalibrated ADC lands on the wrong notes near the top of its range, while a
// clock barely notices — but the correction itself is the same board-level fix
// everywhere.

#include "boardPinouts.hpp" // NUM_CV_INS, NUM_OUTPUTS
#include <Arduino.h>

// Reference voltages (mV) used during CV input calibration capture.
#define CAL_REF1_MV 1000 // first reference: 1 V
#define CAL_REF2_MV 3000 // second reference: 3 V

// Magic/version stamp for the stored blob. Bump whenever the struct's layout
// changes so an older blob is rejected rather than reinterpreted under a stale
// layout.
//
// This replaces the former per-app magics ('CLK2', 'NFQ2', 'GFV1'), which made
// each firmware reject calibration written by any other even though all three
// structs were byte-identical. One magic means one calibration for the board.
// 'FRG2' (was 'FRG1'): the per-channel arrays grew from the base board's
// counts to NUM_MAX_*, so an expander's four outputs and its CV input carry
// coefficients of their own. They must — the expander has its own output
// trimmers and its own input stage, so nothing about it is described by the
// base board's numbers.
#define CAL_MAGIC 0x46524732UL // 'FRG2' — ForgeSeries calibration, layout v1

// ── Calibration data struct ─────────────────────────────────────────────────
struct CalibrationData {
    uint32_t magic; // must equal CAL_MAGIC for a valid, current-layout blob
    boolean valid;
    // Per-channel CV input linear coefficients derived from two external refs.
    // Conversion: mv = cvScale[ch] * raw_adc + cvOffset[ch]
    float cvScale[NUM_MAX_CV_INS];
    float cvOffset[NUM_MAX_CV_INS];
    // Per-channel output (DAC) correction: cmd = dacScale*desired + dacOffset,
    // where desired/cmd are DAC counts (0..MAXDAC == 0..5V).
    float dacScale[NUM_MAX_OUTPUTS];
    float dacOffset[NUM_MAX_OUTPUTS];
};
