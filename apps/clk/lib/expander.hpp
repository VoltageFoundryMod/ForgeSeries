#pragma once

// expander.hpp — how much hardware ClockForge is driving right now.
//
// Expander 1 adds four outputs and one CV input (see core/boardPinouts.hpp).
// Whether one is fitted is a SETTING, not a build flag: there is one firmware
// image, one preset schema and one set of arrays, all sized to NUM_MAX_*, and
// these two functions say how much of that is live. Everything that iterates
// outputs or CV inputs asks here rather than using NUM_OUTPUTS / NUM_CV_INS,
// which keep meaning the base board's own jacks.
//
// The cost of always compiling for eight is about 3 KB of RAM — Output is a
// fat object, mostly its Euclidean pattern and quantizer note buffers. The
// alternative was a second build flavour and a second preset schema to keep in
// step with the first, which is a worse trade at this size.
//
// Included before outputs.hpp and cvInputs.hpp, both of which need the counts.

#include "boardPinouts.hpp"

// 0 = none, 1 = Expander 1. Defined by the host (src/clk_app.cpp on hardware,
// fw_engine.cpp under Rack) and persisted with the preset.
extern int expanderType;

// Is an expander selected? Note this is the user's setting, not a probe: on
// hardware core/boardIO.hpp's ProbeExpander() says whether one actually
// answered on the bus, and under Rack presence comes from module adjacency.
// The setting is what the engine follows, so a patch behaves the same way a
// rack does when the ribbon is unplugged — the outputs simply go nowhere.
static inline bool ExpanderFitted() { return expanderType != 0; }

// How many outputs / CV inputs are live. Indices below these are safe to use
// against any NUM_MAX_*-sized array.
static inline int ActiveOutputs() {
    return ExpanderFitted() ? NUM_MAX_OUTPUTS : NUM_OUTPUTS;
}
static inline int ActiveCvIns() {
    return ExpanderFitted() ? NUM_MAX_CV_INS : NUM_CV_INS;
}
