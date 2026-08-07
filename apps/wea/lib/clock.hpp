#pragma once

// clock.hpp — turning a tempo into steps.
//
// The tempo itself — internal BPM, external clock at IN 1, the changeover
// between them — is core/clockSource.hpp, shared with GravityForge. What is
// here is the part that is a sequencer's own: turning that into the edges that
// actually shift the registers.
//
// ── RATE IS STEPS PER BEAT, ON BOTH SOURCES ──────────────────────────────────
// One control, one meaning, whether the tempo comes from IN 1 or the internal
// BPM: x4 is four steps a beat (sixteenths) and /4 is one step every four beats,
// and that is true either way. The first version of this file had a DIVIDE that
// counted input edges externally and scaled the period internally — so "/2" ran
// at half speed on an external clock and DOUBLE speed on the internal one. Two
// code paths meaning opposite things by the same label.
//
// External clocks additionally get PHASE-LOCKED rather than free-running: every
// beat boundary (counted in input pulses via IN PPQN) resets the step phase, so
// multiplied steps stay nailed to the incoming clock instead of drifting against
// it as the tempo estimate wobbles.
//
// Time is always passed in, never read from a wall clock — see clockSource.hpp.

#ifdef UNIT_TEST
#include "ArduinoFake.h"
#else
#include <Arduino.h>
#endif

#include <stdint.h>

#include "clockSource.hpp" // core

// Steps per beat. Names follow ClockForge's divider so the two modules read
// alike; the set is trimmed to what a shift register can use — a 16-step
// register clocked at x64 is a blur, and /128 is one step a minute.
// SYMMETRIC AROUND x1: /N and xN sit the same number of detents either side, so
// a click one way is the mirror of a click the other. The first version of this
// table was not — it had x6 with no /6 — which nobody notices until they are
// counting clicks in a performance. There is a test for it.
static const char *const ClockRateNames[] = {"/16", "/8", "/6", "/4", "/3", "/2", "x1",
                                             "x2",  "x3", "x4", "x6", "x8", "x16"};
static const float ClockRateSteps[] = {
    1.0f / 16.0f, 1.0f / 8.0f, 1.0f / 6.0f, 1.0f / 4.0f, 1.0f / 3.0f,
    1.0f / 2.0f,  1.0f,        2.0f,        3.0f,        4.0f,
    6.0f,         8.0f,        16.0f};
#define WEA_CLOCK_RATE_COUNT 13
#define WEA_CLOCK_RATE_UNITY 6 // index of "x1"

// x1, and x1 is the tempo you set: at 120 BPM the module steps 120 times a
// minute, and one pulse at IN 1 is one step at IN PPQN 1. The rate then divides
// or multiplies from THERE.
//
// It defaulted to x4 (sixteenths) at first, which is a fine rate to play at but
// a bad one to boot at — the BPM on the header said 120 while the sequencer ran
// at 480 steps a minute, so the one number on screen was not the rate of the one
// thing you were listening to. A default that makes the readout lie costs more
// than a default that needs one detent.
#define WEA_CLOCK_RATE_DEFAULT WEA_CLOCK_RATE_UNITY

class StepClock {
    ClockSource _src;
    uint8_t _rate = WEA_CLOCK_RATE_DEFAULT;

    // Step phase, in microseconds into the current step period.
    unsigned long _stepAccumUs = 0;
    unsigned long _lastUpdateUs = 0;
    // Whether _lastUpdateUs means anything yet. Without this the first Update()
    // measures from zero to whatever micros() happens to read at Begin() — a
    // second or more on hardware — and the module fires a step the instant it
    // powers on, before any clock has arrived. The `elapsed > 1s` guard below
    // does not catch it, because that reading is usually just under a second.
    bool _haveUpdate = false;

    // External phase lock: where we are within the beat, which beat it is, and
    // how many steps this beat has already produced.
    uint8_t _pulseInBeat = 0;
    uint32_t _beatCounter = 0;
    int _stepsInBeat = 0;

    bool _pendingStep = false;

  public:
    // ── Tempo, forwarded ─────────────────────────────────────────────────────
    void SetBpm(int bpm) { _src.SetBpm(bpm); }
    int GetBpm() const { return _src.GetBpm(); }
    float GetEffectiveBpm() const { return _src.GetEffectiveBpm(); }
    void SetPpqn(int p) { _src.SetPpqn(p); }
    int GetPpqn() const { return _src.GetPpqn(); }
    void SetExternal(bool ext) { _src.SetExternal(ext); }
    bool IsExternal() const { return _src.IsExternal(); }
    bool IsExternalLive() const { return _src.IsExternalLive(); }
    unsigned long BeatUs() const { return _src.BeatUs(); }

