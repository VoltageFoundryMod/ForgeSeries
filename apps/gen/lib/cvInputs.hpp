#pragma once

// cvInputs.hpp — CV acquisition, the modulation matrix, and IN 1's role.
//
// ── CV RANGE ─────────────────────────────────────────────────────────────────
// The current hardware revision accepts 0–5 V on IN 2 / IN 3. A later revision
// moves to ±5 V. Every modulation target reads its CV through CvNorm() or
// CvBipolar() below and NOTHING else assumes a polarity, so that hardware change
// is a one-function edit rather than an audit of every target.
//
//   CvNorm(ch)     → 0..1   — "amount" controls (depth, proximity, ball count)
//   CvBipolar(ch)  → -1..1  — "offset" controls (gravity trim, spin trim)
//
// On the present unipolar hardware CvBipolar() maps 0–5 V onto -1..1 with 2.5 V
// as the centre. On the bipolar hardware it becomes the direct reading and
// CvNorm() becomes the rectified/offset one. Both keep their contracts.

#ifdef UNIT_TEST
#include "ArduinoFake.h"
#else
#include <Arduino.h>
#endif

#include "boardIO.hpp"
#include "calibrationData.hpp"
#include "params.hpp"
#include "pinouts.hpp"
#include "utils.hpp"

// Oversampling per read — averages out RP2040 ADC noise.
static constexpr int CV_OVERSAMPLE_SAMPLES = 8;

// One-pole smoothing. Heavier than NoteForge's: nothing here is pitch, so a
// little lag costs nothing and keeps a noisy CV from making the physics jitter.
static constexpr float CV_FILTER_COEFF = 0.35f;

// Ignore IN 1 edges closer together than this — contact bounce on a fast gate
// would otherwise fire several resets/kicks per pulse.
static constexpr unsigned long TRIG_DEBOUNCE_US = 1000;

// Calibrated, filtered CV per input (0..4095 == 0..5V).
float channelADC[NUM_CV_INS], oldChannelADC[NUM_CV_INS];

extern CalibrationData cal;

// ── IN 1 role ────────────────────────────────────────────────────────────────
// One jack, several jobs. Auto-detection is impossible — the hardware has no
// switched jacks and cannot tell an unpatched input from one resting low — so
// the choice is explicit.
enum In1Role : uint8_t {
    In1Clock = 0, // sets the tempo, and therefore the rotation rate
    In1Reset,     // re-place all balls, zero the rotation
    In1Kick,      // impulse every ball — shake the containers
    In1Spawn,     // add a ball to both containers, wrapping at the maximum
    In1RoleLength
};
static const char *const In1RoleNames[] = {"CLOCK", "RESET", "KICK", "SPAWN"};

uint8_t in1Role = In1Clock;

// ── Modulation targets ───────────────────────────────────────────────────────
// Deliberately a short list of the parameters that are musical to sweep. The
// "both" variants exist because moving the two containers together is the most
// common thing you actually want from a single CV.
enum CVTarget : uint8_t {
    CVNone = 0,
    CVGravityA,
    CVGravityB,
    CVGravityBoth,
    CVSpinA,
    CVSpinB,
    CVSpinBoth,
    CVBounceBoth,
    CVBallsBoth,
    CVPegsA,
    CVPegsB,
    CVProximity,
    CVCoupling,
    CVTargetLength
};

static const char *const CVTargetNames[] = {
    "OFF", "GRAV A", "GRAV B", "GRAV AB", "SPIN A", "SPIN B", "SPIN AB",
    "BOUNCE", "BALLS", "PEGS A", "PEGS B", "PROX", "COUPLE"};

// Per-input target and depth (0..100 %).
uint8_t cvTarget[NUM_CV_INS] = {CVProximity, CVGravityBoth};
uint8_t cvDepth[NUM_CV_INS] = {0, 0};

// ── IN 1 edge queue ──────────────────────────────────────────────────────────
volatile bool trigPending = false;
volatile unsigned long lastTrigUs = 0;
volatile bool trigSeen = false;
volatile bool trigLevel = false;
// Timestamp of the edge, captured in the ISR. The clock needs the edge time,
// not the time the main loop got around to draining it, or the derived tempo
// would carry the loop's jitter.
volatile unsigned long trigEdgeUs = 0;

