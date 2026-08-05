#pragma once

// cvInputs.hpp — CV acquisition, the modulation matrix, and IN 1's role.
//
// ── CV RANGE ─────────────────────────────────────────────────────────────────
// The current hardware revision accepts 0-5 V on IN 2 / IN 3. A later revision
// moves to +/-5 V. Every modulation target reads its CV through CvNorm() or
// CvBipolar() below and NOTHING else assumes a polarity, so that hardware change
// stays a one-function edit rather than an audit of every target.
//
//   CvNorm(ch)     -> 0..1   — "amount" controls (couple, level)
//   CvBipolar(ch)  -> -1..1  — "offset"/"trim" controls (speed, parameters)

#ifdef UNIT_TEST
#include "ArduinoFake.h"
#else
#include <Arduino.h>
#endif

#include "attractors.hpp"
#include "boardIO.hpp"
#include "boardPinouts.hpp"
#include "calibrationData.hpp"
#include "cvInput.hpp" // shared acquisition + range adapters
#include "params.hpp"
#include "utils.hpp"

// One-pole smoothing on the CV inputs. As heavy as GravityForge's: nothing here
// is pitch, so a little lag costs nothing and keeps a noisy CV from making the
// integrator jitter.
static constexpr float CV_FILTER_COEFF = 0.35f;

// Ignore IN 1 edges closer together than this — contact bounce on a fast gate
// would otherwise fire several re-seeds per pulse.
static constexpr unsigned long TRIG_DEBOUNCE_US = 1000;

// Calibrated, filtered CV per input, normalised (see core/cvInput.hpp).
float channelCv[NUM_CV_INS], oldChannelCv[NUM_CV_INS];

// ── IN 1 role ────────────────────────────────────────────────────────────────
// One jack, several jobs; the hardware has no switched jacks, so the choice is
// explicit rather than detected.
//
// FREEZE is the odd one out: it is the only role that reads the jack's LEVEL
// rather than its edges, because "hold while high" is what makes it playable
// with a gate. See HandleOutputs() in engine.hpp.
enum In1Role : uint8_t {
    In1Reset = 0, // re-seed both orbits
    In1ResetA,    // ...just generator A
    In1ResetB,    // ...just generator B
    In1Freeze,    // hold both orbits while the input is high
    In1RoleLength
};
static const char *const In1RoleNames[] = {"RESET", "RESET A", "RESET B", "FREEZE"};

uint8_t in1Role = In1Reset;

// ── Modulation targets ───────────────────────────────────────────────────────
// Deliberately a short list of the parameters that are musical to sweep. Only
// the first two system parameters are offered: on every system here they are the
// ones that reshape the attractor (Lorenz RHO, Rössler C, Chua ALPHA), while the
// later ones mostly rescale it.
enum CVTarget : uint8_t {
    CVNone = 0,
    CVSpeedA,
    CVSpeedB,
    CVSpeedBoth,
    CVParam1A,
    CVParam1B,
    CVParam2A,
    CVParam2B,
    CVLevelA,
    CVLevelB,
    CVLevelBoth,
    CVOffsetA,
    CVOffsetB,
    CVCouple,
    // Append, never insert: presets store the target as a raw index, so a new
    // entry in the middle would silently re-point every saved patch's modulation.
    CVTargetLength
};

static const char *const CVTargetNames[] = {
    "OFF", "SPD A", "SPD B", "SPD AB", "P1 A", "P1 B", "P2 A",
    "P2 B", "LVL A", "LVL B", "LVL AB", "OFS A", "OFS B", "COUPLE"};

// Per-input target and depth (0..100 %).
uint8_t cvTarget[NUM_CV_INS] = {CVSpeedBoth, CVCouple};
uint8_t cvDepth[NUM_CV_INS] = {0, 0};

// How far each target moves at 100 % depth.
//
// SPEED is in octaves because it is a rate — +/-3 octaves is a 64:1 span, which
// covers everything from a barely-moving drift to a buzz without ever reaching
// the clamp at either end of a normal setting.
#define CV_SPEED_OCTAVES 3.0f

// ── IN 1 edge queue ──────────────────────────────────────────────────────────
volatile bool trigPending = false;
volatile unsigned long lastTrigUs = 0;
volatile bool trigSeen = false;
volatile bool trigLevel = false;
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

