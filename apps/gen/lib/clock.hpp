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
// that jack's role is CLOCK.
//
// Time is always passed in, never read from a wall clock — that is what makes
// this testable on the host and deterministic in the VCV Rack port.

#ifdef UNIT_TEST
#include "ArduinoFake.h"
#else
#include <Arduino.h>
#endif

#include <stdint.h>

#define CLOCK_MIN_BPM 20
#define CLOCK_MAX_BPM 300

// How many external pulses make one beat. 4 (16th notes) is the most common
// Eurorack convention; 1 suits a clock that already emits quarter notes.
enum ClockPPQN : uint8_t { Ppqn1 = 0, Ppqn2, Ppqn4, Ppqn8, Ppqn24, ClockPPQNLength };
static const char *const ClockPPQNNames[] = {"1", "2", "4", "8", "24"};
static const int ClockPPQNValues[] = {1, 2, 4, 8, 24};

// Grid that peg hits can be deferred onto. OFF is the default and the character
// of the module — the others are there for when a patch needs to lock up.
enum QuantizeDiv : uint8_t { QOff = 0, Q4, Q8, Q16, Q8T, Q16T, QuantizeDivLength };
static const char *const QuantizeDivNames[] = {"OFF", "1/4", "1/8", "1/16", "1/8T", "1/16T"};
// Divisions per beat for each of the above (index 0 unused).
static const int QuantizeDivPerBeat[] = {1, 1, 2, 4, 3, 6};

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
    // ── Parameters ──
    int _internalBpm = 120;
    uint8_t _ppqn = Ppqn4;
    uint8_t _quantize = QOff;

    // Two DIFFERENT things, and conflating them is a bug:
    //   _external     — IN 1 is *configured* as a clock input (a menu setting)
    //   _externalLive — and edges are *actually arriving* right now
    //
    // The hardware has no switched jacks, so it cannot tell a patched cable from
    // an unpatched one — but it can tell whether pulses are coming in, and that
    // is the thing that matters. Without the distinction the module claims to be
    // externally clocked from the moment it boots (CLOCK being IN 1's default
    // role), and worse, a clock that is patched and then unplugged leaves the
    // tempo stuck at whatever it last derived, ignoring the internal BPM for
    // ever.
    bool _external = false;
    bool _externalLive = false;

    // ── Derived / runtime ──
    float _bpm = 120.0f;
    unsigned long _lastEdgeUs = 0;
    bool _haveEdge = false;
    // Median-of-3 over the last few intervals: one jittery edge from a sloppy
    // clock source should not lurch the tempo, and a median rejects a single
    // outlier without the lag an average would add.
    unsigned long _intervals[3] = {0, 0, 0};
    uint8_t _intervalCount = 0;
    uint8_t _intervalPos = 0;

    // Quantize grid phase.
    unsigned long _gridAccumUs = 0;
    unsigned long _lastUpdateUs = 0;
    bool _boundary = false;

  public:
    // ── Parameters ───────────────────────────────────────────────────────────
    void SetBpm(int bpm) {
        _internalBpm = constrain(bpm, CLOCK_MIN_BPM, CLOCK_MAX_BPM);
        // Editing the internal tempo takes effect immediately unless a live
        // external clock is currently overriding it.
        if (!_externalLive) {
            _bpm = (float)_internalBpm;
        }
    }
    int GetBpm() const { return _internalBpm; }
    float GetEffectiveBpm() const { return _bpm; }

    void SetPpqn(int p) { _ppqn = (uint8_t)constrain(p, 0, (int)ClockPPQNLength - 1); }
    int GetPpqn() const { return _ppqn; }

    void SetQuantize(int q) { _quantize = (uint8_t)constrain(q, 0, (int)QuantizeDivLength - 1); }
    int GetQuantize() const { return _quantize; }
    bool QuantizeEnabled() const { return _quantize != QOff; }

    // External *mode* is entered by IN 1's role being CLOCK. That only makes the
    // module willing to follow a clock — it does not mean one is running. Taking
    // the role away drops straight back to the internal BPM.
    void SetExternal(bool ext) {
        if (_external == ext) {
            return;
        }
        _external = ext;
        if (!ext) {
            DropExternal();
        }
    }
    // Is IN 1 configured as a clock input?
    bool IsExternal() const { return _external; }
    // Is a clock actually running into it right now? This is the one the UI
    // wants: it is only true while pulses are genuinely arriving.
    bool IsExternalLive() const { return _externalLive; }

    // ── External clock ───────────────────────────────────────────────────────
    // Call on each rising edge of IN 1 while its role is CLOCK.
    void ExternalEdge(unsigned long nowUs) {
        if (!_external) {
            return;
        }
        if (!_haveEdge) {
            _haveEdge = true;
            _lastEdgeUs = nowUs;
            // One edge is not yet a tempo — wait for an interval before
            // claiming the module is externally clocked.
            return;
        }
        unsigned long interval = nowUs - _lastEdgeUs; // unsigned: wraps correctly
        _lastEdgeUs = nowUs;

        // Reject nonsense: faster than 10 kHz is ringing, slower than 4 s is a
        // stopped clock rather than a very slow one.
        if (interval < 100UL || interval > 4000000UL) {
            return;
        }

        _intervals[_intervalPos] = interval;
        _intervalPos = (uint8_t)((_intervalPos + 1) % 3);
        if (_intervalCount < 3) {
            _intervalCount++;
        }

        unsigned long med = Median3();
        if (med == 0) {
            return;
        }
        float beatUs = (float)med * (float)ClockPPQNValues[_ppqn];
        float bpm = 60000000.0f / beatUs;
        _bpm = constrain(bpm, (float)CLOCK_MIN_BPM, (float)CLOCK_MAX_BPM);
        _externalLive = true; // a real interval was measured
    }

    // How long to wait after the last edge before declaring the clock stopped.
    //
    // Scaled to the tempo actually being received rather than fixed: a 30 BPM
    // clock at 1 ppqn is 2 s between pulses, so a short fixed timeout would keep
    // declaring it dead between beats, while a long fixed one would leave a fast
    // clock hanging for seconds after it stops. Three intervals gives a couple
    // of missed pulses of tolerance.
    unsigned long ExternalTimeoutUs() const {
        unsigned long med = Median3();
        if (med == 0) {
            return 2000000UL; // no interval measured yet
        }
        unsigned long t = med * 3UL;
        if (t < 500000UL) {
            t = 500000UL;
        } else if (t > 5000000UL) {
            t = 5000000UL;
        }
        return t;
    }

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
        // containers turning at a tempo nothing is driving any more.
        if (_externalLive && (nowUs - _lastEdgeUs) > ExternalTimeoutUs()) {
            DropExternal();
        }

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
    unsigned long BeatUs() const {
        float b = _bpm > 1.0f ? _bpm : 1.0f;
        return (unsigned long)(60000000.0f / b);
    }

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
            float secsPerRev = beats * 60.0f / (_bpm > 1.0f ? _bpm : 1.0f);
            omega = CLOCK_TWO_PI / secsPerRev;
        }
        return reverse ? -omega : omega;
    }

  private:
    // Fall back to the internal tempo and forget the external clock's history,
    // so the next clock that arrives is measured fresh rather than blended with
    // intervals from whatever was patched before.
    void DropExternal() {
        _externalLive = false;
        _bpm = (float)_internalBpm;
        _haveEdge = false;
        _intervalCount = 0;
        _intervalPos = 0;
        _intervals[0] = _intervals[1] = _intervals[2] = 0;
    }

    unsigned long Median3() const {
        if (_intervalCount == 0) {
            return 0;
        }
        if (_intervalCount < 3) {
            return _intervals[(_intervalPos + 2) % 3]; // most recent
        }
        unsigned long a = _intervals[0], b = _intervals[1], c = _intervals[2];
        if (a > b) {
            unsigned long t = a;
            a = b;
            b = t;
        }
        if (b > c) {
            unsigned long t = b;
            b = c;
            c = t;
        }
        if (a > b) {
            b = a;
        }
        return b;
    }
};
