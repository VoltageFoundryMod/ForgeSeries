#pragma once

// Shared by NoteForge and GravityForge. The two copies this replaces were
// identical apart from GravityForge's DegreesPerOctave() and SemitoneAt(),
// which NoteForge simply does not call — so this is GravityForge's version,
// unchanged.
//
// ClockForge is NOT a consumer: its quantizer is a separate implementation in
// apps/clk/lib/quantizer.cpp, reached through Output rather than a channel, and
// folding it in would be a port rather than a merge.


// quantizer.hpp — Pitch quantization.
//
// Everything here works in *semitones* rather than raw DAC counts. The hardware
// spans 0–5V over a 12-bit range, and 1V/oct means 5V = 5 octaves = 60
// semitones, so one semitone is MAXDAC/60 counts. Working in semitones keeps
// the maths readable and makes the note/octave display fall out for free.
//
// The quantizer holds a sorted table of the semitones enabled by a channel's
// 12-note mask, replicated across the full 0–60 range, and snaps an incoming
// (fractional) semitone to the nearest entry. A hysteresis band around the
// midpoint between the held note and its neighbour stops the output chattering
// when the input CV sits exactly on a boundary — the single most audible flaw
// of a naive nearest-note quantizer.

#ifdef UNIT_TEST
#include "ArduinoFake.h"
#else
#include <Arduino.h>
#endif

#include <math.h>

// MAXDAC, used by QUANT_COUNTS_PER_SEMITONE below. Included here rather than
// left to the caller for the reason boardPinouts.hpp itself documents: relying
// on the includer to have defined it is a compile error waiting to happen, and
// it is one the native test build hits.
#include "boardPinouts.hpp"

// Output span: 0–5V at 1V/oct = 5 octaves.
#define QUANT_OCTAVES 5
#define QUANT_MAX_SEMITONE (QUANT_OCTAVES * 12) // 60 — inclusive top of range
#define QUANT_SEMITONE_COUNT (QUANT_MAX_SEMITONE + 1)

// Counts per semitone across the output range (MAXDAC / 60 = 68.25 at 12 bit).
#define QUANT_COUNTS_PER_SEMITONE ((float)MAXDAC / (float)QUANT_MAX_SEMITONE)

// Default hysteresis, in semitones, applied past the note boundary before the
// output moves on. 0.15 ≈ 10 mV at 1V/oct — comfortably above ADC noise while
// staying far below the 0.5 semitone that would make notes unreachable.
#define QUANT_HYSTERESIS 0.15f

class Quantizer {
    int8_t _table[QUANT_SEMITONE_COUNT]; // active semitones, ascending
    int _count = 0;
    bool _active[12] = {false};

  public:
    Quantizer() { BuildChromatic(); }

    // Rebuild the lookup from a 12-note mask (index 0 = C).
    void Build(const bool notes[12]) {
        _count = 0;
        bool any = false;
        for (int i = 0; i < 12; i++) {
            _active[i] = notes[i];
            any |= notes[i];
        }
        // An empty mask has no musically sensible answer. Falling back to
        // chromatic keeps the outputs tracking the input instead of freezing on
        // whatever note happened to be held when the last key was switched off.
        if (!any) {
            BuildChromatic();
            return;
        }
        for (int s = 0; s <= QUANT_MAX_SEMITONE; s++) {
            if (_active[s % 12]) {
                _table[_count++] = (int8_t)s;
            }
        }
    }

    void BuildChromatic() {
        for (int i = 0; i < 12; i++) {
            _active[i] = true;
        }
        _count = 0;
        for (int s = 0; s <= QUANT_MAX_SEMITONE; s++) {
            _table[_count++] = (int8_t)s;
        }
    }

    int ActiveCount() const { return _count; }

    // How many enabled notes there are per octave — 7 for a major scale, 5 for a
    // pentatonic, 12 for chromatic. The gravity sequencer needs this to turn a
    // SPREAD expressed in octaves into a number of scale degrees, since a degree
    // is worth a different interval in every scale. Never returns 0, so it is
    // always safe to divide by.
    int DegreesPerOctave() const {
        int n = 0;
        for (int i = 0; i < 12; i++) {
            if (_active[i]) {
                n++;
            }
        }
        return n > 0 ? n : 1;
    }

