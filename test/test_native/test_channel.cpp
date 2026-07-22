#include <gtest/gtest.h>

#include "channel.hpp"

// QuantizerChannel is driven entirely by a caller-supplied microsecond clock, so
// these tests step time explicitly. They model the hardware loop: the main loop
// is bounded by DACWriteAll pushing 13 bytes over 400 kHz I2C, so an iteration
// takes roughly 330 us.
static const unsigned long LOOP_US = 330;

namespace {

float voltsToCounts(float v) { return v / 5.0f * 4095.0f; }

// Feed a channel a constant input for `iterations` loops, advancing the clock.
void hold(QuantizerChannel &ch, float volts, unsigned long &t, int iterations) {
    for (int i = 0; i < iterations; i++) {
        ch.Process(voltsToCounts(volts), t, false, false);
        t += LOOP_US;
    }
}

} // namespace

// ── Settle window ───────────────────────────────────────────────────────────

TEST(Channel, SettleSuppressesNotesCrossedDuringAJump) {
    QuantizerChannel ch; // chromatic by default
    ch.SetSettle(5);     // ms
    unsigned long t = 0;
    hold(ch, 0.0f, t, 50);
    ASSERT_EQ(0, ch.GetQuantizedSemitone());

    // Walk the input up one octave over four loops, the way the input smoother
    // converges after a step. Every intermediate value is held for one loop
    // (330 us), far below the 5 ms window, so none of them may be played.
    const float ramp[4] = {0.80f, 0.96f, 0.99f, 1.00f};
    int changes = 0;
    int last = ch.GetQuantizedSemitone();
    for (int i = 0; i < 4; i++) {
        ch.Process(voltsToCounts(ramp[i]), t, false, false);
        if (ch.GetQuantizedSemitone() != last) {
            last = ch.GetQuantizedSemitone();
            changes++;
        }
        t += LOOP_US;
    }
    EXPECT_EQ(0, changes) << "a note was played during the transient";

    // Once the input has been stable for the settle window, the target lands.
    hold(ch, 1.00f, t, 50);
    EXPECT_EQ(12, ch.GetQuantizedSemitone());
}

TEST(Channel, SettleZeroFollowsImmediately) {
    QuantizerChannel ch;
    ch.SetSettle(0);
    unsigned long t = 0;
    hold(ch, 0.0f, t, 50);

    ch.Process(voltsToCounts(1.0f), t, false, false);
    EXPECT_EQ(12, ch.GetQuantizedSemitone());
}

TEST(Channel, SettleStillPlaysAGenuinelySlowSweep) {
    // A note held for longer than the settle window is a real note, not a
    // transient, and must be played. This is what separates the de-glitcher from
    // a plain slew limiter.
    QuantizerChannel ch;
    ch.SetSettle(5);
    unsigned long t = 0;
    hold(ch, 0.0f, t, 50);

    int changes = 0;
    int last = ch.GetQuantizedSemitone();
    for (int semitone = 1; semitone <= 12; semitone++) {
        hold(ch, semitone / 12.0f, t, 60); // ~20 ms per note, well past the window
        if (ch.GetQuantizedSemitone() != last) {
            last = ch.GetQuantizedSemitone();
            changes++;
        }
    }
    EXPECT_EQ(12, changes);
}

TEST(Channel, SettleDoesNotDelayAScaleChange) {
    // Editing the scale must take effect at once: the held note has left the
    // scale, so waiting out the window would leave a wrong note sounding.
    QuantizerChannel ch;
    ch.SetSettle(50); // deliberately long
    unsigned long t = 0;
    hold(ch, 1.0f / 12.0f, t, 50); // C# (semitone 1)
    ASSERT_EQ(1, ch.GetQuantizedSemitone());

    bool cMajor[12] = {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1}; // no C#
    ch.SetActiveNotes(cMajor);
    ch.Process(voltsToCounts(1.0f / 12.0f), t, false, false);
    EXPECT_NE(1, ch.GetQuantizedSemitone()) << "output stuck on a note not in the scale";
}

