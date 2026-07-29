#pragma once

// presetManager.hpp — Preset schema + functional layer (platform-agnostic)
//
// Owns: LoadSaveParams, LoadDefaultParams(), saveSlot, CollectParams(),
//       UpdateParameters().
//
// Platform storage (Save/Load/EEPROMInit) lives in storage.hpp, which #includes
// this file. Keeping them separate means this file is reused unchanged by the
// VCV Rack plugin, where "EEPROM" is a byte buffer inside the patch.

#ifdef UNIT_TEST
#include "ArduinoFake.h"
#else
#include <Arduino.h>
#endif

#include "calibrationData.hpp"
#include "clock.hpp"
#include "displayManager.hpp"
#include "params.hpp"
#include "jacks.hpp"
#include "sequencer.hpp"

extern GravityChannel channels[NUM_CHANNELS]; // src/main.cpp
extern ContainerParams containerParams[2];    // src/main.cpp
extern WorldParams worldParams;               // src/main.cpp
extern Clock clockEngine;                     // src/main.cpp
extern int menuScreenTimeout;                 // src/main.cpp
extern uint8_t in1Role;                       // lib/cvInputs.hpp
extern uint8_t cvTarget[NUM_CV_INS];          // lib/cvInputs.hpp
extern uint8_t cvDepth[NUM_CV_INS];           // lib/cvInputs.hpp

// ── Preset schema ─────────────────────────────────────────────────────────────
// Slot 0 = auto-load/save on boot; slots 1–(NUM_SLOTS-1) = user presets.
// Increasing this shifts EEPROM_CAL_BASE — re-run calibration after changing.
#define NUM_SLOTS 10

// Bump whenever the LoadSaveParams layout changes so older (incompatible)
// presets fall back to defaults instead of loading garbage into new fields.
// 0xE1: first GravityForge layout.
// 0xE2: per-channel OCTAVE replaced by SPREAD + BIAS.
// 0xE3: loop / phrase mode (beats, wake, nap, per-container shift).
#define VALID_MAGIC 0xE3 // 0xFF = erased flash, 0x00 = zeroed RAM

struct LoadSaveParams {
    uint8_t valid; // VALID_MAGIC = valid data; any other = use defaults

    // ── Musical, per channel ──
    uint16_t noteMask[NUM_CHANNELS]; // bit i = note i enabled (0 = C)
    uint8_t scaleIndex[NUM_CHANNELS];
    uint8_t rootIndex[NUM_CHANNELS];
    uint8_t spread[NUM_CHANNELS]; // octaves the peg ring covers
    int8_t bias[NUM_CHANNELS];   // -100 crowd low .. +100 crowd high
    uint8_t gateMode[NUM_CHANNELS];
    uint16_t attackMs[NUM_CHANNELS];
    uint16_t decayMs[NUM_CHANNELS];
    uint8_t gateLevel[NUM_CHANNELS];
    uint8_t accent[NUM_CHANNELS];

    // ── Physics, per container ──
    float gravity[2];
    float bounce[2];
    float grip[2];
    float freeHz[2];
    uint8_t spin[2];
    uint8_t reverse[2];
    uint8_t balls[2];
    uint8_t pegs[2];
    uint16_t pegMask[2];

    // ── World ──
    float proximity;
    float coupling;

    // ── Loop / phrase mode ──
    uint8_t loopBeats; // 0 = off
    uint8_t loopWake;
    uint8_t loopNap;
    uint8_t loopShift[2];

    // ── Clock ──
    uint16_t bpm;
    uint8_t ppqn;
    uint8_t quantize;

    // ── Routing ──
    uint8_t in1Role;
    uint8_t cvTarget[NUM_CV_INS];
    uint8_t cvDepth[NUM_CV_INS];

    // ── UI ──
    int menuScreenTimeout; // index into screenTimeoutOptions[]
};

