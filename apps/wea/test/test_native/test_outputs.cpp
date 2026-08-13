// test_outputs.cpp — the output matrix (Design.md §5).
//
// Two claims are worth pinning here, because both are things the module is sold
// on and both are silently wrong if the arithmetic drifts:
//
//   THRESH gives a gate output a density control, and the classic "high when
//   bit 0 is 1" is a POINT in that space rather than a special case.
//
//   ROUTING is recomputed from the slots, so editing a jack shows CUSTOM and a
//   preset that happens to match DUO says DUO.

#include <gtest/gtest.h>

#include "outputs.hpp"

namespace {

// ── THRESH ───────────────────────────────────────────────────────────────────

TEST(Thresh, AFilledCellPlaysAndAnEmptyOneDoesNot) {
    // The polarity, pinned on its own because it shipped backwards once: the
    // screen draws bit 1 as a FILLED cell, so a filled cell has to be the one
    // that makes a sound. The original comparison fired on low window values, so
    // an empty-looking row played and a full one was silent.
    //
    // Asserted across the depths rather than at one, because the window is a
    // multi-bit value and "high" has to mean high at every width.
    for (uint8_t d = 1; d <= WEA_MAX_DEPTH; d++) {
        const uint16_t span = (uint16_t)1u << d;
        EXPECT_FALSE(OutputBank::Fires(0, d, 50)) << "all-zero window, depth " << (int)d;
        EXPECT_TRUE(OutputBank::Fires((uint8_t)(span - 1), d, 50))
            << "all-ones window, depth " << (int)d;
    }
}

TEST(Thresh, DensityIsTheThresholdNotItsComplement) {
    // Flipping the polarity by comparing `window >= limit` would have inverted
    // THRESH along with it: a sparse kick at 12 % would have become a busy one.
    // The fraction of window values that fire must be thresh %, counted from the
    // top of the range.
    const uint8_t depth = 6;
    const int span = 1 << depth;
    for (int thresh = 0; thresh <= 100; thresh += 25) {
        int fired = 0;
        for (int w = 0; w < span; w++) {
            if (OutputBank::Fires((uint8_t)w, depth, (uint8_t)thresh)) {
                fired++;
            }
        }
        EXPECT_EQ(fired, thresh * span / 100) << "thresh " << thresh;
    }
}

TEST(Thresh, DepthOneAtFiftyPercentIsTheClassicBitZeroGate) {
    // The original module's gate: high when the bit is 1, low when it is 0. If
    // this ever stops holding, every patch built on the classic behaviour
    // changes character.
    EXPECT_FALSE(OutputBank::Fires(0, 1, 50));
    EXPECT_TRUE(OutputBank::Fires(1, 1, 50));
}

TEST(Thresh, ZeroNeverFiresAndFullAlwaysFires) {
    for (uint8_t w = 0; w < 8; w++) {
        EXPECT_FALSE(OutputBank::Fires(w, 3, 0)) << "window " << (int)w;
        EXPECT_TRUE(OutputBank::Fires(w, 3, 100)) << "window " << (int)w;
    }
}

TEST(Thresh, DensityRisesMonotonicallyWithTheThreshold) {
    // The whole point of comparing a window against a threshold rather than
    // reading one bit: a sparse kick and a busy hat off the same register.
    int previous = -1;
    for (int thresh = 0; thresh <= 100; thresh += 10) {
        int fired = 0;
        for (uint8_t w = 0; w < 8; w++) {
            if (OutputBank::Fires(w, 3, (uint8_t)thresh)) {
                fired++;
            }
        }
        EXPECT_GE(fired, previous) << "thresh " << thresh;
        previous = fired;
    }
    EXPECT_EQ(previous, 8); // 100 % fires on every window value
}

// ── Panel order ──────────────────────────────────────────────────────────────

TEST(JackOrder, WalksDownTheColumnsNotAcrossTheRows) {
    // Every list of the four jacks — the OUT menu pages, the ROUTING summary,
    // the Rack context menu — walks them through this, and it is read while
    // looking at a panel laid out in two columns. Pinned by NAME rather than by
    // index, so it fails if either this array or OutJackNames moves.
    const char *expected[WEA_NUM_OUTS] = {"A1", "A2", "B1", "B2"};
    for (int k = 0; k < WEA_NUM_OUTS; k++) {
        EXPECT_STREQ(OutJackNames[WEA_JACK_COLUMN_ORDER[k]], expected[k])
            << "slot " << k;
    }
}

TEST(JackOrder, IsAPermutationSoEveryJackIsListedExactlyOnce) {
    // A typo here would drop a jack out of every menu in the module and show
    // another one twice, which is the kind of thing that reads as "the menu is
    // broken" rather than as an ordering mistake.
    bool seen[WEA_NUM_OUTS] = {false, false, false, false};
    for (int k = 0; k < WEA_NUM_OUTS; k++) {
        const uint8_t j = WEA_JACK_COLUMN_ORDER[k];
        ASSERT_LT(j, WEA_NUM_OUTS) << "slot " << k << " is out of range";
        EXPECT_FALSE(seen[j]) << "jack " << (int)j << " listed twice";
        seen[j] = true;
    }
    for (int j = 0; j < WEA_NUM_OUTS; j++) {
        EXPECT_TRUE(seen[j]) << "jack " << j << " is never listed";
    }
}

// ── ROUTING ──────────────────────────────────────────────────────────────────

TEST(Routing, EachTemplateIsRecognisedAsItself) {
    for (uint8_t r = 0; r < RoutingLength; r++) {
        OutSlot slots[WEA_NUM_OUTS];
        ApplyRouting(slots, r);
        EXPECT_EQ(RoutingOf(slots), r) << "routing " << (int)r;
    }
}

TEST(Routing, EditingAnyJackMakesItCustom) {
    OutSlot slots[WEA_NUM_OUTS];
    ApplyRouting(slots, RouteDuo);
    ASSERT_EQ(RoutingOf(slots), RouteDuo);

    slots[2].rotate = 4; // one field, on one jack
    EXPECT_EQ(RoutingOf(slots), WEA_ROUTING_CUSTOM);
}

TEST(Routing, ComingBackToATemplateReportsItAgain) {
    // Recomputed rather than stored, so there is no "it was CUSTOM once" state
    // to get stuck in — which is the reason it is recomputed.
    OutSlot slots[WEA_NUM_OUTS];
    ApplyRouting(slots, RoutePulse);
    slots[0].depth = 7;
    ASSERT_EQ(RoutingOf(slots), WEA_ROUTING_CUSTOM);

    slots[0].depth = ROUTING_TEMPLATES[RoutePulse][0].depth;
    EXPECT_EQ(RoutingOf(slots), RoutePulse);
}

TEST(Routing, TheThreeTemplatesAreActuallyDifferent) {
    // A copy-paste slip in the table would leave two routings identical and
    // RoutingOf() would report the first match for both.
    for (uint8_t a = 0; a < RoutingLength; a++) {
        for (uint8_t b = (uint8_t)(a + 1); b < RoutingLength; b++) {
            bool same = true;
            for (int i = 0; i < WEA_NUM_OUTS && same; i++) {
                same = SlotsEqual(ROUTING_TEMPLATES[a][i], ROUTING_TEMPLATES[b][i]);
            }
            EXPECT_FALSE(same) << "routings " << (int)a << " and " << (int)b;
        }
    }
}

TEST(Routing, DuoMirrorsTheSeriesJackMap) {
    // NOTE A / NOTE B / GATE A / GATE B, the same map NoteForge and
    // GravityForge use. The muscle memory across the series depends on it.
    const OutSlot *duo = ROUTING_TEMPLATES[RouteDuo];
    EXPECT_EQ(duo[0].source, SrcA);
    EXPECT_EQ(duo[0].type, OutNote);
    EXPECT_EQ(duo[1].source, SrcB);
    EXPECT_EQ(duo[1].type, OutNote);
    EXPECT_EQ(duo[2].source, SrcA);
    EXPECT_EQ(duo[2].type, OutGate);
    EXPECT_EQ(duo[3].source, SrcB);
    EXPECT_EQ(duo[3].type, OutGate);
}

} // namespace
