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

// ── The step phase ───────────────────────────────────────────────────────────
// StepPhase() drives every moving thing on the home screen. It has one job: ramp
// 0..1 exactly once per step, on both sources, at every rate. The failure that
// makes it worth testing is source-specific and invisible in the numbers above —
// under a DIVIDED EXTERNAL clock the accumulator is re-zeroed at every beat
// while the step spans several, so a naive ratio sweeps a quarter and snaps back
// four times per step, and the screen stutters against a clock it is locked to.

struct PhaseTrace {
    int steps = 0;
    int wraps = 0; // phase going backwards — should happen once per step
    float peak = 0.0f;
};

// A wrap is a drop of more than half the ramp, not any drop at all.
//
// At rates that do not divide the beat exactly — x3 and x6 against IN PPQN 4,
// where the period is 500000/3 = 166666 µs with 2 µs left over — the free-running
// accumulator completes its last period a couple of microseconds BEFORE the beat
// boundary that actually emits the step. Phase reads 0.000012 and then 0, which
// is two backwards steps one tick apart and one instant on any timescale a
// screen has. Counting both would fail a clock that is behaving perfectly.
//
// The truncated-ramp bug this test exists for is not hidden by the threshold: a
// phase that resets every beat under a divided external clock never gets near 1,
// so `peak` catches it whatever the wrap count says.
void SamplePhase(StepClock &c, PhaseTrace &tr, float &prev, bool counting) {
    const float ph = c.StepPhase();
    if (counting) {
        if (prev - ph > 0.5f) {
            tr.wraps++;
        }
        if (ph > tr.peak) {
            tr.peak = ph;
        }
    }
    prev = ph;
}

PhaseTrace TraceInternal(int rateIndex, int beats, int bpm = 120) {
    StepClock c;
    c.SetBpm(bpm);
    c.SetRate(rateIndex);

    unsigned long t = 1000000;
    c.Update(t);

    PhaseTrace tr;
    float prev = c.StepPhase();
    const unsigned long span = (unsigned long)beats * (60000000UL / (unsigned long)bpm);
    for (unsigned long e = 0; e < span; e += kTickUs) {
        t += kTickUs;
        c.Update(t);
        if (c.ConsumeStep()) {
            tr.steps++;
        }
        SamplePhase(c, tr, prev, true);
    }
    return tr;
}

PhaseTrace TraceExternal(int rateIndex, int beats) {
    StepClock c;
    c.SetBpm(60);
    c.SetRate(rateIndex);
    c.SetExternal(true);
    c.SetPpqn(Ppqn4);

    const unsigned long pulseUs = kBeatUs / 4;
    unsigned long t = 1000000;
    c.Update(t);

    PhaseTrace tr;
    float prev = 0.0f;
    const int warmupPulses = 8;
    const int totalPulses = warmupPulses + beats * 4;

    for (int p = 0; p < totalPulses; p++) {
        const bool counting = p >= warmupPulses;
        c.ExternalEdge(t);
        if (c.ConsumeStep() && counting) {
            tr.steps++;
        }
        SamplePhase(c, tr, prev, counting);
        for (unsigned long e = 0; e < pulseUs; e += kTickUs) {
            t += kTickUs;
            c.Update(t);
            if (c.ConsumeStep() && counting) {
                tr.steps++;
            }
            SamplePhase(c, tr, prev, counting);
        }
    }
    return tr;
}

TEST(Phase, InternalRampsOnceAndOnlyOncePerStep) {
    for (int r = 0; r < WEA_CLOCK_RATE_COUNT; r++) {
        const PhaseTrace tr = TraceInternal(r, 32);
        EXPECT_EQ(tr.wraps, tr.steps) << "rate " << ClockRateNames[r];
        EXPECT_GT(tr.peak, 0.9f) << "rate " << ClockRateNames[r]
                                 << " never reaches the end of its step";
    }
}

TEST(Phase, ExternalRampsOnceAndOnlyOncePerStep) {
    // The divided rates are the ones that were wrong: /4 spans four beats, and
    // the beat boundary that re-locks the step phase must not read as a new step.
    for (int r = 0; r < WEA_CLOCK_RATE_COUNT; r++) {
        const PhaseTrace tr = TraceExternal(r, 32);
        EXPECT_EQ(tr.wraps, tr.steps) << "rate " << ClockRateNames[r];
        EXPECT_GT(tr.peak, 0.9f) << "rate " << ClockRateNames[r]
                                 << " never reaches the end of its step";
    }
}

TEST(Phase, StepCountCountsEveryStepExactlyOnce) {
    // The travelling braid advances one cell pitch per step and wraps modulo the
    // strand span, so a count that skipped or doubled would show as the cloth
    // jumping — and it is the count, not the phase, that carries the motion
    // across a clock edge.
    StepClock c;
    c.SetBpm(120);
    c.SetRate(WEA_CLOCK_RATE_UNITY + 3); // x4

    unsigned long t = 1000000;
    c.Update(t);
    EXPECT_EQ(c.StepCount(), 0u);

    int steps = 0;
    for (unsigned long e = 0; e < 8UL * kBeatUs; e += kTickUs) {
        t += kTickUs;
        c.Update(t);
        if (c.ConsumeStep()) {
            steps++;
        }
    }
    EXPECT_EQ(steps, 32);
    EXPECT_EQ((int)c.StepCount(), steps);
}

TEST(Phase, StaysInRange) {
    for (int r = 0; r < WEA_CLOCK_RATE_COUNT; r++) {
        EXPECT_LE(TraceInternal(r, 8).peak, 1.0f);
        EXPECT_LE(TraceExternal(r, 8).peak, 1.0f);
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
