#pragma once

// calibrationData.hpp — CalibrationData struct + LUT constants
//
// Kept separate from presetManager.hpp to break the include cycle:
//   presetManager.hpp → cvInputs.hpp (for CVInputTarget)
//   cvInputs.hpp      → calibrationData.hpp (for CalibrationData + LUT)
//
// No other includes required — only primitive types.

#include "pinouts.hpp" // NUM_CV_INS
#include <Arduino.h>

// ── LUT definition ─────────────────────────────────────────────────────────
// 7 voltage points used during calibration capture and interpolation.
#define CAL_LUT_POINTS 7
static const uint16_t CAL_LUT_MV[CAL_LUT_POINTS] = {0, 500, 1000, 2000,
                                                    3000, 4000, 5000};
// DAC counts that produce those voltages (outputs trimmed to 5V at 4095).
static const uint16_t CAL_LUT_DAC[CAL_LUT_POINTS] = {0, 410, 819, 1638,
                                                     2457, 3276, 4095};

// ── Calibration data struct ─────────────────────────────────────────────────
// Stored at EEPROM_CAL_BASE, past all preset slots, so it survives firmware
// flashes and preset load/save operations.
struct CalibrationData {
    boolean valid;
    // Per-channel LUT: raw ADC reading captured at each CAL_LUT_MV voltage step.
    uint16_t cvLUT[NUM_CV_INS][CAL_LUT_POINTS];
};
