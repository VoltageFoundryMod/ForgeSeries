#include <gtest/gtest.h>

#include "sequencer.hpp"

namespace {

GravityChannel MakeChannel(int scale, int root, int spread, int bias = 0) {
    GravityChannel ch;
    ch.SelectScale(scale);
    ch.SelectRoot(root);
    ch.SetSpread(spread);
    ch.SetBias(bias);
    return ch;
}

// Pitch class of a semitone offset from the bottom of the 0-5V range.
int PitchClass(int semitone) { return ((semitone % 12) + 12) % 12; }

} // namespace

// ── Peg → note mapping ───────────────────────────────────────────────────────

TEST(Sequencer, SpreadStretchesTheRingAcrossOctaves) {
    // 8 pegs over 1 octave of C major: the ring covers exactly 12 semitones, so
    // the last peg is an octave above the first.
    GravityChannel one = MakeChannel(1, 0, 1);
    EXPECT_EQ(12, one.SemitoneForPeg(7, 8) - one.SemitoneForPeg(0, 8));

    // The same 8 pegs over 2 octaves cover 24 semitones — same peg count, wider
    // intervals. That is the whole point of SPREAD.
    GravityChannel two = MakeChannel(1, 0, 2);
    EXPECT_EQ(24, two.SemitoneForPeg(7, 8) - two.SemitoneForPeg(0, 8));

    GravityChannel four = MakeChannel(1, 0, 4);
    EXPECT_EQ(48, four.SemitoneForPeg(7, 8) - four.SemitoneForPeg(0, 8));
}

TEST(Sequencer, SpreadRingStartsOnTheRoot) {
    // The span is snapped to whole octaves so the lowest peg is always the root,
    // never some arbitrary degree part-way up the scale.
    for (int spread = CHANNEL_SPREAD_MIN; spread <= CHANNEL_SPREAD_MAX; spread++) {
        GravityChannel ch = MakeChannel(1, 0, spread); // C major, root C
        EXPECT_EQ(0, ch.SemitoneForPeg(0, 8) % 12)
            << "spread " << spread << " did not start on the root";
    }
}

TEST(Sequencer, BiasCrowdsTheNotesWithoutMovingTheEnds) {
    // The lowest and highest peg are fixed by SPREAD; BIAS only decides where the
    // ones in between bunch up. That independence is what makes the two controls
    // usable together.
    GravityChannel even = MakeChannel(1, 0, 2, 0);
    GravityChannel low = MakeChannel(1, 0, 2, -100);
    GravityChannel high = MakeChannel(1, 0, 2, +100);

    EXPECT_EQ(even.SemitoneForPeg(0, 8), low.SemitoneForPeg(0, 8));
    EXPECT_EQ(even.SemitoneForPeg(0, 8), high.SemitoneForPeg(0, 8));
    EXPECT_EQ(even.SemitoneForPeg(7, 8), low.SemitoneForPeg(7, 8));
    EXPECT_EQ(even.SemitoneForPeg(7, 8), high.SemitoneForPeg(7, 8));

    // A middle peg must actually move, and in the right direction.
    EXPECT_LT(low.SemitoneForPeg(4, 8), even.SemitoneForPeg(4, 8))
        << "negative bias should crowd the notes low";
    EXPECT_GT(high.SemitoneForPeg(4, 8), even.SemitoneForPeg(4, 8))
        << "positive bias should crowd the notes high";
}

