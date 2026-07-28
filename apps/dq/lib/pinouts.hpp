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