// ── Factory defaults ──────────────────────────────────────────────────────────
// Boots to something that plays immediately with nothing patched: both
// containers running, A a slow major arpeggio low down, B faster and higher, and
// the containers apart so the two sequences are independent until the user
// dials PROXIMITY in. Coupling is pre-set to a useful 60 % so that turning
// proximity up does something obvious straight away.
LoadSaveParams LoadDefaultParams() {
    LoadSaveParams p;
    p.valid = VALID_MAGIC;

    p.noteMask[0] = 0x0AB5; // C major
    p.noteMask[1] = 0x0AB5;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        p.scaleIndex[i] = 1; // Major
        p.rootIndex[i] = 0;  // C
        p.gateMode[i] = GateEnvelope;
        p.attackMs[i] = 0;
        p.gateLevel[i] = 100;
        p.accent[i] = 0;
    }
    // A covers two octaves evenly; B is narrower and crowded a little high,
    // so the two containers sit in different registers without either being
    // pinned to one octave.
    p.spread[0] = 2;
    p.bias[0] = 0;
    p.spread[1] = 1;
    p.bias[1] = 30;
    // Decay has to be shorter than the gap between notes or the gate never
    // returns to zero and stops being a gate. A container at these settings
    // fires roughly 7 times a second (~140 ms apart), so ~100 ms articulates
    // with room to spare. Longer envelopes are still one menu row away for
    // pad-like patches — but the default has to sound like a sequencer.
    p.decayMs[0] = 100;
    p.decayMs[1] = 70;

    for (int i = 0; i < 2; i++) {
        p.gravity[i] = 220.0f;
        p.bounce[i] = 0.72f;
        p.grip[i] = 0.30f;
        p.freeHz[i] = 0.25f;
        p.reverse[i] = 0;
        p.pegMask[i] = 0xFFFF;
    }
    p.spin[0] = Spin8;
    p.spin[1] = Spin4;
    p.balls[0] = 3;
    p.balls[1] = 2;
    p.pegs[0] = 8;
    p.pegs[1] = 5;

    p.proximity = 0.0f;
    p.coupling = 0.6f;

    // Loop off by default. The endless evolving stream is what the module IS;
    // booting into a repeating four-bar phrase would hide the thing it does
    // that nothing else does. Looping is one page away when you want to keep
    // something.
    p.loopBeats = 0;
    p.loopWake = 1;
    p.loopNap = 0;
    p.loopShift[0] = 0;
    p.loopShift[1] = 0;

    p.bpm = 120;
    p.ppqn = Ppqn4;
    p.quantize = QOff;

    p.in1Role = In1Clock;
    p.cvTarget[0] = CVProximity;
    p.cvTarget[1] = CVGravityBoth;
    p.cvDepth[0] = 0;
    p.cvDepth[1] = 0;

    p.menuScreenTimeout = 2; // 5 s
    return p;
}

// ── Globals ───────────────────────────────────────────────────────────────────
int saveSlot = 0; // Active save slot (0 = auto-save/load at boot)

