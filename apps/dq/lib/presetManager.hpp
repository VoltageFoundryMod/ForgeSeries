#pragma once

// presetManager.hpp — Preset schema + functional layer (platform-agnostic)
//
// Owns: LoadSaveParams, LoadDefaultParams(), saveSlot, CollectParams(),
//       UpdateParameters().
//
// Platform storage (Save/Load/EEPROMInit) lives in storage.hpp, which
// #includes this file. Keeping them separate means this file is used unchanged
// by the VCV Rack plugin, where "EEPROM" is a byte buffer inside the patch.
//
// Depends on: channels[] (main.cpp / fw_engine.cpp), displayMgr, menuScreenTimeout.

#include <Arduino.h>

#include "calibrationData.hpp"
#include "channel.hpp"
#include "displayManager.hpp"
#include "jacks.hpp"

extern QuantizerChannel channels[NUM_CHANNELS]; // defined in src/main.cpp
extern int menuScreenTimeout;                   // defined in src/main.cpp
extern uint8_t in2Role;                         // defined in lib/cvInputs.hpp
extern uint8_t transposeRange;                  // defined in lib/cvInputs.hpp

// ── Preset schema ─────────────────────────────────────────────────────────────
// Slot 0 = auto-load/save on boot; slots 1–(NUM_SLOTS-1) = user presets.
// Increasing this shifts EEPROM_CAL_BASE — re-run calibration after changing.
#define NUM_SLOTS 10
// Bump whenever the LoadSaveParams layout changes so older (incompatible)
// presets fall back to defaults instead of loading garbage into new fields.
// 0xD1: first RP2040 layout (12-bit note masks, envelope times in ms).
// 0xD2: input sensitivity trim replaced by the note settle window.
// 0xD3: pitch mode (track/S&H), IN 2 routing and transposition.
#define VALID_MAGIC 0xD3 // 0xFF = erased flash, 0x00 = zeroed RAM

struct LoadSaveParams {
    uint8_t valid; // VALID_MAGIC = valid data; any other = use defaults
    // Per-channel scale state. The note mask is the source of truth; scale/root
    // are only remembered so the "load scale" helper reopens where you left it.
    uint16_t noteMask[NUM_CHANNELS]; // bit i = note i enabled (0 = C)
    uint8_t scaleIndex[NUM_CHANNELS];
    uint8_t rootIndex[NUM_CHANNELS];
    int8_t octave[NUM_CHANNELS];
    uint8_t settleMs[NUM_CHANNELS];
    uint8_t glide[NUM_CHANNELS];
    uint8_t syncMode[NUM_CHANNELS];
    uint8_t pitchMode[NUM_CHANNELS];
    uint8_t transposeEnabled[NUM_CHANNELS];
    // Gate/envelope
    uint8_t gateMode[NUM_CHANNELS];
    uint16_t attackMs[NUM_CHANNELS];
    uint16_t decayMs[NUM_CHANNELS];
    uint8_t gateLevel[NUM_CHANNELS];
    // Input routing
    uint8_t in2Role;
    uint8_t transposeRange;
    // UI
    int menuScreenTimeout; // index into screenTimeoutOptions[]
};

// ── Factory defaults ──────────────────────────────────────────────────────────
// Mirrors the original firmware's power-on state: channel 1 chromatic with a
// snappy envelope, channel 2 in C major with a slower one, both firing on note
// change.
LoadSaveParams LoadDefaultParams() {
    LoadSaveParams p;
    p.valid = VALID_MAGIC;

    p.noteMask[0] = 0x0FFF; // chromatic
    p.noteMask[1] = 0x0AB5; // C major: C D E F G A B
    for (int i = 0; i < NUM_CHANNELS; i++) {
        p.scaleIndex[i] = (i == 0) ? 0 : 1; // Chromatic / Major
        p.rootIndex[i] = 0;                 // C
        p.octave[i] = 0;
        p.settleMs[i] = 5;
        p.glide[i] = 0;
        p.syncMode[i] = SyncNote;
        p.pitchMode[i] = PitchTrack;
        p.transposeEnabled[i] = 0;
        p.gateMode[i] = GateEnvelope;
        p.gateLevel[i] = 100;
    }
    p.attackMs[0] = 0;
    p.decayMs[0] = 360;
    p.attackMs[1] = 0;
    p.decayMs[1] = 360;

    p.in2Role = In2Pitch;
    p.transposeRange = TrUp7;

    p.menuScreenTimeout = 2; // default: 5s
    return p;
}

