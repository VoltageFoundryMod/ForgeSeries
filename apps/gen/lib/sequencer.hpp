#pragma once

// sequencer.hpp — GravityChannel: turns one container's peg hits into a voice.
//
// The physics (physics.hpp) knows nothing about music; it reports "ball struck
// peg 5, this hard". This layer owns everything musical about a channel:
//
//   peg index  →  scale degree  →  semitone  →  CV counts
//   peg hit    →  envelope trigger  →  GATE jack
//
// Mapping through *scale degrees* rather than semitones is deliberate and is the
// same reasoning as NoteForge's transposition: stepping along the quantizer's
// enabled-note table means every peg is in the scale, and the interval between
// adjacent pegs follows the scale — a third apart in major, something else in
// pentatonic. Adding a peg can never produce an out-of-key note.

#ifdef UNIT_TEST
#include "ArduinoFake.h"
#else
#include <Arduino.h>
#endif

#include "clock.hpp"
#include "envelope.hpp"
#include "physics.hpp"
#include "quantizer.hpp"
#include "scales.hpp"

// Two independent voices. NoteForge calls them quantizer channels and
// GravityForge calls them containers, but the jack layout is identical, and
// deliberately so: patch cables carry over between the two firmwares.
#define NUM_CHANNELS 2

// SPREAD: how many octaves the peg ring covers. The span is centred in the
// module's five-octave output range and snapped to whole octaves, so the lowest
// peg always lands on the root rather than an arbitrary degree.
#define CHANNEL_SPREAD_MIN 1
#define CHANNEL_SPREAD_MAX 5

// ── 0V NOTE: what the module's notes are called ──────────────────────────────
// The outputs are the MCP4728's 0–5 V, always: five octaves at 1 V/oct, and
// nothing here can move a voltage — least of all downwards, since there is no
// negative rail to move onto. What 0V NOTE sets is the *name*: which note the
// module's 0 V stands for, which is a fact about the VCO on the other end of the
// cable and not about this module. Everything else follows:
//
//     volts(note) == octave(note) - 0V NOTE
//
// so the range runs C<n> at 0 V up to C<n+5> at 5 V, and the note on the home
// screen is the note the VCO plays. The default is C4, VCV Rack's convention and
// the common one on hardware oscillators; set it to whatever the oscillator you
// are patched into calls 0 V. Override the default from the build with
// -DGEN_CV_ZERO_OCTAVE_DEFAULT=n.
//
// It used to be fixed at C0, which is why a note the screen called C2 came out
// of a Rack VCO as C6.
//
// Where the peg span sits INSIDE that range is SPREAD's business, and it stays
// centred (see SemitoneForPeg): the two controls are deliberately independent,
// so renaming the range can never move a pitch.
#ifndef GEN_CV_ZERO_OCTAVE_DEFAULT
#define GEN_CV_ZERO_OCTAVE_DEFAULT 4
#endif

// C0..C5. Past C5 the top of the five-octave span runs off the end of the octave
// numbering oscillators actually print on their panels.
#define GEN_CV_ZERO_OCTAVE_MIN 0
#define GEN_CV_ZERO_OCTAVE_MAX 5

// BIAS: where inside that span the pegs crowd. 0 spaces them evenly; negative
// bunches them into the low notes, positive into the high ones. The lowest and
// highest peg never move, so SPREAD keeps meaning what it says.
#define CHANNEL_BIAS_MIN (-100)
#define CHANNEL_BIAS_MAX 100

// The impact-speed window ACCENT maps onto, measured from the simulation rather
// than guessed: real hits arrive between ~29 (the softest that clears
// PEG_MIN_IMPACT_SPEED at all) and ~170, averaging ~85, and that barely moves
// with gravity — a heavier setting produces MORE hits, not much harder ones.
//
// The first version normalised against a fixed 260, which meant the average hit
// only reached a third of full level and even the hardest strike could never get
// there. Mapping the window that hits actually occupy is what gives ACCENT its
// full range.
#define CHANNEL_ACCENT_SOFT_SPEED 30.0f
#define CHANNEL_ACCENT_HARD_SPEED 150.0f

class GravityChannel {
    // ── Musical parameters ──
    int _scaleIndex = 1; // Major
    int _rootIndex = 0;  // C — pitch class, also what the scale is built on
    int _rootOctave = 0; // which octave of the output range the ring starts in
    int _spread = 2;     // octaves the peg ring covers
    int _bias = 0;       // -100 = crowd low, 0 = even, +100 = crowd high
    bool _notes[12] = {false};
    Quantizer _quantizer;

