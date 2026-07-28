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
#include "calibrationData.hpp" // CalibrationData
#include "cvInput.hpp"         // shared acquisition + range adapters
#include "clockEngine.hpp"
#include "outputs.hpp"
#include "pinouts.hpp"
#include "utils.hpp"

// ── CV modulation target enum
// ─────────────────────────────────────────────────
enum CVTarget {
    None = 0,
    StartStop,
    Reset,
    SetBPM,
    Div1,
    Div2,
    Div3,
    Div4,
    Output1Prob,
    Output2Prob,
    Output3Prob,
    Output4Prob,
    Swing1Amount,
    Swing1Every,
    Swing2Amount,
    Swing2Every,
    Swing3Amount,
    Swing3Every,
    Swing4Amount,
    Swing4Every,
    Output1Level,
    Output2Level,
    Output3Level,
    Output4Level,
    Output1Offset,
    Output2Offset,
    Output3Offset,
    Output4Offset,
    Output1Waveform,
    Output2Waveform,
    Output3Waveform,
    Output4Waveform,
    Output1Duty,
    Output2Duty,
    Output3Duty,
    Output4Duty,
    Envelope1,
    Envelope2,
    Envelope3,
    Envelope4,
};

String CVTargetDescription[] = {
    "None",
    "Start/Stop",
    "Reset",
    "Set BPM",
    "Output 1 Div",
    "Output 2 Div",
    "Output 3 Div",
    "Output 4 Div",
    "Output 1 Prob",
    "Output 2 Prob",
    "Output 3 Prob",
    "Output 4 Prob",
    "Swing 1 Amt",
    "Swing 1 Every",
    "Swing 2 Amt",
    "Swing 2 Every",
    "Swing 3 Amt",
    "Swing 3 Every",
    "Swing 4 Amt",
    "Swing 4 Every",
    "Output 1 Lvl",
    "Output 2 Lvl",
    "Output 3 Lvl",
    "Output 4 Lvl",
    "Output 1 Off",
    "Output 2 Off",
    "Output 3 Off",
    "Output 4 Off",
    "Output 1 Wav",
    "Output 2 Wav",
    "Output 3 Wav",
    "Output 4 Wav",
    "Output 1 Duty",
    "Output 2 Duty",
    "Output 3 Duty",
    "Output 4 Duty",
    "Output 1 Env",
    "Output 2 Env",
    "Output 3 Env",
    "Output 4 Env",
};
int CVTargetLength =
    sizeof(CVTargetDescription) / sizeof(CVTargetDescription[0]);

// ── CV oversample count comes from core/cvInput.hpp (default 8).
// Must be a macro, not a constexpr, so core's #ifndef guard can see it if this
// module ever wants a different value.

// ── CV input state globals
// ────────────────────────────────────────────────────
CVTarget pendingCVInputTarget[NUM_CV_INS] = {CVTarget::None, CVTarget::None};

// Active CV target assignments
CVTarget CVInputTarget[NUM_CV_INS] = {CVTarget::None, CVTarget::None};
int CVInputAttenuation[NUM_CV_INS] = {0, 0};
int CVInputOffset[NUM_CV_INS] = {0, 0};

// CV readings (calibrated, filtered), normalised (see core/cvInput.hpp).
//
// Normalised rather than ADC counts because presets store values derived from
// these, and a count would change meaning if MAXADC or the CV range ever did —
// and both will. It also means the modulation matrix below needs no notion of
// converter resolution at all: MAXDAC appears only in the output domain.
float channelCv[NUM_CV_INS], oldChannelCv[NUM_CV_INS];

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
float lastDispatchedCv[NUM_CV_INS] = {-1.0e9f, -1.0e9f};

// ── extern refs defined in main.cpp / clockEngine.hpp ────────────────────────
extern CalibrationData cal;
extern bool masterState;
extern void SetMasterState(bool state);

// Forward declaration
void HandleCVTarget(int ch, float CVValue, CVTarget cvTarget);