TEST(Channel, FirstNoteAfterPowerOnIsNotDelayed) {
    QuantizerChannel ch;
    ch.SetSettle(50);
    ch.Process(voltsToCounts(1.0f), 0, false, false);
    EXPECT_EQ(12, ch.GetQuantizedSemitone());
}

TEST(Channel, SettleIsClamped) {
    QuantizerChannel ch;
    ch.SetSettle(-5);
    EXPECT_EQ(0, ch.GetSettle());
    ch.SetSettle(9999);
    EXPECT_EQ(CHANNEL_SETTLE_MAX_MS, ch.GetSettle());
}

// ── Octave shift ────────────────────────────────────────────────────────────

TEST(Channel, OctaveShiftTransposesWholeOctaves) {
    QuantizerChannel ch;
    ch.SetSettle(0);
    unsigned long t = 0;
    hold(ch, 1.0f, t, 20); // semitone 12
    EXPECT_EQ(0, ch.GetNoteIndex());
    EXPECT_EQ(1, ch.GetOctaveOut());

    ch.SetOctave(1);
    hold(ch, 1.0f, t, 5);
    EXPECT_EQ(0, ch.GetNoteIndex()); // still C
    EXPECT_EQ(2, ch.GetOctaveOut());
}

TEST(Channel, OctaveShiftFoldsInsteadOfClampingAtTheTop) {
    // Semitone 59 (B, top octave) shifted up 3 octaves lands at 95, past the
    // 0..60 output range. Clamping would emit semitone 60 — a C, which is not
    // the note the quantizer chose and may not even be in the scale. Folding by
    // whole octaves keeps the pitch class.
    QuantizerChannel ch;
    ch.SetSettle(0);
    unsigned long t = 0;
    hold(ch, 59.0f / 12.0f, t, 20);
    ASSERT_EQ(59, ch.GetQuantizedSemitone());
    ASSERT_EQ(11, ch.GetNoteIndex()); // B

    ch.SetOctave(3);
    hold(ch, 59.0f / 12.0f, t, 5);
    EXPECT_EQ(11, ch.GetNoteIndex()) << "octave shift changed the pitch class";
    EXPECT_LE(ch.GetCVOutput(), 4095);
}

TEST(Channel, OctaveShiftFoldsInsteadOfClampingAtTheBottom) {
    QuantizerChannel ch;
    ch.SetSettle(0);
    unsigned long t = 0;
    hold(ch, 1.0f / 12.0f, t, 20); // semitone 1, C#
    ASSERT_EQ(1, ch.GetNoteIndex());

    ch.SetOctave(-3);
    hold(ch, 1.0f / 12.0f, t, 5);
    EXPECT_EQ(1, ch.GetNoteIndex()) << "octave shift changed the pitch class";
}

// ── Gate / sync ─────────────────────────────────────────────────────────────

TEST(Channel, NoteSyncFiresTheEnvelopeOnNoteChangeOnly) {
    QuantizerChannel ch;
    ch.SetSettle(0);
    ch.SetSyncMode(SyncNote);
    ch.envelope.SetAttack(0);
    ch.envelope.SetDecay(1000);
    unsigned long t = 0;
    hold(ch, 0.0f, t, 50);

    // Note change -> gate fires.
    ch.Process(voltsToCounts(1.0f), t, false, false);
    EXPECT_GT(ch.GetGateOutput(), 0);

    // A trigger with no note change does nothing in NOTE sync.
    QuantizerChannel ch2;
    ch2.SetSettle(0);
    ch2.SetSyncMode(SyncNote);
    ch2.envelope.SetAttack(0);
    ch2.envelope.SetDecay(1);
    unsigned long t2 = 0;
    hold(ch2, 0.0f, t2, 50);
    ch2.Process(voltsToCounts(0.0f), t2, true, true);
    EXPECT_EQ(0, ch2.GetGateOutput());
}

