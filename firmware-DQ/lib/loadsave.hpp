#pragma once

#include <Arduino.h>
#include <FlashStorage.h>

#include "pinouts.hpp"

#define NUM_SLOTS 1

// Struct to hold params that are saved/loaded to/from flash
struct LoadSaveParams {
    boolean valid;
    u_int8_t atk[NUM_CHANNELS];
    u_int8_t dcy[NUM_CHANNELS];
    u_int8_t sync[NUM_CHANNELS];
    u_int8_t sensitivity[NUM_CHANNELS];
    u_int8_t oct[NUM_CHANNELS];
    bool note[NUM_CHANNELS][12];
};

struct CalibrationData {
    uint32_t calibrationValues[2][6] = {0}; // [channel][0: 0V, 1: 1V, 2: 2V, 3: 3V, 4: 4V, 5: 5V]
};

// Store the calibration values in flash memory
FlashStorage(cal0, CalibrationData);
// Create slots for saving settings
FlashStorage(slot0, LoadSaveParams);

// Save data to flash memory
void Save(const LoadSaveParams &p) {
    delay(100);
    noInterrupts();
    slot0.write(p);
    interrupts();
}

LoadSaveParams LoadDefaultParams() {
    LoadSaveParams p;
    p.atk[0] = 1;
    p.dcy[0] = 4;
    p.atk[1] = 2;
    p.dcy[1] = 6;
    p.sync[0] = 1;
    p.sync[1] = 1;
    p.oct[0] = 3;
    p.oct[1] = 3;
    p.sensitivity[0] = 4;
    p.sensitivity[1] = 4;

    // Initialize channel 1 with chromatic scale
    p.note[0][0] = true;  // C
    p.note[0][1] = true;  // C#
    p.note[0][2] = true;  // D
    p.note[0][3] = true;  // D#
    p.note[0][4] = true;  // E
    p.note[0][5] = true;  // F
    p.note[0][6] = true;  // F#
    p.note[0][7] = true;  // G
    p.note[0][8] = true;  // G#
    p.note[0][9] = true;  // A
    p.note[0][10] = true; // A#
    p.note[0][11] = true; // B
    // Initialize channel 2 with C major scale
    p.note[1][0] = true;   // C
    p.note[1][1] = false;  // C#
    p.note[1][2] = true;   // D
    p.note[1][3] = false;  // D#
    p.note[1][4] = true;   // E
    p.note[1][5] = true;   // F
    p.note[1][6] = false;  // F#
    p.note[1][7] = true;   // G
    p.note[1][8] = false;  // G#
    p.note[1][9] = true;   // A
    p.note[1][10] = false; // A#
    p.note[1][11] = true;  // B

    return p;
}

LoadSaveParams Load() {
    LoadSaveParams p;
    delay(100);
    noInterrupts();
    p = slot0.read();
    interrupts();
    if (!p.valid) {
        return LoadDefaultParams();
    }
    return p;
}

void SaveCalibrationData(const CalibrationData &cal) { // save calibration data to flash memory
    noInterrupts();
    cal0.write(cal);
    interrupts();
}

void LoadCalibrationData(CalibrationData &cal) { // load calibration data from flash memory
    noInterrupts();
    cal = cal0.read();
    interrupts();
}
