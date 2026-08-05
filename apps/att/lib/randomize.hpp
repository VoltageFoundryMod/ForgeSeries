#pragma once

// randomize.hpp — "roll me a new patch" (platform-agnostic)
//
// One implementation shared by the hardware menu's RANDOM action and the VCV
// Rack plugin's Randomize (Ctrl+R), so the module behaves identically on the
// panel and in the host. Like presetManager.hpp it only touches the *base*
// parameter block, never the live Generator, which carries this instant's CV
// modulation.
//
// What it deliberately does NOT touch: the IN 1 role, the CV modulation matrix,
// the home view and the screen timeout. Those are patch wiring and preferences —
// the things you set up on purpose — and rerolling them turns "give me a new
// shape" into "break my patch". VCV Rack draws the same line when it leaves
// ports alone on randomize. RANGE is on that list too: whether a jack is scaled
// by the published window or by a tracked one is a decision about how you want
// the module to behave, not part of the sound.

#include "attractors.hpp"
#include "generator.hpp"
#include "params.hpp"

extern ChaosWorld world;        // src/att_app.cpp
extern GenParams genParams[2];  // src/att_app.cpp
extern WorldParams worldParams; // src/att_app.cpp

// The generator. A self-contained xorshift, so this behaves the same on the
// RP2040 and under the VCV shim, where the host's std::rand state is not ours to
// disturb.
class AttRandom {
    uint32_t _s = 0x2545F491u;

  public:
    void Seed(uint32_t s) { _s = s ? s : 0x2545F491u; }
    uint32_t Next() {
        _s ^= _s << 13;
        _s ^= _s >> 17;
        _s ^= _s << 5;
        return _s;
    }
    float Unit() { return (float)(Next() >> 8) / 16777216.0f; } // [0,1)
    float Bipolar() { return Unit() * 2.0f - 1.0f; }
};

static AttRandom _rndGen;

// Mixed in on every call so a power cycle followed by RANDOM does not replay the
// same patch. This is a user-triggered UI action, not DSP, so reading the clock
// here does not affect the simulation's determinism.
static inline void _RndReseed(uint32_t entropy) {
    _rndGen.Seed(_rndGen.Next() ^ entropy ^ 0x9E3779B9u);
}

static inline int _RndInt(int lo, int hi) {
    if (hi <= lo)
        return lo;
    return lo + (int)(_rndGen.Next() % (uint32_t)(hi - lo + 1));
}

static inline float _RndFloat(float lo, float hi) {
    return lo + _rndGen.Unit() * (hi - lo);
}

// Log-uniform, which is the only sensible way to draw a rate: a linear draw
// across 0.05..4 spends nine tenths of its rolls above 1x and the module would
// come out fast almost every time.
static inline float _RndRate(float lo, float hi) {
    return lo * powf(hi / lo, _rndGen.Unit());
}

// How far a system parameter may be moved off its published value, as a fraction
// of its legal span. Small on purpose: the published values are published
// because they are where the attractor is, and a wide draw mostly lands on
// systems that collapse to a fixed point (a dead CV) or diverge (a re-seed every
// few seconds). A tenth of the span visibly reshapes the figure while leaving it
// a figure.
#define RND_PARAM_SPREAD 0.10f

// ─────────────────────────────────────────────────────────────────────────────
// Roll a new pair of systems and everything about how they are heard.
//
// `entropy` seeds the draw — pass micros() on the hardware, or the host's engine
// time in a plugin. Applies the result and re-seeds both orbits, so the caller
// only has to mark the preset dirty and refresh the display.
// ─────────────────────────────────────────────────────────────────────────────
inline void RandomizeParams(uint32_t entropy) {
    _RndReseed(entropy);

    for (int i = 0; i < 2; i++) {
        GenParams &g = genParams[i];

        LoadSystemDefaults(g, _RndInt(0, (int)AttractorCount - 1));
        const AttractorSpec &sp = AttSpec(g.system);
        for (int k = 0; k < (int)sp.paramCount; k++) {
            const float span = sp.params[k].max - sp.params[k].min;
            // The draw is taken into a local FIRST. constrain() is a macro that
            // evaluates its argument three times, so a PRNG call inside it draws
            // three different numbers — the one that gets compared against the
            // limits is not the one that gets stored, and the result lands
            // outside the range perhaps one roll in fifty.
            const float jittered = g.param[k] + _rndGen.Bipolar() * RND_PARAM_SPREAD * span;
            g.param[k] = constrain(jittered, sp.params[k].min, sp.params[k].max);
        }

        // Two different axes per generator. A jack pair reading the same axis
        // twice is two copies of one voltage, which is the one outcome a roll
        // must never produce — the whole reason there are two jacks per
        // generator is that they are related but not equal.
        const int a = _RndInt(0, (int)AxisCount - 1);
        int b = _RndInt(0, (int)AxisCount - 2);
        if (b >= a)
            b++;
        g.src[0] = (uint8_t)a;
        g.src[1] = (uint8_t)b;

        // Deliberately asymmetric: A is drawn fast and B slow, so a roll keeps
        // the factory patch's proposition — one pair you hear as motion, one you
        // hear as drift — instead of handing you two of the same thing.
        g.speed = (i == 0) ? _RndRate(0.25f, 4.0f) : _RndRate(0.05f, 1.0f);

        g.level = (uint8_t)_RndInt(70, 100);
        g.offset = (int8_t)_RndInt(-20, 20);
        g.smooth = (uint8_t)_RndInt(0, 40);
    }

    // Biased low. Above about half the two orbits entrain hard enough that the
    // four outputs start to look like two, and a roll that lands there reads as
    // the module having fewer voices rather than as a different patch.
    worldParams.couple = _RndFloat(0.0f, 0.5f);

    world.Reseed();
}
