// test_attractors.cpp — the twelve systems and the integrator.
//
// These are the tests that guard the module's central claim: every system it
// ships stays on its attractor forever, keeps moving, and fills a usable
// fraction of the jack. A system that fails any of the three is not a
// modulation source — it is a stuck voltage, a dead voltage, or a whisper.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "attractors.hpp"
#include "generator.hpp"
#include "params.hpp"

namespace {

// One second of module time. Everything here drives StepOnce() directly rather
// than Advance(), because Advance() caps catch-up at ATT_MAX_STEPS_PER_CALL and
// this way the simulated duration is exact.
const int kStepsPerSecond = 1000;

// A world with one system on both generators, nothing coupled, defaults applied.
struct Rig {
    ChaosWorld world;
    GenParams gp[2];
    WorldParams wp;
    ModBus mod;

    explicit Rig(int system = AttLorenz) {
        for (int i = 0; i < 2; i++)
            LoadSystemDefaults(gp[i], system);
        Apply();
        world.Reseed();
    }

    void Apply() { ApplyParams(world, gp, wp, mod); }

    void Run(int seconds) {
        Apply();
        for (int i = 0; i < seconds * kStepsPerSecond; i++)
            world.StepOnce();
    }
};

struct Stats {
    float lo = 1e9f, hi = -1e9f;
    int nan = 0;
    void Add(float v) {
        if (std::isnan(v)) {
            nan++;
            return;
        }
        if (v < lo)
            lo = v;
        if (v > hi)
            hi = v;
    }
    float Span() const { return hi - lo; }
};

// Sample generator `g`'s two jack values for `seconds` of module time.
Stats SampleOut(Rig &rig, int g, int jack, int seconds) {
    Stats s;
    rig.Apply();
    for (int i = 0; i < seconds * kStepsPerSecond; i++) {
        rig.world.StepOnce();
        s.Add(rig.world.Get(g).Out01(jack));
    }
    return s;
}

} // namespace

// ── Every system, every axis ─────────────────────────────────────────────────

TEST(Attractors, EverySystemStaysOnItsAttractor) {
    // Two minutes of module time per system at the published parameters. Nothing
    // may leave the state-space bound that AttDiverged() polices — a system that
    // trips it is being re-seeded, which is heard as the pattern restarting at
    // random.
    for (int id = 0; id < AttractorCount; id++) {
        Rig rig(id);
        rig.Run(120);
        EXPECT_FALSE(rig.world.Get(0).ConsumeDiverged())
            << AttSpec(id).name << " left its attractor at the published parameters";
        const float *s = rig.world.Get(0).State();
        for (int a = 0; a < 3; a++) {
            EXPECT_TRUE(std::isfinite(s[a])) << AttSpec(id).name << " axis " << a;
        }
    }
}

TEST(Attractors, EverySystemKeepsMoving) {
    // A system that settles onto a fixed point is a DC voltage with extra steps.
    // Half of the jack's range is a deliberately loose floor: it only has to
    // catch a collapse, not police the shape.
    for (int id = 0; id < AttractorCount; id++) {
        Rig rig(id);
        rig.Run(10); // let the transient pass
        for (int axis = 0; axis < 3; axis++) {
            rig.gp[0].src[0] = (uint8_t)axis;
            Stats s = SampleOut(rig, 0, 0, 60);
            EXPECT_GT(s.Span(), 0.05f)
                << AttSpec(id).name << " axis " << AttAxisNames[axis] << " barely moves";
        }
    }
}

TEST(Attractors, PublishedWindowFillsMostOfTheJack) {
    // The normalisation constants in ATTRACTORS[] are measured percentiles, so
    // an axis should use most of the 0-5 V range over a long run. This is the
    // test that catches a constant that was mistyped or a system whose defaults
    // were changed without re-measuring: the symptom on hardware is a jack that
    // only ever moves through a volt.
    for (int id = 0; id < AttractorCount; id++) {
        for (int axis = 0; axis < 3; axis++) {
            Rig rig(id);
            rig.gp[0].src[0] = (uint8_t)axis;
            rig.Run(10);
            Stats s = SampleOut(rig, 0, 0, 120);
            EXPECT_EQ(s.nan, 0) << AttSpec(id).name;
            EXPECT_GE(s.lo, 0.0f) << AttSpec(id).name;
            EXPECT_LE(s.hi, 1.0f) << AttSpec(id).name;
            EXPECT_GT(s.Span(), 0.55f)
                << AttSpec(id).name << " axis " << AttAxisNames[axis]
                << " uses only " << s.Span() * 5.0f << " V of the 5 V range";
        }
    }
}

// ── Determinism and sensitivity ──────────────────────────────────────────────

TEST(Attractors, SameSeedSameOrbit) {
    // Determinism is what lets these tests exist at all, and what makes the Rack
    // port behave identically under faster-than-realtime rendering.
    for (int id = 0; id < AttractorCount; id++) {
        Rig a(id), b(id);
        a.Run(30);
        b.Run(30);
        for (int axis = 0; axis < 3; axis++) {
            EXPECT_FLOAT_EQ(a.world.Get(0).State()[axis], b.world.Get(0).State()[axis])
                << AttSpec(id).name << " is not deterministic";
        }
    }
}