// The measured note table, kept honest against docs/Design.md. The two bias
// extremes are MIRROR IMAGES of each other by construction: crowding low gives a
// scalewise run then a leap, crowding high gives a leap then a scalewise run.
// Neither may stack pegs onto a duplicate note.
TEST(Sequencer, BiasTableMatchesTheDocumentedNotes) {
    // Semitones above the module's lowest note. With ROOT at octave 0 the ring
    // starts at 0 V, which the default 0V NOTE names C4.
    //                    C4  D4  E4  F4  G4  A4  C5  C6
    const int low[8]  = {0,  2,  4,  5,  7,  9,  12, 24};
    //                    C4  E4  G4  B4  D5  F5  A5  C6
    const int even[8] = {0,  4,  7,  11, 14, 17, 21, 24};
    //                    C4  C5  E5  F5  G5  A5  B5  C6
    const int high[8] = {0,  12, 16, 17, 19, 21, 23, 24};

    GravityChannel chLow = MakeChannel(1, 0, 2, -100);
    GravityChannel chEven = MakeChannel(1, 0, 2, 0);
    GravityChannel chHigh = MakeChannel(1, 0, 2, +100);
    for (int peg = 0; peg < 8; peg++) {
        EXPECT_EQ(low[peg], chLow.SemitoneForPeg(peg, 8)) << "bias -100, peg " << peg;
        EXPECT_EQ(even[peg], chEven.SemitoneForPeg(peg, 8)) << "bias 0, peg " << peg;
        EXPECT_EQ(high[peg], chHigh.SemitoneForPeg(peg, 8)) << "bias +100, peg " << peg;
    }

    // No duplicates at either extreme — stacked pegs read as a fault, not as
    // crowding. This is what the mirrored warp buys over a plain t^gamma.
    for (int peg = 1; peg < 8; peg++) {
        EXPECT_NE(low[peg], low[peg - 1]) << "bias -100 stacked two pegs on one note";
        EXPECT_NE(high[peg], high[peg - 1]) << "bias +100 stacked two pegs on one note";
    }
}

TEST(Sequencer, EvenBiasIsActuallyEven) {
    // With BIAS 0 the pegs are equally spaced in scale degrees: 8 pegs across the
    // 14 degrees of two C-major octaves is one peg every two degrees.
    //
    // The ring starts on ROOT — octave 0 here — and opens upward by SPREAD.
    GravityChannel ch = MakeChannel(1, 0, 2, 0);
    //                       C4  E4  G4  B4  D5  F5  A5  C6
    const int expected[8] = {0,  4,  7,  11, 14, 17, 21, 24};
    for (int peg = 0; peg < 8; peg++) {
        EXPECT_EQ(expected[peg], ch.SemitoneForPeg(peg, 8)) << "peg " << peg;
    }
}

// ── ROOT as the ring's anchor ────────────────────────────────────────────────

TEST(Sequencer, RootOctaveMovesTheWholeRing) {
    // The register the user asked for, not one inferred from SPREAD. Two pegs
    // and one octave used to be parked two octaves up with no way to move it —
    // this is the fix, and it is the same ring transposed exactly.
    GravityChannel low = MakeChannel(1, 0, 1);
    GravityChannel high = MakeChannel(1, 0, 1);
    high.SelectRootOctave(3);

    EXPECT_EQ(0, low.SemitoneForPeg(0, 2));
    EXPECT_EQ(12, low.SemitoneForPeg(1, 2));
    EXPECT_EQ(36, high.SemitoneForPeg(0, 2));
    EXPECT_EQ(48, high.SemitoneForPeg(1, 2));
}

TEST(Sequencer, RootStartsTheRingInEveryScale) {
    // The ring starts ON the root, whatever the scale and wherever the octave —
    // this is what makes ROOT readable as "the lowest note".
    for (int scale = 0; scale < numScales; scale++) {
        for (int root = 0; root < 12; root++) {
            for (int oct = 0; oct <= 3; oct++) {
                GravityChannel ch = MakeChannel(scale, root, 2);
                ch.SelectRootOctave(oct);
                EXPECT_EQ(oct * 12 + root, ch.SemitoneForPeg(0, 8))
                    << "scale " << scale << " root " << root << " octave " << oct;
            }
        }
    }
}

