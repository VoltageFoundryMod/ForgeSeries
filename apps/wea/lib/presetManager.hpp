#pragma once

// presetManager.hpp — preset schema + functional layer (platform-agnostic).
//
// Platform storage (Save/Load/EEPROMInit) lives in storage.hpp, which includes
// this file. Keeping them separate means this one is used unchanged by the VCV
// Rack plugin, where "EEPROM" is a byte buffer inside the patch.

#include <Arduino.h>

#include "boardPinouts.hpp"
#include "calibrationData.hpp"
#include "clock.hpp"
#include "cvInputs.hpp"
#include "displayManager.hpp"
#include "outputs.hpp"
#include "params.hpp"
#include "scales.hpp"
#include "shiftreg.hpp"

extern WeavePair registers;
extern OutputBank outputs;
extern StepClock clockEngine;
extern Quantizer quantizer;
extern RegParams regParams[WEA_NUM_REGS];
extern GlobalParams globalParams;
extern int menuScreenTimeout;
extern uint16_t noteMask;
extern uint8_t scaleIndex;
extern uint8_t rootIndex;

int saveSlot = 0; // Active save slot (0 = auto-save/load at boot)

#define NUM_SLOTS 10
// Bump whenever the LoadSaveParams layout changes so older (incompatible)
// presets fall back to defaults instead of loading garbage into new fields.
// 0xE1: first layout.
// 0xE2: DIVIDE (steps per input pulse) became RATE (steps per beat) — the two
//       are not the same number and reading an old index would silently retune
//       every saved pattern.
// 0xE3: the RATE table gained "/6" to make it symmetric around x1, which shifts
//       every index below unity — a stored one would now name a different rate.
#define VALID_MAGIC 0xE3

struct LoadSaveParams {
    uint8_t valid; // VALID_MAGIC = valid data; any other = use defaults

    // THE PATTERNS THEMSELVES. Four bytes, and without them a preset restores a
    // machine that makes a different pattern — which for this module means it
    // restores nothing at all. Saving the pattern is the whole point, and it is
    // also the only way to keep one, since shortening LENGTH destroys the region
    // above it (Design.md §2).
    uint16_t reg[WEA_NUM_REGS];

    uint8_t length[WEA_NUM_REGS];
    uint8_t chance[WEA_NUM_REGS];

    uint8_t weave;
    uint8_t weaveDir;
    int8_t transpose;

    // The four output slots. No ROUTING index — it is recomputed from these
    // (Design.md §5), so it cannot go stale.
    uint8_t outSource[WEA_NUM_OUTS];
    uint8_t outType[WEA_NUM_OUTS];
    uint8_t outDepth[WEA_NUM_OUTS];
    uint8_t outRotate[WEA_NUM_OUTS];
    uint8_t outParam[WEA_NUM_OUTS];
    uint8_t outParam2[WEA_NUM_OUTS];

    // Scale. The mask is the source of truth; scale/root are remembered only so
    // the "load scale" helper reopens where you left it.
    uint16_t noteMask;
    uint8_t scaleIndex;
    uint8_t rootIndex;

    uint16_t bpm;
    uint8_t ppqn;
    uint8_t clockRate;

    uint8_t cvTarget[NUM_CV_INS];
    uint8_t cvDepth[NUM_CV_INS];

    int menuScreenTimeout;
};

// ── Factory defaults ─────────────────────────────────────────────────────────
// A patch that plays the moment a clock is patched: two voices in C minor, both
// registers 16 long, a little drift on each and no coupling — so the first thing
// WEAVE does is audibly bring the two together.
inline LoadSaveParams LoadDefaultParams() {
    LoadSaveParams p;
    p.valid = VALID_MAGIC;

    p.reg[0] = 0xACE1u;
    p.reg[1] = 0x1D87u;
    for (int i = 0; i < WEA_NUM_REGS; i++) {
        p.length[i] = 16;
        p.chance[i] = 25;
    }

    p.weave = 0;
    p.weaveDir = WeaveBoth;
    p.transpose = 0;

    for (int i = 0; i < WEA_NUM_OUTS; i++) {
        const OutSlot &s = ROUTING_TEMPLATES[RouteDuo][i];
        p.outSource[i] = s.source;
        p.outType[i] = s.type;
        p.outDepth[i] = s.depth;
        p.outRotate[i] = s.rotate;
        p.outParam[i] = s.param;
        p.outParam2[i] = s.param2;
    }

    p.noteMask = 0x0AD5; // C minor: C D D# F G G# A#
    p.scaleIndex = 2;    // Minor
    p.rootIndex = 0;     // C
    p.bpm = 120;
    p.ppqn = Ppqn4;
    p.clockRate = WEA_CLOCK_RATE_DEFAULT; // x1 — one step per beat, as set

    p.cvTarget[0] = CVWeave;
    p.cvTarget[1] = CVChanceBoth;
    p.cvDepth[0] = 0;
    p.cvDepth[1] = 0;

    p.menuScreenTimeout = 2;
    return p;
}