TEST(Channel, TrigSyncFiresOnTheTriggerEdgeOnly) {
    QuantizerChannel ch;
    ch.SetSettle(0);
    ch.SetSyncMode(SyncTrig);
    ch.envelope.SetAttack(0);
    ch.envelope.SetDecay(1000);
    unsigned long t = 0;
    hold(ch, 0.0f, t, 50);

    // Note change alone must not fire in TRIG sync.
    ch.Process(voltsToCounts(1.0f), t, false, false);
    EXPECT_EQ(0, ch.GetGateOutput());

    t += LOOP_US;
    ch.Process(voltsToCounts(1.0f), t, true, true);
    EXPECT_GT(ch.GetGateOutput(), 0);
}

// ── Glide ───────────────────────────────────────────────────────────────────

TEST(Channel, GlideRampsTowardTheNewNote) {
    QuantizerChannel ch;
    ch.SetSettle(0);
    ch.SetGlide(50); // 250 ms time constant
    unsigned long t = 0;
    hold(ch, 0.0f, t, 50);
    ASSERT_EQ(0, ch.GetCVOutput());

    ch.Process(voltsToCounts(1.0f), t, false, false);
    uint16_t firstStep = ch.GetCVOutput();
    EXPECT_GT(firstStep, 0);
    EXPECT_LT(firstStep, 819) << "glide jumped straight to the target";

    hold(ch, 1.0f, t, 5000); // plenty of time to arrive
    EXPECT_NEAR(819, ch.GetCVOutput(), 2);
}

TEST(Channel, NoGlideJumpsImmediately) {
    QuantizerChannel ch;
    ch.SetSettle(0);
    ch.SetGlide(0);
    unsigned long t = 0;
    hold(ch, 0.0f, t, 50);
    ch.Process(voltsToCounts(1.0f), t, false, false);
    EXPECT_NEAR(819, ch.GetCVOutput(), 2);
}

// ── Sample & hold ───────────────────────────────────────────────────────────

TEST(Channel, SampleHoldIgnoresTheInputBetweenTriggers) {
    QuantizerChannel ch;
    ch.SetPitchMode(PitchSampleHold);
    ch.SetSettle(0);
    unsigned long t = 0;

    // First reading latches immediately so the module is not silent at power-on.
    hold(ch, 0.0f, t, 20);
    ASSERT_EQ(0, ch.GetQuantizedSemitone());

    // Sweep the input a whole octave with no trigger: nothing may reach the jack.
    for (int i = 1; i <= 12; i++) {
        hold(ch, i / 12.0f, t, 60);
        EXPECT_EQ(0, ch.GetQuantizedSemitone()) << "output moved without a trigger";
    }

    // The trigger latches whatever the input is at that moment.
    ch.Process(voltsToCounts(1.0f), t, true, true);
    EXPECT_EQ(12, ch.GetQuantizedSemitone());
}

TEST(Channel, SampleHoldUsesSettleAsTheSampleDelay) {
    // A sequencer emits pitch and gate together, so at the edge the pitch CV may
    // still be in transit. SETTLE delays the sample past that.
    QuantizerChannel ch;
    ch.SetPitchMode(PitchSampleHold);
    ch.SetSettle(5); // ms
    unsigned long t = 0;
    hold(ch, 0.0f, t, 20);
    ASSERT_EQ(0, ch.GetQuantizedSemitone());

    // Trigger arrives while the input is still at the old value.
    ch.Process(voltsToCounts(0.0f), t, true, false);
    EXPECT_EQ(0, ch.GetQuantizedSemitone()) << "sampled before the delay elapsed";
    t += LOOP_US;

    // The input settles on its new value during the delay window.
    hold(ch, 1.0f, t, 3); // ~1 ms, still inside the 5 ms window
    EXPECT_EQ(0, ch.GetQuantizedSemitone());

    hold(ch, 1.0f, t, 20); // past the window
    EXPECT_EQ(12, ch.GetQuantizedSemitone()) << "never sampled after the delay";
}

TEST(Channel, SampleHoldReSnapsAHeldNoteWhenTheScaleChanges) {
    QuantizerChannel ch;
    ch.SetPitchMode(PitchSampleHold);
    ch.SetSettle(0);
    unsigned long t = 0;
    hold(ch, 1.0f / 12.0f, t, 20); // latch C# (semitone 1)
    ASSERT_EQ(1, ch.GetQuantizedSemitone());

    bool cMajor[12] = {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1}; // no C#
    ch.SetActiveNotes(cMajor);
    // Hold the input somewhere else entirely: the output must re-snap the *held*
    // note into the new scale, not jump to whatever the input is now doing.
    ch.Process(voltsToCounts(3.0f), t, false, false);
    EXPECT_NE(1, ch.GetQuantizedSemitone());
    EXPECT_LE(ch.GetQuantizedSemitone(), 2) << "output followed the input without a trigger";
}

