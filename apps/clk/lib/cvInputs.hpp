#pragma once

// cvInputs.hpp — CV input processing, CVTarget enum, attenuation, calibration
//
// Owns: CVTarget enum, CVTargetDescription[],
// CVInputTarget/Attenuation/Offset[],
//       channelCv[], pendingCVInputTarget[],
//       HandleCVInputs(), HandleCVTarget().
// Depends on: outputs[], BPM/tickCounter/externalTickCounter (clockEngine.hpp),
//             masterState/SetMasterState() (main.cpp via extern).

#include <Arduino.h>

#include "boardIO.hpp"
#include "boardPinouts.hpp"
#include "calibrationData.hpp" // CalibrationData
#include "clockEngine.hpp"
#include "cvInput.hpp" // shared acquisition + range adapters
#include "expander.hpp" // ActiveOutputs / ActiveCvIns
#include "outputs.hpp"
#include "utils.hpp"

// ── CV modulation targets ────────────────────────────────────────────────────
//
// Four global targets, then one block of nine parameters PER OUTPUT. Encoded
// output-major:
//
//     target = kCVPerOutputBase + out * CVFam_COUNT + family
//
// so every output's parameters are contiguous and the outputs are in order.
// That is what makes the expander cheap: outputs 5-8 simply extend the range,
// the stored byte for a given target never changes meaning, and gating the list
// down when no expander is fitted is a smaller CVTargetCount() and nothing else.
//
// It also replaced a 40-entry table of Arduino Strings and a 40-case switch that
// were both a per-output copy-paste. Eight outputs would have made those 76 and
// 112. Names are built on demand instead — see CVTargetName() — and the switch
// dispatches on the family, with the output index decoded out of the target.
enum CVFamily : uint8_t {
    CVFam_Divider = 0,
    CVFam_Probability,
    CVFam_SwingAmount,
    CVFam_SwingEvery,
    CVFam_Level,
    CVFam_Offset,
    CVFam_Waveform,
    CVFam_Duty,
    CVFam_Envelope,
    CVFam_COUNT,
};

enum CVTarget : uint8_t {
    None = 0,
    StartStop,
    Reset,
    SetBPM,
    kCVPerOutputBase, // everything from here is per-output; see the encoding above
};

// Highest target that exists at all (expander fitted). Sizes nothing — the
// stored value is a byte and the tables are generated — but bounds the decode.
static const int kCVTargetMax = kCVPerOutputBase + NUM_MAX_OUTPUTS * CVFam_COUNT;

static inline int CVTargetOutput(int t) { return (t - kCVPerOutputBase) / CVFam_COUNT; }
static inline int CVTargetFamily(int t) { return (t - kCVPerOutputBase) % CVFam_COUNT; }

// How many targets the menu offers right now. Without an expander the list ends
// after output 4, exactly as it always did.
// Without an expander the list stops after output 4 — the same 40 entries it
// has always offered. Stored target bytes keep their meaning either way,
// because the encoding's stride is NUM_MAX_OUTPUTS-independent (output-major).
static inline int CVTargetCount() {
    return kCVPerOutputBase + ActiveOutputs() * CVFam_COUNT;
}

// Display name. Global targets are literals; per-output ones are the family name
// with the output number substituted, into a shared static buffer — the menu
// renders one value at a time, the same assumption the other getters here make.
static String CVTargetName(int t) {
    static const char *const kGlobal[] = {"None", "Start/Stop", "Reset", "Set BPM"};
    if (t < kCVPerOutputBase)
        return kGlobal[t];
    static const char *const kFamily[CVFam_COUNT] = {
        "Out %d Div", "Out %d Prob", "Swing %d Amt", "Swing %d Evry",
        "Out %d Lvl", "Out %d Off", "Out %d Wav", "Out %d Duty", "Out %d Env"};
    static char buf[16];
    snprintf(buf, sizeof(buf), kFamily[CVTargetFamily(t)], CVTargetOutput(t) + 1);
    return buf;
}

