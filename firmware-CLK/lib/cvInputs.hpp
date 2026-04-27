#pragma once

// cvInputs.hpp — CV input processing, CVTarget enum, attenuation, calibration
//
// Owns: CVTarget enum, CVTargetDescription[], CVInputTarget/Attenuation/Offset[],
//       channelADC[], pendingCVInputTarget[],
//       AdjustADCReadings(), HandleCVInputs(), HandleCVTarget().
// Depends on: outputs[], BPM/tickCounter/externalTickCounter (clockEngine.hpp),
//             masterState/SetMasterState() (main.cpp via extern).

#include <Arduino.h>

#include "calibrationData.hpp"  // CalibrationData + CAL_LUT_*
#include "boardIO.hpp"
#include "clockEngine.hpp"
#include "outputs.hpp"
#include "pinouts.hpp"
#include "utils.hpp"

// ── CV modulation target enum ─────────────────────────────────────────────────
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
    Output3Level,
    Output4Level,
    Output3Offset,
    Output4Offset,
    Output3Waveform,
    Output4Waveform,
    Output1Duty,
    Output2Duty,
    Output3Duty,
    Output4Duty,
    Envelope1,
    Envelope2,
    Output3,
    Output4,
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
    "Output 3 Lvl",
    "Output 4 Lvl",
    "Output 3 Off",
    "Output 4 Off",
    "Output 3 Wav",
    "Output 4 Wav",
    "Output 1 Duty",
    "Output 2 Duty",
    "Output 3 Duty",
    "Output 4 Duty",
    "Output 3 Env",
    "Output 4 Env",
    "Output 3",
    "Output 4",
};
int CVTargetLength = sizeof(CVTargetDescription) / sizeof(CVTargetDescription[0]);

// ── CV oversample count ───────────────────────────────────────────────────────
// RP2040 ADC is noisier than SAMD21 (no hardware averaging).
// 8x software oversampling halves the noise floor (~1.4 effective bits gained).
// SAMD21 uses 128x hardware averaging in InitIO(), so 1 read is sufficient.
#if defined(ARDUINO_ARCH_RP2040)
static constexpr int CV_OVERSAMPLE_SAMPLES = 8;
#else
static constexpr int CV_OVERSAMPLE_SAMPLES = 1;
#endif

// ── CV input state globals ────────────────────────────────────────────────────
CVTarget pendingCVInputTarget[NUM_CV_INS] = {CVTarget::None, CVTarget::None};

// Active CV target assignments
CVTarget CVInputTarget[NUM_CV_INS]  = {CVTarget::None, CVTarget::None};
int      CVInputAttenuation[NUM_CV_INS] = {0, 0};
int      CVInputOffset[NUM_CV_INS]      = {0, 0};

// ADC readings (calibrated, filtered)
float channelADC[NUM_CV_INS], oldChannelADC[NUM_CV_INS];

// ── extern refs defined in main.cpp / clockEngine.hpp ────────────────────────
extern CalibrationData cal;
extern bool     masterState;
extern void     SetMasterState(bool state);

// Forward declaration
void HandleCVTarget(int ch, float CVValue, CVTarget cvTarget);

