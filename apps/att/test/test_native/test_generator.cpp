// test_generator.cpp — the layer between an orbit and a jack.
//
// SPEED, LEVEL/OFFSET/SMOOTH, RANGE, FREEZE, COUPLE and the trail buffer. The
// systems themselves are covered in test_attractors.cpp.

#include <gtest/gtest.h>

#include <cmath>

#include "attractors.hpp"
#include "generator.hpp"
#include "params.hpp"

namespace {

const int kStepsPerSecond = 1000;

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
    void Run(int ms) {
        Apply();
        for (int i = 0; i < ms; i++)
            world.StepOnce();
    }
};

// How often generator 0's first output crosses the middle of its range. A rate
// measure that needs no access to the simulation's internals, and the one a
// player actually perceives as "speed".
int CrossingsPerRun(Rig &rig, int seconds) {
    rig.Apply();
    int crossings = 0;
    bool above = rig.world.Get(0).Out01(0) > 0.5f;
    for (int i = 0; i < seconds * kStepsPerSecond; i++) {
        rig.world.StepOnce();
        const bool now = rig.world.Get(0).Out01(0) > 0.5f;
        if (now != above) {
            crossings++;
            above = now;
        }
    }
    return crossings;
}

} // namespace

// ── SPEED ────────────────────────────────────────────────────────────────────

TEST(Generator, SpeedScalesHowFastTheFigureIsTraced) {
    // Not an exact ratio — chaotic orbits do not divide neatly — but the rate
    // has to rise monotonically and substantially with the control, which is
    // what the ATT_MAX_SUBSTEPS cap could silently break.
    Rig slow(AttLorenz), mid(AttLorenz), fast(AttLorenz);
    slow.gp[0].speed = 1.0f;
    mid.gp[0].speed = 4.0f;
    fast.gp[0].speed = 16.0f;

    const int a = CrossingsPerRun(slow, 60);
    const int b = CrossingsPerRun(mid, 60);
    const int c = CrossingsPerRun(fast, 60);

    EXPECT_GT(a, 0);
    EXPECT_GT(b, a);
    EXPECT_GT(c, b);
    EXPECT_GT(c, 3 * a); // 8x the rate setting must be worth far more than 3x
}

TEST(Generator, SpeedIsClampedToItsLegalRange) {
    Rig rig;
    rig.gp[0].speed = 1000.0f;
    rig.gp[1].speed = -5.0f;
    rig.Apply();
    EXPECT_FLOAT_EQ(rig.world.Get(0).GetSpeed(), ATT_SPEED_MAX);
    EXPECT_FLOAT_EQ(rig.world.Get(1).GetSpeed(), ATT_SPEED_MIN);
}

// ── Output shaping ───────────────────────────────────────────────────────────

TEST(Generator, LevelZeroParksTheOutputAtTheCentre) {
    Rig rig;
    rig.gp[0].level = 0;
    rig.Run(2000);
    // 0 V would be the wrong answer here: LEVEL scales the swing around the
    // centre of the range, so with no swing the jack sits at 2.5 V. A CV that
    // jumped to a rail when its depth was turned down would be unusable as a
    // VCA target.
    EXPECT_NEAR(rig.world.Get(0).Out01(0), 0.5f, 1e-6f);
    EXPECT_NEAR(rig.world.Get(0).Out01(1), 0.5f, 1e-6f);
}

TEST(Generator, OffsetSlidesTheWholeRange) {
    Rig rig;
    rig.gp[0].level = 0;
    rig.gp[0].offset = 100;
    rig.Run(100);
    EXPECT_NEAR(rig.world.Get(0).Out01(0), 1.0f, 1e-6f);

    rig.gp[0].offset = -100;
    rig.Run(100);
    EXPECT_NEAR(rig.world.Get(0).Out01(0), 0.0f, 1e-6f);
}

