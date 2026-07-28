#include <gtest/gtest.h>

#include "clock.hpp"

namespace {
constexpr float kPi = 3.14159265f;
}

TEST(Clock, BpmIsClamped) {
    Clock c;
    c.SetBpm(10);
    EXPECT_EQ(CLOCK_MIN_BPM, c.GetBpm());
    c.SetBpm(9999);
    EXPECT_EQ(CLOCK_MAX_BPM, c.GetBpm());
}

TEST(Clock, BeatLength) {
    Clock c;
    c.SetBpm(120);
    EXPECT_EQ(500000UL, c.BeatUs()); // 120 BPM = 500 ms per beat
}

// ── Rotation rate ────────────────────────────────────────────────────────────

TEST(Clock, SpinRateFollowsTempo) {
    Clock c;
    c.SetBpm(120); // 0.5 s per beat

    // Spin4 = 4 beats per revolution = 2 s per revolution = pi rad/s.
    EXPECT_NEAR(kPi, c.OmegaFor(Spin4, false, 0.0f), 1e-4f);
    // Twice the tempo, twice the rotation rate.
    c.SetBpm(240);
    EXPECT_NEAR(2.0f * kPi, c.OmegaFor(Spin4, false, 0.0f), 1e-4f);
}

TEST(Clock, ReverseFlipsTheSign) {
    Clock c;
    c.SetBpm(120);
    EXPECT_NEAR(-c.OmegaFor(Spin4, false, 0.0f), c.OmegaFor(Spin4, true, 0.0f), 1e-6f);
}

TEST(Clock, MoreBeatsPerRevolutionMeansSlower) {
    Clock c;
    c.SetBpm(120);
    EXPECT_GT(c.OmegaFor(Spin1, false, 0.0f), c.OmegaFor(Spin4, false, 0.0f));
    EXPECT_GT(c.OmegaFor(Spin4, false, 0.0f), c.OmegaFor(Spin16, false, 0.0f));
}

TEST(Clock, FreeSpinIgnoresTempo) {
    Clock c;
    c.SetBpm(120);
    float a = c.OmegaFor(SpinFree, false, 1.0f); // 1 rev/s = 2pi rad/s
    c.SetBpm(240);
    float b = c.OmegaFor(SpinFree, false, 1.0f);
    EXPECT_NEAR(2.0f * kPi, a, 1e-4f);
    EXPECT_FLOAT_EQ(a, b) << "FREE must not follow the clock";
}

// ── Quantize grid ────────────────────────────────────────────────────────────

TEST(Clock, GridPeriodMatchesTheDivision) {
    Clock c;
    c.SetBpm(120); // 500 ms per beat

    c.SetQuantize(QOff);
    EXPECT_EQ(0UL, c.GridPeriodUs());
    c.SetQuantize(Q4);
    EXPECT_EQ(500000UL, c.GridPeriodUs()); // quarter = 1 per beat
    c.SetQuantize(Q8);
    EXPECT_EQ(250000UL, c.GridPeriodUs());
    c.SetQuantize(Q16);
    EXPECT_EQ(125000UL, c.GridPeriodUs());
    c.SetQuantize(Q8T);
    EXPECT_EQ(500000UL / 3UL, c.GridPeriodUs()); // 3 per beat
}

TEST(Clock, NoBoundariesWhenQuantizeIsOff) {
    Clock c;
    c.SetBpm(120);
    c.SetQuantize(QOff);
    c.Update(1000);
    for (unsigned long t = 1000; t < 3000000UL; t += 1000) {
        c.Update(t);
        ASSERT_FALSE(c.ConsumeBoundary());
    }
}

TEST(Clock, EmitsOneBoundaryPerDivision) {
    Clock c;
    c.SetBpm(120);
    c.SetQuantize(Q8); // 250 ms

    c.Update(0); // prime
    int boundaries = 0;
    // Exactly 2 s of 1 ms updates → 2000/250 = 8 boundaries.
    for (unsigned long t = 1000; t <= 2000000UL; t += 1000) {
        c.Update(t);
        if (c.ConsumeBoundary()) {
            boundaries++;
        }
    }
    EXPECT_EQ(8, boundaries);
}

