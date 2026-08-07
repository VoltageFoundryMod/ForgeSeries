// test_clock.cpp — RATE, and the two clock sources agreeing about it.
//
// The bug this file exists to prevent shipped once: DIVIDE counted input edges
// on an external clock but scaled the step PERIOD on the internal one, so "/2"
// ran at half speed with a cable patched and DOUBLE speed without. Two code
// paths, one label, opposite meanings.
//
// So every rate here is asserted against BOTH sources, and the last test asserts
// they agree with each other — which is the property that was actually broken,
// and which neither source's own test would have caught.

#include <gtest/gtest.h>

#include "clock.hpp"

namespace {

const unsigned long kBeatUs = 500000; // 120 BPM
const unsigned long kTickUs = 1000;   // the granularity the engine polls at

// Run the internal clock for `beats` beats, counting steps.
int InternalSteps(int rateIndex, int beats, int bpm = 120) {
    StepClock c;
    c.SetBpm(bpm);
    c.SetRate(rateIndex);

    unsigned long t = 1000000; // start away from zero, as the hardware does
    c.Update(t);               // first call primes the elapsed baseline

    int steps = 0;
    const unsigned long span = (unsigned long)beats * (60000000UL / (unsigned long)bpm);
    for (unsigned long e = 0; e < span; e += kTickUs) {
        t += kTickUs;
        c.Update(t);
        if (c.ConsumeStep()) {
            steps++;
        }
    }
    return steps;
}

// Run an external clock at 4 pulses per beat for `beats` beats, counting steps.
// A warm-up beat is discarded: the tempo estimator needs a couple of intervals
// before it is live, and what is being measured here is the locked behaviour.
int ExternalSteps(int rateIndex, int beats) {
    StepClock c;
    c.SetBpm(60); // deliberately NOT the external tempo, so a step generator
                  // still running off the internal BPM shows up as a wrong count
    c.SetRate(rateIndex);
    c.SetExternal(true);
    c.SetPpqn(Ppqn4);

    const unsigned long pulseUs = kBeatUs / 4;
    unsigned long t = 1000000;
    c.Update(t);

    int steps = 0;
    const int warmupPulses = 8; // two beats
    const int totalPulses = warmupPulses + beats * 4;

    for (int p = 0; p < totalPulses; p++) {
        c.ExternalEdge(t);
        if (c.ConsumeStep() && p >= warmupPulses) {
            steps++;
        }
        for (unsigned long e = 0; e < pulseUs; e += kTickUs) {
            t += kTickUs;
            c.Update(t);
            if (c.ConsumeStep() && p >= warmupPulses) {
                steps++;
            }
        }
    }
    return steps;
}

// ── The internal clock ───────────────────────────────────────────────────────

TEST(Rate, InternalUnityIsOneStepPerBeat) {
    EXPECT_EQ(InternalSteps(WEA_CLOCK_RATE_UNITY, 8), 8);
}

TEST(Rate, InternalMultiplicationSpeedsUp) {
    // x2 / x4 / x8 over four beats.
    EXPECT_EQ(InternalSteps(WEA_CLOCK_RATE_UNITY + 1, 4), 8);  // x2
    EXPECT_EQ(InternalSteps(WEA_CLOCK_RATE_UNITY + 3, 4), 16); // x4
    EXPECT_EQ(InternalSteps(WEA_CLOCK_RATE_UNITY + 5, 4), 32); // x8
}

TEST(Rate, InternalDivisionSlowsDown) {
    // This is the direction the old DIVIDE got backwards on this source: "/2"
    // must produce FEWER steps than x1, not more.
    EXPECT_EQ(InternalSteps(WEA_CLOCK_RATE_UNITY - 1, 8), 4); // /2
    EXPECT_EQ(InternalSteps(WEA_CLOCK_RATE_UNITY - 3, 16), 4); // /4
    EXPECT_LT(InternalSteps(WEA_CLOCK_RATE_UNITY - 1, 8),
              InternalSteps(WEA_CLOCK_RATE_UNITY, 8));
}

// ── The external clock ───────────────────────────────────────────────────────

TEST(Rate, ExternalUnityIsOneStepPerBeat) {
    // Four pulses per beat at IN PPQN 4, so x1 is one step every four pulses —
    // not one step per pulse.
    EXPECT_EQ(ExternalSteps(WEA_CLOCK_RATE_UNITY, 8), 8);
}

TEST(Rate, ExternalMultiplicationInterpolatesBetweenPulses) {
    // x8 is twice the incoming pulse rate, so half the steps have no edge to
    // land on and must be generated from the measured interval.
    EXPECT_EQ(ExternalSteps(WEA_CLOCK_RATE_UNITY + 1, 8), 16); // x2
    EXPECT_EQ(ExternalSteps(WEA_CLOCK_RATE_UNITY + 3, 8), 32); // x4
    EXPECT_EQ(ExternalSteps(WEA_CLOCK_RATE_UNITY + 5, 8), 64); // x8
}

TEST(Rate, ExternalDivisionCountsBeats) {
    EXPECT_EQ(ExternalSteps(WEA_CLOCK_RATE_UNITY - 1, 8), 4);  // /2
    EXPECT_EQ(ExternalSteps(WEA_CLOCK_RATE_UNITY - 3, 16), 4); // /4
}

// ── The property that was actually broken ────────────────────────────────────

TEST(Rate, BothSourcesAgreeAtEveryRate) {
    // One control, one meaning. Whatever RATE says, a beat of internal clock and
    // a beat of external clock must produce the same number of steps — the thing
    // the old DIVIDE got exactly backwards.
    const int beats = 16;
    for (int r = 0; r < WEA_CLOCK_RATE_COUNT; r++) {
        const int internalSteps = InternalSteps(r, beats);
        const int externalSteps = ExternalSteps(r, beats);
        EXPECT_EQ(internalSteps, externalSteps)
            << "rate " << ClockRateNames[r] << " disagrees between sources";
    }
}

TEST(Rate, RateIsMonotonic) {
    // Turning the encoder one way must never make the sequencer slower. A table
    // whose names and values fell out of order would be invisible until someone
    // wondered why x3 was slower than x2.
    int previous = -1;
    for (int r = 0; r < WEA_CLOCK_RATE_COUNT; r++) {
        const int steps = InternalSteps(r, 48);
        EXPECT_GT(steps, previous) << "rate " << ClockRateNames[r];
        previous = steps;
    }
}

TEST(Rate, RateIsClampedToTheTable) {
    StepClock c;
    c.SetRate(-5);
    EXPECT_EQ(c.GetRate(), 0);
    c.SetRate(999);
    EXPECT_EQ(c.GetRate(), WEA_CLOCK_RATE_COUNT - 1);
}

TEST(Rate, UnityAndDefaultIndicesNameWhatTheyClaim) {
    // The two named indices are used by the preset defaults and by the tests
    // above; a table edit that shifted them would silently retune the module.
    EXPECT_STREQ(ClockRateNames[WEA_CLOCK_RATE_UNITY], "x1");
    EXPECT_STREQ(ClockRateNames[WEA_CLOCK_RATE_DEFAULT], "x1");
}

TEST(Rate, TheDefaultRateIsTheTempoOnTheHeader) {
    // The module must boot stepping at exactly the BPM it displays. Anything
    // else makes the one number on the home screen describe something other than
    // what you are hearing, which is worse than needing a detent to get where
    // you wanted.
    EXPECT_EQ(InternalSteps(WEA_CLOCK_RATE_DEFAULT, 8, 120), 8);
    EXPECT_EQ(InternalSteps(WEA_CLOCK_RATE_DEFAULT, 8, 60), 8);
}

TEST(Rate, TheTableIsSymmetricAroundUnity) {
    // /N and xN must sit the same distance either side of x1, so a detent one way
    // is the mirror of a detent the other. An asymmetric table is the kind of
    // thing nobody notices until they are counting clicks in a performance.
    for (int k = 1; WEA_CLOCK_RATE_UNITY + k < WEA_CLOCK_RATE_COUNT &&
                    WEA_CLOCK_RATE_UNITY - k >= 0;
         k++) {
        const float up = ClockRateSteps[WEA_CLOCK_RATE_UNITY + k];
        const float down = ClockRateSteps[WEA_CLOCK_RATE_UNITY - k];
        EXPECT_NEAR(up * down, 1.0f, 1e-4f)
            << ClockRateNames[WEA_CLOCK_RATE_UNITY + k] << " vs "
            << ClockRateNames[WEA_CLOCK_RATE_UNITY - k];
    }
}

// ── Handover ─────────────────────────────────────────────────────────────────

TEST(Rate, StoppedExternalClockHandsSteppingBackToInternal) {
    StepClock c;
    c.SetBpm(120);
    c.SetRate(WEA_CLOCK_RATE_UNITY);
    c.SetExternal(true);
    c.SetPpqn(Ppqn4);

    unsigned long t = 1000000;
    c.Update(t);

    // Run an external clock for a few beats.
    for (int p = 0; p < 16; p++) {
        c.ExternalEdge(t);
        c.ConsumeStep();
        for (unsigned long e = 0; e < kBeatUs / 4; e += kTickUs) {
            t += kTickUs;
            c.Update(t);
            c.ConsumeStep();
        }
    }
    ASSERT_TRUE(c.IsExternalLive());

    // Cable pulled. The module must keep playing on its internal tempo rather
    // than stopping dead or running on at a tempo nothing is driving.
    int steps = 0;
    for (unsigned long e = 0; e < 8UL * kBeatUs; e += kTickUs) {
        t += kTickUs;
        c.Update(t);
        if (c.ConsumeStep()) {
            steps++;
        }
    }
    EXPECT_FALSE(c.IsExternalLive());
    EXPECT_GT(steps, 0) << "the sequencer stopped when the clock was unpatched";
}

} // namespace
