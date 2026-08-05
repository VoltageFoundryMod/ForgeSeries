#pragma once

// attractors.hpp — the twelve chaotic systems, and the integrator that runs them.
//
// Every system here is a three-variable autonomous ODE with a strange attractor:
// bounded forever, never repeating, and exquisitely sensitive to where it
// started. That last property is the whole point of the module — two outputs
// taken from one system wander together without ever agreeing, which is exactly
// what a modulation source wants to be and what an LFO can never do.
//
// ── What lives here and what does not ───────────────────────────────────────
// This file is pure mathematics: the systems, their parameter metadata, the
// integrator, and the constants that map an orbit onto a jack. It holds no state
// at all, so it is trivially testable and both hosts share it unchanged.
// Generator (generator.hpp) owns the state, the clock and the output scaling.
//
// ── Why RK4 and not Euler ───────────────────────────────────────────────────
// Euler on a stiff system like Lorenz does not merely lose accuracy, it changes
// the attractor: the orbit spirals outward and eventually blows up, which on a
// jack is a CV that slams to a rail and stays there. RK4 costs four derivative
// evaluations per step and buys a step size an order of magnitude larger for the
// same stability, so it is also the cheaper way to get a usable orbit. See
// docs/Design.md §2 for the measured budget.
//
// ── Why four parameters, never more ─────────────────────────────────────────
// A menu page is six rows (see the note in menuHandlers.hpp), and the SYSTEM
// page has to carry TYPE and SPEED as well. Four is what is left. Two of the
// systems here naturally have more — Aizawa has six, Dadras five — and rather
// than drop them, the ones that only reshape the figure (Aizawa's e and f,
// Dadras's e) are fixed at their published values and the musically useful four
// are exposed. Those constants are named in the derivative functions below.

#ifdef UNIT_TEST
#include "ArduinoFake.h"
#else
#include <Arduino.h>
#endif

#include <math.h>
#include <stdint.h>

// Parameters exposed per system. See the header comment: this is the menu page's
// budget, not the mathematics'.
#define ATT_MAX_PARAMS 4

// ── The systems ──────────────────────────────────────────────────────────────
// APPEND ONLY. Presets store the system as a raw index, so inserting one in the
// middle silently re-points every saved patch at a different attractor.
enum AttractorId : uint8_t {
    AttLorenz = 0,
    AttRossler,
    AttThomas,
    AttChua,
    AttHalvorsen,
    AttChen,
    AttBurkeShaw,
    AttAizawa,
    AttDadras,
    AttSprottB,
    AttSprottC,
    AttFinance,
    AttractorCount
};

// Which state variable an output jack follows.
enum AttAxis : uint8_t { AxisX = 0, AxisY, AxisZ, AxisCount };
static const char *const AttAxisNames[] = {"X", "Y", "Z"};

struct AttParamSpec {
    const char *name; // shown on the parameter row — keep to 5 characters
    float def;
    float min;
    float max;
};

struct AttractorSpec {
    const char *name;      // the TYPE row — keep to 9 characters
    const char *shortName; // the plot header — exactly 4 characters
    uint8_t paramCount;
    AttParamSpec params[ATT_MAX_PARAMS];

    float x0[3]; // seed point, on or near the attractor

    // Largest RK4 step that keeps this system stable, in its own time units.
    // Taken from the reference simulators these systems were tuned in, and
    // verified here: tools/../docs/Design.md §2 records the sweep. The
    // integrator subdivides to stay at or under it however fast the module runs.
    float hMax;

    // Attractor time units per real second at the rate this system is normally
    // *catalogued* at. This is what makes SPEED mean the same thing on every
    // system: Thomas advances 24 units a second here and Chua 0.96, yet both
    // trace their figure at a comparable rate. Without it, switching system would
    // be a wild jump in output rate.
    //
    // SPEED 1.00 is deliberately slower than this — see ATT_RATE_SCALE in
    // generator.hpp, which is the one place the two are related.
    float rate;

    // ── Fixed output normalisation ──────────────────────────────────────────
    // centre/halfSpan per axis, so (value - centre) / halfSpan lands in -1..1.
    //
    // Measured, not guessed: each system was run for ~700 simulated seconds at
    // its default parameters and these are the 0.2/99.8 percentiles of the
    // resulting orbit. Percentiles rather than the true extremes because a
    // chaotic orbit's excursions are rare and enormous — sizing the jack to
    // Lorenz's rarest z spike would spend most of the range on a voltage you
    // hear once a minute. The occasional excursion clamps, which is a hundred
    // times less audible than permanently halving the swing.
    //
    // They are only exact at the default parameters, which is what RANGE ▸ AUTO
    // exists for — see generator.hpp.
    float centre[3];
    float halfSpan[3];
};