TEST(Clock, StallProducesOneBoundaryNotABacklog) {
    // After a long stall several periods have elapsed. Releasing one deferred
    // note is right; releasing a burst of them is not.
    Clock c;
    c.SetBpm(120);
    c.SetQuantize(Q16); // 125 ms
    c.Update(0);
    c.Update(1000);
    c.ConsumeBoundary();

    c.Update(900000UL); // ~0.9 s in a single jump
    EXPECT_TRUE(c.ConsumeBoundary());
    EXPECT_FALSE(c.ConsumeBoundary()) << "only one boundary should be queued";
}

// ── External clock ───────────────────────────────────────────────────────────

TEST(Clock, ExternalEdgesDeriveTempo) {
    Clock c;
    c.SetExternal(true);
    c.SetPpqn(Ppqn1); // one pulse per beat

    // 500 ms between pulses → 120 BPM.
    unsigned long t = 1000000UL;
    for (int i = 0; i < 6; i++) {
        c.ExternalEdge(t);
        t += 500000UL;
    }
    EXPECT_NEAR(120.0f, c.GetEffectiveBpm(), 1.0f);
}

TEST(Clock, ExternalPpqnDividesCorrectly) {
    Clock c;
    c.SetExternal(true);
    c.SetPpqn(Ppqn4); // four pulses per beat

    // 125 ms between pulses × 4 = 500 ms per beat → 120 BPM.
    unsigned long t = 1000000UL;
    for (int i = 0; i < 6; i++) {
        c.ExternalEdge(t);
        t += 125000UL;
    }
    EXPECT_NEAR(120.0f, c.GetEffectiveBpm(), 1.0f);
}

TEST(Clock, MedianRejectsASingleJitteryEdge) {
    Clock c;
    c.SetExternal(true);
    c.SetPpqn(Ppqn1);

    unsigned long t = 1000000UL;
    for (int i = 0; i < 4; i++) { // establish a steady 120 BPM
        c.ExternalEdge(t);
        t += 500000UL;
    }
    float steady = c.GetEffectiveBpm();

    // One early edge — the kind a sloppy clock source produces.
    t += 100000UL;
    c.ExternalEdge(t);
    EXPECT_NEAR(steady, c.GetEffectiveBpm(), 10.0f)
        << "a single outlier interval should not lurch the tempo";
}

TEST(Clock, LeavingExternalRestoresTheInternalTempo) {
    Clock c;
    c.SetBpm(90);
    c.SetExternal(true);
    c.SetPpqn(Ppqn1); // one pulse per beat, so the interval IS the beat
    unsigned long t = 1000000UL;
    for (int i = 0; i < 4; i++) {
        c.ExternalEdge(t);
        t += 250000UL; // 240 BPM
    }
    EXPECT_GT(c.GetEffectiveBpm(), 200.0f);

    c.SetExternal(false);
    EXPECT_FLOAT_EQ(90.0f, c.GetEffectiveBpm())
        << "unpatching the clock must hand tempo back to the internal setting";
}

TEST(Clock, IgnoresNonsenseIntervals) {
    Clock c;
    c.SetExternal(true);
    c.SetPpqn(Ppqn1);
    unsigned long t = 1000000UL;
    for (int i = 0; i < 4; i++) {
        c.ExternalEdge(t);
        t += 500000UL;
    }
    float before = c.GetEffectiveBpm();

    c.ExternalEdge(t + 10UL);       // ringing: 10 us apart
    c.ExternalEdge(t + 20000000UL); // a stopped clock, 20 s later
    EXPECT_NEAR(before, c.GetEffectiveBpm(), 5.0f);
}

// ── External clock liveness ──────────────────────────────────────────────────
// "IN 1 is configured as a clock" and "a clock is actually running" are
// different things. The hardware cannot see a patched cable, but it can see
// whether pulses arrive — and that is what the UI badge and the tempo fallback
// must key off.

