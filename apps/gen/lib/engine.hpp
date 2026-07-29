#pragma once

// engine.hpp — GravityForge's per-iteration engine step.
//
// Shared by the two hosts that run this module: the unified firmware
// (src/gen_app.cpp) and the VCV Rack port (vcv-plugin/src/engine/fw_engine.cpp).
// It used to be written out in both, which is how the Rack port came to be
// missing its LOOP>NAP muting call.
//
// Include AFTER the globals below are defined — the convention menuHandlers.hpp
// already follows. Each host declares them, because each owns its own set.

#include "boardPinouts.hpp"
#include "clock.hpp"
#include "cvInputs.hpp"
#include "params.hpp"
#include "physics.hpp"
#include "sequencer.hpp"

extern PhysicsWorld physicsWorld;
extern GravityChannel channels[NUM_CHANNELS];
extern ContainerParams containerParams[2];
extern WorldParams worldParams;
extern Clock clockEngine;
extern ModBus modBus;

// What IN 1 does with a rising edge, per the menu-selected role.
inline void HandleTriggerRole(unsigned long edgeUs) {
    switch (in1Role) {
    case In1Clock:
        clockEngine.ExternalEdge(edgeUs);
        break;
    case In1Reset:
        physicsWorld.Reset();
        break;
    case In1Kick:
        physicsWorld.Kick(180.0f);
        break;
    case In1Spawn:
        // Wraps back to the minimum rather than saturating: a spawn input that
        // silently stops doing anything after eight pulses reads as broken.
        for (int i = 0; i < 2; i++) {
            int n = containerParams[i].balls + 1;
            containerParams[i].balls =
                (uint8_t)(n > PHYS_MAX_BALLS ? PHYS_MIN_BALLS : n);
        }
        MarkUnsaved();
        REQUEST_DISPLAY_REFRESH();
        break;
    default:
        break;
    }
}

// Advance the simulation and push all four DAC outputs.
inline void HandleOutputs() {
    const unsigned long now = micros();

    unsigned long edgeUs = now;
    if (ConsumeTrigger(&edgeUs)) {
        HandleTriggerRole(edgeUs);
    }
    HandleTriggerLevel();

    clockEngine.Update(now);

    // Base parameters + this loop's CV modulation → the live simulation.
    BuildModBus(modBus);
    ApplyParams(physicsWorld, clockEngine, containerParams, worldParams, modBus);

    physicsWorld.Advance(now);

    // Consumed once and handed to both channels: two calls would give the
    // boundary to channel A and nothing to channel B.
    const bool boundary = clockEngine.ConsumeBoundary();

    for (int i = 0; i < NUM_CHANNELS; i++) {
        channels[i].SetGateHigh(trigLevel);
        // LOOP > NAP silences the voice while the simulation keeps running, so
        // the phrase stays in phase across the rest.
        channels[i].SetMuted(physicsWorld.LoopMuted(i));
        channels[i].Process(physicsWorld.Get(i), now, clockEngine, boundary);
    }

    DACWriteAll(channels[0].GetCVOutput(), channels[1].GetCVOutput(),
                channels[0].GetGateOutput(), channels[1].GetGateOutput());
}