TEST(Generator, OutputsNeverLeaveTheJacksRangeAtAnySetting) {
    // LEVEL and OFFSET can ask for more than the jack has. The clamp is the last
    // thing between that and a DAC write, so it is asserted for every
    // combination of the extremes rather than for the ones we expect to be used.
    for (int lvl = 0; lvl <= 100; lvl += 50) {
        for (int off = -100; off <= 100; off += 100) {
            Rig rig(AttChen);
            rig.gp[0].level = (uint8_t)lvl;
            rig.gp[0].offset = (int8_t)off;
            rig.Apply();
            for (int i = 0; i < 30 * kStepsPerSecond; i++) {
                rig.world.StepOnce();
                for (int j = 0; j < 2; j++) {
                    const float v = rig.world.Get(0).Out01(j);
                    ASSERT_GE(v, 0.0f) << "level " << lvl << " offset " << off;
                    ASSERT_LE(v, 1.0f) << "level " << lvl << " offset " << off;
                }
            }
        }
    }
}

TEST(Generator, SmoothReducesTheOutputsStepToStepMovement) {
    // SMOOTH is a one-pole lag, so its effect is on how far the value moves
    // between consecutive module steps, not on the range it eventually covers.
    auto meanStep = [](int smooth) {
        Rig rig(AttRossler);
        rig.gp[0].smooth = (uint8_t)smooth;
        rig.gp[0].speed = 4.0f;
        rig.Run(2000);
        float prev = rig.world.Get(0).Out01(0);
        double total = 0;
        const int n = 20 * kStepsPerSecond;
        for (int i = 0; i < n; i++) {
            rig.world.StepOnce();
            const float now = rig.world.Get(0).Out01(0);
            total += std::fabs(now - prev);
            prev = now;
        }
        return total / n;
    };

    const double raw = meanStep(0);
    const double smoothed = meanStep(100);
    EXPECT_GT(raw, 0.0);
    EXPECT_LT(smoothed, raw * 0.5);
}

TEST(Generator, EachJackFollowsItsOwnAxis) {
    Rig rig;
    rig.gp[0].src[0] = AxisX;
    rig.gp[0].src[1] = AxisZ;
    rig.Run(5000);
    EXPECT_NE(rig.world.Get(0).OutNorm(0), rig.world.Get(0).OutNorm(1));

    // ...and pointing both at one axis is allowed, and gives exactly one signal.
    rig.gp[0].src[1] = AxisX;
    rig.Run(1000);
    EXPECT_FLOAT_EQ(rig.world.Get(0).OutNorm(0), rig.world.Get(0).OutNorm(1));
}

// ── RANGE ────────────────────────────────────────────────────────────────────

TEST(Generator, AutoRangeRecoversTheSwingWhenParametersShrinkTheOrbit) {
    // The published window is measured at the published parameters. Move them
    // far enough and the orbit no longer fills it — which on hardware is a jack
    // that has quietly lost most of its range. AUTO is the answer, and this is
    // the test that says it works.
    // Rössler with C at the bottom of its range: the orbit keeps its shape and
    // stays lively, but is less than half the size the published window was
    // measured at, so a fixed jack loses more than half its swing.
    auto swing = [](bool autoRange) {
        Rig rig(AttRossler);
        rig.gp[0].param[2] = 2.5f;
        rig.gp[0].autoRange = autoRange ? 1 : 0;
        // At the catalogued rate: the tracker's relax is scaled by SPEED, so the
        // settle below is a fixed amount of orbit rather than of wall time.
        rig.gp[0].speed = 1.0f / ATT_RATE_SCALE;
        rig.Run(40 * kStepsPerSecond); // let the tracker close in on the new orbit
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < 60 * kStepsPerSecond; i++) {
            rig.world.StepOnce();
            const float v = rig.world.Get(0).Out01(0);
            lo = std::fmin(lo, v);
            hi = std::fmax(hi, v);
        }
        return hi - lo;
    };

    const float fixed = swing(false);
    const float tracked = swing(true);
    EXPECT_LT(fixed, 0.6f); // the premise: the published window is now too wide
    EXPECT_GT(tracked, fixed * 1.5f);
    EXPECT_GT(tracked, 0.8f);
}

