#pragma once

// cvInputs.hpp — Pitch CV acquisition and the TRIG input.
//
// Unlike ClockForge, both CV inputs on this module have a fixed job: they are
// the two pitch signals being quantized. There is no CV-target matrix — the
// third jack (TRIG) is the only modulation input, and it drives the gate/
// envelope sync.
//
// Owns: channelADC[], AdjustADCReadings(), HandleCVInputs(), and the TRIG
// input's ISR + edge queue.

#include <Arduino.h>

#include "boardIO.hpp"
#include "calibrationData.hpp"
#include "pinouts.hpp"
#include "utils.hpp"

// Oversampling count per read — averages out RP2040 ADC noise. Pitch accuracy
// is the point of this module, so this is worth the ~8 µs it costs.
static constexpr int CV_OVERSAMPLE_SAMPLES = 8;

// Weight of the previous filtered sample in the one-pole smoother. Kept light:
// the quantizer's own hysteresis rejects boundary jitter, and heavy filtering
// here would smear fast sequencer pitch steps into audible glides.
static constexpr float CV_FILTER_COEFF = 0.2f;

// Ignore TRIG edges closer together than this — contact bounce / ringing on a
// fast gate would otherwise retrigger the envelope several times per note.
static constexpr unsigned long TRIG_DEBOUNCE_US = 1000;

// Calibrated, filtered pitch CV per channel (0..4095 == 0..5V).
float channelADC[NUM_CV_INS], oldChannelADC[NUM_CV_INS];

// ── IN 2 routing ─────────────────────────────────────────────────────────────
// IN 2 is normally channel 2's pitch input. It can instead be handed over to a
// transposition CV, in which case channel 2 quantizes IN 1 alongside channel 1:
// two voicings of the same melody, transposable together. Giving up a pitch
// input has to be explicit, hence the menu switch rather than auto-detection —
// the hardware has no switched jacks and cannot tell an unpatched input from
// one sitting at 0 V.
enum In2Role : uint8_t {
    In2Pitch = 0, // channel 2 pitch input (default)
    In2Transpose, // transposition CV; channel 2 follows IN 1
    In2RoleLength
};

static const char *const In2RoleNames[] = {"PITCH", "TRANSP"};

// Transposition ranges, in scale degrees. The unipolar ranges are first and one
// of them is the default on purpose: at 0 V they transpose by nothing, so
// enabling the mode with nothing patched cannot silently detune the module.
enum TransposeRange : uint8_t {
    TrUp7 = 0,
    TrUp12,
    TrBi7,
    TrBi12,
    TransposeRangeLength
};

static const char *const TransposeRangeNames[] = {"+7", "+12", "-7/+7", "-12/+12"};
static const int TransposeRangeLow[] = {0, 0, -7, -12};
static const int TransposeRangeHigh[] = {7, 12, 7, 12};

uint8_t in2Role = In2Pitch;
uint8_t transposeRange = TrUp7;
int transposeDegrees = 0; // current transposition, in scale degrees

// Deadband around the step boundary, in degrees. Without it the reading sits on
// the edge between two degrees and trills between them — the same failure the
// note quantizer's hysteresis exists to prevent.
static constexpr float TRANSPOSE_HYSTERESIS = 0.15f;

// ── extern refs defined in main.cpp ──────────────────────────────────────────
extern CalibrationData cal;

// ── TRIG input ───────────────────────────────────────────────────────────────
volatile bool trigPending = false;      // set by the ISR, drained by the loop
volatile unsigned long lastTrigUs = 0;  // for debouncing
volatile bool trigSeen = false;         // has any edge arrived yet?
volatile bool trigLevel = false;        // current input level (Gate mode)

// Rising-edge ISR on CLK_IN_PIN. Must stay trivial — it only queues the edge.
// trigSeen guards the debounce window so the very first edge is never dropped:
// on hardware micros() is already large at boot, but the VCV engine starts its
// clock at zero, where `now - lastTrigUs` would otherwise read as 0.
void TriggerReceived() {
    unsigned long now = micros();
    if (trigSeen && now - lastTrigUs < TRIG_DEBOUNCE_US) {
        return;
    }
    trigSeen = true;
    lastTrigUs = now;
    trigPending = true;
}

// Atomically take the pending edge, if any.
bool ConsumeTrigger() {
    bool pending;
    noInterrupts();
    pending = trigPending;
    trigPending = false;
    interrupts();
    return pending;
}

// ─────────────────────────────────────────────────────────────────────────────
// Read one ADC channel and apply linear calibration.
// Raw ADC → millivolts via per-channel coefficients (mv = scale*raw + offset),
// then → 0–4095 (0–5V scale) so downstream code sees the same 12-bit range.
// Falls back to nominal scaling when calibration data is not yet valid.
// ─────────────────────────────────────────────────────────────────────────────
void AdjustADCReadings(int CV_IN_PIN, int ch) {
    int32_t sum = 0;
    for (int i = 0; i < CV_OVERSAMPLE_SAMPLES; i++) {
        sum += analogRead(CV_IN_PIN);
    }
    int raw = (int)(sum / CV_OVERSAMPLE_SAMPLES);

    float mv;
    if (cal.valid) {
        mv = cal.cvScale[ch] * raw + cal.cvOffset[ch];
    } else {
        mv = (float)raw * 5000.0f / 4095.0f;
    }
    channelADC[ch] = constrain(mv * 4095.0f / 5000.0f, 0.0f, 4095.0f);
}

// Poll both pitch CV inputs and smooth them.
void HandleCVInputs() {
    for (int i = 0; i < NUM_CV_INS; i++) {
        oldChannelADC[i] = channelADC[i];
        AdjustADCReadings(CV_IN_PINS[i], i);
        // ONE_POLE(out, in, coeff): out += coeff * (in - out)
        //   out = new raw reading, in = previous filtered value
        ONE_POLE(channelADC[i], oldChannelADC[i], CV_FILTER_COEFF);
    }
}

// Sample the TRIG input level (Gate mode follows it directly).
void HandleTriggerLevel() {
    trigLevel = digitalRead(CLK_IN_PIN) == HIGH;
}

// Map IN 2 onto a whole number of scale degrees, with a deadband so the value
// does not flicker between two degrees when the CV rests on a boundary.
// A no-op (and leaves transposeDegrees at 0) unless IN 2 is in transpose mode.
void HandleTransposeInput() {
    if (in2Role != In2Transpose) {
        transposeDegrees = 0;
        return;
    }
    int idx = constrain((int)transposeRange, 0, (int)TransposeRangeLength - 1);
    float low = (float)TransposeRangeLow[idx];
    float high = (float)TransposeRangeHigh[idx];
    float degrees = low + (channelADC[1] / 4095.0f) * (high - low);

    // Only move once the reading has crossed the midpoint between the current
    // degree and the next by the hysteresis margin.
    float delta = degrees - (float)transposeDegrees;
    if (delta < 0) {
        delta = -delta;
    }
    if (delta > 0.5f + TRANSPOSE_HYSTERESIS) {
        transposeDegrees = constrain((int)lroundf(degrees), (int)low, (int)high);
    }
}
