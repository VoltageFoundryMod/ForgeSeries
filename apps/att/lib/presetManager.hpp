#pragma once

// presetManager.hpp — preset schema + functional layer (platform-agnostic)
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

#include "attractors.hpp"
#include "boardPinouts.hpp"
#include "calibrationData.hpp"
#include "displayManager.hpp"
#include "generator.hpp"
#include "params.hpp"

extern GenParams genParams[2];       // src/att_app.cpp
extern WorldParams worldParams;      // src/att_app.cpp
extern int menuScreenTimeout;        // src/att_app.cpp
extern uint8_t homeView;             // src/att_app.cpp
extern uint8_t in1Role;              // lib/cvInputs.hpp
extern uint8_t cvTarget[NUM_CV_INS]; // lib/cvInputs.hpp
extern uint8_t cvDepth[NUM_CV_INS];  // lib/cvInputs.hpp

// ── Preset schema ─────────────────────────────────────────────────────────────
// Slot 0 = auto-load/save on boot; slots 1..(NUM_SLOTS-1) = user presets.
#define NUM_SLOTS 10

// Bump whenever the LoadSaveParams layout changes so older (incompatible)
// presets fall back to defaults instead of loading garbage into new fields.
// 0xA1: first ChaosForge layout.
#define VALID_MAGIC 0xA1 // 0xFF = erased flash, 0x00 = zeroed RAM

struct LoadSaveParams {
    uint8_t valid; // VALID_MAGIC = valid data; any other = use defaults

    // ── Per generator ──
    uint8_t system[2];
    float param[2][ATT_MAX_PARAMS];
    float speed[2];
    uint8_t src[2][2]; // [generator][jack] -> axis
    uint8_t level[2];
    int8_t offset[2];
    uint8_t smooth[2];
    uint8_t autoRange[2];

    // ── World ──
    float couple;

    // ── Routing ──
    uint8_t in1Role;
    uint8_t cvTarget[NUM_CV_INS];
    uint8_t cvDepth[NUM_CV_INS];

    // ── UI ──
    uint8_t homeView;
    int menuScreenTimeout;
};

// ── Factory defaults ──────────────────────────────────────────────────────────
// Boots to something useful with nothing patched, and — because the two
// generators are completely independent at COUPLE 0 — it boots to TWO worked
// examples rather than one:
//
//   A (OUT 1 / OUT 2) — Lorenz X and Y at 1.00x. The iconic double wing, and the
//                       module's identity: a pair of voltages that circle each
//                       other for minutes without ever repeating. Fast enough to
//                       watch on the screen and to hear as motion.
//   B (OUT 3 / OUT 4) — Rössler Y and Z at 0.30x with a little SMOOTH. The other
//                       end of the range: a slow, almost-flat drift with an
//                       occasional sharp fold on Z, which is the closest thing
//                       the module has to an event output.
//
// So the first patch cable already demonstrates both what this module does that
// an LFO cannot, and that it will also sit still for a whole phrase.
//
// COUPLE starts at 0. It is the signature control, but it is also the one that
// makes the four outputs stop being independent — booting with it up would hide
// the module's default proposition, and it is one page away.
LoadSaveParams LoadDefaultParams() {
    LoadSaveParams p;
    p.valid = VALID_MAGIC;

    // ── A: the double wing ──
    p.system[0] = AttLorenz;
    for (int k = 0; k < ATT_MAX_PARAMS; k++)
        p.param[0][k] = (k < AttSpec(AttLorenz).paramCount) ? AttSpec(AttLorenz).params[k].def : 0.0f;
    p.speed[0] = 5.0f;
    p.src[0][0] = AxisX;
    p.src[0][1] = AxisY;
    p.level[0] = 100;
    p.offset[0] = 0;
    p.smooth[0] = 0;
    p.autoRange[0] = 0;

    // ── B: the slow fold ──
    p.system[1] = AttRossler;
    for (int k = 0; k < ATT_MAX_PARAMS; k++)
        p.param[1][k] = (k < AttSpec(AttRossler).paramCount) ? AttSpec(AttRossler).params[k].def : 0.0f;
    // Half of A's rate: about one circuit of the spiral every twelve seconds,
    // against A's four-second wing. Slow enough to read as a drift rather than a
    // wobble, quick enough that the plot draws a figure while you watch.
    p.speed[1] = 8.0f;
    p.src[1][0] = AxisX;
    p.src[1][1] = AxisY;
    p.level[1] = 100;
    p.offset[1] = 0;
    // Just enough lag to round the leading edge of the z fold. Any more and the
    // fold — the one fast event this side has — stops being one.
    p.smooth[1] = 15;
    p.autoRange[1] = 0;

    p.couple = 0.0f;

    p.in1Role = In1Reset;
    p.cvTarget[0] = CVSpeedBoth;
    p.cvTarget[1] = CVCouple;
    p.cvDepth[0] = 0;
    p.cvDepth[1] = 0;

    p.homeView = 0;          // both plots
    p.menuScreenTimeout = 2; // 5 s
    return p;
}