TEST(Sequencer, SpreadCapsTheRootOctaveSoTheRingFits) {
    // Five octaves of range: a 2-octave ring can start no higher than octave 3,
    // and widening it to 5 has to walk the root back down to 0 rather than let
    // the top of the ring flatten against the ceiling.
    GravityChannel ch = MakeChannel(1, 0, 2);
    EXPECT_EQ(QUANT_OCTAVES - 2, ch.MaxRootOctave());

    ch.SelectRootOctave(4);
    EXPECT_EQ(3, ch.GetRootOctave()) << "root octave was not capped by SPREAD";

    ch.SetSpread(5);
    EXPECT_EQ(0, ch.GetRootOctave()) << "widening SPREAD must pull the root down";
    EXPECT_EQ(0, ch.SemitoneForPeg(0, 8));
    EXPECT_EQ(QUANT_MAX_SEMITONE, ch.SemitoneForPeg(7, 8));
}

TEST(Sequencer, RootSemitoneIsOneContinuousControl) {
    // The menu edits ROOT as a single number so one detent is one semitone and
    // the row reads "C4" — pitch class and octave have to stay consistent.
    GravityChannel ch = MakeChannel(1, 0, 2);
    ch.SelectRootSemitone(15); // D#, octave 1
    EXPECT_EQ(3, ch.GetRootIndex());
    EXPECT_EQ(1, ch.GetRootOctave());
    EXPECT_EQ(15, ch.GetRootSemitone());
    EXPECT_EQ(15, ch.SemitoneForPeg(0, 8)) << "the ring must start on the root";

    // Clamped at both ends, and the scale follows the pitch class.
    ch.SelectRootSemitone(-4);
    EXPECT_EQ(0, ch.GetRootSemitone());
    ch.SelectRootSemitone(999);
    EXPECT_LE(ch.GetRootSemitone(), ch.MaxRootSemitone() + 11);
    EXPECT_TRUE(ch.GetActiveNote(ch.GetRootIndex())) << "the root fell outside its own scale";
}

TEST(Sequencer, EveryPegLandsInTheScale) {
    // The whole point of mapping through scale degrees rather than semitones:
    // no peg count, root or scale can produce an out-of-key note.
    for (int scale = 0; scale < numScales; scale++) {
        for (int root = 0; root < 12; root++) {
            GravityChannel ch = MakeChannel(scale, root, 2);
            for (int peg = 0; peg < PHYS_MAX_PEGS; peg++) {
                int st = ch.SemitoneForPeg(peg, PHYS_MAX_PEGS);
                EXPECT_TRUE(ch.GetActiveNote(PitchClass(st)))
                    << "scale " << scale << " root " << root << " peg " << peg
                    << " produced out-of-scale semitone " << st;
            }
        }
    }
}

TEST(Sequencer, PegMappingIsMonotonic) {
    // Adjacent pegs must ascend (or hold at the ceiling) — a peg ring that
    // jumped around would not read as an arpeggio.
    GravityChannel ch = MakeChannel(1, 0, 2);
    int prev = ch.SemitoneForPeg(0, PHYS_MAX_PEGS);
    for (int peg = 1; peg < PHYS_MAX_PEGS; peg++) {
        int st = ch.SemitoneForPeg(peg, PHYS_MAX_PEGS);
        EXPECT_GE(st, prev) << "peg " << peg;
        prev = st;
    }
}

TEST(Sequencer, StaysInRangeAtEveryExtreme) {
    // Widest spread, hardest bias either way, every peg count and every scale
    // must stay inside 0..60 rather than running off the quantizer table.
    for (int scale = 0; scale < numScales; scale++) {
        for (int bias = CHANNEL_BIAS_MIN; bias <= CHANNEL_BIAS_MAX; bias += 50) {
            GravityChannel ch = MakeChannel(scale, 0, CHANNEL_SPREAD_MAX, bias);
            for (int n = PHYS_MIN_PEGS; n <= PHYS_MAX_PEGS; n++) {
                for (int peg = 0; peg < n; peg++) {
                    int st = ch.SemitoneForPeg(peg, n);
                    EXPECT_GE(st, 0) << "scale " << scale << " bias " << bias;
                    EXPECT_LE(st, QUANT_MAX_SEMITONE) << "scale " << scale << " bias " << bias;
                }
            }
        }
    }
}