// ─────────────────────────────────────────────────────────────────────────────
// Poll both CV inputs and dispatch to HandleCVTarget() on meaningful change
// ─────────────────────────────────────────────────────────────────────────────
void HandleCVInputs() {
    for (int i = 0; i < NUM_CV_INS; i++) {
        oldChannelCv[i] = channelCv[i];
        // Oversampling and calibration live in core/cvInput.hpp — the same
        // acquisition path every module uses.
        channelCv[i] = CvRead(i);

        // Is this CV input mirrored to a output? (waveform "CV 1" reads CV in 1,
        // "CV 2" reads CV in 2).  Such outputs feed the quantizer and want a
        // light filter for responsive pitch tracking; everything else (BPM,
        // dividers, …) benefits from heavier filtering to suppress ADC noise.
        WaveformType passthroughWave = (i == 0) ? WaveformType::CVInput1 : WaveformType::CVInput2;
        bool feedsOutput = false;
        for (int o = 0; o < NUM_OUTPUTS; o++) {
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
            for (int o = 0; o < NUM_OUTPUTS; o++) {
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
        if (CVValue > 0.5f) {
            SetMasterState(true);
        } else {
            SetMasterState(false);
        }
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
    case CVTarget::Div1:
        outputs[0].SetDivider(
            CvMap(CVValue, 0, outputs[0].GetDividerAmounts()));
        break;
    case CVTarget::Div2:
        outputs[1].SetDivider(
            CvMap(CVValue, 0, outputs[1].GetDividerAmounts()));
        break;
    case CVTarget::Div3:
        outputs[2].SetDivider(
            CvMap(CVValue, 0, outputs[2].GetDividerAmounts()));
        break;
    case CVTarget::Div4:
        outputs[3].SetDivider(
            CvMap(CVValue, 0, outputs[3].GetDividerAmounts()));
        break;
    case CVTarget::Output1Prob:
        outputs[0].SetPulseProbability(CvMap(CVValue, 1, 100));
        break;
    case CVTarget::Output2Prob:
        outputs[1].SetPulseProbability(CvMap(CVValue, 1, 100));
        break;
    case CVTarget::Output3Prob:
        outputs[2].SetPulseProbability(CvMap(CVValue, 1, 100));
        break;
    case CVTarget::Output4Prob:
        outputs[3].SetPulseProbability(CvMap(CVValue, 1, 100));
        break;
    case CVTarget::Swing1Amount:
        outputs[0].SetSwingAmount(
            CvMap(CVValue, 0, outputs[0].GetSwingAmounts()));
        break;
    case CVTarget::Swing1Every:
        outputs[0].SetSwingEvery(
            CvMap(CVValue, 1, outputs[0].GetSwingEveryAmounts()));
        break;
    case CVTarget::Swing2Amount:
        outputs[1].SetSwingAmount(
            CvMap(CVValue, 0, outputs[1].GetSwingAmounts()));
        break;
    case CVTarget::Swing2Every:
        outputs[1].SetSwingEvery(
            CvMap(CVValue, 1, outputs[1].GetSwingEveryAmounts()));
        break;
    case CVTarget::Swing3Amount:
        outputs[2].SetSwingAmount(
            CvMap(CVValue, 0, outputs[2].GetSwingAmounts()));
        break;
    case CVTarget::Swing3Every:
        outputs[2].SetSwingEvery(
            CvMap(CVValue, 1, outputs[2].GetSwingEveryAmounts()));
        break;
    case CVTarget::Swing4Amount:
        outputs[3].SetSwingAmount(
            CvMap(CVValue, 0, outputs[3].GetSwingAmounts()));
        break;
    case CVTarget::Swing4Every:
        outputs[3].SetSwingEvery(
            CvMap(CVValue, 1, outputs[3].GetSwingEveryAmounts()));
        break;
    case CVTarget::Output1Level:
        outputs[0].SetLevel(CvMap(CVValue, 0, 100));
        break;
    case CVTarget::Output2Level:
        outputs[1].SetLevel(CvMap(CVValue, 0, 100));
        break;
    case CVTarget::Output3Level:
        outputs[2].SetLevel(CvMap(CVValue, 0, 100));
        break;
    case CVTarget::Output4Level:
        outputs[3].SetLevel(CvMap(CVValue, 0, 100));
        break;
    case CVTarget::Output1Offset:
        outputs[0].SetOffset(CvMap(CVValue, 0, 100));
        break;
    case CVTarget::Output2Offset:
        outputs[1].SetOffset(CvMap(CVValue, 0, 100));
        break;
    case CVTarget::Output3Offset:
        outputs[2].SetOffset(CvMap(CVValue, 0, 100));
        break;
    case CVTarget::Output4Offset:
        outputs[3].SetOffset(CvMap(CVValue, 0, 100));
        break;
    // map upper bound is WaveformTypeLength - 1 so a full-scale CV selects the
    // last waveform (not an out-of-range index).
    case CVTarget::Output1Waveform:
        outputs[0].SetWaveformType(static_cast<WaveformType>(
            CvMap(CVValue, 0, WaveformTypeLength - 1)));
        break;
    case CVTarget::Output2Waveform:
        outputs[1].SetWaveformType(static_cast<WaveformType>(
            CvMap(CVValue, 0, WaveformTypeLength - 1)));
        break;
    case CVTarget::Output3Waveform:
        outputs[2].SetWaveformType(static_cast<WaveformType>(
            CvMap(CVValue, 0, WaveformTypeLength - 1)));
        break;
    case CVTarget::Output4Waveform:
        outputs[3].SetWaveformType(static_cast<WaveformType>(
            CvMap(CVValue, 0, WaveformTypeLength - 1)));
        break;
    case CVTarget::Output1Duty:
        outputs[0].SetDutyCycle(CvMap(CVValue, 0, 100));
        break;
    case CVTarget::Output2Duty:
        outputs[1].SetDutyCycle(CvMap(CVValue, 0, 100));
        break;
    case CVTarget::Output3Duty:
        outputs[2].SetDutyCycle(CvMap(CVValue, 0, 100));
        break;
    case CVTarget::Output4Duty:
        outputs[3].SetDutyCycle(CvMap(CVValue, 0, 100));
        break;
    case CVTarget::Envelope1: {
        // Schmitt-trigger hysteresis: higher threshold to arm, lower to release.
        bool wasTriggered = outputs[0].GetExternalTrigger();
        outputs[0].SetExternalTrigger(
            CVValue > (wasTriggered ? 0.40f : 0.60f));
        break;
    }
    case CVTarget::Envelope2: {
        bool wasTriggered = outputs[1].GetExternalTrigger();
        outputs[1].SetExternalTrigger(
            CVValue > (wasTriggered ? 0.40f : 0.60f));
        break;
    }
    case CVTarget::Envelope3: {
        bool wasTriggered = outputs[2].GetExternalTrigger();
        outputs[2].SetExternalTrigger(
            CVValue > (wasTriggered ? 0.40f : 0.60f));
        break;
    }
    case CVTarget::Envelope4: {
        bool wasTriggered = outputs[3].GetExternalTrigger();
        outputs[3].SetExternalTrigger(
            CVValue > (wasTriggered ? 0.40f : 0.60f));
        break;
    }
    }

    interrupts();
}