// `static` so several test translation units can include this header, per the
// repo-wide rule in AGENTS.md.
static const AttractorSpec ATTRACTORS[AttractorCount] = {
    // ── Lorenz (1963) — atmospheric convection, the double wing ──────────────
    {"LORENZ", "LRNZ", 3,
     {{"SIGMA", 10.0f, 1.0f, 20.0f}, {"RHO", 28.0f, 1.0f, 50.0f}, {"BETA", 2.667f, 0.1f, 6.0f}},
     {0.1f, 0.0f, 0.0f}, 0.005f, 2.4f,
     {0.051f, 0.095f, 24.258f}, {16.961f, 22.255f, 19.128f}},

    // ── Rössler (1976) — one nonlinear term; a slow spiral and a sharp fold ──
    // The fold is the useful part: x and y drift smoothly, then z spikes. Take
    // z on a jack and you have a sparse, self-generating accent.
    {"ROSSLER", "RSSL", 3,
     {{"A", 0.2f, 0.01f, 0.5f}, {"B", 0.2f, 0.01f, 0.5f}, {"C", 5.7f, 1.0f, 12.0f}},
     {0.1f, 0.0f, 0.0f}, 0.02f, 9.6f,
     {1.133f, -1.463f, 10.600f}, {10.164f, 9.239f, 10.586f}},

    // ── Thomas (1999) — cyclically symmetric, sine-driven ────────────────────
    // All three axes are statistically identical, so any pair makes an equally
    // good Lissajous. The gentlest system here: no spikes at all.
    //
    // B is the dissipation, and it is the one parameter on this module whose
    // range is mostly NOT chaotic — the chaotic bands are roughly 0.03..0.07,
    // 0.09..0.10 and a sliver at 0.18, and everything between them is a limit
    // cycle. The default sits in the widest band. The periodic windows are left
    // reachable on purpose: a limit cycle here is a pair of smooth, perfectly
    // repeating LFOs with an unusual shape, which is a useful thing for the
    // module to be able to do. See docs/Design.md §4.
    {"THOMAS", "THMS", 1,
     {{"B", 0.05f, 0.03f, 0.25f}},
     {0.1f, 0.0f, 0.1f}, 0.05f, 24.0f,
     {-0.153f, 0.126f, -0.379f}, {10.581f, 11.256f, 10.789f}},

    // ── Chua (1983) — the double scroll, and the one you can build in analog ─
    {"CHUA", "CHUA", 4,
     {{"ALPHA", 15.6f, 5.0f, 30.0f},
      {"BETA", 28.0f, 10.0f, 50.0f},
      {"M0", -1.143f, -2.0f, 0.0f},
      {"M1", -0.714f, -2.0f, 0.0f}},
     {0.1f, 0.0f, 0.0f}, 0.002f, 0.96f,
     {0.0f, 0.0f, 0.001f}, {2.248f, 0.382f, 3.596f}},

    // ── Halvorsen — three-fold cyclic symmetry, three interlocked scrolls ────
    {"HALVORSEN", "HLVR", 1,
     {{"A", 1.4f, 0.8f, 2.2f}},
     {-5.0f, 0.0f, 0.0f}, 0.005f, 3.0f,
     {-3.400f, -3.406f, -3.392f}, {9.641f, 9.639f, 9.635f}},

    // ── Chen (1999) — found while trying to control Lorenz; wider and faster ─
    {"CHEN", "CHEN", 3,
     {{"A", 35.0f, 20.0f, 50.0f}, {"B", 3.0f, 1.0f, 6.0f}, {"C", 28.0f, 15.0f, 40.0f}},
     {0.1f, 0.0f, 0.0f}, 0.002f, 1.2f,
     {-0.004f, 0.026f, 25.815f}, {20.905f, 23.362f, 17.642f}},

    // ── Burke-Shaw — a compact figure-eight, tightly wound ───────────────────
    {"BURKESHAW", "BRKS", 2,
     {{"S", 10.0f, 3.0f, 20.0f}, {"V", 4.272f, 1.0f, 8.0f}},
     {0.6f, 0.0f, 0.0f}, 0.004f, 2.4f,
     {0.002f, -0.003f, 0.001f}, {1.652f, 2.175f, 1.830f}},

    // ── Aizawa — a torus with a spiralling skirt; e and f fixed (see header) ─
    {"AIZAWA", "AIZW", 4,
     {{"A", 0.95f, 0.5f, 1.5f},
      {"B", 0.7f, 0.3f, 1.2f},
      {"C", 0.6f, 0.1f, 1.2f},
      {"D", 3.5f, 1.0f, 6.0f}},
     {0.1f, 0.0f, 0.0f}, 0.01f, 6.0f,
     {0.003f, 0.032f, 0.742f}, {1.463f, 1.451f, 1.101f}},

    // ── Dadras (2009) — a knotted structure; e fixed at 9 (see header) ───────
    {"DADRAS", "DDRS", 4,
     {{"A", 3.0f, 1.0f, 6.0f}, {"B", 2.7f, 0.5f, 5.0f}, {"C", 1.7f, 0.5f, 4.0f}, {"D", 2.0f, 0.5f, 4.0f}},
     {1.0f, 1.0f, 0.0f}, 0.004f, 2.4f,
     {0.359f, -1.018f, 0.261f}, {11.699f, 7.646f, 7.332f}},

    // ── Sprott B and C (1994) — minimal chaos, three terms, no parameters ────
    // Nothing to turn, which is the point: they are the two systems whose
    // character comes entirely from SPEED and the axis pair you take.
    {"SPROTT B", "SPRB", 0,
     {},
     {0.1f, 0.0f, 0.0f}, 0.012f, 7.2f,
     {-0.005f, -0.010f, -0.015f}, {4.403f, 2.729f, 4.475f}},

    {"SPROTT C", "SPRC", 0,
     {},
     {0.1f, 0.0f, 0.0f}, 0.012f, 7.2f,
     {0.060f, 0.032f, -1.766f}, {2.836f, 1.842f, 6.512f}},

    // ── Finance — boom/bust cycles: interest rate, demand, price index ───────
    // The one system here whose y axis is strongly one-sided — a modulation that
    // leans rather than swings.
    //
    // A is the savings rate and is what decides whether this system is chaotic
    // at all: below ~0.82 and above ~1.26 it settles into a clean limit cycle,
    // so the range is drawn tightly around the chaotic band rather than around
    // the equation's mathematically legal values. C changes the figure's shape
    // but, unusually, not its Lyapunov exponent at all.
    {"FINANCE", "FNCE", 3,
     {{"A", 0.95f, 0.80f, 1.30f}, {"B", 0.2f, 0.05f, 0.35f}, {"C", 1.1f, 0.5f, 2.0f}},
     {0.1f, 0.2f, -0.5f}, 0.02f, 12.0f,
     {0.045f, 0.914f, -0.012f}, {2.204f, 1.878f, 1.401f}},
};

