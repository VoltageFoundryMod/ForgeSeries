#pragma once

// presetManager.hpp — Preset schema + functional layer (platform-agnostic)
//
// Owns: LoadSaveParams, CalibrationData, LoadDefaultParams(),
//       saveSlot, CollectParams(), UpdateParameters().
//
// Platform storage (Save/Load/EEPROMInit) lives in storage.hpp, which
// #includes this file. Keeping them separate means this file can be used
// unchanged in a VCVRack plugin, desktop simulator, or any new platform.
//
// Depends on: outputs[] (clockEngine.hpp), BPM/externalDividerIndex (clockEngine.hpp),
//             CVInputTarget/Attenuation/Offset (cvInputs.hpp).

#include <Arduino.h>

#include "calibrationData.hpp"  // CalibrationData, CAL_LUT_*, CAL_LUT_POINTS
#include "clockEngine.hpp"
#include "cvInputs.hpp"
#include "displayManager.hpp"
#include "outputs.hpp"
#include "pinouts.hpp"

extern DisplayManager displayMgr;  // defined in src/main.cpp
extern int menuScreenTimeout;       // defined in src/main.cpp

// ── Preset schema ─────────────────────────────────────────────────────────────
// Number of preset slots.
// Slot 0 = auto-load/save on boot; slots 1–(NUM_SLOTS-1) = user presets.
// Increasing this shifts EEPROM_CAL_BASE — re-run CV calibration after changing.
#define NUM_SLOTS   5
#define VALID_MAGIC 0xA5  // Written to `valid` on save; 0xFF = erased flash, 0x00 = zeroed RAM

struct LoadSaveParams {
    uint8_t valid;  // VALID_MAGIC = valid data; any other = use defaults
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
    int menuScreenTimeout;  // index into screenTimeoutOptions[]
};

// CV calibration — see calibrationData.hpp for the struct definition.

// ── Factory defaults ──────────────────────────────────────────────────────────
LoadSaveParams LoadDefaultParams() {
    LoadSaveParams p;
    p.valid               = VALID_MAGIC;
    p.BPM                 = 120;
    p.externalClockDivIdx = 0;
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        p.divIdx[i]           = 9;
        p.dutyCycle[i]        = 50;
        p.outputState[i]      = true;
        p.outputLevel[i]      = 100;
        p.outputOffset[i]     = 0;
        p.swingIdx[i]         = 0;
        p.swingEvery[i]       = 2;
        p.pulseProbability[i] = 100;
        p.euclideanParams[i]  = {false, 10, 6, 1, 0};
        p.phaseShift[i]       = 0;
        p.waveformType[i]     = 0;
        p.envParams[i]        = {200.0f, 200.0f, 70.0f, 250.0f, 0.5f, 0.5f, 0.5f, false};
        p.quantizerParams[i]  = {false, 3, 4, 1, 0};
    }
    for (int i = 0; i < NUM_CV_INS; i++) {
        p.CVInputTarget[i]      = 0;
        p.CVInputAttenuation[i] = 0;
        p.CVInputOffset[i]      = 0;
    }
    p.menuScreenTimeout = 2;  // default: 5s
    return p;
}

// ── Globals ───────────────────────────────────────────────────────────────────
int saveSlot = 0;   // Active save slot (0 = auto-save/load at boot)

// ─────────────────────────────────────────────────────────────────────────────
// Gather all current state into a LoadSaveParams snapshot
// ─────────────────────────────────────────────────────────────────────────────
LoadSaveParams CollectParams() {
    LoadSaveParams p;
    p.valid               = true;
    p.BPM                 = BPM;
    p.externalClockDivIdx = externalDividerIndex;
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        p.divIdx[i]           = outputs[i].GetDividerIndex();
        p.dutyCycle[i]        = outputs[i].GetDutyCycle();
        p.outputState[i]      = outputs[i].GetOutputState();
        p.outputLevel[i]      = outputs[i].GetLevel();
        p.outputOffset[i]     = outputs[i].GetOffset();
        p.swingIdx[i]         = outputs[i].GetSwingAmountIndex();
        p.swingEvery[i]       = outputs[i].GetSwingEvery();
        p.pulseProbability[i] = outputs[i].GetPulseProbability();
        p.euclideanParams[i]  = outputs[i].GetEuclideanParams();
        p.phaseShift[i]       = outputs[i].GetPhase();
        p.waveformType[i]     = int(outputs[i].GetWaveformType());
        p.envParams[i]        = outputs[i].GetEnvelopeParams();
        p.quantizerParams[i]  = outputs[i].GetQuantizerParams();
    }
    for (int i = 0; i < NUM_CV_INS; i++) {
        p.CVInputTarget[i]      = CVInputTarget[i];
        p.CVInputAttenuation[i] = CVInputAttenuation[i];
        p.CVInputOffset[i]      = CVInputOffset[i];
    }
    p.menuScreenTimeout = menuScreenTimeout;
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// Apply a LoadSaveParams snapshot to all subsystem state
// ─────────────────────────────────────────────────────────────────────────────
void UpdateParameters(LoadSaveParams p) {
    BPM                  = constrain(p.BPM, minBPM, maxBPM);
    externalDividerIndex = p.externalClockDivIdx;
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        outputs[i].SetDivider(p.divIdx[i]);
        outputs[i].SetDutyCycle(p.dutyCycle[i]);
        outputs[i].SetOutputState(p.outputState[i]);
        outputs[i].SetLevel(p.outputLevel[i]);
        outputs[i].SetOffset(p.outputOffset[i]);
        outputs[i].SetSwingAmount(p.swingIdx[i]);
        outputs[i].SetSwingEvery(p.swingEvery[i]);
        outputs[i].SetPulseProbability(p.pulseProbability[i]);
        outputs[i].SetEuclideanParams(p.euclideanParams[i]);
        outputs[i].SetPhase(p.phaseShift[i]);
        outputs[i].SetWaveformType(static_cast<WaveformType>(p.waveformType[i]));
        outputs[i].SetEnvelopeParams(p.envParams[i]);
        outputs[i].SetQuantizerParams(p.quantizerParams[i]);
    }
    for (int i = 0; i < NUM_CV_INS; i++) {
        CVInputTarget[i]      = static_cast<CVTarget>(p.CVInputTarget[i]);
        CVInputAttenuation[i] = p.CVInputAttenuation[i];
        CVInputOffset[i]      = p.CVInputOffset[i];
    }
    menuScreenTimeout = constrain(p.menuScreenTimeout, 0, 4);
    static const unsigned long kTimeoutOpts[] = {0, 2000, 5000, 10000, 20000};
    displayMgr.SetMenuTimeout(kTimeoutOpts[menuScreenTimeout]);
}
