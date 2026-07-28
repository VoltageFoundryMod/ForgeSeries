#pragma once

// cvInput.hpp — CV acquisition and range adaptation for the ForgeSeries board.
//
// Every module reads the same two analog CV jacks through the same ADC with the
// same two-point calibration, so acquisition lives here once. What each app
// *does* with the value stays in its own cvInputs.hpp.
//
// ── The unit: normalised float ──────────────────────────────────────────────
// A reading is a float where 1.0 means +5 V at the jack, on every hardware
// revision:
//
//     unipolar (0..+5 V)   CvRead() returns  0.0 .. 1.0
//     bipolar  (-5..+5 V)  CvRead() returns -1.0 .. 1.0,  0 V == 0.0
//
// Normalised rather than ADC counts because a count is meaningless without also
// knowing MAXADC and the polarity convention. Presets store these values, so a
// count would silently change meaning the moment either did — and both are
// expected to (a +/-5 V revision is planned, and MAXDAC moves if the DAC is ever
// upgraded). A normalised 0.5 survives all of it.
//
// Cost is nil in practice: the RP2040 is Cortex-M0+ with no FPU, so float is
// soft-emulated, but the CV path was already float (calibration is a float
// multiply-add, and the smoothing filters are float). SCP's FFT is the one
// genuinely hot path and stays fixed-point in fixfft.
//
// Deliberately NOT normalised elsewhere:
//   * pitch is carried in semitones (see CvSemitones) — 1 V/oct is semantically
//     a voltage, and expressing C4 as 0.4 helps nobody;
//   * DAC output stays in counts, converted at the write in boardIO.hpp, which
//     is the one place MAXDAC belongs.
//
// ── Polarity ────────────────────────────────────────────────────────────────
//     build_flags = -DFORGE_CV_BIPOLAR      ; -5..+5 V hardware
//     (unset)                               ; 0..+5 V hardware (default)
//
// Note the flag barely appears below. Once calibration has mapped raw counts to
// millivolts, dividing by CV_FULLSCALE_MV yields the correct normalised value on
// BOTH revisions — the coefficients captured on bipolar hardware simply produce
// negative millivolts near 0 V. Only the uncalibrated fallback and the two
// rescaling adapters need to know.

#include "boardPinouts.hpp"
#include "calibrationData.hpp"
#include <Arduino.h>

// Defined by each app in its main.cpp; loaded from storage (and, once the
// unified firmware lands, owned by the shell and shared by every app).
extern CalibrationData cal;

// Millivolts at the jack corresponding to a normalised 1.0. Both revisions peak
// at +5 V; they differ only in how far *down* they go.
#define CV_FULLSCALE_MV 5000.0f

// Lowest value CvRead() can return on this hardware.
#ifdef FORGE_CV_BIPOLAR
#define CV_MIN_NORM (-1.0f)
#else
#define CV_MIN_NORM (0.0f)
#endif
#define CV_NORM_SPAN (1.0f - CV_MIN_NORM)

// Oversampling depth for a single CV read. The RP2040 ADC is noisy enough that
// a single conversion visibly jitters a quantizer a semitone at the top of its
// range. Apps may override before including — as a macro, so this #ifndef sees
// it (a constexpr would not).
#ifndef CV_OVERSAMPLE_SAMPLES
#define CV_OVERSAMPLE_SAMPLES 8
#endif

// ── Acquisition ─────────────────────────────────────────────────────────────
// Read one CV channel, calibrated and oversampled, as a normalised value.
inline float CvRead(int ch) {
    if (ch < 0 || ch >= NUM_CV_INS)
        return 0.0f;

    int32_t sum = 0;
    for (int i = 0; i < CV_OVERSAMPLE_SAMPLES; i++)
        sum += analogRead(CV_IN_PINS[ch]);
    const float raw = (float)(sum / CV_OVERSAMPLE_SAMPLES);

    if (cal.valid) {
        // Calibration yields millivolts, which already carries the sign on
        // bipolar hardware — no polarity term needed here.
        return (cal.cvScale[ch] * raw + cal.cvOffset[ch]) / CV_FULLSCALE_MV;
    }

    // Uncalibrated: assume the ADC spans the jack's full range linearly.
    return CV_MIN_NORM + (raw / (float)MAXADC) * CV_NORM_SPAN;
}

// ── Range adapters ──────────────────────────────────────────────────────────
// Rescale a reading onto a fixed range regardless of the hardware revision.
// Callers pick by intent, and must not open-code either mapping: which one is
// the identity swaps when the polarity flag flips.
//
//                unipolar (0..1 in)      bipolar (-1..1 in)
//   CvUni        identity                (v + 1) / 2
//   CvBi         v * 2 - 1               identity

// 0..1 across the jack's full range. "Amount" controls (depth, rate, count).
inline float CvUni(float v) {
    return constrain((v - CV_MIN_NORM) / CV_NORM_SPAN, 0.0f, 1.0f);
}

// -1..+1 across the jack's full range. "Offset"/"trim" controls, centred.
inline float CvBi(float v) { return constrain(CvUni(v) * 2.0f - 1.0f, -1.0f, 1.0f); }

// ── Pitch ───────────────────────────────────────────────────────────────────
// Volt-per-octave pitch in (fractional) semitones. 1 V = 12 semitones and 0 V is
// semitone 0 on both revisions, so on bipolar hardware negative CV yields
// negative semitones.
//
// Deliberately unclamped: how far a module's pitch range extends, and where it
// puts its lowest note, is the module's decision. Today every module treats 0 V
// as C0; putting C0 at -3 V to win eight octaves from the bipolar jack would be
// an offset applied by the caller, before its own clamp.
inline float CvSemitones(float v) {
    return v * (CV_FULLSCALE_MV / 1000.0f) * 12.0f;
}

// ── Legacy counts ───────────────────────────────────────────────────────────
// Normalised -> 0..MAXADC, for code still working in ADC units. Prefer the
// adapters above: this cannot represent negative CV and clamps at zero.
inline uint16_t CvCounts(float v) {
    return (uint16_t)constrain(CvUni(v) * (float)MAXADC, 0.0f, (float)MAXADC);
}