// A system index that is always safe to use as a subscript.
static inline uint8_t AttClampId(int id) {
    return (uint8_t)constrain(id, 0, (int)AttractorCount - 1);
}

static inline const AttractorSpec &AttSpec(int id) { return ATTRACTORS[AttClampId(id)]; }

// Half the legal span of one system parameter, or 0 for a parameter that system
// does not have. This is the unit CV modulation is expressed in — see the note
// at the top of BuildModBus() in cvInputs.hpp for why a parameter's depth has to
// be a fraction of its own range rather than an absolute amount.
static inline float ParamHalfSpan(int id, int idx) {
    const AttractorSpec &sp = AttSpec(id);
    if (idx < 0 || idx >= (int)sp.paramCount)
        return 0.0f;
    return 0.5f * (sp.params[idx].max - sp.params[idx].min);
}

// ── The derivatives ─────────────────────────────────────────────────────────
// One switch rather than a table of function pointers: on the RP2040 a switch
// over a dense enum compiles to a jump table with no indirect-call overhead, and
// it keeps every system visible in one place. `p` is always ATT_MAX_PARAMS long;
// entries past the system's paramCount are unused rather than absent.
static inline void AttDerivative(uint8_t id, const float s[3], const float p[ATT_MAX_PARAMS],
                                 float d[3]) {
    const float x = s[0], y = s[1], z = s[2];
    switch (id) {
    case AttLorenz:
        d[0] = p[0] * (y - x);
        d[1] = x * (p[1] - z) - y;
        d[2] = x * y - p[2] * z;
        break;

    case AttRossler:
        d[0] = -(y + z);
        d[1] = x + p[0] * y;
        d[2] = p[1] + z * (x - p[2]);
        break;

    case AttThomas:
        d[0] = sinf(y) - p[0] * x;
        d[1] = sinf(z) - p[0] * y;
        d[2] = sinf(x) - p[0] * z;
        break;

    case AttChua: {
        // The piecewise-linear diode. Written with fabsf rather than branches so
        // it has no data-dependent path — the whole point of Chua's circuit is
        // that this one nonlinearity is buildable in hardware.
        const float h = p[3] * x + 0.5f * (p[2] - p[3]) * (fabsf(x + 1.0f) - fabsf(x - 1.0f));
        d[0] = p[0] * (y - x - h);
        d[1] = x - y + z;
        d[2] = -p[1] * y;
        break;
    }

    case AttHalvorsen:
        d[0] = -p[0] * x - 4.0f * y - 4.0f * z - y * y;
        d[1] = -p[0] * y - 4.0f * z - 4.0f * x - z * z;
        d[2] = -p[0] * z - 4.0f * x - 4.0f * y - x * x;
        break;

    case AttChen:
        d[0] = p[0] * (y - x);
        d[1] = (p[2] - p[0]) * x - x * z + p[2] * y;
        d[2] = x * y - p[1] * z;
        break;

    case AttBurkeShaw:
        d[0] = -p[0] * (x + y);
        d[1] = -y - p[0] * x * z;
        d[2] = p[0] * x * y + p[1];
        break;

    case AttAizawa: {
        // e and f are the two shape terms held at their published values — see
        // the four-parameter note in the header comment.
        const float e = 0.25f, f = 0.1f;
        d[0] = (z - p[1]) * x - p[3] * y;
        d[1] = p[3] * x + (z - p[1]) * y;
        d[2] = p[2] + p[0] * z - z * z * z / 3.0f - (x * x + y * y) * (1.0f + e * z) +
               f * z * x * x * x;
        break;
    }

    case AttDadras: {
        const float e = 9.0f; // held; see the header comment
        d[0] = y - p[0] * x + p[1] * y * z;
        d[1] = p[2] * y - x * z + z;
        d[2] = p[3] * x * y - e * z;
        break;
    }

    case AttSprottB:
        d[0] = y * z;
        d[1] = x - y;
        d[2] = 1.0f - x * y;
        break;

    case AttSprottC:
        d[0] = y * z;
        d[1] = x - y;
        d[2] = 1.0f - x * x;
        break;

    case AttFinance:
        d[0] = z + (y - p[1]) * x;
        d[1] = 1.0f - p[1] * y - x * x;
        d[2] = -x - p[0] * z;
        break;

    default:
        d[0] = d[1] = d[2] = 0.0f;
        break;
    }
}

