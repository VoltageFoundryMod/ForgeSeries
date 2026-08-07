#pragma once

// clock.hpp — tempo, container rotation rate, and the optional quantize grid.
//
// The physics in this module ALWAYS free-runs; the clock never steps it. What
// the clock does is:
//
//   1. set how fast the containers rotate, expressed in beats per revolution so
//      the motion stays musically related to the patch rather than being a raw
//      degrees/second number that means nothing;
//   2. provide an optional grid that peg hits can be deferred onto (see
//      GravityChannel), for when a patch needs the rhythm locked.
//
// Tempo is the internal BPM, or derived from an external clock at IN 1 when
// that jack's role is CLOCK. THAT part — the internal/external changeover, the
// edge-to-BPM derivation, the stopped-clock timeout — now lives in
// core/clockSource.hpp, because WeaveForge needs exactly the same behaviour.
// What is left here is what is GravityForge's own: the rotation rates, the
// quantize grid and SPACE.
//
// The class keeps its name and its whole public API, and forwards the tempo
// half to the member. Composition rather than a rewrite: this module ships, its
// callers and its 21 tests were written against this surface, and a promotion
// into core/ is not a reason to disturb any of them.
//
// Time is always passed in, never read from a wall clock — that is what makes
// this testable on the host and deterministic in the VCV Rack port.

#ifdef UNIT_TEST
#include "ArduinoFake.h"
#else
#include <Arduino.h>
#endif

#include <stdint.h>

// CLOCK_MIN_BPM / CLOCK_MAX_BPM, ClockPPQN and its tables come from here, so
// callers and tests that used them through clock.hpp still see them.
#include "clockSource.hpp"

// Grid that peg hits can be deferred onto. OFF is the default and the character
// of the module — the others are there for when a patch needs to lock up.
enum QuantizeDiv : uint8_t { QOff = 0, Q4, Q8, Q16, Q8T, Q16T, QuantizeDivLength };
static const char *const QuantizeDivNames[] = {"OFF", "1/4", "1/8", "1/16", "1/8T", "1/16T"};
// Divisions per beat for each of the above (index 0 unused).
static const int QuantizeDivPerBeat[] = {1, 1, 2, 4, 3, 6};

// SPACE — the minimum gap between two notes from one container, in beats. OFF is
// the module's original behaviour: every strike that clears the physics speaks,
// however close together they land.
//
// Measured in beats rather than milliseconds so the spacing follows the tempo,
// and named with the same "beats" convention as SpinRate above ("1/4" is a
// quarter of a beat, "4" is four beats) so the two controls read alike.
//
// This is a *floor*, not a grid: a note is dropped if it arrives too soon after
// the last one that spoke, but a note arriving later than the gap plays the
// instant it happens. QUANTIZE is the control that moves notes onto a grid —
// SPACE only thins them out, which is what keeps the rhythm human while making
// it sparse.
enum NoteSpace : uint8_t {
    SpaceOff = 0,
    SpaceQuarterBeat,
    SpaceHalfBeat,
    SpaceBeat,
    Space2Beats,
    Space4Beats,
    Space8Beats,
    NoteSpaceLength
};
static const char *const NoteSpaceNames[] = {"OFF", "1/4", "1/2", "1", "2", "4", "8"};
static const float NoteSpaceBeats[] = {0.0f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};

// Container rotation, in beats per full revolution. FREE detaches it from the
// clock entirely and uses the manual rate instead.
enum SpinRate : uint8_t {
    SpinHalf = 0, // 1/2 beat per revolution — very fast
    Spin1,
    Spin2,
    Spin4,
    Spin8,
    Spin16,
    SpinFree,
    SpinRateLength
};
static const char *const SpinRateNames[] = {"1/2", "1", "2", "4", "8", "16", "FREE"};
static const float SpinRateBeats[] = {0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 0.0f};

static const float CLOCK_TWO_PI = 6.28318530718f;

class Clock {
    // Tempo, and where it comes from. See core/clockSource.hpp.
    ClockSource _src;

