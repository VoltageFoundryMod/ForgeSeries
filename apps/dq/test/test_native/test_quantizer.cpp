#include <gtest/gtest.h>

#include "quantizer.hpp"

namespace {

// C major: C D E F G A B
const bool kMajor[12] = {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1};
// Only C and G enabled — the wide gap makes boundary behaviour easy to pin down.
const bool kFifths[12] = {1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0};

Quantizer MakeQuantizer(const bool notes[12]) {
    Quantizer q;
    q.Build(notes);
    return q;
}

} // namespace

TEST(Quantizer, ChromaticByDefault) {
    Quantizer q;
    EXPECT_EQ(QUANT_SEMITONE_COUNT, q.ActiveCount());
    EXPECT_EQ(7, q.Quantize(7.1f, -1));
}

TEST(Quantizer, MajorScaleActiveCount) {
    Quantizer q = MakeQuantizer(kMajor);
    // 7 notes per octave across semitones 0..60 inclusive: 5 octaves + the top C.
    EXPECT_EQ(7 * 5 + 1, q.ActiveCount());
}

TEST(Quantizer, SnapsToNearestScaleNote) {
    Quantizer q = MakeQuantizer(kMajor);
    // D# (3) is not in C major; 3.4 is nearer E (4) than D (2).
    EXPECT_EQ(4, q.Quantize(3.4f, -1));
    // 5.4 is nearest F (5) — F# is absent.
    EXPECT_EQ(5, q.Quantize(5.4f, -1));
    // Exact scale notes stay put.
    EXPECT_EQ(9, q.Quantize(9.0f, -1));
}

TEST(Quantizer, SkipsDisabledNotesEntirely) {
    Quantizer q = MakeQuantizer(kFifths);
    // Everything between C (0) and G (7) must land on one of the two.
    for (float x = 0.0f; x <= 7.0f; x += 0.25f) {
        int out = q.Quantize(x, -1);
        EXPECT_TRUE(out == 0 || out == 7) << "input " << x << " gave " << out;
    }
}

TEST(Quantizer, HysteresisHoldsTheCurrentNoteAtTheBoundary) {
    Quantizer q = MakeQuantizer(kFifths);
    // Midpoint between C(0) and G(7) is 3.5. Coming from C, the output must not
    // move to G until the input clears 3.5 + hysteresis.
    EXPECT_EQ(0, q.Quantize(3.6f, 0, 0.15f));
    EXPECT_EQ(7, q.Quantize(3.7f, 0, 0.15f));
    // Symmetrically, coming from G it holds past the midpoint on the way down.
    EXPECT_EQ(7, q.Quantize(3.4f, 7, 0.15f));
    EXPECT_EQ(0, q.Quantize(3.3f, 7, 0.15f));
}

TEST(Quantizer, HysteresisDoesNotBlockScaleChanges) {
    Quantizer q = MakeQuantizer(kMajor);
    // Hold D (2), then switch to a scale where D is gone. The output must move
    // immediately instead of being pinned by hysteresis on an unreachable note.
    q.Build(kFifths);
    EXPECT_FALSE(q.Emits(2));
    EXPECT_EQ(0, q.Quantize(2.0f, 2));
}

TEST(Quantizer, EmptyMaskFallsBackToChromatic) {
    const bool none[12] = {0};
    Quantizer q = MakeQuantizer(none);
    EXPECT_EQ(QUANT_SEMITONE_COUNT, q.ActiveCount());
    EXPECT_EQ(3, q.Quantize(3.0f, -1));
}

TEST(Quantizer, EmitsReportsMembership) {
    Quantizer q = MakeQuantizer(kMajor);
    EXPECT_TRUE(q.Emits(0));   // C
    EXPECT_FALSE(q.Emits(1));  // C#
    EXPECT_TRUE(q.Emits(12));  // C, next octave
    EXPECT_FALSE(q.Emits(-1)); // out of range
    EXPECT_FALSE(q.Emits(QUANT_MAX_SEMITONE + 1));
}

TEST(Quantizer, OutputStaysInsideTheHardwareRange) {
    Quantizer q = MakeQuantizer(kMajor);
    EXPECT_GE(q.Quantize(-10.0f, -1), 0);
    EXPECT_LE(q.Quantize(1000.0f, -1), QUANT_MAX_SEMITONE);
}

// ── Unit conversions ────────────────────────────────────────────────────────

TEST(Quantizer, CountsAndSemitonesRoundTrip) {
    // 0V = semitone 0, 5V (4095 counts) = semitone 60.
    EXPECT_NEAR(0.0f, CountsToSemitones(0.0f), 1e-4f);
    EXPECT_NEAR(60.0f, CountsToSemitones(4095.0f), 1e-3f);
    // One octave is exactly 1V, i.e. one fifth of full scale.
    EXPECT_NEAR(819.0f, SemitonesToCounts(12.0f), 0.01f);
    EXPECT_NEAR(12.0f, CountsToSemitones(SemitonesToCounts(12.0f)), 1e-3f);
}

TEST(Quantizer, ConversionClampsToOutputRange) {
    EXPECT_FLOAT_EQ(0.0f, SemitonesToCounts(-5.0f));
    EXPECT_FLOAT_EQ(4095.0f, SemitonesToCounts(200.0f));
}

TEST(Quantizer, NoteAndOctaveDecomposition) {
    EXPECT_EQ(0, SemitoneToNoteIndex(0));
    EXPECT_EQ(0, SemitoneToOctave(0));
    EXPECT_EQ(9, SemitoneToNoteIndex(21)); // A, second octave
    EXPECT_EQ(1, SemitoneToOctave(21));
    EXPECT_EQ(0, SemitoneToNoteIndex(60)); // top C
    EXPECT_EQ(5, SemitoneToOctave(60));
}
