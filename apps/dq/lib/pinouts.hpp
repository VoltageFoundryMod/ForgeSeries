#pragma once

// pinouts.hpp — board wiring, plus this app's jack semantics.
//
// The wiring itself is shared by every ForgeSeries module and lives in
// core/boardPinouts.hpp. What follows is what *this* module makes of it.

#include "boardPinouts.hpp"

// Two independent voices. NoteForge calls them quantizer channels and
// GravityForge calls them containers, but the jack layout is the same, and
// deliberately so: patch cables carry over between the two firmwares.
#define NUM_CHANNELS 2

// Output jack assignment (index into the DAC/output arrays).
// Jack 1 = CV 1, Jack 2 = CV 2, Jack 3 = GATE 1, Jack 4 = GATE 2.
#define OUT_CV(ch) (ch)                  // ch 0 -> jack 1, ch 1 -> jack 2
#define OUT_GATE(ch) (NUM_CHANNELS + ch) // ch 0 -> jack 3, ch 1 -> jack 4

// What this module calls its output jacks, used by the calibration wizard
// (core/calibration.hpp). Jack naming is module semantics, so it lives here
// with NUM_CHANNELS and OUT_CV/OUT_GATE rather than in the shared wizard.
static const char *const CAL_OUT_NAMES[NUM_OUTPUTS] = {"CV 1", "CV 2", "GATE1", "GATE2"};
