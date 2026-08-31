#pragma once

// engine.hpp — ClockForge's per-iteration engine step and transport.
//
// Shared by the two hosts that run this module: the unified firmware
// (src/clk_app.cpp) and the VCV Rack port (vcv-plugin/src/engine/fw_engine.cpp).
//
// The metrics calls are in here rather than being the reason for two copies:
// metrics.hpp is in core/ and its instance is shared, so the Rack build carries
// them too. They cost a counter increment and keep the hosts identical.
//
// Include AFTER the globals below are defined — the convention menuHandlers.hpp
// already follows.

#include "boardPinouts.hpp"
#include "clockEngine.hpp"
#include "cvInputs.hpp"
#include "expander.hpp"
#include "metrics.hpp"
#include "outputs.hpp"

extern Output outputs[NUM_MAX_OUTPUTS];
extern bool masterState;

// Global play/stop. Declared by cvInputs.hpp (a CV target can drive it) and
// referenced from MENU_ITEMS.
inline void SetMasterState(bool state) {
    // Coming back from stopped restarts the count rather than resuming mid-bar.
    if (!masterState && state) {
        tickCounter = 0;
        externalTickCounter = 0;
    }
    masterState = state;
    for (int i = 0; i < NUM_MAX_OUTPUTS; i++) {
        outputs[i].SetMasterState(state);
    }
}

inline void ToggleMasterState() { SetMasterState(!masterState); }

// Drive all four outputs for one iteration.
inline void HandleOutputs() {
    // Pass 1: each output's raw (pre-quantisation) value plus a normalised
    // snapshot. Cross operations read the frozen snapshot so results are
    // order-independent — no feedback when two outputs cross-modulate.
    const int nOut = ActiveOutputs();
    float raw[NUM_MAX_OUTPUTS];
    float norm[NUM_MAX_OUTPUTS];
    for (int i = 0; i < nOut; i++) {
        raw[i] = outputs[i].ComputeRawOutput();
        norm[i] = raw[i] / (float)MAXDAC;
    }

    // Pass 2: apply cross operations against the snapshot, then quantise/clamp.
    uint16_t v[NUM_MAX_OUTPUTS] = {0};
    for (int i = 0; i < nOut; i++) {
        float r = raw[i];
        if (outputs[i].HasCrossOp()) {
            const int src = outputs[i].GetCrossSourceIndex();
            float srcNorm;
            if (src < NUM_MAX_OUTPUTS) {
                srcNorm = norm[src]; // another output's pre-cross value
            } else {
                // IN1 / IN2 sampled CV, normalised 0..1 by the core adapter
                srcNorm = CvUni(channelCv[src - NUM_MAX_OUTPUTS]);
            }
            r = outputs[i].ApplyCrossOp(raw[i], srcNorm);
        }
        v[i] = (uint16_t)outputs[i].FinalizeOutput(r);
    }

    metrics.BeginDACMeasurement();
    DACWriteAll(v[0], v[1], v[2], v[3]);
    // Second transaction on the same bus, and only when an expander is fitted.
    // The two banks land one transaction apart — see DACWriteAllExp.
    if (nOut > NUM_OUTPUTS)
        DACWriteAllExp(v[4], v[5], v[6], v[7]);
    metrics.EndDACMeasurement();

    for (int i = 0; i < nOut; i++) {
        outputs[i].GenEnvelope();
    }
}