// ─────────────────────────────────────────────────────────────────────────────
// Read one ADC channel and apply LUT piecewise-linear calibration.
// Raw ADC → millivolts via the per-channel LUT, then → 0–4095 (0–5V scale)
// so downstream code sees the same 12-bit range it always has.
// ─────────────────────────────────────────────────────────────────────────────
void AdjustADCReadings(int CV_IN_PIN, int ch) {
    // Average multiple samples to reduce RP2040 ADC noise.
    // SAMD21 uses hardware 128x averaging so CV_OVERSAMPLE_SAMPLES == 1 there.
    int32_t sum = 0;
    for (int i = 0; i < CV_OVERSAMPLE_SAMPLES; i++) {
        sum += analogRead(CV_IN_PIN);
    }
    int raw = (int)(sum / CV_OVERSAMPLE_SAMPLES);

    // Piecewise-linear interpolation through the calibration LUT.
    // LUT maps known voltages → expected raw ADC values captured during calibration.
    // We invert: raw ADC → millivolts, then millivolts → 0-4095 (0-5V scale).
    float mv = 0.0f;
    if (raw <= (int)cal.cvLUT[ch][0]) {
        mv = CAL_LUT_MV[0];
    } else if (raw >= (int)cal.cvLUT[ch][CAL_LUT_POINTS - 1]) {
        mv = CAL_LUT_MV[CAL_LUT_POINTS - 1];
    } else {
        for (int i = 0; i < CAL_LUT_POINTS - 1; i++) {
            int lo = cal.cvLUT[ch][i];
            int hi = cal.cvLUT[ch][i + 1];
            if (raw >= lo && raw <= hi) {
                float t = (hi > lo) ? (float)(raw - lo) / (float)(hi - lo) : 0.0f;
                mv = CAL_LUT_MV[i] + t * (CAL_LUT_MV[i + 1] - CAL_LUT_MV[i]);
                break;
            }
        }
    }
    // Convert millivolts (0–5000) to 12-bit DAC scale (0–4095)
    channelADC[ch] = constrain(mv * 4095.0f / 5000.0f, 0.0f, 4095.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Poll both CV inputs and dispatch to HandleCVTarget() on meaningful change
// ─────────────────────────────────────────────────────────────────────────────
void HandleCVInputs() {
    for (int i = 0; i < NUM_CV_INS; i++) {
        oldChannelADC[i] = channelADC[i];
        AdjustADCReadings(CV_IN_PINS[i], i);

        // Choose IIR filter aggressiveness based on target:
        //   Direct CV passthrough (Output3/Output4) feeds the quantizer.
        //   Use a light filter (α≈0.15 of old state) so pitch changes propagate
        //   within 1–2 loop iterations.  The quantizer has its own ±10-count
        //   hysteresis, so extra smoothing here only adds unwanted lag.
        //   All other targets (BPM, dividers, …) tolerate and benefit from
        //   heavier filtering (α=0.5) to suppress ADC noise on slow parameters.
        //
        //   ONE_POLE(out, in, coeff): out = (1-coeff)*out + coeff*in
        //     here out=new_raw, in=old_filtered → result = (1-α)*new_raw + α*old
        bool isDirectCV = (CVInputTarget[i] == CVTarget::Output3 ||
                           CVInputTarget[i] == CVTarget::Output4);
        if (isDirectCV) {
            ONE_POLE(channelADC[i], oldChannelADC[i], 0.15f);
            // Always propagate — let the quantizer's internal hysteresis decide
            HandleCVTarget(i, channelADC[i], CVInputTarget[i]);
        } else {
            ONE_POLE(channelADC[i], oldChannelADC[i], 0.5f);
            if (abs(channelADC[i] - oldChannelADC[i]) > 10) {
                HandleCVTarget(i, channelADC[i], CVInputTarget[i]);
            }
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
    float offsetValue     = attenuatedValue + (CVInputOffset[ch] / 100.0f * MAXDAC);
    CVValue = constrain(offsetValue, 0, MAXDAC);

    // CRITICAL SECTION: protect parameter updates that affect timing
    noInterrupts();

    switch (cvTarget) {
    case CVTarget::None:
        break;
    case CVTarget::StartStop:
        if (CVValue > MAXDAC / 2) {
            SetMasterState(true);
        } else {
            SetMasterState(false);
        }
        break;
    case CVTarget::Reset:
        if (CVValue > MAXDAC / 2 && !lastResetState) {
            tickCounter        = 0;
            externalTickCounter = 0;
            lastResetState     = true;
        } else if (CVValue < MAXDAC / 2) {
            lastResetState = false;
        }
        break;
    case CVTarget::SetBPM:
        UpdateBPM(map(CVValue, 0, MAXDAC, minBPM, maxBPM));
        break;
    case CVTarget::Div1:
        outputs[0].SetDivider(map(CVValue, 0, MAXDAC, 0, outputs[0].GetDividerAmounts()));
        break;
    case CVTarget::Div2:
        outputs[1].SetDivider(map(CVValue, 0, MAXDAC, 0, outputs[1].GetDividerAmounts()));
        break;
    case CVTarget::Div3:
        outputs[2].SetDivider(map(CVValue, 0, MAXDAC, 0, outputs[2].GetDividerAmounts()));
        break;
    case CVTarget::Div4:
        outputs[3].SetDivider(map(CVValue, 0, MAXDAC, 0, outputs[3].GetDividerAmounts()));
        break;
    case CVTarget::Output1Prob:
        outputs[0].SetPulseProbability(map(CVValue, 0, MAXDAC, 1, 100));
        break;
    case CVTarget::Output2Prob:
        outputs[1].SetPulseProbability(map(CVValue, 0, MAXDAC, 1, 100));
        break;
    case CVTarget::Output3Prob:
        outputs[2].SetPulseProbability(map(CVValue, 0, MAXDAC, 1, 100));
        break;
    case CVTarget::Output4Prob:
        outputs[3].SetPulseProbability(map(CVValue, 0, MAXDAC, 1, 100));
        break;
    case CVTarget::Swing1Amount:
        outputs[0].SetSwingAmount(map(CVValue, 0, MAXDAC, 0, outputs[0].GetSwingAmounts()));
        break;
    case CVTarget::Swing1Every:
        outputs[0].SetSwingEvery(map(CVValue, 0, MAXDAC, 1, outputs[0].GetSwingEveryAmounts()));
        break;
    case CVTarget::Swing2Amount:
        outputs[1].SetSwingAmount(map(CVValue, 0, MAXDAC, 0, outputs[1].GetSwingAmounts()));
        break;
    case CVTarget::Swing2Every:
        outputs[1].SetSwingEvery(map(CVValue, 0, MAXDAC, 1, outputs[1].GetSwingEveryAmounts()));
        break;
    case CVTarget::Swing3Amount:
        outputs[2].SetSwingAmount(map(CVValue, 0, MAXDAC, 0, outputs[2].GetSwingAmounts()));
        break;
    case CVTarget::Swing3Every:
        outputs[2].SetSwingEvery(map(CVValue, 0, MAXDAC, 1, outputs[2].GetSwingEveryAmounts()));
        break;
    case CVTarget::Swing4Amount:
        outputs[3].SetSwingAmount(map(CVValue, 0, MAXDAC, 0, outputs[3].GetSwingAmounts()));
        break;
    case CVTarget::Swing4Every:
        outputs[3].SetSwingEvery(map(CVValue, 0, MAXDAC, 1, outputs[3].GetSwingEveryAmounts()));
        break;
    case CVTarget::Output3Offset:
        outputs[2].SetOffset(map(CVValue, 0, MAXDAC, 0, 100));
        break;
    case CVTarget::Output4Offset:
        outputs[3].SetOffset(map(CVValue, 0, MAXDAC, 0, 100));
        break;
    case CVTarget::Output3Level:
        outputs[2].SetLevel(map(CVValue, 0, MAXDAC, 0, 100));
        break;
    case CVTarget::Output4Level:
        outputs[3].SetLevel(map(CVValue, 0, MAXDAC, 0, 100));
        break;
    case CVTarget::Output3Waveform:
        outputs[2].SetWaveformType(static_cast<WaveformType>(map(CVValue, 0, MAXDAC, 0, WaveformTypeLength)));
        break;
    case CVTarget::Output4Waveform:
        outputs[3].SetWaveformType(static_cast<WaveformType>(map(CVValue, 0, MAXDAC, 0, WaveformTypeLength)));
        break;
    case CVTarget::Output1Duty:
        outputs[0].SetDutyCycle(map(CVValue, 0, MAXDAC, 0, 100));
        break;
    case CVTarget::Output2Duty:
        outputs[1].SetDutyCycle(map(CVValue, 0, MAXDAC, 0, 100));
        break;
    case CVTarget::Output3Duty:
        outputs[2].SetDutyCycle(map(CVValue, 0, MAXDAC, 0, 100));
        break;
    case CVTarget::Output4Duty:
        outputs[3].SetDutyCycle(map(CVValue, 0, MAXDAC, 0, 100));
        break;
    case CVTarget::Envelope1:
        outputs[2].SetExternalTrigger(CVValue > MAXDAC / 2);
        break;
    case CVTarget::Envelope2:
        outputs[3].SetExternalTrigger(CVValue > MAXDAC / 2);
        break;
    case CVTarget::Output3:
        outputs[2].SetCVValue(CVValue);
        break;
    case CVTarget::Output4:
        outputs[3].SetCVValue(CVValue);
        break;
    }

    interrupts();
}
