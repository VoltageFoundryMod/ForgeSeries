#ifdef UNIT_TEST
#include "ArduinoFake.h"
#else
#include "Arduino.h"
#endif

#include "boardIO.hpp"
#include <math.h>

// 1V/oct over a 0-5V, 12-bit range: 60 semitones span the full DAC scale, so
// one semitone = MAXDAC/60 = 68.25 counts.  Note 0 = 0V, note 60 = 5V.
static const float QUANT_COUNTS_PER_SEMITONE = (float)MAXDAC / 60.0f;

// Build the list of active note voltages (in DAC counts) across the whole
// 0..MAXDAC (0-5V) range.
// Inputs:
//   note: array of 12 booleans, one per pitch class (C..B); 1 = note active.
// Outputs:
//   buff: filled in ascending order with each active note's voltage in counts.
//         Must hold at least 61 ints.
// Returns: the number of entries written (1..61, or 0 if no notes are active).
int BuildQuantBuffer(const bool note[], int buff[]) {
    int k = 0;
    for (int j = 0; j <= 60; j++) { // semitone 0 (0V) .. 60 (5V) inclusive
        if (note[j % 12]) {
            buff[k++] = (int)lroundf(j * QUANT_COUNTS_PER_SEMITONE);
        }
    }
    return k;
}

// Identify the closest pitch class (0..11, C..B) to a voltage in DAC counts.
void GetNote(float CV_OUT, int *note_index) {
    int semitone = (int)lroundf(CV_OUT / QUANT_COUNTS_PER_SEMITONE);
    *note_index = ((semitone % 12) + 12) % 12;
}

// Quantize an input value to the nearest active note.
// Inputs:
//   in          : value to quantize, in DAC counts (0..MAXDAC).
//   prevOut     : previously emitted value (counts) — used for hysteresis.
//   buff, count : active note voltages and their number (from BuildQuantBuffer).
//   sensitivity : pitch tracking amount, 0..8 (4 = unity / true 1V-oct).
//   oct         : octave transpose index, 0..6 (3 = no shift).
// Output:
//   *out        : the quantized value (counts), clamped to 0..MAXDAC.
//
// The result is always written.  A small hysteresis band holds the previously
// selected note until the input moves past the midpoint to the neighbouring
// note, preventing boundary chatter from a noisy input.
void QuantizeCV(float in, float prevOut, const int buff[], int count,
                int sensitivity, int oct, float *out) {
    // No active notes: pass the input through, clamped to range.
    if (count <= 0) {
        *out = constrain(in, 0.0f, (float)MAXDAC);
        return;
    }

    // Apply sensitivity (pitch tracking amount) and clamp to the input range.
    in = in * (16 + sensitivity) / 20.0f;
    in = constrain(in, 0.0f, (float)MAXADC);

    // Octave transpose, applied after note selection.
    float octOffset = (float)(oct - 3) * 12.0f * QUANT_COUNTS_PER_SEMITONE;

    // Find the active note nearest to `in` (full scan; buff[] is ascending).
    int best = 0;
    float bestDist = fabsf(in - (float)buff[0]);
    for (int i = 1; i < count; i++) {
        float d = fabsf(in - (float)buff[i]);
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    float chosen = (float)buff[best];

    // Hysteresis: if the input is still within ~0.1 semitone of the midpoint
    // toward the previously held note, keep that note to avoid chatter at the
    // boundary.  (If the previous note is no longer active — e.g. the scale or
    // octave changed — no match is found and the fresh nearest note is used.)
    const float HYST = QUANT_COUNTS_PER_SEMITONE * 0.10f;
    float prevNote = prevOut - octOffset;
    for (int i = 0; i < count; i++) {
        if (fabsf((float)buff[i] - prevNote) < 0.5f) { // prevNote == active note i
            if (fabsf(in - (float)buff[i]) <= bestDist + HYST) {
                chosen = (float)buff[i];
            }
            break;
        }
    }

    *out = constrain(chosen + octOffset, 0.0f, (float)MAXDAC);
}
