#pragma once

#include <Arduino.h>
#include <FlashStorage.h>

#include "outputs.hpp"

#define NUM_SLOTS 4

// Struct to hold params that are saved/loaded to/from EEPROM
struct LoadSaveParams {
    boolean valid;
    unsigned int BPM;
    unsigned int externalClockDivIdx;
    int divIdx[NUM_OUTPUTS];
    int dutyCycle[NUM_OUTPUTS];
    bool outputState[NUM_OUTPUTS];
    uint32_t outputLevel[NUM_OUTPUTS];
    int outputOffset[NUM_OUTPUTS];
    int swingIdx[NUM_OUTPUTS];
    int swingEvery[NUM_OUTPUTS];
    int pulseProbability[NUM_OUTPUTS];
    EuclideanParams euclideanParams[NUM_OUTPUTS];
    int phaseShift[NUM_OUTPUTS];
    int waveformType[NUM_OUTPUTS];
    byte CVInputTarget[NUM_CV_INS];
    int CVInputAttenuation[NUM_CV_INS];
    int CVInputOffset[NUM_CV_INS];
    EnvelopeParams envParams[NUM_OUTPUTS];
    QuantizerParams quantizerParams[NUM_OUTPUTS];
};

struct CalibrationData {
    uint32_t calibrationValues[2][6] = {0}; // [channel][0: 0V, 1: 1V, 2: 2V, 3: 3V, 4: 4V, 5: 5V]
};

// Store the calibration values in flash memory
FlashStorage(cal0, CalibrationData);
// Create 4 slots for saving settings
FlashStorage(slot0, LoadSaveParams);
FlashStorage(slot1, LoadSaveParams);
FlashStorage(slot2, LoadSaveParams);
FlashStorage(slot3, LoadSaveParams);

// Save data to flash memory
void Save(const LoadSaveParams &p, int slot) { // save setting data to flash memory
    if (slot < 0 || slot >= NUM_SLOTS)
        return;
    delay(100);
    noInterrupts();
    switch (slot) {
    case 0:
        slot0.write(p);
        break;
    case 1:
        slot1.write(p);
        break;
    case 2:
        slot2.write(p);
        break;
    case 3:
        slot3.write(p);
        break;
    }
    interrupts();
}

// Load default setting data
LoadSaveParams LoadDefaultParams() {
    LoadSaveParams p;
    p.BPM = 120;
    p.externalClockDivIdx = 0;
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        p.divIdx[i] = 9;
        p.dutyCycle[i] = 50;
        p.outputState[i] = true;
        p.outputLevel[i] = 100;
        p.outputOffset[i] = 0;
        p.swingIdx[i] = 0;
        p.swingEvery[i] = 2;
        p.pulseProbability[i] = 100;
        p.euclideanParams[i] = {false, 10, 6, 1, 0};
        p.phaseShift[i] = 0;
        p.waveformType[i] = 0;
        p.envParams[i] = {200.0f, 200.0f, 70.0f, 250.0f, 0.5f, 0.5f, 0.5f, false};
        p.quantizerParams[i] = {false, 3, 4, 1, 0};
    }
    for (int i = 0; i < NUM_CV_INS; i++) {
        p.CVInputTarget[i] = 0;
        p.CVInputAttenuation[i] = 0;
        p.CVInputOffset[i] = 0;
    }
    return p;
}

// Load setting data from flash memory
LoadSaveParams Load(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS)
        return LoadDefaultParams();
    LoadSaveParams p;
    noInterrupts();

    // Read the data from flash memory
    switch (slot) {
    case 0:
        p = slot0.read();
        break;
    case 1:
        p = slot1.read();
        break;
    case 2:
        p = slot2.read();
        break;
    case 3:
        p = slot3.read();
        break;
    }
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