TEST(Sequencer, SinglePegDoesNotDivideByZero) {
    // pegCount 1 makes the "fraction around the ring" denominator zero.
    GravityChannel ch = MakeChannel(1, 0, 3, 50);
    int st = ch.SemitoneForPeg(0, 1);
    EXPECT_GE(st, 0);
    EXPECT_LE(st, QUANT_MAX_SEMITONE);
}

// ── Integration with the simulation ──────────────────────────────────────────

TEST(Sequencer, EmitsNotesFromPhysicsHits) {
    PhysicsWorld w;
    Clock clk;
    GravityChannel ch = MakeChannel(1, 0, 2);
    w.Get(0).SetPegCount(8);
    w.Get(0).SetBallCount(3);

    int noteChanges = 0;
    for (int i = 0; i < 6000; i++) {
        unsigned long t = 1000UL + (unsigned long)i * 1000UL;
        w.Advance(t);
        ch.Process(w.Get(0), t, clk, false);
        if (ch.ConsumeNoteChanged()) {
            noteChanges++;
        }
    }

    EXPECT_GT(noteChanges, 0) << "physics produced no notes";
    EXPECT_GE(ch.GetSemitone(), 0);
    // The CV jack should agree with the note the channel says it is playing.
    EXPECT_NEAR((float)ch.GetCVOutput(), SemitonesToCounts((float)ch.GetSemitone()), 1.0f);
}

// ── 0V NOTE ──────────────────────────────────────────────────────────────────

namespace {

// Run a container until the channel has emitted a note, so the pitch outputs
// have something to report.
void PlayUntilFirstNote(GravityChannel &ch, PhysicsWorld &w, const Clock &clk) {
    w.Get(0).SetPegCount(8);
    w.Get(0).SetBallCount(3);
    for (int i = 0; i < 6000 && ch.GetSemitone() < 0; i++) {
        unsigned long t = 1000UL + (unsigned long)i * 1000UL;
        w.Advance(t);
        ch.Process(w.Get(0), t, clk, false);
    }
    ASSERT_GE(ch.GetSemitone(), 0) << "no note was ever emitted";
}

} // namespace

TEST(Sequencer, ZeroReferenceDefaultsToC4AndIsClamped) {
    GravityChannel ch;
    EXPECT_EQ(GEN_CV_ZERO_OCTAVE_DEFAULT, ch.GetCvZeroOctave());

    ch.SetCvZeroOctave(GEN_CV_ZERO_OCTAVE_MAX + 3);
    EXPECT_EQ(GEN_CV_ZERO_OCTAVE_MAX, ch.GetCvZeroOctave());
    ch.SetCvZeroOctave(GEN_CV_ZERO_OCTAVE_MIN - 3);
    EXPECT_EQ(GEN_CV_ZERO_OCTAVE_MIN, ch.GetCvZeroOctave());
}

TEST(Sequencer, ZeroReferenceNamesTheVoltageItDoesNotMoveIt) {
    // The reference says which C the patch calls 0 V. The jack is 0–5 V whatever
    // it is set to — if the reference moved the output as well, it would fight
    // SPREAD for control of the pitch.
    PhysicsWorld w;
    Clock clk;
    GravityChannel ch = MakeChannel(1, 0, 2);
    PlayUntilFirstNote(ch, w, clk);

    const uint16_t counts = ch.GetCVOutput();
    const int semitone = ch.GetSemitone();

    for (int ref = GEN_CV_ZERO_OCTAVE_MIN; ref <= GEN_CV_ZERO_OCTAVE_MAX; ref++) {
        ch.SetCvZeroOctave(ref);
        EXPECT_EQ(counts, ch.GetCVOutput()) << "0V NOTE C" << ref << " moved the jack";
        EXPECT_EQ(semitone, ch.GetSemitone());

        // The one thing it does move is the name, by one octave number per
        // octave of reference.
        EXPECT_EQ(ref + semitone / 12, ch.GetOctaveOut());
    }
}