TEST(Generator, AutoRangeNeverDividesByAVanishingWindow) {
    // A system parked near a fixed point has a near-zero span, and dividing by
    // it would turn numerical dust into a full-scale CV.
    Rig rig(AttThomas);
    rig.gp[0].autoRange = 1;
    rig.gp[0].param[0] = 0.5f; // heaviest dissipation: the orbit collapses inward
    rig.gp[0].speed = 0.05f;
    rig.Apply();
    for (int i = 0; i < 120 * kStepsPerSecond; i++) {
        rig.world.StepOnce();
        const float v = rig.world.Get(0).Out01(0);
        ASSERT_FALSE(std::isnan(v));
        ASSERT_GE(v, 0.0f);
        ASSERT_LE(v, 1.0f);
    }
}

// ── FREEZE ───────────────────────────────────────────────────────────────────

TEST(Generator, FreezeHoldsTheOrbitAndTheOutputs) {
    Rig rig;
    rig.Run(3000);
    const float held0 = rig.world.Get(0).Out01(0);
    const float held1 = rig.world.Get(1).Out01(1);
    const float state = rig.world.Get(0).State()[0];

    rig.world.SetFrozen(true);
    for (int i = 0; i < 20; i++)
        rig.world.Advance(4000000UL + (unsigned long)i * 100000UL);

    EXPECT_FLOAT_EQ(rig.world.Get(0).Out01(0), held0);
    EXPECT_FLOAT_EQ(rig.world.Get(1).Out01(1), held1);
    EXPECT_FLOAT_EQ(rig.world.Get(0).State()[0], state);

    // Releasing resumes rather than fast-forwarding: the paused seconds are not
    // banked in the accumulator, or letting go of a long FREEZE would rip
    // through everything that "should" have happened.
    rig.world.SetFrozen(false);
    rig.world.Advance(6100000UL);
    const float after = rig.world.Get(0).State()[0];
    rig.world.Advance(6101000UL);
    EXPECT_NE(rig.world.Get(0).State()[0], after);
}

// ── COUPLE ───────────────────────────────────────────────────────────────────

TEST(Generator, CouplingPullsTwoCopiesOfOneSystemTogether) {
    // Asserted by magnitude, not by "the numbers changed": a chaotic system
    // diverges from any perturbation at all, so a test that only checks for
    // difference passes even when coupling does nothing audible.
    auto separation = [](float couple) {
        Rig rig(AttLorenz);
        rig.wp.couple = couple;
        rig.Run(90 * kStepsPerSecond);
        double total = 0;
        const int n = 30 * kStepsPerSecond;
        for (int i = 0; i < n; i++) {
            rig.world.StepOnce();
            total += std::fabs(rig.world.Get(0).OutNorm(0) - rig.world.Get(1).OutNorm(0));
        }
        return total / n;
    };

    const double apart = separation(0.0f);
    const double locked = separation(1.0f);
    EXPECT_GT(apart, 0.2);        // two free orbits are genuinely unrelated
    EXPECT_LT(locked, apart / 4); // ...and fully coupled ones track each other
}

TEST(Generator, CouplingLeavesDifferentSystemsBoundedAndAlive) {
    // Two different systems cannot lock, so coupling drags each off its own
    // attractor instead. It must not throw either of them off it for good.
    Rig rig;
    LoadSystemDefaults(rig.gp[0], AttChen);
    LoadSystemDefaults(rig.gp[1], AttFinance);
    rig.wp.couple = 1.0f;
    rig.Apply();
    rig.world.Reseed();

    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 120 * kStepsPerSecond; i++) {
        rig.world.StepOnce();
        for (int g = 0; g < 2; g++) {
            for (int j = 0; j < 2; j++) {
                const float v = rig.world.Get(g).Out01(j);
                ASSERT_FALSE(std::isnan(v));
                ASSERT_GE(v, 0.0f);
                ASSERT_LE(v, 1.0f);
            }
        }
        const float v = rig.world.Get(0).Out01(0);
        lo = std::fmin(lo, v);
        hi = std::fmax(hi, v);
    }
    EXPECT_GT(hi - lo, 0.1f) << "coupling flattened the orbit into a stuck voltage";
}

