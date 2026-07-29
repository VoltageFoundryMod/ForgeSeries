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

// Two independent voices. NoteForge calls them quantizer channels and
// GravityForge calls them containers, but the jack layout is identical, and
// deliberately so: patch cables carry over between the two firmwares.
#define NUM_CHANNELS 2

// Jack 1 = CV 1, Jack 2 = CV 2, Jack 3 = GATE 1, Jack 4 = GATE 2.
#define OUT_CV(ch) (ch)                  // ch 0 -> jack 1, ch 1 -> jack 2
#define OUT_GATE(ch) (NUM_CHANNELS + ch) // ch 0 -> jack 3, ch 1 -> jack 4

// Names used by the calibration wizard (core/calibration.hpp).
static const char *const CAL_OUT_NAMES[NUM_OUTPUTS] = {"CV 1", "CV 2", "GATE1", "GATE2"};