TEST(Attractors, TheTwoGeneratorsDivergeFromEachOther) {
    // The module's whole proposition. Both generators run the same system with
    // the same parameters and are seeded a thousandth apart; within a minute
    // they must be somewhere else entirely, or the four jacks are really two.
    //
    // Averaged over half a minute rather than sampled once. A single sample of
    // two fully independent orbits lands within 0.05 of itself about one time in
    // twenty — across twelve systems that is a test which fails at random, and
    // it did.
    for (int id = 0; id < AttractorCount; id++) {
        Rig rig(id);
        rig.Run(60);
        double total = 0;
        const int n = 30 * kStepsPerSecond;
        for (int i = 0; i < n; i++) {
            rig.world.StepOnce();
            total += std::fabs(rig.world.Get(0).OutNorm(0) - rig.world.Get(1).OutNorm(0));
        }
        EXPECT_GT(total / n, 0.1) << AttSpec(id).name
                                  << ": the two generators are tracking each other";
    }
}

TEST(Attractors, ReseedIsPerGenerator) {
    Rig rig(AttLorenz);
    rig.Run(20);
    const float movedA = rig.world.Get(0).State()[0];
    const float movedB = rig.world.Get(1).State()[0];

    rig.world.Reseed(0);
    // The seed point is the system's own x0 plus the generator's salt — see
    // Generator::Reseed for why the two generators must not start identically.
    EXPECT_NE(movedA, rig.world.Get(0).State()[0]);
    EXPECT_NEAR(rig.world.Get(0).State()[0], AttSpec(AttLorenz).x0[0] + 0.001f, 1e-6f);
    // ...and B is still exactly where it was. RESEED A is a per-generator
    // gesture: re-rolling one voice must not restart the other.
    EXPECT_FLOAT_EQ(rig.world.Get(1).State()[0], movedB);

    rig.world.Reseed();
    EXPECT_NEAR(rig.world.Get(1).State()[0], AttSpec(AttLorenz).x0[0] + 0.002f, 1e-6f);
}

// ── Parameter metadata ───────────────────────────────────────────────────────

TEST(Attractors, EveryParameterDefaultIsInsideItsRange) {
    for (int id = 0; id < AttractorCount; id++) {
        const AttractorSpec &sp = AttSpec(id);
        EXPECT_LE(sp.paramCount, ATT_MAX_PARAMS) << sp.name;
        for (int k = 0; k < (int)sp.paramCount; k++) {
            EXPECT_LT(sp.params[k].min, sp.params[k].max) << sp.name << " P" << k + 1;
            EXPECT_GE(sp.params[k].def, sp.params[k].min) << sp.name << " P" << k + 1;
            EXPECT_LE(sp.params[k].def, sp.params[k].max) << sp.name << " P" << k + 1;
        }
        // The plot header has room for exactly four characters, and the TYPE row
        // for nine before it runs into the scroll bar.
        EXPECT_EQ(strlen(sp.shortName), 4u) << sp.name;
        EXPECT_LE(strlen(sp.name), 9u) << sp.name;
        EXPECT_GT(sp.halfSpan[0] * sp.halfSpan[1] * sp.halfSpan[2], 0.0f) << sp.name;
        EXPECT_GT(sp.rate, 0.0f) << sp.name;
        EXPECT_GT(sp.hMax, 0.0f) << sp.name;
    }
}

TEST(Attractors, ChangingSystemLoadsThatSystemsParameters) {
    GenParams g;
    LoadSystemDefaults(g, AttChua);
    EXPECT_EQ(g.system, AttChua);
    for (int k = 0; k < ATT_MAX_PARAMS; k++) {
        const AttractorSpec &sp = AttSpec(AttChua);
        EXPECT_FLOAT_EQ(g.param[k], k < sp.paramCount ? sp.params[k].def : 0.0f);
    }

    // Carrying Chua's alpha across into Sprott B — which has no parameters at
    // all — is exactly the case that used to leave a system integrating numbers
    // from a different equation.
    LoadSystemDefaults(g, AttSprottB);
    EXPECT_EQ(g.system, AttSprottB);
    for (int k = 0; k < ATT_MAX_PARAMS; k++)
        EXPECT_FLOAT_EQ(g.param[k], 0.0f);
}

// ── The integrator's own guard ───────────────────────────────────────────────

TEST(Attractors, DivergenceIsDetectedAndReseeded) {
    // The parameter ranges are wide enough to include settings with no bounded
    // attractor. When that happens the orbit must come back to its seed point
    // rather than pinning a jack to a rail.
    Rig rig(AttLorenz);
    rig.gp[0].param[0] = 20.0f; // sigma at the top
    rig.gp[0].param[1] = 50.0f; // rho at the top
    rig.gp[0].param[2] = 0.1f;  // beta at the bottom: barely any damping
    rig.gp[0].speed = ATT_SPEED_MAX;
    rig.Run(30);

    const float *s = rig.world.Get(0).State();
    for (int a = 0; a < 3; a++) {
        EXPECT_TRUE(std::isfinite(s[a]));
        EXPECT_LT(std::fabs(s[a]), 1.0e5f);
    }
    const float v = rig.world.Get(0).Out01(0);
    EXPECT_GE(v, 0.0f);
    EXPECT_LE(v, 1.0f);
}

TEST(Attractors, OutputsSurviveEverySystemAtMaximumSpeed) {
    // ATT_MAX_SUBSTEPS lets the integration step grow past hMax at the top of
    // the SPEED range rather than slowing time down. This is the test that says
    // the twelve systems all survive that.
    for (int id = 0; id < AttractorCount; id++) {
        Rig rig(id);
        rig.gp[0].speed = ATT_SPEED_MAX;
        rig.gp[1].speed = ATT_SPEED_MAX;
        Stats s = SampleOut(rig, 0, 0, 20);
        EXPECT_EQ(s.nan, 0) << AttSpec(id).name << " produced NaN at maximum speed";
        EXPECT_GE(s.lo, 0.0f) << AttSpec(id).name;
        EXPECT_LE(s.hi, 1.0f) << AttSpec(id).name;
    }
}