    // The semitone at position `index` in the enabled-note table, clamped to the
    // ends. This is how the gravity sequencer maps a peg to a pitch: stepping
    // along enabled degrees rather than semitones means every peg lands in the
    // scale, and the interval between adjacent pegs follows the scale — a third
    // in major, something else in pentatonic. Clamping rather than wrapping
    // matches TransposeDegrees(): wrapping would jump octaves for a one-peg
    // change.
    int SemitoneAt(int index) const {
        if (_count == 0) {
            return 0;
        }
        return _table[constrain(index, 0, _count - 1)];
    }

    // Transpose by whole scale degrees rather than semitones.
    //
    // Semitone transposition would push notes out of the scale and re-snapping
    // then collapses neighbours onto the same pitch. Stepping along the enabled
    // note table instead always lands on a note the scale contains, and the
    // interval follows the scale — +2 degrees is a third in a major scale and
    // something else entirely in a pentatonic, which is the musical point.
    //
    // Transposition past either end of the table clamps rather than wrapping:
    // wrapping would jump several octaves for a one-degree change in CV.
    int TransposeDegrees(int semitone, int degrees) const {
        if (_count == 0 || degrees == 0) {
            return semitone;
        }
        int idx = IndexOf(semitone);
        if (idx < 0) {
            return semitone;
        }
        return _table[constrain(idx + degrees, 0, _count - 1)];
    }

    // Position of `semitone` within the enabled note table, or the nearest entry
    // if it is not itself enabled. -1 when the table is empty.
    int IndexOf(int semitone) const {
        if (_count == 0) {
            return -1;
        }
        int best = 0;
        int bestDistance = semitone - _table[0];
        if (bestDistance < 0) {
            bestDistance = -bestDistance;
        }
        for (int i = 1; i < _count; i++) {
            int d = semitone - _table[i];
            if (d < 0) {
                d = -d;
            }
            if (d < bestDistance) {
                bestDistance = d;
                best = i;
            }
        }
        return best;
    }

    // Is `semitone` one of the notes this quantizer can emit?
    bool Emits(int semitone) const {
        if (semitone < 0 || semitone > QUANT_MAX_SEMITONE) {
            return false;
        }
        return _active[semitone % 12];
    }

    // Snap `semitoneIn` (fractional) to the nearest enabled semitone.
    //
    // `lastOut` is the previously emitted semitone (-1 when there is none). When
    // the nearest note differs from it, the input must travel past the midpoint
    // between the two by `hysteresis` semitones before the change is accepted.
    int Quantize(float semitoneIn, int lastOut,
                 float hysteresis = QUANT_HYSTERESIS) const {
        if (_count == 0) {
            return constrain((int)lroundf(semitoneIn), 0, QUANT_MAX_SEMITONE);
        }
        int best = _table[0];
        float bestDistance = fabsf(semitoneIn - (float)_table[0]);
        for (int i = 1; i < _count; i++) {
            float d = fabsf(semitoneIn - (float)_table[i]);
            if (d < bestDistance) {
                bestDistance = d;
                best = _table[i];
            }
        }
        // Hold the previous note until the input clears the boundary band. Only
        // applies while the previous note is still emittable — a scale change
        // must be free to move the output immediately.
        if (lastOut >= 0 && lastOut != best && Emits(lastOut)) {
            float midpoint = ((float)lastOut + (float)best) * 0.5f;
            if (best > lastOut ? semitoneIn < midpoint + hysteresis
                               : semitoneIn > midpoint - hysteresis) {
                return lastOut;
            }
        }
        return best;
    }
};

// ── Unit conversions ─────────────────────────────────────────────────────────

// DAC counts (0..MAXDAC == 0..5V) → fractional semitones.
inline float CountsToSemitones(float counts) {
    return counts / QUANT_COUNTS_PER_SEMITONE;
}

// Semitones → DAC counts, clamped to the output range.
inline float SemitonesToCounts(float semitones) {
    float counts = semitones * QUANT_COUNTS_PER_SEMITONE;
    return constrain(counts, 0.0f, (float)MAXDAC);
}

// Pitch class (0=C … 11=B) of a semitone offset from the bottom of the range.
inline int SemitoneToNoteIndex(int semitone) {
    return ((semitone % 12) + 12) % 12;
}

// Octave number of a semitone offset. The bottom of the 0–5V range is octave 0.
inline int SemitoneToOctave(int semitone) {
    return semitone / 12;
}
