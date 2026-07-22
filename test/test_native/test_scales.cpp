#include <gtest/gtest.h>

#include "scales.hpp"

// C  C# D  D# E  F  F# G  G# A  A# B
// 0  1  2  3  4  5  6  7  8  9  10 11

// Test Chromatic
TEST(BuildScale, Chromatic) {
    bool expected[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    bool result[12];
    BuildScale(0, 0, result);
    for (int i = 0; i < 12; i++) {
        EXPECT_EQ(expected[i], result[i]);
    }
}

// Test Major C
TEST(BuildScale, Major_C) {
    bool expected[12] = {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1};
    bool result[12];
    BuildScale(1, 0, result);
    for (int i = 0; i < 12; i++) {
        EXPECT_EQ(expected[i], result[i]);
    }
}

// Test Major C#
TEST(BuildScale, Major_Csharp) {
    // Notes: C♯, D♯, E♯, F♯, G♯, A♯, B♯, C♯
    bool expected[12] = {1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0};
    bool result[12];
    BuildScale(1, 1, result);
    for (int i = 0; i < 12; i++) {
        EXPECT_EQ(expected[i], result[i]);
    }
}

// Test Major D#
TEST(BuildScale, Major_Dsharp) {
    // Notes: D♯, E♯, F, G♯, A♯, B♯, C, D♯
    bool expected[12] = {1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0};
    bool result[12];
    BuildScale(1, 3, result);
    for (int i = 0; i < 12; i++) {
        EXPECT_EQ(expected[i], result[i]);
    }
}

// Test A Minor
TEST(BuildScale, Minor_A) {
    // Notes: A, B, C, D, E, F, G, A
    bool expected[12] = {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1};
    bool result[12];
    BuildScale(2, 9, result);
    for (int i = 0; i < 12; i++) {
        EXPECT_EQ(expected[i], result[i]);
    }
}

// Test A# Minor
TEST(BuildScale, Minor_Asharp) { // Notes: A#, C, C#, D#, F, F#, G#, A#
    bool expected[12] = {1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0};
    bool result[12];
    BuildScale(2, 10, result);
    for (int i = 0; i < 12; i++) {
        EXPECT_EQ(expected[i], result[i]);
    }
}

TEST(BuildScale, Major_D) {
    // Notes: D, E, F♯, G, A, B, C♯, D
    bool expected[12] = {0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1};
    bool result[12];
    BuildScale(1, 2, result);
    for (int i = 0; i < 12; i++) {
        EXPECT_EQ(expected[i], result[i]);
    }
}

// Test C Minor
TEST(BuildScale, Minor_C) {
    // Notes: C, D, Eb, F, G, Ab, Bb, C
    bool expected[12] = {1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0};
    bool result[12];
    BuildScale(2, 0, result);
    for (int i = 0; i < 12; i++) {
        EXPECT_EQ(expected[i], result[i]);
    }
}

TEST(BuildScale, Minor_F) {
    // Notes: F, G, Ab, Bb, C, Db, Eb, F
    bool expected[12] = {1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0};
    bool result[12];
    BuildScale(2, 5, result);
    for (int i = 0; i < 12; i++) {
        EXPECT_EQ(expected[i], result[i]);
    }
}

// Test Pentatonic Minor for G
TEST(BuildScale, PentatonicMinor_G) {
    // Notes: G, B♭, C, D, F, G
    bool expected[12] = {1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0};
    bool result[12];
    BuildScale(8, 7, result);
    for (int i = 0; i < 12; i++) {
        EXPECT_EQ(expected[i], result[i]);
    }
}

// ── Length handling ─────────────────────────────────────────────────────────
// The scale rows are zero-padded out to 7 entries. Reading that padding would
// silently re-enable the root, so these cases pin the enabled-note count for
// every scale, especially the ones shorter than 7 notes.

TEST(BuildScale, ScaleLengthsMatchEnabledNoteCount) {
    for (int s = 0; s < numScales; s++) {
        bool result[12];
        BuildScale(s, 0, result);
        int enabled = 0;
        for (int i = 0; i < 12; i++) {
            enabled += result[i] ? 1 : 0;
        }
        EXPECT_EQ(scaleNoteCount[s], enabled) << "scale index " << s;
    }
}

TEST(BuildScale, PentatonicMajor_C) {
    // Notes: C, D, E, G, A
    bool expected[12] = {1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0};
    bool result[12];
    BuildScale(9, 0, result);
    for (int i = 0; i < 12; i++) {
        EXPECT_EQ(expected[i], result[i]);
    }
}

TEST(BuildScale, WholeTone_C) {
    // Notes: C, D, E, F#, G#, A#
    bool expected[12] = {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0};
    bool result[12];
    BuildScale(12, 0, result);
    for (int i = 0; i < 12; i++) {
        EXPECT_EQ(expected[i], result[i]);
    }
}

TEST(BuildScale, Blues_C) {
    // Notes: C, Eb, F, F#, G, Bb
    bool expected[12] = {1, 0, 0, 1, 0, 1, 1, 1, 0, 0, 1, 0};
    bool result[12];
    BuildScale(14, 0, result);
    for (int i = 0; i < 12; i++) {
        EXPECT_EQ(expected[i], result[i]);
    }
}

// A previously populated mask must be replaced wholesale, not merged into.
TEST(BuildScale, ClearsPreviousMask) {
    bool result[12];
    BuildScale(0, 0, result); // chromatic — everything on
    BuildScale(9, 0, result); // pentatonic major
    int enabled = 0;
    for (int i = 0; i < 12; i++) {
        enabled += result[i] ? 1 : 0;
    }
    EXPECT_EQ(5, enabled);
}

// Out-of-range indices must leave the caller's mask untouched.
TEST(BuildScale, IgnoresInvalidScaleIndex) {
    bool result[12];
    BuildScale(1, 0, result); // C major
    BuildScale(numScales, 0, result);
    bool expected[12] = {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1};
    for (int i = 0; i < 12; i++) {
        EXPECT_EQ(expected[i], result[i]);
    }
}

// Names must stay in step with the note tables — a missing entry here would
// read past the end of the array on the menu and keyboard pages.
TEST(BuildScale, NameTablesAreComplete) {
    for (int s = 0; s < numScales; s++) {
        EXPECT_NE(nullptr, scaleNames[s]);
        EXPECT_NE(nullptr, scaleShortNames[s]);
        EXPECT_LE(strlen(scaleShortNames[s]), 4u) << "scale index " << s;
    }
}