// One classical RK4 step of size `h` (in the system's own time units).
static inline void AttRk4(uint8_t id, float s[3], const float p[ATT_MAX_PARAMS], float h) {
    float k1[3], k2[3], k3[3], k4[3], t[3];

    AttDerivative(id, s, p, k1);
    for (int i = 0; i < 3; i++)
        t[i] = s[i] + 0.5f * h * k1[i];
    AttDerivative(id, t, p, k2);
    for (int i = 0; i < 3; i++)
        t[i] = s[i] + 0.5f * h * k2[i];
    AttDerivative(id, t, p, k3);
    for (int i = 0; i < 3; i++)
        t[i] = s[i] + h * k3[i];
    AttDerivative(id, t, p, k4);

    for (int i = 0; i < 3; i++)
        s[i] += h * (k1[i] + 2.0f * k2[i] + 2.0f * k3[i] + k4[i]) / 6.0f;
}

// Has the orbit left the attractor for good?
//
// It can: the parameter ranges above are wide enough to include settings where a
// system has no bounded attractor at all, and a divergent orbit reaches 1e30 in
// a handful of steps. The caller re-seeds rather than clamping — a clamped
// divergence is a jack pinned to a rail, which reads as a dead module, whereas a
// re-seed is heard as the pattern restarting.
// The test is written as a positive range check rather than as
// `isfinite() || fabsf() > limit` so that NaN is caught by the same expression:
// every comparison against NaN is false, so a NaN state fails the "is in range"
// test and is reported as divergence. isfinite is a macro on one toolchain and a
// std:: function on the other, and this needs neither.
static inline bool AttDiverged(const float s[3]) {
    for (int i = 0; i < 3; i++) {
        if (!(s[i] > -1.0e5f && s[i] < 1.0e5f))
            return true;
    }
    return false;
}
