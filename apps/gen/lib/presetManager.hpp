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

#include "boardPinouts.hpp"
#include "calibrationData.hpp"
#include "clock.hpp"
#include "displayManager.hpp"
#include "params.hpp"
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
// 0xE4: per-container DENSITY and SPACE.
// 0xE5: 0V NOTE — the note the module's 0 V stands for.
// 0xE6: ROOT carries an octave — the peg ring starts on it instead of being
//       centred in the output range.
#define VALID_MAGIC 0xE6 // 0xFF = erased flash, 0x00 = zeroed RAM

struct LoadSaveParams {
    uint8_t valid; // VALID_MAGIC = valid data; any other = use defaults

    // ── Musical, per channel ──
    uint16_t noteMask[NUM_CHANNELS]; // bit i = note i enabled (0 = C)
    uint8_t scaleIndex[NUM_CHANNELS];
    uint8_t rootIndex[NUM_CHANNELS];
    uint8_t rootOctave[NUM_CHANNELS]; // which octave of the range the ring starts in
    uint8_t spread[NUM_CHANNELS];     // octaves the peg ring covers
    int8_t bias[NUM_CHANNELS];        // -100 crowd low .. +100 crowd high
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
    uint8_t density[2]; // % of otherwise-valid strikes that speak
    uint8_t space[2];   // minimum gap between notes, index into NoteSpaceNames

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
    // 0V NOTE — the note the module's 0 V stands for. One value for the module,
    // not per channel: it describes the rack the module is patched into, and
    // both pitch jacks go into the same rack.
    uint8_t cvZeroOctave;
    uint8_t in1Role;
    uint8_t cvTarget[NUM_CV_INS];
    uint8_t cvDepth[NUM_CV_INS];

    // ── UI ──
    int menuScreenTimeout; // index into screenTimeoutOptions[]
};

