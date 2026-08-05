#pragma once

// params.hpp — the module's *base* parameter block, and how it reaches the sim.
//
// Same split as GravityForge, for the same reason: CV modulation has to sit on
// top of what the user set without destroying it. If modulation were written
// straight into the Generator, a patched CV would overwrite the stored SPEED,
// the menu would show whatever the CV last happened to be, and saving a preset
// would capture the modulated value rather than the setting.
//
//   GenParams / WorldParams  what the user set (the menu edits this, presets
//                            store this)
//   ModBus                   this pass's modulation offsets, rebuilt from the CV
//                            inputs every loop
//   ApplyParams()            base + modulation -> the live ChaosWorld
//
// Generator itself has no idea where its numbers came from, which is what keeps
// it unit-testable.

#ifdef UNIT_TEST
#include "ArduinoFake.h"
#else
#include <Arduino.h>
#endif

#include "attractors.hpp"
#include "generator.hpp"

struct GenParams {
    uint8_t system = AttLorenz;
    // Absolute values in each parameter's own units, not normalised: the menu
    // shows them as the literature names them (SIGMA 10.0, RHO 28.0), which is
    // the only way a published parameter set can be dialled in by hand.
    float param[ATT_MAX_PARAMS] = {10.0f, 28.0f, 2.667f, 0.0f};
    float speed = ATT_SPEED_DEFAULT;
    uint8_t src[2] = {AxisX, AxisY};
    uint8_t level = 100;   // %
    int8_t offset = 0;     // -100..100 %
    uint8_t smooth = 0;    // %
    uint8_t autoRange = 0; // 0 = the published window, 1 = tracked
};

struct WorldParams {
    float couple = 0.0f; // 0..1
};

// Load a system's published parameter values into a base block. Called whenever
// SYSTEM changes: parameter 1 means sigma on Lorenz and alpha on Chua, so
// carrying the old numbers across would hand the new system a set of values from
// a different equation — usually one with no attractor at all.
static inline void LoadSystemDefaults(GenParams &g, int id) {
    const AttractorSpec &sp = AttSpec(id);
    g.system = AttClampId(id);
    for (int i = 0; i < ATT_MAX_PARAMS; i++)
        g.param[i] = (i < sp.paramCount) ? sp.params[i].def : 0.0f;
}

// This pass's modulation offsets. Cleared and refilled every loop, so a CV that
// stops moving simply stops contributing — there is no state to unwind.
struct ModBus {
    // Octaves, applied multiplicatively: SPEED is a rate, and adding to a rate
    // that spans 0.01x to 16x would be inaudible at one end and violent at the
    // other.
    float speedOct[2] = {0.0f, 0.0f};
    float param[2][ATT_MAX_PARAMS] = {{0.0f}, {0.0f}};
    float level[2] = {0.0f, 0.0f};  // additive, 0..1 domain
    float offset[2] = {0.0f, 0.0f}; // additive, -1..1 domain
    float couple = 0.0f;            // additive, 0..1 domain

    void Clear() {
        for (int i = 0; i < 2; i++) {
            speedOct[i] = level[i] = offset[i] = 0.0f;
            for (int k = 0; k < ATT_MAX_PARAMS; k++)
                param[i][k] = 0.0f;
        }
        couple = 0.0f;
    }
};

// Push base + modulation into the live simulation. Called once per loop, before
// ChaosWorld::Advance().
inline void ApplyParams(ChaosWorld &world, const GenParams gp[2], const WorldParams &wp,
                        const ModBus &mod) {
    for (int i = 0; i < 2; i++) {
        Generator &g = world.Get(i);
        const GenParams &p = gp[i];

        // First: a system change re-seeds the orbit, so it has to happen before
        // the parameters that describe the new system are pushed in.
        g.SetSystem(p.system);

        const AttractorSpec &sp = AttSpec(p.system);
        for (int k = 0; k < ATT_MAX_PARAMS; k++) {
            if (k >= sp.paramCount) {
                g.SetParam(k, 0.0f);
                continue;
            }
            g.SetParam(k, constrain(p.param[k] + mod.param[i][k], sp.params[k].min,
                                    sp.params[k].max));
        }

        g.SetSpeed(p.speed * exp2f(mod.speedOct[i]));
        g.SetSource(0, p.src[0]);
        g.SetSource(1, p.src[1]);
        g.SetLevel(constrain((float)p.level / 100.0f + mod.level[i], 0.0f, 1.0f));
        g.SetOffset(constrain((float)p.offset / 100.0f + mod.offset[i], -1.0f, 1.0f));
        g.SetSmooth((float)p.smooth / 100.0f);
        g.SetAutoRange(p.autoRange != 0);
    }

    world.SetCouple(constrain(wp.couple + mod.couple, 0.0f, 1.0f));
}