// ─────────────────────────────────────────────────────────────────────────────
// Gather all current state into a LoadSaveParams snapshot.
//
// Note this reads the *base* parameter block, never the live Container: the
// container is carrying whatever CV modulation is applied this instant, and
// saving that would bake a moving CV into the preset.
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
        p.spread[i] = (uint8_t)channels[i].GetSpread();
        p.bias[i] = (int8_t)channels[i].GetBias();
        p.gateMode[i] = (uint8_t)channels[i].envelope.GetMode();
        p.attackMs[i] = (uint16_t)channels[i].envelope.GetAttack();
        p.decayMs[i] = (uint16_t)channels[i].envelope.GetDecay();
        p.gateLevel[i] = (uint8_t)channels[i].GetGateLevel();
        p.accent[i] = (uint8_t)channels[i].GetAccent();
    }

    for (int i = 0; i < 2; i++) {
        p.gravity[i] = containerParams[i].gravity;
        p.bounce[i] = containerParams[i].bounce;
        p.grip[i] = containerParams[i].grip;
        p.freeHz[i] = containerParams[i].freeHz;
        p.spin[i] = containerParams[i].spin;
        p.reverse[i] = containerParams[i].reverse ? 1 : 0;
        p.balls[i] = containerParams[i].balls;
        p.pegs[i] = containerParams[i].pegs;
        p.pegMask[i] = containerParams[i].pegMask;
    }

    p.proximity = worldParams.proximity;
    p.coupling = worldParams.coupling;

    p.loopBeats = worldParams.loopBeats;
    p.loopWake = worldParams.loopWake;
    p.loopNap = worldParams.loopNap;
    p.loopShift[0] = worldParams.loopShift[0];
    p.loopShift[1] = worldParams.loopShift[1];

    p.bpm = (uint16_t)clockEngine.GetBpm();
    p.ppqn = (uint8_t)clockEngine.GetPpqn();
    p.quantize = (uint8_t)clockEngine.GetQuantize();

    p.in1Role = in1Role;
    for (int i = 0; i < NUM_CV_INS; i++) {
        p.cvTarget[i] = cvTarget[i];
        p.cvDepth[i] = cvDepth[i];
    }

    p.menuScreenTimeout = menuScreenTimeout;
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// Apply a LoadSaveParams snapshot to all subsystem state.
//
// Uses the plain Set* accessors on the channels, never Select*: the stored note
// mask is the source of truth because it may have been hand-edited away from any
// named scale, and Select* would overwrite it with a freshly generated one.
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
        channels[i].SetSpread(p.spread[i]);
        channels[i].SetBias(p.bias[i]);
        channels[i].envelope.SetMode(p.gateMode[i]);
        channels[i].envelope.SetAttack(p.attackMs[i]);
        channels[i].envelope.SetDecay(p.decayMs[i]);
        channels[i].SetGateLevel(p.gateLevel[i]);
        channels[i].SetAccent(p.accent[i]);
    }

    for (int i = 0; i < 2; i++) {
        containerParams[i].gravity = constrain(p.gravity[i], PARAM_GRAVITY_MIN, PARAM_GRAVITY_MAX);
        containerParams[i].bounce = constrain(p.bounce[i], PARAM_BOUNCE_MIN, PARAM_BOUNCE_MAX);
        containerParams[i].grip = constrain(p.grip[i], PARAM_GRIP_MIN, PARAM_GRIP_MAX);
        containerParams[i].freeHz = constrain(p.freeHz[i], PARAM_FREEHZ_MIN, PARAM_FREEHZ_MAX);
        containerParams[i].spin = (uint8_t)constrain((int)p.spin[i], 0, (int)SpinRateLength - 1);
        containerParams[i].reverse = p.reverse[i] != 0;
        containerParams[i].balls = (uint8_t)constrain((int)p.balls[i], PHYS_MIN_BALLS, PHYS_MAX_BALLS);
        containerParams[i].pegs = (uint8_t)constrain((int)p.pegs[i], PHYS_MIN_PEGS, PHYS_MAX_PEGS);
        containerParams[i].pegMask = p.pegMask[i];
    }

    worldParams.proximity = constrain(p.proximity, 0.0f, 1.0f);
    worldParams.coupling = constrain(p.coupling, 0.0f, 1.0f);

    worldParams.loopBeats = (uint8_t)constrain((int)p.loopBeats, 0, PARAM_LOOP_BEATS_MAX);
    worldParams.loopWake = (uint8_t)constrain((int)p.loopWake, PARAM_LOOP_WAKE_MIN,
                                              PARAM_LOOP_WAKE_MAX);
    worldParams.loopNap = (uint8_t)constrain((int)p.loopNap, 0, PARAM_LOOP_NAP_MAX);
    for (int i = 0; i < 2; i++) {
        worldParams.loopShift[i] =
            (uint8_t)constrain((int)p.loopShift[i], 0, PARAM_LOOP_SHIFT_MAX);
    }

    clockEngine.SetBpm((int)p.bpm);
    clockEngine.SetPpqn(p.ppqn);
    clockEngine.SetQuantize(p.quantize);

    in1Role = (uint8_t)constrain((int)p.in1Role, 0, (int)In1RoleLength - 1);
    clockEngine.SetExternal(in1Role == In1Clock);
    for (int i = 0; i < NUM_CV_INS; i++) {
        cvTarget[i] = (uint8_t)constrain((int)p.cvTarget[i], 0, (int)CVTargetLength - 1);
        cvDepth[i] = (uint8_t)constrain((int)p.cvDepth[i], 0, 100);
    }

    menuScreenTimeout = constrain(p.menuScreenTimeout, 0, 4);
    static const unsigned long kTimeoutOpts[] = {0, 2000, 5000, 10000, 20000};
    displayMgr.SetMenuTimeout(kTimeoutOpts[menuScreenTimeout]);
}