    // ── GravityForge's own ──
    uint8_t _quantize = QOff;

    // Quantize grid phase.
    unsigned long _gridAccumUs = 0;
    unsigned long _lastUpdateUs = 0;
    bool _boundary = false;

  public:
    // ── Parameters ───────────────────────────────────────────────────────────
    void SetBpm(int bpm) { _src.SetBpm(bpm); }
    int GetBpm() const { return _src.GetBpm(); }
    float GetEffectiveBpm() const { return _src.GetEffectiveBpm(); }

    void SetPpqn(int p) { _src.SetPpqn(p); }
    int GetPpqn() const { return _src.GetPpqn(); }

    void SetQuantize(int q) { _quantize = (uint8_t)constrain(q, 0, (int)QuantizeDivLength - 1); }
    int GetQuantize() const { return _quantize; }
    bool QuantizeEnabled() const { return _quantize != QOff; }

    void SetExternal(bool ext) { _src.SetExternal(ext); }
    bool IsExternal() const { return _src.IsExternal(); }
    bool IsExternalLive() const { return _src.IsExternalLive(); }

    // ── External clock ───────────────────────────────────────────────────────
    // Call on each rising edge of IN 1 while its role is CLOCK.
    void ExternalEdge(unsigned long nowUs) { _src.ExternalEdge(nowUs); }

    unsigned long ExternalTimeoutUs() const { return _src.ExternalTimeoutUs(); }

    // ── Per-loop update ──────────────────────────────────────────────────────
    // Advances the quantize grid. Consume the result with ConsumeBoundary().
    void Update(unsigned long nowUs) {
        unsigned long elapsed = nowUs - _lastUpdateUs;
        _lastUpdateUs = nowUs;
        // First call, or a wild jump — do not synthesise a burst of boundaries.
        if (elapsed > 1000000UL) {
            elapsed = 0;
        }

        // Has the external clock stopped? Unpatching a cable or stopping the
        // sequencer must hand tempo back to the internal setting, not leave the
        // containers turning at a tempo nothing is driving any more. Ordered
        // before the grid below, so a boundary is never computed from a tempo
        // that has just been dropped.
        _src.Update(nowUs);

        if (_quantize == QOff) {
            _gridAccumUs = 0;
            _boundary = false;
            return;
        }

        unsigned long period = GridPeriodUs();
        if (period == 0) {
            _boundary = false;
            return;
        }
        _gridAccumUs += elapsed;
        if (_gridAccumUs >= period) {
            // Modulo rather than a single subtraction: after a stall several
            // periods may have elapsed, and we want one boundary, not a
            // backlog of them.
            _gridAccumUs %= period;
            _boundary = true;
        }
    }

    bool ConsumeBoundary() {
        bool b = _boundary;
        _boundary = false;
        return b;
    }

    // ── Derived rates ────────────────────────────────────────────────────────
    unsigned long BeatUs() const { return _src.BeatUs(); }

    unsigned long GridPeriodUs() const {
        if (_quantize == QOff) {
            return 0;
        }
        int per = QuantizeDivPerBeat[_quantize];
        return per > 0 ? BeatUs() / (unsigned long)per : 0;
    }

    // Angular velocity for a container, in rad/s.
    //  spin      — index into SpinRateNames/SpinRateBeats
    //  reverse   — spin the other way
    //  freeHz    — revolutions per second, used only when spin == SpinFree
    float OmegaFor(int spin, bool reverse, float freeHz) const {
        int s = constrain(spin, 0, (int)SpinRateLength - 1);
        float omega;
        if (s == SpinFree) {
            omega = CLOCK_TWO_PI * freeHz;
        } else {
            float beats = SpinRateBeats[s];
            if (beats <= 0.0f) {
                return 0.0f;
            }
            float bpm = _src.GetEffectiveBpm();
            float secsPerRev = beats * 60.0f / (bpm > 1.0f ? bpm : 1.0f);
            omega = CLOCK_TWO_PI / secsPerRev;
        }
        return reverse ? -omega : omega;
    }
};