TEST(Clock, NotLiveUntilPulsesActuallyArrive) {
    Clock c;
    c.SetBpm(140);
    c.SetExternal(true); // IN 1 role = CLOCK, but nothing patched

    EXPECT_TRUE(c.IsExternal()) << "the jack is configured as a clock input";
    EXPECT_FALSE(c.IsExternalLive()) << "but no clock is running into it";
    EXPECT_FLOAT_EQ(140.0f, c.GetEffectiveBpm()) << "so the internal tempo rules";

    // Running the loop with no edges must not change that.
    for (unsigned long t = 1000; t < 3000000UL; t += 1000) {
        c.Update(t);
    }
    EXPECT_FALSE(c.IsExternalLive());
    EXPECT_FLOAT_EQ(140.0f, c.GetEffectiveBpm());
}

TEST(Clock, OneEdgeIsNotYetATempo) {
    Clock c;
    c.SetBpm(140);
    c.SetExternal(true);
    c.SetPpqn(Ppqn1);
    c.ExternalEdge(1000000UL); // a single pulse gives no interval
    EXPECT_FALSE(c.IsExternalLive());
    EXPECT_FLOAT_EQ(140.0f, c.GetEffectiveBpm());
}

TEST(Clock, GoesLiveOnceAnIntervalIsMeasured) {
    Clock c;
    c.SetBpm(140);
    c.SetExternal(true);
    c.SetPpqn(Ppqn1);
    c.ExternalEdge(1000000UL);
    c.ExternalEdge(1500000UL); // 500 ms apart -> 120 BPM
    EXPECT_TRUE(c.IsExternalLive());
    EXPECT_NEAR(120.0f, c.GetEffectiveBpm(), 1.0f);
}

TEST(Clock, StoppedClockHandsTempoBackToInternal) {
    // Unplugging the cable (or stopping the sequencer) must not leave the
    // containers turning at a tempo nothing is driving any more.
    Clock c;
    c.SetBpm(90);
    c.SetExternal(true);
    c.SetPpqn(Ppqn1);

    unsigned long t = 1000000UL;
    for (int i = 0; i < 4; i++) { // steady 240 BPM
        c.ExternalEdge(t);
        c.Update(t);
        t += 250000UL;
    }
    ASSERT_TRUE(c.IsExternalLive());
    ASSERT_GT(c.GetEffectiveBpm(), 200.0f);

    // Clock stops. Keep the loop running with no more edges.
    for (int i = 0; i < 3000; i++) {
        t += 1000;
        c.Update(t);
    }
    EXPECT_FALSE(c.IsExternalLive()) << "the clock stopped and was not noticed";
    EXPECT_FLOAT_EQ(90.0f, c.GetEffectiveBpm())
        << "tempo must fall back to the internal setting";
}

TEST(Clock, SlowExternalClockIsNotDeclaredDeadBetweenPulses) {
    // The timeout scales with the measured interval. A 30 BPM clock at 1 ppqn is
    // 2 s between pulses; a fixed short timeout would drop it every beat.
    Clock c;
    c.SetExternal(true);
    c.SetPpqn(Ppqn1);

    unsigned long t = 1000000UL;
    for (int i = 0; i < 4; i++) {
        c.ExternalEdge(t);
        c.Update(t);
        t += 2000000UL; // 2 s -> 30 BPM
    }
    ASSERT_TRUE(c.IsExternalLive());

    // Wait most of one interval without a pulse — still alive.
    for (int i = 0; i < 1800; i++) {
        t += 1000;
        c.Update(t);
    }
    EXPECT_TRUE(c.IsExternalLive()) << "a slow clock was wrongly declared stopped";
}

TEST(Clock, InternalTempoEditsApplyWhileNoClockIsRunning) {
    Clock c;
    c.SetExternal(true); // configured, but nothing patched
    c.SetBpm(200);
    EXPECT_FLOAT_EQ(200.0f, c.GetEffectiveBpm());
}