// ── CV oversample count comes from core/cvInput.hpp (default 8).
// Must be a macro, not a constexpr, so core's #ifndef guard can see it if this
// module ever wants a different value.

// ── CV input state globals
// ────────────────────────────────────────────────────
CVTarget pendingCVInputTarget[NUM_MAX_CV_INS] = {CVTarget::None, CVTarget::None, CVTarget::None};

// Active CV target assignments
CVTarget CVInputTarget[NUM_MAX_CV_INS] = {CVTarget::None, CVTarget::None, CVTarget::None};
int CVInputAttenuation[NUM_MAX_CV_INS] = {0, 0, 0};
int CVInputOffset[NUM_MAX_CV_INS] = {0, 0, 0};

// CV readings (calibrated, filtered), normalised (see core/cvInput.hpp).
//
// Normalised rather than ADC counts because presets store values derived from
// these, and a count would change meaning if MAXADC or the CV range ever did —
// and both will. It also means the modulation matrix below needs no notion of
// converter resolution at all: MAXDAC appears only in the output domain.
float channelCv[NUM_MAX_CV_INS], oldChannelCv[NUM_MAX_CV_INS];

// Map a normalised 0..1 CV onto an integer parameter range.
//
// Replaces map(CVValue, 0, MAXDAC, lo, hi). Truncating (not rounding) keeps the
// boundary behaviour of the integer map() this came from.
static inline int CvMap(float cv, int lo, int hi) {
    return lo + (int)(cv * (float)(hi - lo));
}

// Last CV value actually dispatched to a modulation target, per channel. The
// dispatch gate compares against this (cumulative change) rather than the
// previous sample, so slow-moving CV still re-dispatches once it has drifted
// past the threshold. Comparing only adjacent samples breaks down when
// HandleCVInputs() is polled fast (e.g. the VCV engine at several kHz): each
// step's delta stays below the threshold and the target would never update.
// Sentinel forces a dispatch on the first reading after a target is assigned.
float lastDispatchedCv[NUM_MAX_CV_INS] = {-1.0e9f, -1.0e9f, -1.0e9f};

// ── extern refs defined in main.cpp / clockEngine.hpp ────────────────────────
extern bool masterState;
extern void SetMasterState(bool state);

// Forward declaration
void HandleCVTarget(int ch, float CVValue, CVTarget cvTarget);