void HandleCVInputs() {
    for (int i = 0; i < NUM_CV_INS; i++) {
        oldChannelCv[i] = channelCv[i];
        // Oversampling and calibration live in core/cvInput.hpp — the same
        // acquisition path every module uses.
        channelCv[i] = CvRead(i);
        ONE_POLE(channelCv[i], oldChannelCv[i], CV_FILTER_COEFF);
    }
}

void HandleTriggerLevel() { trigLevel = digitalRead(CLK_IN_PIN) == HIGH; }

// ── The two range adapters (see the header comment) ──────────────────────────
inline float CvNorm(int ch) {
    if (ch < 0 || ch >= NUM_CV_INS) {
        return 0.0f;
    }
    return CvUni(channelCv[ch]);
}

inline float CvBipolar(int ch) {
    if (ch < 0 || ch >= NUM_CV_INS) {
        return 0.0f;
    }
    return CvBi(channelCv[ch]);
}

// ─────────────────────────────────────────────────────────────────────────────
// Build this pass's modulation offsets from the CV inputs.
//
// `gp` is read for the parameter ranges only: a parameter's depth has to be
// expressed as a fraction of ITS OWN span, because the spans differ by four
// orders of magnitude across the twelve systems (Finance's A lives in 0.0001 ..
// 0.01, Chen's in 20 .. 50). A single absolute depth would be inaudible on one
// and destroy the attractor on the other.
// ─────────────────────────────────────────────────────────────────────────────
void BuildModBus(ModBus &mod, const GenParams gp[2]) {
    mod.Clear();

    for (int i = 0; i < NUM_CV_INS; i++) {
        if (cvTarget[i] == CVNone || cvDepth[i] == 0) {
            continue;
        }
        const float depth = (float)cvDepth[i] / 100.0f;
        const float uni = CvNorm(i);    // 0..1
        const float bip = CvBipolar(i); // -1..1

        switch (cvTarget[i]) {
        case CVSpeedA:
            mod.speedOct[0] += bip * depth * CV_SPEED_OCTAVES;
            break;
        case CVSpeedB:
            mod.speedOct[1] += bip * depth * CV_SPEED_OCTAVES;
            break;
        case CVSpeedBoth:
            mod.speedOct[0] += bip * depth * CV_SPEED_OCTAVES;
            mod.speedOct[1] += bip * depth * CV_SPEED_OCTAVES;
            break;

        case CVParam1A:
            mod.param[0][0] += bip * depth * ParamHalfSpan(gp[0].system, 0);
            break;
        case CVParam1B:
            mod.param[1][0] += bip * depth * ParamHalfSpan(gp[1].system, 0);
            break;
        case CVParam2A:
            mod.param[0][1] += bip * depth * ParamHalfSpan(gp[0].system, 1);
            break;
        case CVParam2B:
            mod.param[1][1] += bip * depth * ParamHalfSpan(gp[1].system, 1);
            break;

        // LEVEL modulates DOWNWARD from the menu setting, so a patched CV makes
        // the jack a VCA rather than something that fights the setting: at 0 V
        // the output is silent-at-centre and at 5 V it is exactly what the menu
        // says. Modulating upward would do nothing at the default LEVEL 100 %,
        // which is where the control is nearly always left.
        case CVLevelA:
            mod.level[0] += (uni - 1.0f) * depth;
            break;
        case CVLevelB:
            mod.level[1] += (uni - 1.0f) * depth;
            break;
        case CVLevelBoth:
            mod.level[0] += (uni - 1.0f) * depth;
            mod.level[1] += (uni - 1.0f) * depth;
            break;

        case CVOffsetA:
            mod.offset[0] += bip * depth;
            break;
        case CVOffsetB:
            mod.offset[1] += bip * depth;
            break;

        // COUPLE reads best as an absolute amount, so it takes the unipolar
        // adapter: leave the menu at 0 and let the CV pull the two orbits
        // together. A slow LFO here is the single most direct way to get a patch
        // that breathes.
        case CVCouple:
            mod.couple += uni * depth;
            break;

        case CVNone:
        default:
            break;
        }
    }
}