// Gather the live state into a blob. A parameter reachable from the menu but
// missing here is silently not persisted, and in Rack silently lost on patch
// reload.
inline LoadSaveParams CollectParams() {
    LoadSaveParams p;
    p.valid = VALID_MAGIC;

    for (int i = 0; i < WEA_NUM_REGS; i++) {
        p.reg[i] = registers.Reg((uint8_t)i).Value();
        p.length[i] = regParams[i].length;
        p.chance[i] = regParams[i].chance;
    }

    p.weave = globalParams.weave;
    p.weaveDir = globalParams.dir;
    p.transpose = globalParams.transpose;

    for (int i = 0; i < WEA_NUM_OUTS; i++) {
        const OutSlot &s = outputs.Slot(i);
        p.outSource[i] = s.source;
        p.outType[i] = s.type;
        p.outDepth[i] = s.depth;
        p.outRotate[i] = s.rotate;
        p.outParam[i] = s.param;
        p.outParam2[i] = s.param2;
    }

    p.noteMask = noteMask;
    p.scaleIndex = scaleIndex;
    p.rootIndex = rootIndex;

    p.bpm = (uint16_t)clockEngine.GetBpm();
    p.ppqn = (uint8_t)clockEngine.GetPpqn();
    p.clockRate = (uint8_t)clockEngine.GetRate();

    for (int i = 0; i < NUM_CV_INS; i++) {
        p.cvTarget[i] = cvTarget[i];
        p.cvDepth[i] = cvDepth[i];
    }

    p.menuScreenTimeout = menuScreenTimeout;
    return p;
}

// Rebuild the quantizer from the mask. Called whenever the mask changes and on
// every preset load.
inline void RebuildQuantizer() {
    bool notes[12];
    for (int i = 0; i < 12; i++) {
        notes[i] = (noteMask >> i) & 1;
    }
    quantizer.Build(notes);
}

inline void UpdateParameters(LoadSaveParams p) {
    for (int i = 0; i < WEA_NUM_REGS; i++) {
        registers.Reg((uint8_t)i).SetValue(p.reg[i]);
        regParams[i].length = (uint8_t)constrain((int)p.length[i], WEA_MIN_LENGTH,
                                                 WEA_MAX_LENGTH);
        regParams[i].chance = (uint8_t)constrain((int)p.chance[i], 0, 100);
        registers.Reg((uint8_t)i).SetLength(regParams[i].length);
    }

    globalParams.weave = (uint8_t)constrain((int)p.weave, 0, 100);
    globalParams.dir = (uint8_t)constrain((int)p.weaveDir, 0, (int)WeaveDirLength - 1);
    globalParams.transpose = (int8_t)constrain((int)p.transpose, -24, 24);

    for (int i = 0; i < WEA_NUM_OUTS; i++) {
        OutSlot &s = outputs.Slot(i);
        s.source = (uint8_t)constrain((int)p.outSource[i], 0, (int)RegSourceLength - 1);
        s.type = (uint8_t)constrain((int)p.outType[i], 0, (int)OutTypeLength - 1);
        s.depth = (uint8_t)constrain((int)p.outDepth[i], 1, WEA_MAX_DEPTH);
        s.rotate = (uint8_t)(p.outRotate[i] & 31);
        s.param = p.outParam[i];
        s.param2 = p.outParam2[i];
    }

    noteMask = p.noteMask ? p.noteMask : 0x0FFF;
    scaleIndex = (uint8_t)constrain((int)p.scaleIndex, 0, numScales - 1);
    rootIndex = (uint8_t)constrain((int)p.rootIndex, 0, 11);
    RebuildQuantizer();

    clockEngine.SetBpm((int)p.bpm);
    clockEngine.SetPpqn((int)p.ppqn);
    clockEngine.SetRate((int)p.clockRate);
    // IN 1 is always the clock on this module, so external mode is never off.
    clockEngine.SetExternal(true);

    for (int i = 0; i < NUM_CV_INS; i++) {
        cvTarget[i] = (uint8_t)constrain((int)p.cvTarget[i], 0, (int)CVTargetLength - 1);
        cvDepth[i] = (uint8_t)constrain((int)p.cvDepth[i], 0, 100);
    }

    menuScreenTimeout = constrain(p.menuScreenTimeout, 0, 4);
    static const unsigned long kTimeoutOpts[] = {0, 2000, 5000, 10000, 20000};
    displayMgr.SetMenuTimeout(kTimeoutOpts[menuScreenTimeout]);
}
