#include <gtest/gtest.h>
// uncomment line below if you plan to use GMock
// #include <gmock/gmock.h>

#include "scales.cpp"

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
TEST(BuildScale, Minor_Asharp) {  // Notes: A#, C, C#, D#, F, F#, G#, A#
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