// ── Factory defaults ──────────────────────────────────────────────────────────
// Boots to something that plays immediately with nothing patched, and — because
// the two containers are wholly independent at PROXIMITY 0 — it boots to TWO
// worked examples rather than one, so both ends of the module's range are
// audible on the first patch cable:
//
//   A (OUT 1 / OUT 3) — the sequencer. Busy, low, C major across two octaves,
//                       short envelopes. This is the module's identity and it is
//                       unchanged from the original factory patch.
//   B (OUT 2 / OUT 4) — the ambient voice. One slowly falling ball, C pentatonic
//                       major up high, long soft envelopes, and the two thinning
//                       controls doing real work.
//
// Patching only OUT 1 therefore still gives exactly what the module has always
// given; OUT 2 is the demonstration that the same engine does slow, and that
// GRAVITY / DENSITY / SPACE are how you get there.
//
// Coupling is pre-set to a useful 60 % so that turning PROXIMITY up does
// something obvious straight away — and with the two containers set this far
// apart in character, it is also the most interesting thing to turn: A's busy
// strikes start shaking notes out of B's slow one.
LoadSaveParams LoadDefaultParams() {
    LoadSaveParams p;
    p.valid = VALID_MAGIC;

    for (int i = 0; i < NUM_CHANNELS; i++) {
        p.rootIndex[i] = 0; // C
        p.gateMode[i] = GateEnvelope;
        p.gateLevel[i] = 100;
        p.accent[i] = 0;
    }
    for (int i = 0; i < 2; i++) {
        p.grip[i] = 0.30f;
        p.freeHz[i] = 0.25f;
        p.reverse[i] = 0;
        p.pegMask[i] = 0xFFFF;
    }

    // ── A: the sequencer ─────────────────────────────────────────────────────
    p.noteMask[0] = 0x0AB5; // C major
    p.scaleIndex[0] = 1;    // Major
    p.rootOctave[0] = 0;    // from the bottom of the range up: C4–C6 at 0V NOTE C4
    p.spread[0] = 2;        // two octaves, evenly spaced
    p.bias[0] = 0;
    p.gravity[0] = 220.0f;
    p.bounce[0] = 0.72f;
    p.spin[0] = Spin8;
    p.balls[0] = 3;
    p.pegs[0] = 8;
    // Decay has to be shorter than the gap between notes or the gate never
    // returns to zero and stops being a gate. A container at these settings
    // fires roughly 6 times a second (~160 ms apart), so ~100 ms articulates
    // with room to spare.
    p.attackMs[0] = 0;
    p.decayMs[0] = 100;
    // No thinning on this side. The dense evolving stream is what the module IS,
    // and the same argument that keeps LOOP off at boot keeps these neutral here:
    // both are ways of *taming* the thing it does, and at least one container has
    // to be the thing itself.
    p.density[0] = 100;
    p.space[0] = SpaceOff;

    // ── B: the ambient voice ─────────────────────────────────────────────────
    // Every number here is doing one job, and between them they cover the three
    // controls a user reaches for to slow the module down.
    p.noteMask[1] = 0x0295; // C pentatonic major — C D E G A
    p.scaleIndex[1] = 9;    // Pentatonic major
    // A SUBSET of A's C major, deliberately. The two containers are running
    // independently and will drift into every possible alignment, so B's notes
    // have to be ones that cannot clash with A's whatever lands together. A
    // pentatonic has no semitone in it at all, which is what makes it sit under
    // a busy major sequence without ever fighting it.
    // Two octaves above A's root, so B's one octave sits on top of A's two
    // instead of inside them. Before ROOT carried an octave this had to be
    // approximated with BIAS, because both containers were centred in the same
    // range whatever their spread.
    p.rootOctave[1] = 0; // C4–C5 at 0V NOTE C4
    p.spread[1] = 1;     // one octave, high — well clear of A's two
    p.bias[1] = -30;
    // GRAVITY is the tempo. 20 is a speed scale of 0.30, so this container runs
    // at just under a third of A's, and with a single ball it speaks about once
    // every 1.5 s against A's six times a second.
    p.gravity[1] = 20.0f;
    p.bounce[1] = 0.45f; // damped, so it settles rather than rattling
    // SPIN stays slow on purpose: the rotating wall is an energy source that does
    // NOT scale with gravity, so a fast spin here would stir the container harder
    // than gravity pulls on it and undo the slowness. See Design.md §4.
    p.spin[1] = Spin16;
    p.balls[1] = 1;
    p.pegs[1] = 5; // one peg per pentatonic degree — the ring is the scale
    // Long and soft — the envelope the module could not previously hold at all,
    // since at A's density anything approaching a second never returns to zero.
    //
    // It works here only because SPACE puts a hard 1 s floor under the gap, and
    // attack + decay is sized against THAT floor rather than against the average:
    // 120 + 750 = 870 ms leaves 130 ms of guaranteed silence before the soonest
    // possible next note. Sizing it against the ~2.3 s average would look fine on
    // paper and clip on every close pair.
    p.attackMs[1] = 120;
    p.decayMs[1] = 750;
    // The two thinning controls, set visibly off their defaults — which is how a
    // user finds them on the page. SPACE 2 beats is that 1 s floor at the default
    // 120 BPM; DENSITY 85 drops the occasional note so the slow line wanders
    // rather than ticking.
    p.density[1] = 85;
    p.space[1] = Space2Beats;

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

    // C4 = 0 V, VCV Rack's convention and the common one on hardware VCOs.
    p.cvZeroOctave = GEN_CV_ZERO_OCTAVE_DEFAULT;

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
        p.rootOctave[i] = (uint8_t)channels[i].GetRootOctave();
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
        p.density[i] = containerParams[i].density;
        p.space[i] = containerParams[i].space;
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

    p.cvZeroOctave = (uint8_t)channels[0].GetCvZeroOctave(); // both channels agree
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
        // SPREAD first: it caps how high the root octave may sit, and loading
        // them the other way round would clamp a valid preset down an octave.
        channels[i].SetSpread(p.spread[i]);
        channels[i].SelectRootOctave((int)p.rootOctave[i]);
        channels[i].SetBias(p.bias[i]);
        channels[i].envelope.SetMode(p.gateMode[i]);
        channels[i].envelope.SetAttack(p.attackMs[i]);
        channels[i].envelope.SetDecay(p.decayMs[i]);
        channels[i].SetGateLevel(p.gateLevel[i]);
        channels[i].SetAccent(p.accent[i]);
        channels[i].SetCvZeroOctave((int)p.cvZeroOctave); // SetCvZeroOctave clamps
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
        containerParams[i].density = (uint8_t)constrain((int)p.density[i], 0, 100);
        containerParams[i].space =
            (uint8_t)constrain((int)p.space[i], 0, (int)NoteSpaceLength - 1);
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
