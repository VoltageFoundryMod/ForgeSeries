#include <gtest/gtest.h>

#include "envelope.hpp"

// The envelope is driven entirely by a caller-supplied microsecond clock, so
// these tests step time explicitly rather than sleeping.

TEST(Envelope, IdleUntilTriggered) {
    Envelope e;
    EXPECT_EQ(0, e.Update(0));
    EXPECT_FALSE(e.IsActive());
}

TEST(Envelope, ZeroAttackJumpsStraightToFull) {
    Envelope e;
    e.SetAttack(0);
    e.SetDecay(1000);
    e.Trigger(0);
    EXPECT_EQ(ENVELOPE_MAX, e.Update(0));
    EXPECT_TRUE(e.IsActive());
}

TEST(Envelope, AttackRisesThenDecayFalls) {
    Envelope e;
    e.SetAttack(100); // ms
    e.SetDecay(400);
    e.Trigger(0);

    uint16_t atStart = e.Update(0);
    uint16_t midAttack = e.Update(50000); // 50 ms in
    uint16_t peak = e.Update(100000);     // attack complete
    EXPECT_LT(atStart, midAttack);
    EXPECT_LT(midAttack, peak);
    EXPECT_EQ(ENVELOPE_MAX, peak);

    // Decay is timed from the moment the attack completed.
    uint16_t midDecay = e.Update(300000);
    EXPECT_GT(peak, midDecay);
    EXPECT_GT(midDecay, 0);
    EXPECT_EQ(0, e.Update(500001));
    EXPECT_FALSE(e.IsActive());
}

TEST(Envelope, AttackIsConcave) {
    // The carried-over curve rises fast then flattens: half-way through the
    // attack the output is already well past half scale.
    Envelope e;
    e.SetAttack(100);
    e.SetDecay(1000);
    e.Trigger(0);
    uint16_t half = e.Update(50000);
    EXPECT_GT(half, ENVELOPE_MAX / 2);
}

TEST(Envelope, RetriggerRestartsFromTheAttack) {
    Envelope e;
    e.SetAttack(100);
    e.SetDecay(1000);
    e.Trigger(0);
    e.Update(100000); // reach the peak
    uint16_t decaying = e.Update(500000);
    EXPECT_LT(decaying, ENVELOPE_MAX);

    e.Trigger(500000);
    EXPECT_LT(e.Update(500000), decaying + 1); // back to the start of the attack
    EXPECT_EQ(ENVELOPE_MAX, e.Update(600000));
}

TEST(Envelope, ZeroDecayEndsImmediately) {
    Envelope e;
    e.SetAttack(0);
    e.SetDecay(0);
    e.Trigger(0);
    EXPECT_EQ(0, e.Update(1));
    EXPECT_FALSE(e.IsActive());
}

TEST(Envelope, LevelScalesTheOutput) {
    Envelope e;
    e.SetAttack(0);
    e.SetDecay(1000);
    e.SetLevel(50);
    e.Trigger(0);
    EXPECT_NEAR(ENVELOPE_MAX / 2, e.Update(0), 2);

    e.SetLevel(0);
    EXPECT_EQ(0, e.Update(0));
}

TEST(Envelope, TriggerModeEmitsAFixedPulse) {
    Envelope e;
    e.SetMode(GateTrigger);
    e.SetDecay(4000); // must be ignored in this mode
    e.Trigger(0);
    EXPECT_EQ(ENVELOPE_MAX, e.Update(0));
    EXPECT_EQ(ENVELOPE_MAX, e.Update(ENVELOPE_TRIGGER_MS * 1000UL - 1));
    EXPECT_EQ(0, e.Update(ENVELOPE_TRIGGER_MS * 1000UL));
}

TEST(Envelope, GateModeFollowsTheInputLevelAndIgnoresTriggers) {
    Envelope e;
    e.SetMode(GateGate);
    e.Trigger(0); // must not start anything on its own
    EXPECT_EQ(0, e.Update(0));

    e.SetGateHigh(true);
    EXPECT_EQ(ENVELOPE_MAX, e.Update(1000));
    EXPECT_TRUE(e.IsActive());

    e.SetGateHigh(false);
    EXPECT_EQ(0, e.Update(2000));
    EXPECT_FALSE(e.IsActive());
}

TEST(Envelope, ParametersAreClamped) {
    Envelope e;
    e.SetAttack(-100);
    EXPECT_EQ(0, e.GetAttack());
    e.SetAttack(99999);
    EXPECT_EQ(ENVELOPE_MAX_ATTACK, e.GetAttack());
    e.SetDecay(99999);
    EXPECT_EQ(ENVELOPE_MAX_DECAY, e.GetDecay());
    e.SetLevel(500);
    EXPECT_EQ(100, e.GetLevel());
    e.SetMode(99);
    EXPECT_EQ(GateModeLength - 1, e.GetMode());
}
