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
// module's 0–5 V output range and snapped to whole octaves, so the lowest peg
// always lands on the root rather than an arbitrary degree.
#define CHANNEL_SPREAD_MIN 1
#define CHANNEL_SPREAD_MAX 5

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
    int _rootIndex = 0;  // C
    int _spread = 2;     // octaves the peg ring covers
    int _bias = 0;       // -100 = crowd low, 0 = even, +100 = crowd high
    bool _notes[12] = {false};
    Quantizer _quantizer;

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

    void SetSpread(int oct) { _spread = constrain(oct, CHANNEL_SPREAD_MIN, CHANNEL_SPREAD_MAX); }
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
    // The peg ring is stretched across SPREAD octaves and warped by BIAS.
    //
    // Peg i sits at a fraction t = i/(pegCount-1) around the ring. Raising t to a
    // power moves the notes in between without moving the ends:
    //
    //   BIAS  0    even         C2  E2  G2  B2  D3  F3  A3  C4
    //   BIAS -100  crowd low    C2  D2  E2  F2  G2  A2  C3  C4
    //   BIAS +100  crowd high   C2  C3  E3  F3  G3  A3  B3  C4
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

        int span = perOctave * _spread;
        if (span > total - 1) {
            span = total - 1;
        }
        if (span < 1) {
            span = 1;
        }

        // Centre the span in the output range, snapped to whole octaves so the
        // lowest peg lands on the root instead of some arbitrary degree.
        int baseOctave = (int)lroundf(((float)(total - 1 - span) * 0.5f) / (float)perOctave);
        int base = baseOctave * perOctave;
        if (base + span > total - 1) {
            base = total - 1 - span;
        }
        if (base < 0) {
            base = 0;
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
    int GetOctaveOut() const { return _semitone < 0 ? 0 : SemitoneToOctave(_semitone); }
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