    // The note the module's 0 V stands for, and so the octave number of its
    // lowest note. Naming only — see the 0V NOTE block above.
    int _cvZeroOctave = GEN_CV_ZERO_OCTAVE_DEFAULT;

    // Base gate level, kept here rather than read back from the envelope because
    // ACCENT rewrites the envelope's level on every hit — the menu must show
    // what the user set, not the momentarily modulated value.
    int _baseLevel = 100;
    int _accent = 0; // 0..100 %, how much hit energy scales the gate level

    // ── Runtime ──
    int _semitone = -1; // currently sounding, -1 = nothing yet
    uint16_t _cvCounts = 0;
    uint16_t _gateValue = 0;
    bool _noteChanged = false;

    // Deferred hit, used only when the clock's quantize grid is on.
    // Last-wins: a newer hit replaces a still-pending one rather than stacking,
    // so a dense passage releases as one note on the boundary instead of a burst
    // of retriggers that sounds nothing like the physics that produced it.
    int _pendingPeg = -1;
    int _pendingPegCount = 0;
    float _pendingEnergy = 0.0f;

    // Asleep for this loop (LOOP ▸ NAP). The container keeps bouncing — it has
    // to, or the phrase would fall out of phase and come back somewhere else —
    // so this silences the voice rather than stopping the simulation.
    bool _muted = false;

  public:
    Envelope envelope;

    GravityChannel() {
        RebuildScale();
        envelope.SetLevel(_baseLevel);
    }

    // ── Scale / root ─────────────────────────────────────────────────────────
    // Select* records the choice AND rebuilds the note table (user-facing).
    // Set* only records it — used by preset load, where the stored mask is the
    // source of truth. Same split, and same reason, as NoteForge.
    void SelectScale(int idx) {
        _scaleIndex = constrain(idx, 0, numScales - 1);
        RebuildScale();
    }
    void SelectRoot(int idx) {
        _rootIndex = ((idx % 12) + 12) % 12;
        RebuildScale();
    }
    void SetScaleIndex(int idx) { _scaleIndex = constrain(idx, 0, numScales - 1); }
    void SetRootIndex(int idx) { _rootIndex = ((idx % 12) + 12) % 12; }
    int GetScaleIndex() const { return _scaleIndex; }
    int GetRootIndex() const { return _rootIndex; }

    // ── ROOT is an absolute note, not just a key ──────────────────────────────
    // The ring starts on the root and opens upward by SPREAD, so the root is
    // what puts the channel in a register — the module's octave control, folded
    // into a row that had to exist anyway (there is no seventh row on the page).
    //
    // Without it the span had to guess: it was centred in the output range, which
    // parked a one-octave ring two octaves up with no way to move it. "Where the
    // notes are" is now something the user states rather than a side effect of
    // how wide they asked the ring to be.
    //
    // The octave is capped so the whole ring fits under 5 V: raising SPREAD pulls
    // the root down rather than letting the top of the ring silently flatten
    // against the ceiling.
    int MaxRootOctave() const { return QUANT_OCTAVES - _spread; }
    void SelectRootOctave(int oct) { _rootOctave = constrain(oct, 0, MaxRootOctave()); }
    int GetRootOctave() const { return _rootOctave; }

    // Root as one number: semitones above the module's lowest note. This is what
    // the menu edits, so one detent is one semitone and the row reads "C4".
    int GetRootSemitone() const { return _rootOctave * 12 + _rootIndex; }
    int MaxRootSemitone() const { return MaxRootOctave() * 12; }
    void SelectRootSemitone(int st) {
        st = constrain(st, 0, MaxRootSemitone() + 11);
        SelectRootOctave(st / 12);
        SelectRoot(st % 12); // rebuilds the scale on the new pitch class
    }

    void SetActiveNotes(const bool n[12]) {
        for (int i = 0; i < 12; i++) {
            _notes[i] = n[i];
        }
        _quantizer.Build(_notes);
    }
    bool GetActiveNote(int i) const { return (i >= 0 && i < 12) ? _notes[i] : false; }
    void SetActiveNote(int i, bool on) {
        if (i < 0 || i >= 12) {
            return;
        }
        _notes[i] = on;
        _quantizer.Build(_notes);
    }

    // ── 0V NOTE ──────────────────────────────────────────────────────────────
    // Global in the menu (one row, applied to both channels) but stored per
    // channel: a channel is the thing that knows a pitch, and keeping it here
    // means the Rack port gets per-instance isolation for free from the
    // GravityChannel entry already in engine_state.def.
    void SetCvZeroOctave(int oct) {
        _cvZeroOctave = constrain(oct, GEN_CV_ZERO_OCTAVE_MIN, GEN_CV_ZERO_OCTAVE_MAX);
    }
    int GetCvZeroOctave() const { return _cvZeroOctave; }

