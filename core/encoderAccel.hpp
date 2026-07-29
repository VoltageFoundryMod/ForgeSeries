#pragma once

// encoderAccel.hpp — encoder rotation acceleration, shared by every module.
//
// Turning faster steps further, so a 0-100 parameter is reachable without
// twenty detents. This was byte-identical in all three firmwares and again in
// all three unified app TUs: six copies of the same twenty lines.
//
// Deliberately just the acceleration. What a detent *does* — move the menu
// cursor, edit a value, arm a live preview — is genuinely per-module and stays
// in each app's own handler.

#include <Arduino.h>

// `inline`: header-defined state, and the unified firmware links one TU per app
// plus the shell. Without it each TU carries a private copy of the timers.
inline float speedFactor = 1.0f;
inline unsigned long lastEncoderTime = 0;
inline int lastEncoderDir = 0; // +1 / -1, 0 = nothing yet

// Call once per acted-on detent, with its direction.
inline void UpdateSpeedFactor(int dir) {
    const unsigned long now = millis();
    const unsigned long timeDiff = now - lastEncoderTime;
    lastEncoderTime = now;

    // Reversing always restarts at a single step, so a turn-around never
    // overshoots by eight.
    if (lastEncoderDir != 0 && dir != lastEncoderDir) {
        speedFactor = 1.0f;
        lastEncoderDir = dir;
        return;
    }
    lastEncoderDir = dir;

    if (timeDiff < 30) {
        speedFactor = 8.0f; // very fast spin
    } else if (timeDiff < 60) {
        speedFactor = 4.0f;
    } else if (timeDiff < 120) {
        speedFactor = 2.0f;
    } else {
        speedFactor = 1.0f; // normal
    }
}
