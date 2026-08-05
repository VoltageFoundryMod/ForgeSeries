#pragma once

// engine.hpp — ChaosForge's per-iteration engine step.
//
// Shared by the two hosts that run this module: the unified firmware
// (src/att_app.cpp) and the VCV Rack port (vcv-plugin/src/engine/fw_engine.cpp).
// Writing it out twice instead is how GravityForge's Rack port silently lost its
// LOOP-NAP muting; there is one copy here so the two cannot drift.
//
// Include AFTER the globals below are defined — the convention menuHandlers.hpp
// already follows. Each host declares them, because each owns its own set.

#include "boardPinouts.hpp"
#include "cvInputs.hpp"
#include "generator.hpp"
#include "params.hpp"

extern ChaosWorld world;
extern GenParams genParams[2];
extern WorldParams worldParams;
extern ModBus modBus;

// What IN 1 does with a rising edge, per the menu-selected role.
//
// FREEZE is absent on purpose: it is a level, not an edge, and is applied in
// HandleOutputs() below.
inline void HandleTriggerRole(unsigned long /*edgeUs*/) {
    switch (in1Role) {
    case In1Reset:
        world.Reseed();
        break;
    case In1ResetA:
        world.Reseed(0);
        break;
    case In1ResetB:
        world.Reseed(1);
        break;
    default:
        break;
    }
}

// Advance both orbits and push all four DAC outputs.
//
// Jack map — the pairing is the module's whole proposition, so it is fixed:
//   OUT 1 / OUT 2   generator A's two axes
//   OUT 3 / OUT 4   generator B's two axes
// Two outputs from one orbit are related but never equal, which is what an LFO
// pair cannot give you; two outputs from different orbits are unrelated until
// COUPLE says otherwise.
inline void HandleOutputs() {
    const unsigned long now = micros();

    unsigned long edgeUs = now;
    if (ConsumeTrigger(&edgeUs)) {
        HandleTriggerRole(edgeUs);
    }
    HandleTriggerLevel();

    // Base parameters + this pass's CV modulation -> the live simulation.
    BuildModBus(modBus, genParams);
    ApplyParams(world, genParams, worldParams, modBus);

    world.SetFrozen(in1Role == In1Freeze && trigLevel);
    world.Advance(now);

    DACWriteAll((uint16_t)lroundf(world.Get(0).Out01(0) * (float)MAXDAC),
                (uint16_t)lroundf(world.Get(0).Out01(1) * (float)MAXDAC),
                (uint16_t)lroundf(world.Get(1).Out01(0) * (float)MAXDAC),
                (uint16_t)lroundf(world.Get(1).Out01(1) * (float)MAXDAC));
}