    // Widening the ring can leave the root too high for it to fit, so the root
    // comes down with it. Doing it here, where the menu can see the result, is
    // what keeps the ROOT row honest — the alternative is a ring that quietly
    // stops starting on the note the row says it starts on.
    void SetSpread(int oct) {
        _spread = constrain(oct, CHANNEL_SPREAD_MIN, CHANNEL_SPREAD_MAX);
        SelectRootOctave(_rootOctave);
    }
    int GetSpread() const { return _spread; }
    void SetBias(int b) { _bias = constrain(b, CHANNEL_BIAS_MIN, CHANNEL_BIAS_MAX); }
    int GetBias() const { return _bias; }

    void SetGateLevel(int pct) {
        _baseLevel = constrain(pct, 0, 100);
        envelope.SetLevel(_baseLevel);
    }
    int GetGateLevel() const { return _baseLevel; }

    void SetAccent(int pct) { _accent = constrain(pct, 0, 100); }
    int GetAccent() const { return _accent; }

    // The gate level (0..100) a hit of this impact speed produces. Public because
    // it is the single source of truth for ACCENT — Emit() uses it, and it is
    // what the tests pin the response curve against.
    //
    // Envelope mode only. A TRIG pulse has to keep a fixed height or it stops
    // being a trigger: a soft hit would emit a few hundred millivolts and plenty
    // of modules simply would not fire. Dynamics belong on the shaped output.
    int AccentedLevel(float energy) const {
        if (_accent <= 0 || envelope.GetMode() != GateEnvelope) {
            return _baseLevel;
        }
        float norm = (energy - CHANNEL_ACCENT_SOFT_SPEED) /
                     (CHANNEL_ACCENT_HARD_SPEED - CHANNEL_ACCENT_SOFT_SPEED);
        norm = constrain(norm, 0.0f, 1.0f);
        float a = (float)_accent / 100.0f;
        return (int)((float)_baseLevel * ((1.0f - a) + a * norm) + 0.5f);
    }

    // ── Pitch mapping ────────────────────────────────────────────────────────
    // The peg ring starts on ROOT, is stretched across SPREAD octaves, and is
    // warped by BIAS. Three controls, and between them they say exactly where
    // every peg is — nothing is inferred.
    //
    // Peg i sits at a fraction t = i/(pegCount-1) around the ring. Raising t to a
    // power moves the notes in between without moving the ends:
    //
    //   ROOT C4, SPREAD 2, 8 pegs (voltages 0–2 V):
    //   BIAS  0    even         C4  E4  G4  B4  D5  F5  A5  C6
    //   BIAS -100  crowd low    C4  D4  E4  F4  G4  A4  C5  C6
    //   BIAS +100  crowd high   C4  C5  E5  F5  G5  A5  B5  C6
    //
    // Keeping the endpoints fixed is what makes SPREAD and BIAS independent: the
    // ring always covers exactly the octaves SPREAD asks for, and BIAS only
    // decides where the notes bunch up inside it.
    //
    // Everything is counted in *scale degrees*, never semitones, so no setting
    // can produce an out-of-key note (see EveryPegLandsInTheScale).
    int SemitoneForPeg(int peg, int pegCount) const {
        const int perOctave = _quantizer.DegreesPerOctave();
        const int total = _quantizer.ActiveCount();
        if (total <= 1) {
            return _quantizer.SemitoneAt(0);
        }

        // Where the ring starts: the root itself, or — if the note mask has been
        // hand-edited to switch the root off — the first enabled note above it.
        // Never below, or the bottom peg would sound lower than the row says.
        int base = _quantizer.IndexAtOrAbove(GetRootSemitone());
        if (base < 0 || base > total - 1) {
            base = total - 1;
        }

        int span = perOctave * _spread;
        // The root octave is already capped so a whole-octave ring fits (see
        // MaxRootOctave), but a root part-way up the top octave can still leave
        // it a degree or two short. Cover less rather than stacking the last
        // pegs onto the top note, which reads as a fault.
        if (base + span > total - 1) {
            span = total - 1 - base;
        }
        if (span < 1) {
            span = 1;
        }

        float t = (pegCount > 1) ? (float)peg / (float)(pegCount - 1) : 0.0f;
        t = constrain(t, 0.0f, 1.0f);
        // Warp the ring position. gamma is always <= 1 and the two directions are
        // MIRROR IMAGES of each other rather than gamma vs 1/gamma.
        //
        // That matters because the warp is discretised onto whole scale degrees:
        // a steep curve near t=0 rounds several pegs onto the same bottom note,
        // while the same curve near t=1 spreads them out. Using t^gamma for both
        // directions therefore crowds low much harder than it crowds high —
        // measured, full negative bias collapsed three of eight pegs onto the
        // root while full positive bias produced no duplicates at all.
        // Mirroring makes the control behave the same in both directions.
        //
        // The /70 divisor sets how hard full bias bites; stronger than this and
        // the extremes start stacking pegs onto one note again, which reads as a
        // fault rather than as crowding.
        float gamma = powf(2.0f, -fabsf((float)_bias) / 70.0f);
        float warped = (_bias >= 0) ? powf(t, gamma)
                                    : 1.0f - powf(1.0f - t, gamma);

        return _quantizer.SemitoneAt(base + (int)lroundf(warped * (float)span));
    }