TEST(Sequencer, TheNoteOnScreenIsTheVoltageOnTheJack) {
    // The invariant the setting exists for: volts == octave - 0V NOTE (plus the
    // pitch class). Tell the module what its VCO calls 0 V and the note on the
    // screen is the note that VCO plays.
    PhysicsWorld w;
    Clock clk;
    GravityChannel ch = MakeChannel(1, 0, 3);
    PlayUntilFirstNote(ch, w, clk);

    for (int ref = GEN_CV_ZERO_OCTAVE_MIN; ref <= GEN_CV_ZERO_OCTAVE_MAX; ref++) {
        ch.SetCvZeroOctave(ref);
        float named = (float)(ch.GetOctaveOut() - ref) + (float)ch.GetNoteIndex() / 12.0f;
        float jack = (float)ch.GetCVOutput() / QUANT_COUNTS_PER_SEMITONE / 12.0f;
        EXPECT_NEAR(named, jack, 0.02f) << "0V NOTE C" << ref;
    }
}

TEST(Sequencer, TheNoteSpanFillsTheOutputRange) {
    // The widest spread uses every octave the jack has: the lowest note sits at
    // 0 V — the reference note itself — and the highest at 5 V, five octaves up.
    // Anything else would leave part of the range unreachable.
    GravityChannel ch = MakeChannel(1, 0, CHANNEL_SPREAD_MAX);
    ch.SetCvZeroOctave(4);

    EXPECT_EQ(0, ch.SemitoneForPeg(0, 8)) << "the widest spread must start at 0 V";
    EXPECT_EQ(QUANT_MAX_SEMITONE, ch.SemitoneForPeg(7, 8));
    EXPECT_EQ(QUANT_OCTAVES, QUANT_MAX_SEMITONE / 12);
}

TEST(Sequencer, QuantizeHoldsNotesUntilTheBoundary) {
    PhysicsWorld w;
    Clock clk;
    clk.SetQuantize(Q16);
    GravityChannel ch = MakeChannel(1, 0, 2);
    w.Get(0).SetBallCount(3);

    // Never signal a boundary: nothing may reach the output, however many times
    // the balls strike a peg.
    for (int i = 0; i < 6000; i++) {
        unsigned long t = 1000UL + (unsigned long)i * 1000UL;
        w.Advance(t);
        ch.Process(w.Get(0), t, clk, false);
    }
    EXPECT_EQ(-1, ch.GetSemitone()) << "a deferred note escaped without a boundary";
    EXPECT_TRUE(ch.HasPending()) << "the hit should be queued, not dropped";

    // Now release it.
    ch.Process(w.Get(0), 7000000UL, clk, true);
    EXPECT_GE(ch.GetSemitone(), 0);
    EXPECT_FALSE(ch.HasPending());
}

TEST(Sequencer, QuantizeIsLastWinsNotAQueue) {
    // Several hits arriving before the boundary must collapse to one note, not
    // stack into a burst of retriggers at the boundary.
    PhysicsWorld w;
    Clock clk;
    clk.SetQuantize(Q4);
    GravityChannel ch = MakeChannel(1, 0, 2);
    w.Get(0).SetBallCount(PHYS_MAX_BALLS);

    for (int i = 0; i < 6000; i++) {
        unsigned long t = 1000UL + (unsigned long)i * 1000UL;
        w.Advance(t);
        ch.Process(w.Get(0), t, clk, false);
    }
    ASSERT_TRUE(ch.HasPending());

    ch.Process(w.Get(0), 7000000UL, clk, true);
    EXPECT_FALSE(ch.HasPending()) << "exactly one pending hit is held, never a backlog";
}

// ── Gate ─────────────────────────────────────────────────────────────────────

