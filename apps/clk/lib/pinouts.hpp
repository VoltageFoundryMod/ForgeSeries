#pragma once

// pinouts.hpp — board wiring, plus this app's jack semantics.
//
// The wiring itself is shared by every ForgeSeries module and lives in
// core/boardPinouts.hpp. This module treats the four outputs as four
// independent jacks and adds nothing to it.

#include "boardPinouts.hpp"

// What this module calls its output jacks, used by the calibration wizard
// (core/calibration.hpp). Jack naming is module semantics, so it lives here
// with NUM_CHANNELS and OUT_CV/OUT_GATE rather than in the shared wizard.
static const char *const CAL_OUT_NAMES[NUM_OUTPUTS] = {"OUT1", "OUT2", "OUT3", "OUT4"};
