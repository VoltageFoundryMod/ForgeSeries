#pragma once

// randomize.hpp — "roll me a new patch" (platform-agnostic)
//
// One implementation shared by the hardware menu's RANDOM action and the VCV
// Rack plugin's Randomize, so the module behaves identically on the panel and in
// the host.
//
// What it deliberately does NOT touch: the output matrix, the CV modulation
// routing, the scale, the clock and the screen timeout. Those are patch wiring
// and decisions you made on purpose — rerolling them turns "give me a new
// phrase" into "break my patch", which is the line VCV Rack itself draws when it
// leaves ports alone on randomize.
//
// What it DOES touch is the two registers and the three controls that shape
// them, because on this module that IS the patch: the pattern, how long it is,
// how fast it drifts and how much the two halves share.

#include "params.hpp"
#include "shiftreg.hpp"

extern WeavePair registers;
extern RegParams regParams[WEA_NUM_REGS];
extern GlobalParams globalParams;

static WeaveRandom _rndGen;

inline void RandomizeParams(uint32_t seed) {
    _rndGen.Seed(seed);

    for (int i = 0; i < WEA_NUM_REGS; i++) {
        registers.Reg((uint8_t)i).Randomize(_rndGen);

        // Lengths from the musically useful end of the range. 2 and 3 are
        // legal and occasionally wanted, but a rolled patch that lands on a
        // two-step loop reads as the randomiser having failed.
        regParams[i].length = (uint8_t)(4 + (_rndGen.Next() % 13)); // 4..16

        // CHANCE away from both endpoints. 0 would roll a patch that never
        // changes and 100 one that inverts every step — both are things you
        // choose deliberately, not things you want to be handed.
        regParams[i].chance = (uint8_t)(5 + (_rndGen.Next() % 56)); // 5..60

        registers.Reg((uint8_t)i).SetLength(regParams[i].length);
    }

    globalParams.weave = (uint8_t)(_rndGen.Next() % 101);
    globalParams.dir = (uint8_t)(_rndGen.Next() % WeaveDirLength);
}
