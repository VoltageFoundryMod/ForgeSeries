#pragma once

// jacks.hpp — what THIS module makes of the four output jacks.
//
// Not a pinout: the wiring is identical on every ForgeSeries board and lives in
// core/boardPinouts.hpp, which this pulls in. What belongs here is the module's
// own reading of those jacks — how many voices they form, which jack is CV and
// which is gate, and what to call them in the calibration wizard.
//
// This has to be a header, not something main.cpp defines. The app headers that
// need these (presetManager, menuHandlers, menuRender, ...) are compiled by
// THREE translation units per module: src/main.cpp, the unified firmware's
// app TU, and the VCV Rack engine. Only one of those is main.cpp.

#include "boardPinouts.hpp"

// ClockForge treats the four outputs as four independent jacks — no voice
// pairing, so no NUM_CHANNELS or OUT_CV/OUT_GATE.

// Names used by the calibration wizard (core/calibration.hpp).
static const char *const CAL_OUT_NAMES[NUM_OUTPUTS] = {"OUT1", "OUT2", "OUT3", "OUT4"};
