// Add presets for common scales

// Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Pentatonic Minor, Harmonic Minor, Melodic Minor, Whole Tone, Diminished, Chromatic
char const *scaleNames[] = {"Chrom", "Maj", "Min", "Dor", "Phr", "Lyd", "Mix", "Loc", "PenMin", "HarMin", "MelMin", "Whol", "Dim"};
char const *noteNames[] = {"C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B"};
int const numScales = sizeof(scaleNames) / sizeof(scaleNames[0]);

// ScaleNotes contains the note indexes for each scale
// Eg. 0 for root, 2 for major second, 4 for major third, 5 for perfect fourth, 7 for perfect fifth, 9 for major sixth, 11 for major seventh
// C  C# D  D# E F F# G G# A A# B
// 0  1  2  3  4 5 6  7 8  9 10 11

int scaleNotes[numScales][7] = {
    {},                      // Chromatic (This does not matter for this array but we need the index 0)
    {0, 2, 4, 5, 7, 9, 11},  // Major
    {0, 2, 3, 5, 7, 8, 10},  // Minor
    {0, 2, 3, 5, 7, 9, 10},  // Dorian
    {0, 1, 3, 5, 7, 8, 10},  // Phrygian
    {0, 2, 4, 6, 7, 9, 11},  // Lydian
    {0, 2, 4, 5, 7, 9, 10},  // Mixolydian
    {0, 1, 3, 5, 6, 8, 10},  // Locrian
    {0, 3, 5, 7, 10},        // Pentatonic minor
    {0, 2, 3, 5, 7, 8, 11},  // Harmonic Minor
    {0, 2, 3, 5, 7, 9, 11},  // Melodic Minor
    {0, 2, 4, 6, 8, 10},     // Whole Tone
    {0, 1, 3, 4, 6, 7, 9}    // Diminished
};

// Build the scale for each note in the scale bases on scaleNotes and each note in the scale
// Receives the scale index and the note index and fills a boolean array with the notes
void BuildScale(int scaleIndex, int noteIndex, bool *note) {
    // Chromatic scale
    if (scaleIndex == 0) {
        // Fill all notes
        for (int i = 0; i < 12; i++) {
            note[i] = 1;
        }
    } else {
        for (int i = 0; i < 12; i++) {
            note[i] = 0;  // Clear all notes
        }
        for (int i = 0; i < 7; i++) {
            int transposedNote = (scaleNotes[scaleIndex][i] + noteIndex) % 12;
            note[transposedNote] = 1;
        }
    }
}
