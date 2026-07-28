#pragma once

// params.hpp — the module's *base* parameter block, and how it reaches the sim.
//
// Why this is separate from Container: CV modulation has to sit on top of the
// user's setting without destroying it. If the modulation wrote straight into
// the container, a patched CV input would overwrite the stored gravity and the
// menu would show whatever the CV last happened to be — and saving a preset
// would capture the modulated value rather than the setting.
//
// So the split is:
//   ContainerParams / WorldParams  — what the user set (menu edits this, presets
//                                    store this)
//   ModBus                         — this loop's modulation offsets, rebuilt from
//                                    the CV inputs every pass
//   ApplyParams()                  — base + modulation → the live Container
//
// Container itself stays a pure simulation with no notion of where its numbers
// came from, which is what keeps it unit-testable.

#ifdef UNIT_TEST
#include "ArduinoFake.h"
#else
#include <Arduino.h>
#endif

#include "clock.hpp"
#include "physics.hpp"

// Parameter ranges — also the clamp limits the menu setters use.
#define PARAM_GRAVITY_MIN 20.0f
#define PARAM_GRAVITY_MAX 900.0f
#define PARAM_BOUNCE_MIN 0.10f
#define PARAM_BOUNCE_MAX 0.98f
#define PARAM_GRIP_MIN 0.0f
#define PARAM_GRIP_MAX 1.0f
#define PARAM_FREEHZ_MIN 0.0f
#define PARAM_FREEHZ_MAX 4.0f

// Loop / phrase mode. The same parameter set and the same ranges as ClockForge's
// Loops, so the two modules behave identically where they overlap.
#define PARAM_LOOP_BEATS_MAX 64 // 0 = off
#define PARAM_LOOP_WAKE_MIN 1
#define PARAM_LOOP_WAKE_MAX 16
#define PARAM_LOOP_NAP_MAX 16 // 0 = never nap
#define PARAM_LOOP_SHIFT_MAX 16

struct ContainerParams {
    float gravity = 220.0f;
    float bounce = 0.72f;
    float grip = 0.30f;
    uint8_t spin = Spin4; // beats per revolution
    bool reverse = false;
    float freeHz = 0.25f; // used only when spin == SpinFree
    uint8_t balls = 3;
    uint8_t pegs = 8;
    uint16_t pegMask = 0xFFFF;
};

struct WorldParams {
    float proximity = 0.0f; // 0..1
    float coupling = 0.6f;  // 0..1

    // ── Loop / phrase mode ──
    // The length is in beats because that is the unit the phrase is heard in;
    // PhysicsWorld converts it to an exact step count and does the rewind on a
    // step boundary. Nap/wake/shift count whole loops.
    uint8_t loopBeats = 0; // 0 = off
    uint8_t loopWake = 1;  // loops a container speaks before napping
    uint8_t loopNap = 0;   // loops it stays quiet for (0 = never nap)
    uint8_t loopShift[2] = {0, 0}; // per-container offset into that cycle
};

// This loop's modulation offsets. Cleared and refilled every pass, so a CV that
// stops moving simply stops contributing — there is no state to unwind.
struct ModBus {
    float gravity[2] = {0.0f, 0.0f};   // additive, px/s²
    float bounce[2] = {0.0f, 0.0f};    // additive
    float spinScale[2] = {0.0f, 0.0f}; // additive on a 1.0 base multiplier
    float balls[2] = {0.0f, 0.0f};     // additive, whole balls
    float pegs[2] = {0.0f, 0.0f};      // additive, whole pegs
    float proximity = 0.0f;            // additive, 0..1 domain
    float coupling = 0.0f;             // additive, 0..1 domain

    void Clear() {
        for (int i = 0; i < 2; i++) {
            gravity[i] = bounce[i] = spinScale[i] = balls[i] = pegs[i] = 0.0f;
        }
        proximity = coupling = 0.0f;
    }
};

// Push base + modulation into the live simulation. Called once per loop, before
// PhysicsWorld::Advance().
inline void ApplyParams(PhysicsWorld &world, const Clock &clk,
                        const ContainerParams cp[2], const WorldParams &wp,
                        const ModBus &mod) {
    for (int i = 0; i < 2; i++) {
        Container &c = world.Get(i);
        const ContainerParams &p = cp[i];

        c.SetGravity(constrain(p.gravity + mod.gravity[i], PARAM_GRAVITY_MIN, PARAM_GRAVITY_MAX));
        c.SetRestitution(constrain(p.bounce + mod.bounce[i], PARAM_BOUNCE_MIN, PARAM_BOUNCE_MAX));
        c.SetSpinGrip(p.grip);

        // Spin is modulated as a multiplier on the clock-derived rate rather
        // than by stepping the beats-per-revolution list: the list is
        // deliberately coarse (musical ratios), and a CV sweeping across it
        // would jump between rates instead of gliding.
        float omega = clk.OmegaFor(p.spin, p.reverse, p.freeHz);
        float scale = constrain(1.0f + mod.spinScale[i], 0.0f, 3.0f);
        c.SetOmega(omega * scale);

        c.SetBallCount((int)lroundf((float)p.balls + mod.balls[i]));
        c.SetPegCount((int)lroundf((float)p.pegs + mod.pegs[i]));
        c.SetPegMask(p.pegMask);
    }

    world.SetProximity(constrain(wp.proximity + mod.proximity, 0.0f, 1.0f));
    world.SetCoupling(constrain(wp.coupling + mod.coupling, 0.0f, 1.0f));

    // Loop length in beats → exact 1 ms steps. Recomputed every pass so a tempo
    // change is picked up, but PhysicsWorld only latches it at a loop boundary
    // (see SetLoop) — the phrase must end where it started.
    unsigned long loopSteps = 0;
    if (wp.loopBeats > 0) {
        loopSteps = (unsigned long)wp.loopBeats * clk.BeatUs() / PHYS_STEP_US;
        if (loopSteps < 1) {
            loopSteps = 1;
        }
    }
    world.SetLoop((int)wp.loopBeats, loopSteps, (int)wp.loopWake, (int)wp.loopNap,
                  (int)wp.loopShift[0], (int)wp.loopShift[1]);
}