// ─────────────────────────────────────────────────────────────────────────────
// Poll both CV inputs and dispatch to HandleCVTarget() on meaningful change
// ─────────────────────────────────────────────────────────────────────────────
void HandleCVInputs() {
    for (int i = 0; i < ActiveCvIns(); i++) {
        oldChannelCv[i] = channelCv[i];
        // Oversampling and calibration live in core/cvInput.hpp — the same
        // acquisition path every module uses.
        channelCv[i] = CvRead(i);

        // Is this CV input mirrored to a output? (waveform "CV 1" reads CV in 1,
        // "CV 2" reads CV in 2).  Such outputs feed the quantizer and want a
        // light filter for responsive pitch tracking; everything else (BPM,
        // dividers, …) benefits from heavier filtering to suppress ADC noise.
        WaveformType passthroughWave = kCvPassthroughWave[i];
        bool feedsOutput = false;
        for (int o = 0; o < ActiveOutputs(); o++) {
            if (outputs[o].GetWaveformType() == passthroughWave) {
                feedsOutput = true;
                break;
            }
        }

        // ONE_POLE(out, in, coeff): out = (1-coeff)*out + coeff*in
        //   here out=new_raw, in=old_filtered → result = (1-α)*new_raw + α*old
        if (feedsOutput) {
            ONE_POLE(channelCv[i], oldChannelCv[i], 0.15f); // light: pitch tracking
        } else {
            ONE_POLE(channelCv[i], oldChannelCv[i], 0.5f); // heavy: noise rejection
        }

        // Every modulation target wants 0..1 across whatever the jack can
        // deliver, so rescale once here rather than in each of the ~38 branches.
        // On the +/-5 V hardware this maps -5 V to 0 and +5 V to 1, so the
        // matrix gains the full input swing instead of clamping half of it away.
        const float cv = CvUni(channelCv[i]);

        // Push the filtered CV to every output mirroring this input.  The
        // quantizer's own ±hysteresis decides note changes downstream.
        if (feedsOutput) {
            for (int o = 0; o < ActiveOutputs(); o++) {
                if (outputs[o].GetWaveformType() == passthroughWave) {
                    outputs[o].SetCVValue(cv);
                }
            }
        }

        // Dispatch to the assigned modulation target (BPM, divider, prob, …),
        // gated on a meaningful change since the last dispatch to ignore ADC
        // jitter while still tracking slow CV sweeps. The threshold is the
        // former 10 counts, expressed so it stays put if MAXADC ever changes.
        if (CVInputTarget[i] != CVTarget::None &&
            fabsf(cv - lastDispatchedCv[i]) > (10.0f / (float)MAXADC)) {
            HandleCVTarget(i, cv, CVInputTarget[i]);
            lastDispatchedCv[i] = cv;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Apply a CV value to its assigned target parameter
// ─────────────────────────────────────────────────────────────────────────────
volatile bool lastResetState = false;

void HandleCVTarget(int ch, float CVValue, CVTarget cvTarget) {
    // Attenuate and offset the CVValue
    float attenuatedValue = CVValue * ((100 - CVInputAttenuation[ch]) / 100.0f);
    float offsetValue = attenuatedValue + (CVInputOffset[ch] / 100.0f);
    CVValue = constrain(offsetValue, 0.0f, 1.0f);

    // CRITICAL SECTION: protect parameter updates that affect timing
    noInterrupts();

    switch (cvTarget) {
    case CVTarget::None:
        break;
    case CVTarget::StartStop:
        SetMasterState(CVValue > 0.5f);
        break;
    case CVTarget::Reset:
        if (CVValue > 0.5f && !lastResetState) {
            tickCounter = 0;
            externalTickCounter = 0;
            lastResetState = true;
        } else if (CVValue < 0.5f) {
            lastResetState = false;
        }
        break;
    case CVTarget::SetBPM:
        UpdateBPM(CvMap(CVValue, minBPM, maxBPM));
        break;
    default: {
        // Per-output target: which output, and which of its parameters.
        const int o = CVTargetOutput(cvTarget);
        if (o < 0 || o >= ActiveOutputs())
            break; // targets an output this build has no expander for
        Output &out = outputs[o];
        switch (CVTargetFamily(cvTarget)) {
        case CVFam_Divider:
            out.SetDivider(CvMap(CVValue, 0, out.GetDividerAmounts()));
            break;
        case CVFam_Probability:
            out.SetPulseProbability(CvMap(CVValue, 1, 100));
            break;
        case CVFam_SwingAmount:
            out.SetSwingAmount(CvMap(CVValue, 0, out.GetSwingAmounts()));
            break;
        case CVFam_SwingEvery:
            out.SetSwingEvery(CvMap(CVValue, 1, out.GetSwingEveryAmounts()));
            break;
        case CVFam_Level:
            out.SetLevel(CvMap(CVValue, 0, 100));
            break;
        case CVFam_Offset:
            out.SetOffset(CvMap(CVValue, 0, 100));
            break;
        case CVFam_Waveform:
            // Upper bound is WaveformTypeLength - 1 so a full-scale CV selects
            // the last waveform rather than an out-of-range index.
            out.SetWaveformType(static_cast<WaveformType>(
                CvMap(CVValue, 0, WaveformTypeLength - 1)));
            break;
        case CVFam_Duty:
            out.SetDutyCycle(CvMap(CVValue, 0, 100));
            break;
        case CVFam_Envelope: {
            // Schmitt-trigger hysteresis: higher threshold to arm, lower to release.
            const bool wasTriggered = out.GetExternalTrigger();
            out.SetExternalTrigger(CVValue > (wasTriggered ? 0.40f : 0.60f));
            break;
        }
        default:
            break;
        }
        break;
    }
    }

    interrupts();
}
