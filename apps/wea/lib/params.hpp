#pragma once

// params.hpp — what the user set, what the CV added, and what the engine runs.
//
// Three separate things, and conflating any two of them is the bug this split
// exists to prevent:
//
//   RegParams / GlobalParams — the menu values. Saved to presets, only ever
//                              changed by the encoder or a preset load.
//   ModBus                   — this loop's CV contribution, rebuilt from
//                              scratch every pass.
//   LiveParams               — the sum, clamped to legal ranges. What the
//                              engine actually clocks with.
//
// Writing CV straight into the menu values would mean an unplugged cable leaves
// the module wherever the voltage happened to be, and a preset save would store
// the modulated value rather than the setting.

#ifdef UNIT_TEST
#include "ArduinoFake.h"
#else
#include <Arduino.h>
#endif

#include <stdint.h>

#include "shiftreg.hpp"

#define WEA_NUM_REGS 2

// ── The menu values ──────────────────────────────────────────────────────────
struct RegParams {
    uint8_t length = 16; // 2..16, the feedback point
    uint8_t chance = 25; // 0..100 %, see Design.md §2 for the geometry
};

struct GlobalParams {
    uint8_t weave = 0;         // 0..100 %
    uint8_t dir = WeaveBoth;   // BOTH / A>B / B>A
    int8_t transpose = 0;      // semitones applied to every NOTE output
};

// ── This loop's CV ───────────────────────────────────────────────────────────
struct ModBus {
    float length[WEA_NUM_REGS];
    float chance[WEA_NUM_REGS];
    float weave;
    float transpose;
    float rotate; // added to every output's ROTATE — moves all taps together
    bool reset;
    bool lock; // ShiftReg's Digital-2 trick: hold the pattern while high

    void Clear() {
        for (int i = 0; i < WEA_NUM_REGS; i++) {
            length[i] = 0.0f;
            chance[i] = 0.0f;
        }
        weave = 0.0f;
        transpose = 0.0f;
        rotate = 0.0f;
        reset = false;
        lock = false;
    }
};

// ── What the engine clocks with ──────────────────────────────────────────────
struct LiveParams {
    uint8_t length[WEA_NUM_REGS];
    uint8_t chance[WEA_NUM_REGS];
    uint8_t weave;
    uint8_t dir;
    int8_t transpose;
    uint8_t rotate;
};

static inline uint8_t WeaClampPercent(float v) {
    if (v < 0.0f) {
        return 0;
    }
    if (v > 100.0f) {
        return 100;
    }
    return (uint8_t)lroundf(v);
}

// Fold the menu values and the CV together, clamp, and push what belongs to the
// registers into them. Called once a loop, before the clock is serviced — so a
// step that lands this pass already sees this pass's modulation.
inline void ApplyParams(WeavePair &pair, const RegParams reg[WEA_NUM_REGS],
                        const GlobalParams &g, const ModBus &mod,
                        LiveParams &live) {
    for (int i = 0; i < WEA_NUM_REGS; i++) {
        float len = (float)reg[i].length + mod.length[i];
        live.length[i] = (uint8_t)constrain((int)lroundf(len), WEA_MIN_LENGTH,
                                            WEA_MAX_LENGTH);

        // LOCK wins over everything, including a CHANCE knob at 100 %. It is the
        // "hold what you have" input, and an input that only mostly holds is not
        // one you would ever patch.
        live.chance[i] = mod.lock ? 0 : WeaClampPercent((float)reg[i].chance +
                                                        mod.chance[i]);

        pair.Reg((uint8_t)i).SetLength(live.length[i]);
    }

    live.weave = WeaClampPercent((float)g.weave + mod.weave);
    live.dir = g.dir;
    live.transpose =
        (int8_t)constrain((int)lroundf((float)g.transpose + mod.transpose), -24, 24);
    live.rotate = (uint8_t)constrain((int)lroundf(mod.rotate), 0, 31);
}
