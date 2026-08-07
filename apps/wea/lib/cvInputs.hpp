#pragma once

// cvInputs.hpp — CV acquisition, the modulation matrix, and IN 1.
//
// ── IN 1 IS ALWAYS THE CLOCK ─────────────────────────────────────────────────
// There is no role menu here, unlike GravityForge and ChaosForge. Those run a
// simulation with a life of its own, so IN 1 is free to mean RESET or KICK. A
// shift register has no state evolution between clocks — unclocked, this module
// is a static voltage — so spending the only interrupt-capable jack on anything
// else would make it inoperable in its default patch. RESET and the ShiftReg
// unlock gate are CV targets on IN 2/IN 3 instead. Design.md §1.
//
// ── CV RANGE ─────────────────────────────────────────────────────────────────
// Everything reads through CvNorm()/CvBipolar(), which defer to core, so the
// 0–5 V → ±5 V hardware change stays the -DFORGE_CV_BIPOLAR build flag.

#ifdef UNIT_TEST
#include "ArduinoFake.h"
#else
#include <Arduino.h>
#endif

#include "boardIO.hpp"
#include "boardPinouts.hpp"
#include "calibrationData.hpp"
#include "cvInput.hpp" // shared acquisition + range adapters
#include "params.hpp"
#include "utils.hpp"

// Light smoothing. Lighter than GravityForge's: LENGTH and ROTATE are quantised
// to integers here, so a heavy filter only adds lag to a value that is going to
// be rounded anyway.
static constexpr float CV_FILTER_COEFF = 0.25f;

// Ignore IN 1 edges closer together than this — contact bounce on a fast gate
// would otherwise shift the register several times per pulse.
static constexpr unsigned long TRIG_DEBOUNCE_US = 1000;

// Where a gate-style CV target crosses. Hysteresis is not needed: both targets
// are latching or level-sensitive, not edge-counted.
static constexpr float CV_GATE_THRESHOLD = 0.5f;

// Calibrated, filtered CV per input, normalised (see core/cvInput.hpp).
float channelCv[NUM_CV_INS], oldChannelCv[NUM_CV_INS];

// ── Modulation targets ───────────────────────────────────────────────────────
enum CVTarget : uint8_t {
    CVNone = 0,
    CVLenA,
    CVLenB,
    CVLenBoth,
    CVChanceA,
    CVChanceB,
    CVChanceBoth,
    CVWeave,
    CVTranspose,
    CVRotate,
    CVReset,
    CVLock,
    // Appended, never inserted: presets store the target as a raw index, so a
    // new entry in the middle would silently re-point every saved patch.
    CVTargetLength
};

static const char *const CVTargetNames[] = {
    "OFF",   "LEN A",  "LEN B", "LEN AB", "CHNC A", "CHNC B",
    "CHNC AB", "WEAVE", "TRANS", "ROTATE", "RESET",  "LOCK"};

uint8_t cvTarget[NUM_CV_INS] = {CVWeave, CVChanceBoth};
uint8_t cvDepth[NUM_CV_INS] = {0, 0};

// ── IN 1 edge queue ──────────────────────────────────────────────────────────
volatile bool trigPending = false;
volatile unsigned long lastTrigUs = 0;
volatile bool trigSeen = false;
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

void HandleCVInputs() {
    for (int i = 0; i < NUM_CV_INS; i++) {
        oldChannelCv[i] = channelCv[i];
        channelCv[i] = CvRead(i);
        ONE_POLE(channelCv[i], oldChannelCv[i], CV_FILTER_COEFF);
    }
}

// ── The two range adapters ───────────────────────────────────────────────────
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
// Build this loop's modulation offsets from the CV inputs.
//
// Depth scales each target over a range chosen so 100 % is a musically useful
// full sweep: LENGTH spans its whole 2–16, CHANCE and WEAVE their whole 0–100.
// ─────────────────────────────────────────────────────────────────────────────
void BuildModBus(ModBus &mod) {
    mod.Clear();

    for (int i = 0; i < NUM_CV_INS; i++) {
        if (cvTarget[i] == CVNone) {
            continue;
        }

        // RESET and LOCK are gates, not amounts — they act on level and ignore
        // depth, so an unset depth does not silently disable them.
        if (cvTarget[i] == CVReset) {
            mod.reset |= CvNorm(i) > CV_GATE_THRESHOLD;
            continue;
        }
        if (cvTarget[i] == CVLock) {
            mod.lock |= CvNorm(i) > CV_GATE_THRESHOLD;
            continue;
        }

        if (cvDepth[i] == 0) {
            continue;
        }
        const float depth = (float)cvDepth[i] / 100.0f;
        const float uni = CvNorm(i);    // 0..1
        const float bip = CvBipolar(i); // -1..1

        switch (cvTarget[i]) {
        case CVLenA:
            mod.length[0] += bip * depth * (float)(WEA_MAX_LENGTH - WEA_MIN_LENGTH);
            break;
        case CVLenB:
            mod.length[1] += bip * depth * (float)(WEA_MAX_LENGTH - WEA_MIN_LENGTH);
            break;
        case CVLenBoth:
            mod.length[0] += bip * depth * (float)(WEA_MAX_LENGTH - WEA_MIN_LENGTH);
            mod.length[1] += bip * depth * (float)(WEA_MAX_LENGTH - WEA_MIN_LENGTH);
            break;

        // CHANCE and WEAVE take the bipolar adapter: they are trims around
        // whatever the menu is set to, so a centred CV leaves the panel setting
        // alone and the knob still means something with a cable patched.
        case CVChanceA:
            mod.chance[0] += bip * depth * 100.0f;
            break;
        case CVChanceB:
            mod.chance[1] += bip * depth * 100.0f;
            break;
        case CVChanceBoth:
            mod.chance[0] += bip * depth * 100.0f;
            mod.chance[1] += bip * depth * 100.0f;
            break;
        case CVWeave:
            mod.weave += bip * depth * 100.0f;
            break;

        case CVTranspose:
            mod.transpose += bip * depth * 24.0f;
            break;
        case CVRotate:
            mod.rotate += uni * depth * 31.0f;
            break;

        case CVNone:
        default:
            break;
        }
    }
}