// Rising-edge ISR on CLK_IN_PIN. Stays trivial — it only queues the edge.
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
    trigEdgeUs = now;
    trigPending = true;
}

// Atomically take the pending edge, if any, along with when it happened.
bool ConsumeTrigger(unsigned long *edgeUs = nullptr) {
    bool pending;
    noInterrupts();
    pending = trigPending;
    trigPending = false;
    if (edgeUs) {
        *edgeUs = trigEdgeUs;
    }
    interrupts();
    return pending;
}

// ─────────────────────────────────────────────────────────────────────────────
// Read one ADC channel and apply linear calibration.
// Raw ADC → millivolts via per-channel coefficients, then → 0–4095 (0–5V scale).
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

void HandleCVInputs() {
    for (int i = 0; i < NUM_CV_INS; i++) {
        oldChannelADC[i] = channelADC[i];
        AdjustADCReadings(CV_IN_PINS[i], i);
        ONE_POLE(channelADC[i], oldChannelADC[i], CV_FILTER_COEFF);
    }
}

void HandleTriggerLevel() {
    trigLevel = digitalRead(CLK_IN_PIN) == HIGH;
}

// ── The two range adapters (see the header comment) ──────────────────────────
inline float CvNorm(int ch) {
    if (ch < 0 || ch >= NUM_CV_INS) {
        return 0.0f;
    }
    return constrain(channelADC[ch] / 4095.0f, 0.0f, 1.0f);
}

inline float CvBipolar(int ch) {
    // Unipolar hardware: 0–5 V maps onto -1..1 about 2.5 V.
    // Bipolar hardware: this becomes the direct reading.
    return constrain(CvNorm(ch) * 2.0f - 1.0f, -1.0f, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Build this loop's modulation offsets from the CV inputs.
//
// Depth scales each target over a range chosen so that 100 % is a musically
// useful full sweep rather than an arbitrary number — e.g. gravity spans most of
// its legal range, ball count spans the whole 1–8.
// ─────────────────────────────────────────────────────────────────────────────
void BuildModBus(ModBus &mod) {
    mod.Clear();

    for (int i = 0; i < NUM_CV_INS; i++) {
        if (cvTarget[i] == CVNone || cvDepth[i] == 0) {
            continue;
        }
        float depth = (float)cvDepth[i] / 100.0f;
        float uni = CvNorm(i);     // 0..1
        float bip = CvBipolar(i);  // -1..1

        switch (cvTarget[i]) {
        case CVGravityA:
            mod.gravity[0] += bip * depth * 500.0f;
            break;
        case CVGravityB:
            mod.gravity[1] += bip * depth * 500.0f;
            break;
        case CVGravityBoth:
            mod.gravity[0] += bip * depth * 500.0f;
            mod.gravity[1] += bip * depth * 500.0f;
            break;

        case CVSpinA:
            mod.spinScale[0] += bip * depth * 1.5f;
            break;
        case CVSpinB:
            mod.spinScale[1] += bip * depth * 1.5f;
            break;
        case CVSpinBoth:
            mod.spinScale[0] += bip * depth * 1.5f;
            mod.spinScale[1] += bip * depth * 1.5f;
            break;

        case CVBounceBoth:
            mod.bounce[0] += bip * depth * 0.4f;
            mod.bounce[1] += bip * depth * 0.4f;
            break;

        case CVBallsBoth:
            mod.balls[0] += uni * depth * (float)(PHYS_MAX_BALLS - PHYS_MIN_BALLS);
            mod.balls[1] += uni * depth * (float)(PHYS_MAX_BALLS - PHYS_MIN_BALLS);
            break;

        case CVPegsA:
            mod.pegs[0] += uni * depth * (float)(PHYS_MAX_PEGS - PHYS_MIN_PEGS);
            break;
        case CVPegsB:
            mod.pegs[1] += uni * depth * (float)(PHYS_MAX_PEGS - PHYS_MIN_PEGS);
            break;

        // Proximity and coupling live in 0..1 and read best as absolute
        // "amount" controls, so they use the unipolar adapter.
        case CVProximity:
            mod.proximity += uni * depth;
            break;
        case CVCoupling:
            mod.coupling += uni * depth;
            break;

        case CVNone:
        default:
            break;
        }
    }
}