TEST(Sequencer, GateLevelSurvivesAccentModulation) {
    // ACCENT rewrites the envelope's level on every hit; the menu must still
    // read back what the user set.
    GravityChannel ch = MakeChannel(1, 0, 2);
    ch.SetGateLevel(80);
    ch.SetAccent(100);

    PhysicsWorld w;
    Clock clk;
    w.Get(0).SetBallCount(4);
    for (int i = 0; i < 4000; i++) {
        unsigned long t = 1000UL + (unsigned long)i * 1000UL;
        w.Advance(t);
        ch.Process(w.Get(0), t, clk, false);
    }
    EXPECT_EQ(80, ch.GetGateLevel());
    EXPECT_EQ(100, ch.GetAccent());
}

TEST(Sequencer, ScaleChangeKeepsTheNoteMaskInSync) {
    GravityChannel ch = MakeChannel(1, 0, 2); // C major
    EXPECT_TRUE(ch.GetActiveNote(0));         // C
    EXPECT_FALSE(ch.GetActiveNote(1));        // C#

    ch.SelectScale(0); // chromatic
    for (int n = 0; n < 12; n++) {
        EXPECT_TRUE(ch.GetActiveNote(n)) << "note " << n;
    }
}

// ── Gate articulation ────────────────────────────────────────────────────────
// A gate that never falls back to zero is a sawtooth, not a gate — nothing
// downstream can trigger from it and no note ever articulates.
//
// Regression guard for the physics chatter that made this fail: a ball settling
// against a *rotating* wall registered a contact every step, and because
// rotation kept sliding a new peg underneath it, the short re-trigger window let
// it fire ~33 times a second. With a 320 ms decay the envelope was reset at ~90 %
// every time and the jack sat between 0.5 V and 5 V forever.
TEST(Sequencer, GateReturnsToZeroBetweenHits) {
    PhysicsWorld w;
    Clock clk;
    GravityChannel ch = MakeChannel(1, 0, 2);
    ch.envelope.SetMode(GateEnvelope);
    ch.envelope.SetAttack(0);
    ch.envelope.SetDecay(100); // the factory default for container A
    w.Get(0).SetBallCount(3);
    w.Get(0).SetPegCount(8);
    w.Get(0).SetOmega(0.8f); // rotating — the case that produced the chatter

    int idle = 0, total = 0;
    for (int i = 0; i < 12000; i++) {
        unsigned long t = 1000UL + (unsigned long)i * 1000UL;
        w.Advance(t);
        ch.Process(w.Get(0), t, clk, false);
        if (i > 2000) { // let the container reach its steady state first
            total++;
            if (ch.GetGateOutput() < 200) { // < ~0.25 V counts as "off"
                idle++;
            }
        }
    }
    float idleFraction = (float)idle / (float)total;
    EXPECT_GT(idleFraction, 0.15f)
        << "fraction of time the GATE jack is actually low";
}


// ── ACCENT ───────────────────────────────────────────────────────────────────

TEST(Sequencer, AccentSpansTheRealImpactRange) {
    // ACCENT maps the window hits actually occupy (~30..150, measured), so a soft
    // hit is quiet and a hard one reaches full level. The first version
    // normalised against a fixed 260 while real impacts never exceed ~170, which
    // capped the average note at a third of its level and made a hard strike
    // unreachable.
    GravityChannel ch = MakeChannel(1, 0, 2);
    ch.envelope.SetMode(GateEnvelope);
    ch.SetGateLevel(100);
    ch.SetAccent(100);

    EXPECT_EQ(0, ch.AccentedLevel(30.0f)) << "the softest hit should be silent";
    EXPECT_NEAR(50, ch.AccentedLevel(90.0f), 3) << "an average hit sits mid-scale";
    EXPECT_EQ(100, ch.AccentedLevel(150.0f)) << "a hard hit must reach full level";
    EXPECT_EQ(100, ch.AccentedLevel(400.0f)) << "beyond the window must clamp, not overshoot";

    // Half accent keeps the top half of the range: dynamics without disappearing.
    ch.SetAccent(50);
    EXPECT_NEAR(50, ch.AccentedLevel(30.0f), 3);
    EXPECT_EQ(100, ch.AccentedLevel(150.0f));

    // Accent off means every hit plays at the set level.
    ch.SetAccent(0);
    EXPECT_EQ(100, ch.AccentedLevel(30.0f));
    EXPECT_EQ(100, ch.AccentedLevel(150.0f));
}