// ── Transposition ───────────────────────────────────────────────────────────

TEST(Channel, TransposeStepsInScaleDegrees) {
    QuantizerChannel ch;
    ch.SetSettle(0);
    bool cMajor[12] = {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1};
    ch.SetActiveNotes(cMajor);
    ch.SetTransposeEnabled(true);
    unsigned long t = 0;

    hold(ch, 0.0f, t, 20);
    ASSERT_EQ(0, ch.GetNoteIndex()); // C

    // +2 degrees in C major is a third: C -> E, not C -> D.
    ch.SetTransposeDegrees(2);
    hold(ch, 0.0f, t, 5);
    EXPECT_EQ(4, ch.GetNoteIndex()); // E

    // +7 degrees is a full octave in a 7-note scale.
    ch.SetTransposeDegrees(7);
    hold(ch, 0.0f, t, 5);
    EXPECT_EQ(0, ch.GetNoteIndex());
    EXPECT_EQ(1, ch.GetOctaveOut());
}

TEST(Channel, TransposeFollowsTheScaleNotSemitones) {
    // The same +2 degrees is a different interval in a pentatonic scale — that
    // is the point of transposing by degree rather than by semitone.
    QuantizerChannel ch;
    ch.SetSettle(0);
    bool pentMinor[12] = {1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0}; // C Eb F G Bb
    ch.SetActiveNotes(pentMinor);
    ch.SetTransposeEnabled(true);
    unsigned long t = 0;
    hold(ch, 0.0f, t, 20);
    ASSERT_EQ(0, ch.GetNoteIndex());

    ch.SetTransposeDegrees(2);
    hold(ch, 0.0f, t, 5);
    EXPECT_EQ(5, ch.GetNoteIndex()); // F, a fourth up
}

TEST(Channel, TransposeIsIgnoredWhenDisabled) {
    QuantizerChannel ch;
    ch.SetSettle(0);
    ch.SetTransposeEnabled(false);
    unsigned long t = 0;
    hold(ch, 0.0f, t, 20);

    ch.SetTransposeDegrees(5);
    hold(ch, 0.0f, t, 5);
    EXPECT_EQ(0, ch.GetNoteIndex());
    EXPECT_EQ(0, ch.GetTransposeDegrees());
}

TEST(Channel, TransposeStaysInScale) {
    QuantizerChannel ch;
    ch.SetSettle(0);
    bool cMajor[12] = {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1};
    ch.SetActiveNotes(cMajor);
    ch.SetTransposeEnabled(true);
    unsigned long t = 0;
    hold(ch, 0.0f, t, 20);

    for (int d = -12; d <= 12; d++) {
        ch.SetTransposeDegrees(d);
        hold(ch, 0.0f, t, 3);
        int n = ch.GetNoteIndex();
        EXPECT_TRUE(cMajor[n]) << "degree " << d << " produced out-of-scale note " << n;
    }
}

TEST(Channel, TransposeClampsAtTheEndsOfTheRange) {
    // Clamping rather than wrapping: a wrap would jump several octaves for a
    // one-degree change in the CV.
    QuantizerChannel ch;
    ch.SetSettle(0);
    ch.SetTransposeEnabled(true);
    unsigned long t = 0;
    hold(ch, 0.0f, t, 20);

    ch.SetTransposeDegrees(-12); // already at the bottom
    hold(ch, 0.0f, t, 3);
    EXPECT_EQ(0, ch.GetSoundingSemitone());

    hold(ch, 5.0f, t, 20); // top of the range
    ch.SetTransposeDegrees(12);
    hold(ch, 5.0f, t, 3);
    EXPECT_LE(ch.GetSoundingSemitone(), QUANT_MAX_SEMITONE);
}
