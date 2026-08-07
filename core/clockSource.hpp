#pragma once

// clockSource.hpp — where a module gets its tempo.
//
// The internal BPM, an external clock at IN 1, the changeover between them, and
// nothing else. What a module DOES with a tempo — a rotation rate, a step, a
// quantize grid — stays with the module, because no two of them agree on that.
//
// Lifted out of GravityForge's lib/clock.hpp when WeaveForge needed the same
// changeover behaviour. GravityForge's Clock still exists and still owns its
// grid, its SPACE and its spin rates; it holds one of these as a member.
//
// ── TIME IS ALWAYS PASSED IN ─────────────────────────────────────────────────
// Nothing here calls micros(). That is the load-bearing rule of this file: it is
// what lets the host test runner drive a clock at any tempo without waiting, and
// what makes the VCV Rack ports deterministic instead of following the host's
// wall clock. A member function that reaches for the current time is a bug, not
// a convenience.
//
// ── NOT ClockForge's clock ───────────────────────────────────────────────────
// apps/clk/lib/clockEngine.hpp is a 960-PPQN hardware-timer ISR with file-scope
// BPM and tickCounter globals, driving that whole module from the interrupt.
// This is a passed-in-time value object. They are opposite architectures that
// share a word, and folding one into the other would be a port rather than a
// merge — the same call README.md already records for ClockForge's quantizer.
// Do not "unify" them without a reason of ClockForge's own.

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

class ClockSource {
    // ── Parameters ──
    int _internalBpm = 120;
    uint8_t _ppqn = Ppqn4;

    // Two DIFFERENT things, and conflating them is a bug:
    //   _external     — IN 1 is *configured* as a clock input (a menu setting)
    //   _externalLive — and edges are *actually arriving* right now
    //
    // The hardware has no switched jacks, so it cannot tell a patched cable from
    // an unpatched one — but it can tell whether pulses are coming in, and that
    // is the thing that matters. Without the distinction a module claims to be
    // externally clocked from the moment it boots, and worse, a clock that is
    // patched and then unplugged leaves the tempo stuck at whatever it last
    // derived, ignoring the internal BPM for ever.
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
    // Notices that an external clock has stopped and hands tempo back to the
    // internal setting. Call this once a loop, before anything that reads the
    // tempo — a module that skips it keeps running at the tempo of a cable that
    // was unplugged.
    void Update(unsigned long nowUs) {
        if (_externalLive && (nowUs - _lastEdgeUs) > ExternalTimeoutUs()) {
            DropExternal();
        }
    }

    // ── Derived ──────────────────────────────────────────────────────────────
    unsigned long BeatUs() const {
        float b = _bpm > 1.0f ? _bpm : 1.0f;
        return (unsigned long)(60000000.0f / b);
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