// ── The trail (what the screen draws) ────────────────────────────────────────

TEST(Generator, TheTrailFillsAndStaysInsideThePlot) {
    // At the catalogued rate, because a trail point is worth a fixed amount of
    // ARC (see ATT_TRAIL_HZ): at SPEED 1.00 a full 256-point buffer is a minute
    // of drawing, which is the honest consequence of the module being slow, not
    // something to assert against here.
    Rig rig;
    rig.gp[0].speed = 1.0f / ATT_RATE_SCALE;
    rig.Run(30 * kStepsPerSecond);
    EXPECT_EQ(rig.world.Get(0).TrailCount(), ATT_TRAIL_LEN);

    for (int i = 0; i < rig.world.Get(0).TrailCount(); i++) {
        float x, y;
        rig.world.Get(0).TrailPoint(i, x, y);
        EXPECT_LE(std::fabs(x), 1.28f);
        EXPECT_LE(std::fabs(y), 1.28f);
    }
}

TEST(Generator, TheTrailKeepsMovingEvenAtTheSlowestSpeed) {
    // Points are pushed on the orbit's clock, so without the time floor in
    // PushTrail() a very slow SPEED would leave the screen unchanged for seconds
    // at a time and read as a hung module.
    Rig rig;
    rig.gp[0].speed = ATT_SPEED_MIN;
    rig.Run(1000);
    const int before = rig.world.Get(0).TrailCount();
    rig.Run(1000);
    EXPECT_GT(rig.world.Get(0).TrailCount(), before);
}

TEST(Generator, ReseedClearsTheTrail) {
    Rig rig;
    rig.gp[0].speed = 1.0f / ATT_RATE_SCALE;
    rig.gp[1].speed = 1.0f / ATT_RATE_SCALE;
    rig.Run(30 * kStepsPerSecond);
    rig.world.Reseed(0);
    EXPECT_EQ(rig.world.Get(0).TrailCount(), 0);
    EXPECT_EQ(rig.world.Get(1).TrailCount(), ATT_TRAIL_LEN);
}

// ── The modulation bus ───────────────────────────────────────────────────────

TEST(Generator, ModulationRidesOnTopOfTheBaseParametersWithoutChangingThem) {
    Rig rig;
    rig.gp[0].speed = 1.0f;
    rig.mod.speedOct[0] = 2.0f; // +2 octaves
    rig.Apply();

    EXPECT_FLOAT_EQ(rig.world.Get(0).GetSpeed(), 4.0f);
    // The base block is what the menu shows and what presets store, so it must
    // come through untouched — this is the bug the ModBus split exists to
    // prevent.
    EXPECT_FLOAT_EQ(rig.gp[0].speed, 1.0f);
}

TEST(Generator, ModulatedParametersAreClampedToTheSystemsOwnRange) {
    Rig rig(AttLorenz);
    rig.mod.param[0][1] = 1000.0f; // rho, pushed far past its ceiling
    rig.Apply();
    // Not asserted through a getter on the generator — there is none by design —
    // but through the behaviour that matters: the orbit still exists.
    rig.Run(30 * kStepsPerSecond);
    const float v = rig.world.Get(0).Out01(0);
    EXPECT_GE(v, 0.0f);
    EXPECT_LE(v, 1.0f);
    EXPECT_FLOAT_EQ(rig.gp[0].param[1], AttSpec(AttLorenz).params[1].def);
}
