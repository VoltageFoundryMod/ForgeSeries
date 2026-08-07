// test_shiftreg.cpp — the register, and the weave.
//
// These are the tests that guard the module's central claims, the ones the
// panel and the manual are sold on (docs/Design.md §2, §3):
//
//   CHANCE 0                        → the pattern repeats every LENGTH steps
//   CHANCE 100                      → it repeats every 2×LENGTH steps
//   WEAVE 100 + CHANCE 0            → the two registers ARE one ring of A+B
//   DIR A▸B                         → B cannot contaminate A, bit for bit
//
// If any of these is only approximately true, every control on the module is
// lying about what it does.

#include <gtest/gtest.h>

#include <vector>

#include "shiftreg.hpp"

namespace {

const uint8_t kNoChance[2] = {0, 0};

// Full observable state of the pair. Period is measured over this rather than
// over one output bit, because a bit stream can repeat while the machine behind
// it has not — that would still drift audibly on the other outputs.
struct State {
    uint16_t a, b;
    bool operator==(const State &o) const { return a == o.a && b == o.b; }
};

State Snapshot(const WeavePair &p) {
    return State{p.Reg(0).Value(), p.Reg(1).Value()};
}

// Clock `n` times, discarding the result — used to let the region above the
// feedback point fill with the loop before a period is measured.
void Settle(WeavePair &p, uint8_t weave, uint8_t dir, const uint8_t chance[2],
            int n) {
    for (int i = 0; i < n; i++) {
        p.Clock(weave, dir, chance);
    }
}

// The number of clocks until the machine returns to where it started, or 0 if
// it has not come back within `limit`.
int Period(WeavePair &p, uint8_t weave, uint8_t dir, const uint8_t chance[2],
           int limit = 4096) {
    const State start = Snapshot(p);
    for (int n = 1; n <= limit; n++) {
        p.Clock(weave, dir, chance);
        if (Snapshot(p) == start) {
            return n;
        }
    }
    return 0;
}

// A pair with a single 1 circulating and nothing else. One-hot content is what
// makes an assertion on the period exact: a richer pattern can have a period
// that divides the loop length (all-zeros repeats every step), so a test built
// on one would pass for the wrong reason.
WeavePair OneHot(uint8_t lenA, uint8_t lenB, uint16_t a = 1, uint16_t b = 0) {
    WeavePair p;
    p.Seed(12345);
    p.Reg(0).SetLength(lenA);
    p.Reg(1).SetLength(lenB);
    p.Reg(0).SetValue(a);
    p.Reg(1).SetValue(b);
    return p;
}

// ── The register ─────────────────────────────────────────────────────────────

TEST(ShiftReg, ChanceZeroLoopsAtExactlyLength) {
    for (uint8_t n = WEA_MIN_LENGTH; n <= WEA_MAX_LENGTH; n++) {
        WeavePair p = OneHot(n, n);
        Settle(p, 0, WeaveBoth, kNoChance, WEA_REG_BITS);
        EXPECT_EQ(Period(p, 0, WeaveBoth, kNoChance), (int)n)
            << "length " << (int)n;
    }
}

TEST(ShiftReg, ChanceFullLoopsAtExactlyTwiceLength) {
    // The original module's 7 o'clock trick: always-flip is as deterministic as
    // never-flip, and buys a pattern twice as long as LENGTH says.
    const uint8_t kAlways[2] = {100, 100};
    for (uint8_t n = WEA_MIN_LENGTH; n <= 8; n++) {
        WeavePair p = OneHot(n, n);
        Settle(p, 0, WeaveBoth, kAlways, WEA_REG_BITS * 2);
        EXPECT_EQ(Period(p, 0, WeaveBoth, kAlways), (int)n * 2)
            << "length " << (int)n;
    }
}

TEST(ShiftReg, RegionAboveTheFeedbackPointIsACopyOfTheLoop) {
    // Design.md §2: bits above LENGTH are a delay line, not an attic. Locked,
    // they are the same loop at another phase — which is what makes ROTATE
    // beyond LENGTH musically meaningful instead of reading dead bits.
    const uint8_t n = 5;
    WeavePair p = OneHot(n, n, 0x0013);
    Settle(p, 0, WeaveBoth, kNoChance, WEA_REG_BITS);

    for (uint8_t k = 0; k + n < WEA_REG_BITS; k++) {
        EXPECT_EQ(p.Reg(0).Bit(k + n), p.Reg(0).Bit(k)) << "bit " << (int)k;
    }
}

TEST(ShiftReg, LengthIsClampedToTheLegalRange) {
    ShiftRegister r;
    r.SetLength(0);
    EXPECT_EQ(r.Length(), WEA_MIN_LENGTH);
    r.SetLength(200);
    EXPECT_EQ(r.Length(), WEA_MAX_LENGTH);
}

// ── WEAVE ────────────────────────────────────────────────────────────────────

TEST(Weave, FullWeaveChainsBothRegistersIntoOneRing) {
    // The module's proposition: at WEAVE 100 the two registers are one ring of
    // A+B, not two of A and B.
    for (uint8_t n = 2; n <= 8; n++) {
        WeavePair p = OneHot(n, n);
        Settle(p, 100, WeaveBoth, kNoChance, WEA_REG_BITS * 2);
        EXPECT_EQ(Period(p, 100, WeaveBoth, kNoChance), (int)n * 2)
            << "length " << (int)n;
    }
}

TEST(Weave, UnequalLengthsChainToTheSumNotTwiceEither) {
    // Nothing requires the two registers to be the same length, and the ring
    // they make is A+B long. The equal-length 2N in the manual is this with
    // A == B, so the general case is what the code has to get right.
    struct {
        uint8_t a, b;
    } cases[] = {{5, 3}, {7, 2}, {4, 12}, {16, 16}};

    for (auto &c : cases) {
        WeavePair p = OneHot(c.a, c.b);
        Settle(p, 100, WeaveBoth, kNoChance, WEA_COMBINED_BITS * 2);
        EXPECT_EQ(Period(p, 100, WeaveBoth, kNoChance), (int)(c.a + c.b))
            << "lengths " << (int)c.a << "/" << (int)c.b;
    }
}

TEST(Weave, ZeroWeaveLeavesEachRegisterOnItsOwnLength) {
    WeavePair p = OneHot(5, 3, 1, 1);
    Settle(p, 0, WeaveBoth, kNoChance, WEA_REG_BITS);
    // Independent loops of 5 and 3 return together every 15, not every 8.
    EXPECT_EQ(Period(p, 0, WeaveBoth, kNoChance), 15);
}

TEST(Weave, OneWayCouplingLeavesTheSenderBitIdentical) {
    // A▸B must leave A exactly as it would have been alone — not merely
    // statistically similar. This is also what guards the "no draw when a
    // register cannot receive" rule in Clock(): consuming a random number for a
    // decision that cannot go the other way would desynchronise A's own stream.
    const uint8_t chance[2] = {0, 40};

    WeavePair coupled = OneHot(7, 5, 0x004B, 0x0037);
    WeavePair alone = OneHot(7, 5, 0x004B, 0x0037);

    for (int i = 0; i < 256; i++) {
        coupled.Clock(100, WeaveAtoB, chance);
        alone.Clock(0, WeaveBoth, chance);
        ASSERT_EQ(coupled.Reg(0).Value(), alone.Reg(0).Value())
            << "diverged at clock " << i;
    }
}

TEST(Weave, OneWayCouplingStillMovesTheReceiver) {
    // The companion to the test above: proving A is untouched is only half the
    // claim if B is untouched too.
    WeavePair coupled = OneHot(7, 5, 0x004B, 0x0037);
    WeavePair alone = OneHot(7, 5, 0x004B, 0x0037);

    bool diverged = false;
    for (int i = 0; i < 64 && !diverged; i++) {
        coupled.Clock(100, WeaveAtoB, kNoChance);
        alone.Clock(0, WeaveBoth, kNoChance);
        diverged = coupled.Reg(1).Value() != alone.Reg(1).Value();
    }
    EXPECT_TRUE(diverged);
}

// ── Windows ──────────────────────────────────────────────────────────────────

TEST(Window, ReadsDepthBitsFromTheRotateOffset) {
    ShiftRegister r;
    r.SetValue(0x000B); // 0000 0000 0000 1011

    EXPECT_EQ(r.Window(0, 4), 0x0B);
    EXPECT_EQ(r.Window(1, 4), 0x05);
    EXPECT_EQ(r.Window(0, 1), 0x01);
    EXPECT_EQ(r.Window(2, 2), 0x02);
}

TEST(Window, WrapsAroundTheTopOfTheRegister) {
    ShiftRegister r;
    r.SetValue(0x000B);
    // Rotating past bit 15 must come back to bit 0 — the register is a ring, so
    // a window near the top spans the seam rather than reading zeros.
    EXPECT_EQ(r.Window(14, 4), 0x0C);
}

TEST(Window, CombinedRingSpansTheBoundaryBetweenAAndB) {
    WeavePair p;
    p.Reg(0).SetValue(0x8000); // top bit of A  → combined bit 15
    p.Reg(1).SetValue(0x0001); // bottom bit of B → combined bit 16

    EXPECT_EQ(p.Combined(), 0x00018000u);
    EXPECT_EQ(p.CombinedWindow(15, 2), 0x03);
    EXPECT_EQ(p.Window(SrcAB, 15, 2), 0x03);
    // The same offsets read through A alone see only A's half.
    EXPECT_EQ(p.Window(SrcA, 15, 2), 0x01);
}

TEST(Window, DepthIsClampedRatherThanOverflowing) {
    ShiftRegister r;
    r.SetValue(0xFFFF);
    EXPECT_EQ(r.Window(0, 0), 0x01);            // 0 bits is not a thing
    EXPECT_EQ(r.Window(0, WEA_MAX_DEPTH), 0xFF); // and 8 is the ceiling
    EXPECT_EQ(r.Window(0, 200), 0xFF);
}

// ── Determinism ──────────────────────────────────────────────────────────────

TEST(Random, EndpointsAreExactAndDrawNothing) {
    // CHANCE 0 must never flip and WEAVE 100 must always cross — "very nearly
    // always" would make a locked pattern drift once an hour, which is worse
    // than not locking at all. Not consuming the sequence at the endpoints also
    // keeps one frozen register from shifting the other's draws.
    WeaveRandom rng;
    rng.Seed(999);
    const uint32_t before = rng.State();

    for (int i = 0; i < 32; i++) {
        EXPECT_FALSE(rng.Percent(0));
        EXPECT_TRUE(rng.Percent(100));
    }
    EXPECT_EQ(rng.State(), before);
}

TEST(Random, SameSeedGivesTheSameSequence) {
    // The host runner, the RP2040 and the Rack port must agree, which they only
    // do if nothing here reaches for Arduino random().
    const uint8_t chance[2] = {35, 60};

    WeavePair a = OneHot(9, 6, 0xBEEF, 0x1234);
    WeavePair b = OneHot(9, 6, 0xBEEF, 0x1234);

    for (int i = 0; i < 512; i++) {
        a.Clock(45, WeaveBoth, chance);
        b.Clock(45, WeaveBoth, chance);
        ASSERT_EQ(a.Reg(0).Value(), b.Reg(0).Value()) << "clock " << i;
        ASSERT_EQ(a.Reg(1).Value(), b.Reg(1).Value()) << "clock " << i;
    }
}

TEST(Random, MidChanceActuallyDrifts) {
    // The negative of the lock tests: if CHANCE did nothing, every test above
    // would still pass.
    const uint8_t chance[2] = {50, 50};
    WeavePair p = OneHot(8, 8, 0x00F0, 0x00F0);
    Settle(p, 0, WeaveBoth, chance, WEA_REG_BITS);
    EXPECT_EQ(Period(p, 0, WeaveBoth, chance, 256), 0);
}

} // namespace
