#pragma once

// cvInput.hpp — CV acquisition and range adaptation for the ForgeSeries board.
//
// Every module reads the same two analog CV jacks through the same 12-bit ADC
// with the same two-point calibration, so the acquisition path lives here once.
// What each app *does* with the value stays in its own cvInputs.hpp.
//
// ── Polarity ────────────────────────────────────────────────────────────────
// The current hardware revision accepts 0..+5 V. The next one accepts -5..+5 V.
// Which one you are building for is a single build flag:
//
//     build_flags = -DFORGE_CV_BIPOLAR      ; -5..+5 V hardware
//     (unset)                               ; 0..+5 V hardware (default)
//
// Nothing above this file may assume a polarity. Apps read CV through the three
// adapters below, whose contracts hold on BOTH hardware revisions:
//
//     CvVolts(ch)    -> actual volts at the jack. The honest primitive.
//     CvNorm(ch)     -> 0..1    for "amount" controls (depth, rate, count)
//     CvBipolar(ch)  -> -1..+1  for "offset"/"trim" controls (centred at zero)
//
// The two normalised adapters swap which of them is the direct reading and which
// is the derived one when the flag flips, which is exactly why callers must not
// open-code either mapping:
//
//                    unipolar (0..5V)            bipolar (-5..+5V)
//     CvNorm         mv / 5000        (direct)   (mv + 5000) / 10000  (derived)
//     CvBipolar      2*CvNorm - 1     (derived)  mv / 5000            (direct)
//
// A patch that sends 0 V reads as CvNorm 0 on both revisions. A patch that
// sends 2.5 V reads as CvBipolar 0 on unipolar hardware and +0.5 on bipolar -
// that is inherent to the hardware change, not something this layer can hide.

#include "boardPinouts.hpp"
#include "calibrationData.hpp"
#include <Arduino.h>

// Defined by each app in its main.cpp; loaded from storage (and, once the
// unified firmware lands, owned by the shell and shared by every app).
extern CalibrationData cal;

// ── Hardware range ──────────────────────────────────────────────────────────
#ifdef FORGE_CV_BIPOLAR
#define CV_RANGE_MIN_MV (-5000.0f)
#else
#define CV_RANGE_MIN_MV (0.0f)
#endif
#define CV_RANGE_MAX_MV (5000.0f)
#define CV_RANGE_SPAN_MV (CV_RANGE_MAX_MV - CV_RANGE_MIN_MV)

// Oversampling depth for a single CV read. The RP2040 ADC is noisy enough that
// a single conversion visibly jitters a quantizer a semitone at the top of its
// range. Apps may override before including.
#ifndef CV_OVERSAMPLE_SAMPLES
#define CV_OVERSAMPLE_SAMPLES 8
#endif

// ── Acquisition ─────────────────────────────────────────────────────────────
// Read one CV channel and return calibrated millivolts at the jack.
//
// Calibration maps raw ADC counts straight to millivolts, so it absorbs the
// hardware's input range on its own: on bipolar hardware the captured
// coefficients simply produce negative millivolts near 0 V input. That is why
// the polarity flag affects only the normalising adapters below and NOT this
// function - do not add a polarity term here.
//
// Falls back to nominal full-scale mapping when no valid calibration is stored.
inline float CvReadMillivolts(int ch) {
    if (ch < 0 || ch >= NUM_CV_INS)
        return 0.0f;

    int32_t sum = 0;
    for (int i = 0; i < CV_OVERSAMPLE_SAMPLES; i++)
        sum += analogRead(CV_IN_PINS[ch]);
    const int raw = (int)(sum / CV_OVERSAMPLE_SAMPLES);

    if (cal.valid)
        return cal.cvScale[ch] * (float)raw + cal.cvOffset[ch];

    // Uncalibrated: assume the ADC spans the jack's full range linearly.
    return CV_RANGE_MIN_MV + ((float)raw / (float)MAXADC) * CV_RANGE_SPAN_MV;
}

// ── Range adapters ──────────────────────────────────────────────────────────
inline float CvVoltsFromMv(float mv) { return mv * 0.001f; }

// 0..1 across the jack's full range. "Amount" controls.
inline float CvNormFromMv(float mv) {
    return constrain((mv - CV_RANGE_MIN_MV) / CV_RANGE_SPAN_MV, 0.0f, 1.0f);
}

// -1..+1 across the jack's full range. "Offset"/"trim" controls, centred.
inline float CvBipolarFromMv(float mv) {
    return constrain(CvNormFromMv(mv) * 2.0f - 1.0f, -1.0f, 1.0f);
}

// Legacy 0..MAXADC counts, for code that still works in ADC units. Prefer the
// float adapters in new code - this one cannot represent negative CV and so
// clamps at zero on bipolar hardware.
inline uint16_t CvCountsFromMv(float mv) {
    return (uint16_t)constrain(CvNormFromMv(mv) * (float)MAXADC, 0.0f,
                               (float)MAXADC);
}