// ── Globals ───────────────────────────────────────────────────────────────────
int saveSlot = 0; // Active save slot (0 = auto-save/load at boot)

// ─────────────────────────────────────────────────────────────────────────────
// Gather all current state into a LoadSaveParams snapshot.
//
// Note this reads the *base* parameter block, never the live Generator: the
// generator is carrying whatever CV modulation is applied this instant, and
// saving that would bake a moving CV into the preset.
// ─────────────────────────────────────────────────────────────────────────────
LoadSaveParams CollectParams() {
    LoadSaveParams p;
    p.valid = VALID_MAGIC;

    for (int i = 0; i < 2; i++) {
        p.system[i] = genParams[i].system;
        for (int k = 0; k < ATT_MAX_PARAMS; k++)
            p.param[i][k] = genParams[i].param[k];
        p.speed[i] = genParams[i].speed;
        p.src[i][0] = genParams[i].src[0];
        p.src[i][1] = genParams[i].src[1];
        p.level[i] = genParams[i].level;
        p.offset[i] = genParams[i].offset;
        p.smooth[i] = genParams[i].smooth;
        p.autoRange[i] = genParams[i].autoRange;
    }

    p.couple = worldParams.couple;

    p.in1Role = in1Role;
    for (int i = 0; i < NUM_CV_INS; i++) {
        p.cvTarget[i] = cvTarget[i];
        p.cvDepth[i] = cvDepth[i];
    }

    p.homeView = homeView;
    p.menuScreenTimeout = menuScreenTimeout;
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// Apply a LoadSaveParams snapshot to all subsystem state.
//
// Every field is re-clamped on the way in. A preset written by an older schema
// is rejected by VALID_MAGIC, but a preset written by THIS schema can still
// carry a system index that a later build reordered, or a parameter that is
// legal for a different system — and an out-of-range parameter is the one thing
// that can leave a system with no bounded attractor at all.
// ─────────────────────────────────────────────────────────────────────────────
void UpdateParameters(LoadSaveParams p) {
    for (int i = 0; i < 2; i++) {
        genParams[i].system = AttClampId(p.system[i]);
        const AttractorSpec &sp = AttSpec(genParams[i].system);
        for (int k = 0; k < ATT_MAX_PARAMS; k++) {
            genParams[i].param[k] =
                (k < sp.paramCount)
                    ? constrain(p.param[i][k], sp.params[k].min, sp.params[k].max)
                    : 0.0f;
        }
        genParams[i].speed = constrain(p.speed[i], ATT_SPEED_MIN, ATT_SPEED_MAX);
        genParams[i].src[0] = (uint8_t)constrain((int)p.src[i][0], 0, (int)AxisCount - 1);
        genParams[i].src[1] = (uint8_t)constrain((int)p.src[i][1], 0, (int)AxisCount - 1);
        genParams[i].level = (uint8_t)constrain((int)p.level[i], 0, ATT_LEVEL_MAX);
        genParams[i].offset =
            (int8_t)constrain((int)p.offset[i], -ATT_OFFSET_MAX, ATT_OFFSET_MAX);
        genParams[i].smooth = (uint8_t)constrain((int)p.smooth[i], 0, ATT_SMOOTH_MAX);
        genParams[i].autoRange = p.autoRange[i] ? 1 : 0;
    }

    worldParams.couple = constrain(p.couple, 0.0f, 1.0f);

    in1Role = (uint8_t)constrain((int)p.in1Role, 0, (int)In1RoleLength - 1);
    for (int i = 0; i < NUM_CV_INS; i++) {
        cvTarget[i] = (uint8_t)constrain((int)p.cvTarget[i], 0, (int)CVTargetLength - 1);
        cvDepth[i] = (uint8_t)constrain((int)p.cvDepth[i], 0, 100);
    }

    homeView = (uint8_t)constrain((int)p.homeView, 0, 2);
    menuScreenTimeout = constrain(p.menuScreenTimeout, 0, 4);
    static const unsigned long kTimeoutOpts[] = {0, 2000, 5000, 10000, 20000};
    displayMgr.SetMenuTimeout(kTimeoutOpts[menuScreenTimeout]);
}