    // ── Rate ─────────────────────────────────────────────────────────────────
    void SetRate(int r) {
        const int n = constrain(r, 0, WEA_CLOCK_RATE_COUNT - 1);
        if (n != _rate) {
            _rate = (uint8_t)n;
            // Start the new rate from a clean phase rather than carrying a
            // partial period across: a rate changed mid-phrase should take
            // effect from here, not fire early because most of the old period
            // had already elapsed.
            _stepAccumUs = 0;
            _beatCounter = 0;
        }
    }
    int GetRate() const { return _rate; }
    float RateSteps() const { return ClockRateSteps[_rate]; }
    const char *RateName() const { return ClockRateNames[_rate]; }

    // How many beats one step spans. 1 for any multiplication; N for /N.
    int BeatsPerStep() const {
        const float s = RateSteps();
        return (s >= 1.0f) ? 1 : (int)lroundf(1.0f / s);
    }

    // ── Edges ────────────────────────────────────────────────────────────────
    // A rising edge at IN 1. Feeds the tempo estimator, and the beat boundary
    // it may represent re-locks the step phase.
    void ExternalEdge(unsigned long nowUs) {
        _src.ExternalEdge(nowUs);

        const int perBeat = ClockPPQNValues[_src.GetPpqn()];
        if (++_pulseInBeat >= (uint8_t)perBeat) {
            _pulseInBeat = 0;
            OnBeatBoundary();
        }
    }

    // Once a loop. Generates steps from the tempo, and lets the tempo source
    // notice that an external clock has stopped.
    void Update(unsigned long nowUs) {
        unsigned long elapsed = 0;
        if (_haveUpdate) {
            elapsed = nowUs - _lastUpdateUs;
        } else {
            _haveUpdate = true; // this call only establishes the baseline
        }
        _lastUpdateUs = nowUs;
        // A wild jump — do not synthesise a burst of steps to catch up.
        if (elapsed > 1000000UL) {
            elapsed = 0;
        }

        const bool wasLive = _src.IsExternalLive();
        _src.Update(nowUs);

        // An external clock that has just stopped hands stepping back to the
        // internal generator. Clear the phase on the way across so the first
        // internal step is a full period after the last external one.
        if (wasLive && !_src.IsExternalLive()) {
            _stepAccumUs = 0;
            _pulseInBeat = 0;
            _beatCounter = 0;
            _stepsInBeat = 0;
            return;
        }

        const unsigned long period = StepPeriodUs();
        if (period == 0) {
            return;
        }

        _stepAccumUs += elapsed;
        if (_stepAccumUs < period) {
            return;
        }
        // Modulo, not a subtraction: after a stall we want one step, not a
        // backlog of them arriving at once.
        _stepAccumUs %= period;

        // ── Who owns this step? ──────────────────────────────────────────────
        // Free-running, the accumulator owns every step. With an external clock
        // live it owns only the ones BETWEEN beat boundaries: the boundary emits
        // the first step of its beat, and the accumulator would otherwise emit
        // one at the same instant — which is exactly double speed at x1, and
        // half a step of jitter at every rate.
        if (!_src.IsExternalLive()) {
            _pendingStep = true;
            return;
        }
        const float s = RateSteps();
        if (s < 1.0f) {
            return; // divided: boundaries are the only source of steps
        }
        if (_stepsInBeat < (int)lroundf(s)) {
            _pendingStep = true;
            _stepsInBeat++;
        }
    }

    // One step, once. The engine drains this each loop.
    bool ConsumeStep() {
        bool s = _pendingStep;
        _pendingStep = false;
        return s;
    }

    // Time between steps: the beat, scaled by the rate.
    //
    // Under division the period spans several beats, and while an external clock
    // is running the accumulator is reset at every beat boundary — so it never
    // reaches this and every divided step comes from OnBeatBoundary() instead.
    // That is deliberate: it makes /4 land exactly on every fourth beat rather
    // than on the tempo estimate's idea of four beats.
    unsigned long StepPeriodUs() const {
        const float s = RateSteps();
        if (s <= 0.0f) {
            return 0;
        }
        return (unsigned long)((float)_src.BeatUs() / s);
    }

  private:
    // A beat started. Re-lock the step phase to it, and emit a step if this beat
    // is one a step falls on.
    void OnBeatBoundary() {
        _stepAccumUs = 0;
        if (_beatCounter % (uint32_t)BeatsPerStep() == 0) {
            _pendingStep = true;
        }
        _beatCounter++;
        // This beat's first step has been accounted for, so the accumulator may
        // fill in the rest of them and no more.
        _stepsInBeat = 1;
    }
};