// ── Globals ───────────────────────────────────────────────────────────────────
int saveSlot = 0; // Active save slot (0 = auto-save/load at boot)

// ─────────────────────────────────────────────────────────────────────────────
// Gather all current state into a LoadSaveParams snapshot
// ─────────────────────────────────────────────────────────────────────────────
LoadSaveParams CollectParams() {
    LoadSaveParams p;
    p.valid = VALID_MAGIC;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        uint16_t mask = 0;
        for (int n = 0; n < 12; n++) {
            if (channels[i].GetActiveNote(n)) {
                mask |= (uint16_t)(1u << n);
            }
        }
        p.noteMask[i] = mask;
        p.scaleIndex[i] = (uint8_t)channels[i].GetScaleIndex();
        p.rootIndex[i] = (uint8_t)channels[i].GetRootIndex();
        p.octave[i] = (int8_t)channels[i].GetOctave();
        p.settleMs[i] = (uint8_t)channels[i].GetSettle();
        p.glide[i] = (uint8_t)channels[i].GetGlide();
        p.syncMode[i] = (uint8_t)channels[i].GetSyncMode();
        p.pitchMode[i] = (uint8_t)channels[i].GetPitchMode();
        p.transposeEnabled[i] = channels[i].GetTransposeEnabled() ? 1 : 0;
        p.gateMode[i] = (uint8_t)channels[i].envelope.GetMode();
        p.attackMs[i] = (uint16_t)channels[i].envelope.GetAttack();
        p.decayMs[i] = (uint16_t)channels[i].envelope.GetDecay();
        p.gateLevel[i] = (uint8_t)channels[i].envelope.GetLevel();
    }
    p.in2Role = in2Role;
    p.transposeRange = transposeRange;
    p.menuScreenTimeout = menuScreenTimeout;
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// Apply a LoadSaveParams snapshot to all subsystem state
// ─────────────────────────────────────────────────────────────────────────────
void UpdateParameters(LoadSaveParams p) {
    for (int i = 0; i < NUM_CHANNELS; i++) {
        bool notes[12];
        for (int n = 0; n < 12; n++) {
            notes[n] = (p.noteMask[i] >> n) & 1u;
        }
        channels[i].SetActiveNotes(notes);
        channels[i].SetScaleIndex(p.scaleIndex[i]);
        channels[i].SetRootIndex(p.rootIndex[i]);
        channels[i].SetOctave(p.octave[i]);
        channels[i].SetSettle(p.settleMs[i]);
        channels[i].SetGlide(p.glide[i]);
        channels[i].SetSyncMode(p.syncMode[i]);
        channels[i].SetPitchMode(p.pitchMode[i]);
        channels[i].SetTransposeEnabled(p.transposeEnabled[i] != 0);
        channels[i].envelope.SetMode(p.gateMode[i]);
        channels[i].envelope.SetAttack(p.attackMs[i]);
        channels[i].envelope.SetDecay(p.decayMs[i]);
        channels[i].envelope.SetLevel(p.gateLevel[i]);
    }
    in2Role = (uint8_t)constrain((int)p.in2Role, 0, (int)In2RoleLength - 1);
    transposeRange = (uint8_t)constrain((int)p.transposeRange, 0, (int)TransposeRangeLength - 1);
    menuScreenTimeout = constrain(p.menuScreenTimeout, 0, 4);
    static const unsigned long kTimeoutOpts[] = {0, 2000, 5000, 10000, 20000};
    displayMgr.SetMenuTimeout(kTimeoutOpts[menuScreenTimeout]);
}
