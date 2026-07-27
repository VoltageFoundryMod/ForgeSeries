#pragma once

// scales.hpp — Musical scale definitions and the note mask builder.
//
// A "scale" here is only a helper for *populating* a channel's 12-note mask:
// the mask is the source of truth for quantization and the user is free to edit
// individual keys afterwards (the keyboard page), which leaves the mask no
// longer matching any named scale.  That is intentional and matches the
// hardware's workflow.
//
// Appending to the tables below is safe — existing scale indices never move, so
// stored presets keep resolving to the same scale.

// Menu-length names (up to 6 chars) and the abbreviated forms used on the
// keyboard page, where only ~4 characters fit beside the keys.
static char const *const scaleNames[] = {"Chrom", "Major", "Minor", "Dorian", "Phryg", "Lydian",
                            "Mixo", "Locr", "PenMin", "PenMaj", "HarMin", "MelMin",
                            "Whole", "Dimin", "Blues"};
static char const *const scaleShortNames[] = {"Chrm", "Maj", "Min", "Dor", "Phr", "Lyd",
                                 "Mix", "Loc", "PnMi", "PnMa", "Harm", "Mel",
                                 "Whol", "Dim", "Blue"};
static char const *const noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static char const *const noteNamesLong[] = {"C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B"};
static int const numScales = sizeof(scaleNames) / sizeof(scaleNames[0]);

// ScaleNotes holds the semitone offsets of each scale, relative to its root.
// C  C# D  D# E F F# G G# A A# B
// 0  1  2  3  4 5 6  7 8  9 10 11
//
// Scales have differing lengths, so scaleNoteCount[] carries the real length of
// each row.  (Iterating a fixed 7 entries would read the zero-padded tail of the
// shorter rows and silently re-enable the root — the bug this table replaces.)
static int const scaleNotes[numScales][7] = {
    {},                     // 0  Chromatic — special-cased in BuildScale()
    {0, 2, 4, 5, 7, 9, 11}, // 1  Major
    {0, 2, 3, 5, 7, 8, 10}, // 2  Minor
    {0, 2, 3, 5, 7, 9, 10}, // 3  Dorian
    {0, 1, 3, 5, 7, 8, 10}, // 4  Phrygian
    {0, 2, 4, 6, 7, 9, 11}, // 5  Lydian
    {0, 2, 4, 5, 7, 9, 10}, // 6  Mixolydian
    {0, 1, 3, 5, 6, 8, 10}, // 7  Locrian
    {0, 3, 5, 7, 10},       // 8  Pentatonic minor
    {0, 2, 4, 7, 9},        // 9  Pentatonic major
    {0, 2, 3, 5, 7, 8, 11}, // 10 Harmonic minor
    {0, 2, 3, 5, 7, 9, 11}, // 11 Melodic minor
    {0, 2, 4, 6, 8, 10},    // 12 Whole tone
    {0, 1, 3, 4, 6, 7, 9},  // 13 Diminished
    {0, 3, 5, 6, 7, 10},    // 14 Blues
};

static int const scaleNoteCount[numScales] = {
    12, // Chromatic (all notes; the table row is unused)
    7,  // Major
    7,  // Minor
    7,  // Dorian
    7,  // Phrygian
    7,  // Lydian
    7,  // Mixolydian
    7,  // Locrian
    5,  // Pentatonic minor
    5,  // Pentatonic major
    7,  // Harmonic minor
    7,  // Melodic minor
    6,  // Whole tone
    7,  // Diminished
    6,  // Blues
};

// Fill a 12-entry boolean mask with the notes of `scaleIndex` transposed to
// `rootIndex`.  Any previously enabled note not in the scale is cleared.
static inline void BuildScale(int scaleIndex, int rootIndex, bool *note) {
    if (scaleIndex < 0 || scaleIndex >= numScales) {
        return;
    }
    for (int i = 0; i < 12; i++) {
        note[i] = false;
    }
    if (scaleIndex == 0) { // Chromatic — every note on
        for (int i = 0; i < 12; i++) {
            note[i] = true;
        }
        return;
    }
    for (int i = 0; i < scaleNoteCount[scaleIndex]; i++) {
        note[(scaleNotes[scaleIndex][i] + rootIndex) % 12] = true;
    }
}