    // ── Per-loop processing ──────────────────────────────────────────────────
    // Drains any peg hit from `c`, emits it (immediately, or deferred onto the
    // grid), and advances the envelope exactly once. The resulting jack values
    // are then read with GetCVOutput() / GetGateOutput().
    void Process(Container &c, unsigned long nowUs, const Clock &clk, bool gridBoundary) {
        // The peg ring's size is part of the pitch mapping now (it sets where
        // each peg falls within SPREAD), so it has to travel with the hit —
        // including a deferred one, whose peg count must be the one that was in
        // force when the ball actually struck.
        const int pegCount = c.GetPegCount();

        int peg = 0;
        float energy = 0.0f;

        // Napping: drain the hit and throw it away. Draining rather than leaving
        // it queued is the point — a hit left pending would fire the instant the
        // container woke up, putting a note at the top of every wake loop that
        // the phrase does not contain.
        if (_muted) {
            c.ConsumeHit(peg, energy);
            _pendingPeg = -1;
            envelope.Update(nowUs); // keep the envelope's own timing honest
            _gateValue = 0;
            return;
        }

        if (c.ConsumeHit(peg, energy)) {
            if (clk.QuantizeEnabled()) {
                _pendingPeg = peg; // last-wins, see the member comment
                _pendingEnergy = energy;
                _pendingPegCount = pegCount;
            } else {
                Emit(peg, pegCount, energy, nowUs);
            }
        }

        if (clk.QuantizeEnabled() && gridBoundary && _pendingPeg >= 0) {
            Emit(_pendingPeg, _pendingPegCount, _pendingEnergy, nowUs);
            _pendingPeg = -1;
        }

        _gateValue = envelope.Update(nowUs);
    }

    // Report the IN 1 level, for the envelope's GATE mode.
    void SetGateHigh(bool high) { envelope.SetGateHigh(high); }

    // Silence this voice for a napping loop. CV holds its last note rather than
    // dropping to zero — a nap is a rest, not a note change, and anything
    // tracking the pitch output should stay where it was.
    void SetMuted(bool m) { _muted = m; }
    bool IsMuted() const { return _muted; }

    // ── Outputs (cached by Process(); reading them never advances state) ─────
    uint16_t GetCVOutput() const { return _cvCounts; }
    uint16_t GetGateOutput() const { return _gateValue; }

    int GetSemitone() const { return _semitone; }
    int GetNoteIndex() const { return _semitone < 0 ? 0 : SemitoneToNoteIndex(_semitone); }
    // Numbered from 0V NOTE, so the screen agrees with the VCO: this note leaves
    // the jack at (its octave - 0V NOTE) volts.
    int GetOctaveOut() const {
        return _cvZeroOctave + (_semitone < 0 ? 0 : SemitoneToOctave(_semitone));
    }
    // Follows the JACK, not the envelope: a napping channel outputs nothing, so
    // the display must not keep flashing an envelope nobody can hear.
    bool IsGateActive() const { return !_muted && envelope.IsActive(); }
    bool HasPending() const { return _pendingPeg >= 0; }

    bool ConsumeNoteChanged() {
        bool c = _noteChanged;
        _noteChanged = false;
        return c;
    }

  private:
    void RebuildScale() {
        BuildScale(_scaleIndex, _rootIndex, _notes);
        _quantizer.Build(_notes);
    }

    void Emit(int peg, int pegCount, float energy, unsigned long nowUs) {
        int st = SemitoneForPeg(peg, pegCount);
        if (st != _semitone) {
            _noteChanged = true;
        }
        _semitone = st;
        _cvCounts = (uint16_t)SemitonesToCounts((float)st);

        // ACCENT: scale the gate level by how hard the ball hit.
        envelope.SetLevel(AccentedLevel(energy));
        envelope.Trigger(nowUs);
    }
};
