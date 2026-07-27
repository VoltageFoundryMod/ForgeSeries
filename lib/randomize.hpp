#pragma once

// randomize.hpp — "roll me a new patch" (platform-agnostic)
//
// One implementation shared by the hardware menu's RANDOM action and the VCV
// Rack plugin's Randomize (Ctrl+R), so the module behaves identically on the
// panel and in the host. Like presetManager.hpp this only touches the *base*
// parameter block (containerParams / worldParams) and the channels — never the
// live Container, which carries this instant's CV modulation.
//
// What it deliberately does NOT touch: tempo, clock divider, quantize grid, the
// IN 1 role and the CV modulation matrix. Those are patch-level routing and sync
// decisions — the things you cabled up on purpose — and rerolling them turns a
// "give me a new rhythm" gesture into "break my patch". VCV Rack draws the same
// line when it leaves ports alone on randomize.
//
// Every range below is narrower than the parameter's own min/max. The full
// ranges are all reachable by hand, but a uniform draw across them mostly yields
// containers that either barely ring a peg or buzz — neither reads as a sequence.

#include "params.hpp"
#include "physics.hpp"
#include "scales.hpp"
#include "sequencer.hpp"

extern GravityChannel channels[NUM_CHANNELS]; // src/main.cpp
extern ContainerParams containerParams[2];    // src/main.cpp
extern WorldParams worldParams;               // src/main.cpp
extern PhysicsWorld physicsWorld;             // src/main.cpp
extern Clock clockEngine;                     // src/main.cpp
extern ModBus modBus;                         // src/main.cpp

// The generator. PhysRandom is the firmware's own xorshift — self-contained, so
// this behaves the same on the RP2040 and under the VCV shim, where the host's
// std::rand state is not ours to disturb.
static PhysRandom _rndGen;

// Mixed in on every call so a power cycle followed by RANDOM does not replay the
// same patch. This is a user-triggered UI action, not DSP, so reading the clock
// here does not affect the simulation's determinism.
static inline void _RndReseed(uint32_t entropy) {
    _rndGen.Seed(_rndGen.Next() ^ entropy ^ 0x9E3779B9u);
}

// Uniform integer in [lo, hi] (inclusive).
static inline int _RndInt(int lo, int hi) {
    if (hi <= lo)
        return lo;
    return lo + (int)(_rndGen.Next() % (uint32_t)(hi - lo + 1));
}

// Uniform float in [lo, hi).
static inline float _RndFloat(float lo, float hi) {
    return lo + _rndGen.Unit() * (hi - lo);
}

// ─────────────────────────────────────────────────────────────────────────────
// Roll new physics, note and gate settings for both containers.
//
// `entropy` seeds the draw — pass micros() on the hardware, or the host's engine
// time in a plugin. Applies the result and re-seeds the simulation, so the caller
// only has to mark the preset dirty and refresh the display.
// ─────────────────────────────────────────────────────────────────────────────
inline void RandomizeParams(uint32_t entropy) {
    _RndReseed(entropy);

    for (int c = 0; c < 2; c++) {
        // ── Physics ──
        containerParams[c].gravity = _RndFloat(120.0f, 600.0f);
        containerParams[c].bounce = _RndFloat(0.55f, 0.92f);
        containerParams[c].grip = _RndFloat(0.0f, 0.8f);
        // Clock-locked ratios only: SpinFree needs a rate control that the
        // six-row hardware page has no space for, so it is not offered here.
        containerParams[c].spin = (uint8_t)_RndInt(0, (int)SpinFree - 1);
        containerParams[c].reverse = _RndInt(0, 1) != 0;
        containerParams[c].balls = (uint8_t)_RndInt(1, 5);
        containerParams[c].pegs = (uint8_t)_RndInt(PHYS_MIN_PEGS, 12);

        // Muting pegs is what opens the rhythm up, but a container with most of
        // its ring muted just goes quiet. Roll ~75 % active, then top back up to
        // at least half so no container can be randomized into silence.
        int pegs = containerParams[c].pegs;
        uint16_t mask = 0;
        for (int p = 0; p < pegs; p++) {
            if (_RndInt(0, 3) != 0)
                mask = (uint16_t)(mask | (1u << p));
        }
        int live = 0;
        for (int p = 0; p < pegs; p++)
            live += (mask >> p) & 1u;
        while (live < (pegs + 1) / 2) {
            int p = _RndInt(0, pegs - 1);
            if (!((mask >> p) & 1u)) {
                mask = (uint16_t)(mask | (1u << p));
                live++;
            }
        }
        containerParams[c].pegMask = mask;

        // ── Notes ──
        // Scale 0 is Chromatic: a random chromatic peg ring is noise rather than
        // a sequence, so start at 1. SelectScale/SelectRoot rebuild the note mask;
        // the plain SetScaleIndex/SetRootIndex accessors would leave the previous
        // mask in place and the scale label would then lie about the notes.
        channels[c].SelectScale(_RndInt(1, numScales - 1));
        channels[c].SelectRoot(_RndInt(0, 11));
        channels[c].SetSpread(_RndInt(CHANNEL_SPREAD_MIN, 3));
        channels[c].SetBias(_RndInt(-60, 60));

        // ── Gate ──
        // Decay stays far below ENVELOPE_MAX_DECAY: a container at these settings
        // fires several times a second, and an envelope longer than the gap never
        // returns to zero — it stops being a gate at all.
        channels[c].envelope.SetMode(_RndInt(0, (int)GateModeLength - 1));
        channels[c].envelope.SetAttack(_RndInt(0, 60));
        channels[c].envelope.SetDecay(_RndInt(40, 400));
        channels[c].SetGateLevel(_RndInt(70, 100));
        channels[c].SetAccent(_RndInt(0, 80));
    }

    // The signature control. Biased toward some overlap so a roll actually shows
    // the coupling off rather than landing on two independent containers.
    worldParams.proximity = _RndFloat(0.0f, 0.85f);
    worldParams.coupling = _RndFloat(0.3f, 1.0f);

    // Seed the simulation from the new values before the next step, so ball
    // counts and peg rings are right on the very first frame.
    ApplyParams(physicsWorld, clockEngine, containerParams, worldParams, modBus);
    physicsWorld.Reset();
}