TEST(Sequencer, AccentNeverShrinksATriggerOrGate) {
    // A trigger has to keep a fixed height whatever the impact, or downstream
    // modules stop firing. Same for a plain gate.
    GravityChannel ch = MakeChannel(1, 0, 2);
    ch.SetGateLevel(100);
    ch.SetAccent(100);

    ch.envelope.SetMode(GateTrigger);
    EXPECT_EQ(100, ch.AccentedLevel(30.0f)) << "ACCENT shrank a TRIG pulse";

    ch.envelope.SetMode(GateGate);
    EXPECT_EQ(100, ch.AccentedLevel(30.0f)) << "ACCENT shrank a GATE";

    // ...but it still applies to the shaped output, which is where dynamics live.
    ch.envelope.SetMode(GateEnvelope);
    EXPECT_LT(ch.AccentedLevel(30.0f), 100);
}

// ── Loop nap muting ──────────────────────────────────────────────────────────

namespace {

// Drive a channel until its container produces a hit, then report the gate.
uint16_t GateAfterHits(GravityChannel &ch, Container &c, int ms) {
    Clock clk;
    Contact contacts[PHYS_MAX_BALLS];
    uint16_t peak = 0;
    for (int i = 0; i < ms; i++) {
        unsigned long now = 1000UL + (unsigned long)i * 1000UL;
        int count = 0;
        c.Step(now, contacts, count, PHYS_MAX_BALLS);
        ch.Process(c, now, clk, false);
        if (ch.GetGateOutput() > peak) {
            peak = ch.GetGateOutput();
        }
    }
    return peak;
}

} // namespace

TEST(Sequencer, NappingChannelEmitsNoGate) {
    GravityChannel ch;
    Container c;
    c.SetBallCount(4);
    ASSERT_GT(GateAfterHits(ch, c, 2000), 0) << "the channel never fired at all";

    GravityChannel muted;
    Container c2;
    c2.SetBallCount(4);
    muted.SetMuted(true);
    EXPECT_EQ(0, GateAfterHits(muted, c2, 2000)) << "a napping channel must be silent";
}

TEST(Sequencer, NappingChannelHoldsItsPitchAndDoesNotBurstOnWake) {
    // Two things the nap must not do: drop the CV to zero (a rest is not a note
    // change), and stockpile hits that all fire the moment it wakes up.
    GravityChannel ch;
    Container c;
    Clock clk;
    Contact contacts[PHYS_MAX_BALLS];
    c.SetBallCount(4);

    // Play until a note has sounded.
    int i = 0;
    for (; i < 3000 && ch.GetSemitone() < 0; i++) {
        unsigned long now = 1000UL + (unsigned long)i * 1000UL;
        int count = 0;
        c.Step(now, contacts, count, PHYS_MAX_BALLS);
        ch.Process(c, now, clk, false);
    }
    ASSERT_GE(ch.GetSemitone(), 0) << "no note was ever emitted";
    uint16_t heldCv = ch.GetCVOutput();

    // Nap through a stretch that would otherwise have produced several notes.
    ch.SetMuted(true);
    for (int n = 0; n < 1000; n++, i++) {
        unsigned long now = 1000UL + (unsigned long)i * 1000UL;
        int count = 0;
        c.Step(now, contacts, count, PHYS_MAX_BALLS);
        ch.Process(c, now, clk, false);
        ASSERT_EQ(0, ch.GetGateOutput());
    }
    EXPECT_EQ(heldCv, ch.GetCVOutput()) << "the pitch output moved during a nap";

    // The first step awake must not release a queued hit.
    ch.SetMuted(false);
    EXPECT_FALSE(ch.HasPending()) << "a hit was stockpiled during the nap";
}
