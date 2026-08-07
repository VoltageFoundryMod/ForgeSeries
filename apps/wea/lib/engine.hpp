#pragma once

// engine.hpp — WeaveForge's per-iteration engine step.
//
// Shared by the two hosts that run this module: the unified firmware
// (src/wea_app.cpp) and the VCV Rack port. Writing it out in both is how
// GravityForge's Rack port came to be missing its LOOP▸NAP muting, so it lives
// here once.
//
// Include AFTER the globals below are defined — the convention menuHandlers.hpp
// already follows. Each host declares them, because each owns its own set.

#include "boardPinouts.hpp"
#include "clock.hpp"
#include "cvInputs.hpp"
#include "outputs.hpp"
#include "params.hpp"
#include "quantizer.hpp"
#include "shiftreg.hpp"

extern WeavePair registers;
extern OutputBank outputs;
extern StepClock clockEngine;
extern Quantizer quantizer;
extern RegParams regParams[WEA_NUM_REGS];
extern GlobalParams globalParams;
extern ModBus modBus;
extern LiveParams liveParams;
extern bool resetArmed;

// Put both registers back to a known pattern. RESET on a shift register cannot
// mean "go to step 0" the way it does on a step sequencer — there is no step 0,
// only a ring — so it means "start this phrase again from a defined state".
inline void ResetRegisters() {
    registers.Reg(0).SetValue(0xACE1u);
    registers.Reg(1).SetValue(0x1D87u);
}

// Advance the module and push all four DAC outputs.
inline void HandleOutputs() {
    const unsigned long now = micros();

    // Drain IN 1 first: an edge that arrived this pass should step this pass,
    // not next, or the module runs a loop late behind its clock.
    unsigned long edgeUs = now;
    if (ConsumeTrigger(&edgeUs)) {
        clockEngine.ExternalEdge(edgeUs);
    }

    clockEngine.Update(now);

    // Menu values + this pass's CV → what the engine actually runs with. Before
    // the step below, so a step landing this pass already sees this pass's
    // modulation rather than the previous one's.
    BuildModBus(modBus);
    ApplyParams(registers, regParams, globalParams, modBus, liveParams);

    // RESET is a gate, so it is edge-detected here rather than in the CV layer:
    // holding it high must not re-reset on every loop, which would freeze the
    // module rather than restart it.
    if (modBus.reset) {
        if (!resetArmed) {
            resetArmed = true;
            ResetRegisters();
        }
    } else {
        resetArmed = false;
    }

    if (clockEngine.ConsumeStep()) {
        registers.Clock(liveParams.weave, liveParams.dir, liveParams.chance);
        outputs.Step(registers, quantizer, liveParams.transpose, liveParams.rotate,
                     now);
    }

    // Every pass, not only on a step: trigger widths are wall-clock durations
    // and slew is a per-loop filter, so both would be quantised to the clock if
    // they were serviced inside the branch above.
    outputs.Update(now);

    DACWriteAll(outputs.DacValue(0), outputs.DacValue(1), outputs.DacValue(2),
                outputs.DacValue(3));
}
