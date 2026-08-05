#pragma once

// generator.hpp — the two orbits, their clock, and how an orbit becomes a jack.
//
// Generator owns one system's state and everything that turns it into two
// control voltages; ChaosWorld owns both generators, the fixed-step clock they
// share, and the coupling between them.
//
// ── Determinism is a requirement, not a nicety ──────────────────────────────
// Same as GravityForge: a fixed timestep, no wall-clock reads inside the DSP,
// and time passed in as a parameter. That is what lets the host tests run the
// simulation faster than real time and still get the module's actual behaviour,
// and it is what makes the VCV port match the hardware when Rack renders a
// mixdown at 10x. Do not introduce micros() calls below this line.
//
// ── Why a fixed 1 ms module step on top of the integrator's own step ────────
// The integrator's step is in the attractor's own time units and changes with
// SPEED; the module step is real milliseconds and never changes. Keeping them
// separate is what makes SPEED a smooth control rather than a change in
// simulation grid: the module always advances in 1 ms slices, and SPEED decides
// how much attractor time each slice covers.

#ifdef UNIT_TEST
#include "ArduinoFake.h"
#else
#include <Arduino.h>
#endif

#include <math.h>
#include <stdint.h>

#include "attractors.hpp"

// ── The module clock ────────────────────────────────────────────────────────
#define ATT_STEP_US 1000UL
#define ATT_STEP_DT 0.001f

// Hard cap on catch-up steps in one Advance() call, for the same reason
// GravityForge has one: a display stall must never turn into an unbounded
// catch-up loop on the core that also writes the DAC. Dropping simulated time is
// the lesser failure.
#define ATT_MAX_STEPS_PER_CALL 8

// Integrator substeps inside one 1 ms module step.
//
// The step size wanted is (rate x speed x 1 ms) and the system's hMax caps how
// large a step stays accurate, so normally the count is whatever it takes to
// stay under hMax. This caps that count, and when it binds the step size grows
// past hMax instead of time slowing down — an honest clock with degraded
// accuracy beats a SPEED control that silently stops speeding up. At
// ATT_SPEED_MAX the largest overshoot across the twelve systems is 1.2x hMax,
// which every one of them survives; anything past that is caught by
// AttDiverged() and re-seeded.
#define ATT_MAX_SUBSTEPS 8

#define ATT_SPEED_MIN 0.01f
#define ATT_SPEED_MAX 16.0f
#define ATT_SPEED_DEFAULT 1.0f

// ── Output shaping ranges ───────────────────────────────────────────────────
#define ATT_LEVEL_MAX 100  // %
#define ATT_OFFSET_MAX 100 // +/- %, one unit = 1 % of the half-range (25 mV)
#define ATT_SMOOTH_MAX 100 // %
// SMOOTH 100 % is this many milliseconds of one-pole lag. Long enough to turn
// Rössler's z spike into a swell, short enough that it never feels broken.
#define ATT_SMOOTH_MAX_MS 200.0f

// ── Auto range ──────────────────────────────────────────────────────────────
// How fast the tracked window closes back in on the orbit, as a fraction of the
// current window per second. Slow on purpose: the window must survive the
// minutes-long gaps between a chaotic orbit's excursions, or the CV would be
// quietly re-gained every time the trajectory went somewhere new.
#define ATT_AUTO_RELAX_PER_S 0.05f
// Never let the tracked window collapse: a system parked near a fixed point has
// a near-zero span, and dividing by it turns numerical dust into full-scale CV.
#define ATT_AUTO_MIN_SPAN 1.0e-3f

// ── Trail (the Lissajous view) ──────────────────────────────────────────────
// Points are pushed on the orbit's own clock, not the frame clock, so the drawn
// arc covers the same amount of *trajectory* at every SPEED — the figure looks
// like itself whether it is being traced in a second or in a minute.
//
// The length is what decides whether the screen shows the FIGURE or just where
// the orbit happens to be. 256 points is 6.4 seconds of Lorenz at SPEED 1, which
// is enough to draw both wings; at 96 it showed one lobe and read as a wobble.
// The cost is 1 KB of RAM per instance and ~500 short drawLine calls per frame,
// about 1 % of Core 1 at the 20 fps the renderer is rate-limited to.
#define ATT_TRAIL_LEN 256
#define ATT_TRAIL_HZ 40.0f    // points per second at SPEED 1.00
#define ATT_TRAIL_MAX_MS 50UL // ...but never leave the trace frozen below that

class Generator {
  public:
    // ── Configuration (written by ApplyParams every pass) ────────────────────
    void SetSystem(int id) {
        const uint8_t n = AttClampId(id);
        if (n == _id)
            return;
        _id = n;
        Reseed(); // a new system's state space has nothing to do with the old one
    }
    uint8_t GetSystem() const { return _id; }

    void SetParam(int i, float v) {
        if (i < 0 || i >= ATT_MAX_PARAMS)
            return;
        _p[i] = v;
    }
    float GetParam(int i) const { return (i < 0 || i >= ATT_MAX_PARAMS) ? 0.0f : _p[i]; }

    void SetSpeed(float s) { _speed = constrain(s, ATT_SPEED_MIN, ATT_SPEED_MAX); }
    void SetSource(int jack, int axis) {
        if (jack < 0 || jack > 1)
            return;
        _src[jack] = (uint8_t)constrain(axis, 0, (int)AxisCount - 1);
    }
    void SetLevel(float l01) { _level = constrain(l01, 0.0f, 1.0f); }
    void SetOffset(float o) { _offset = constrain(o, -1.0f, 1.0f); }
    void SetSmooth(float s01) { _smooth = constrain(s01, 0.0f, 1.0f); }
    void SetAutoRange(bool on) {
        if (on == _autoRange)
            return;
        _autoRange = on;
        ResetTracker(); // start from the published window rather than from dust
    }

    // Put the orbit back on its seed point.
    //
    // `salt` separates the two generators: seeded identically, two copies of the
    // same system would trace the same figure forever, and the module would look
    // broken in exactly the way a chaotic system is supposed to make impossible.
    // A displacement this small is invisible for a second or two and total
    // within ten — which is the phenomenon the module exists to sell.
    void Reseed(int salt = 0) {
        const AttractorSpec &sp = AttSpec(_id);
        for (int i = 0; i < 3; i++)
            _s[i] = sp.x0[i];
        _s[0] += 0.001f * (float)(salt + 1);

        ResetTracker();
        for (int k = 0; k < 2; k++) {
            _norm[k] = 0.0f;
            _smoothed[k] = 0.0f;
        }
        _trailCount = 0;
        _trailHead = 0;
        _trailArc = 0.0f;
        _trailUs = 0;
        _diverged = false;
    }
    void Seed(int salt) { _salt = (int8_t)salt; Reseed(salt); }

    // ── One module step (ATT_STEP_DT of real time) ──────────────────────────
    void Step() {
        const AttractorSpec &sp = AttSpec(_id);

        // Attractor time this slice covers, split into whole substeps no larger
        // than the system tolerates. See ATT_MAX_SUBSTEPS for what happens when
        // the cap binds.
        const float dtAtt = sp.rate * _speed * ATT_STEP_DT;
        int n = 1 + (int)(dtAtt / sp.hMax);
        if (n > ATT_MAX_SUBSTEPS)
            n = ATT_MAX_SUBSTEPS;
        const float h = dtAtt / (float)n;

        for (int i = 0; i < n; i++)
            AttRk4(_id, _s, _p, h);

        if (AttDiverged(_s)) {
            // Left the attractor for good — parameters can be set to values
            // where there is no bounded orbit at all. Re-seeding is heard as the
            // pattern restarting; the alternative, a clamped rail, is heard as a
            // dead module.
            Reseed(_salt);
            _diverged = true;
            return;
        }

        TrackRange();
        for (int k = 0; k < 2; k++) {
            const float target = NormalisedAxis(_src[k]);
            // One-pole lag. `a` is the fraction of the gap closed per module
            // step; SMOOTH 0 gives a == 1, i.e. the value straight through.
            const float tauMs = _smooth * ATT_SMOOTH_MAX_MS;
            const float a = (tauMs <= 0.0f) ? 1.0f : (1.0f / (1.0f + tauMs));
            _smoothed[k] += a * (target - _smoothed[k]);
            _norm[k] = _smoothed[k];
        }

        PushTrail(dtAtt);
    }

    // ── Coupling support (ChaosWorld drives this) ───────────────────────────
    // Both sides are expressed in normalised units so that systems with wildly
    // different natural sizes — Chua's y lives in +/-0.4, Chen's in +/-23 — can be
    // pulled toward each other at all.
    float NormalisedAxis(int axis) const {
        const int a = constrain(axis, 0, 2);
        float centre, half;
        Window(a, centre, half);
        return (_s[a] - centre) / half;
    }
    void NudgeAxis(int axis, float dNorm) {
        const int a = constrain(axis, 0, 2);
        float centre, half;
        Window(a, centre, half);
        _s[a] += dNorm * half;
    }

    // ── Outputs ─────────────────────────────────────────────────────────────
    // The jack value for output `jack` (0 or 1), as 0..1 of the 0-5 V range.
    //
    // LEVEL scales around the centre and OFFSET slides it, so LEVEL 100 /
    // OFFSET 0 is the full 0-5 V swing centred at 2.5 V. Both are applied here
    // rather than inside the simulation: what the orbit does must not depend on
    // how it is being listened to.
    float Out01(int jack) const {
        const int k = constrain(jack, 0, 1);
        return constrain(0.5f + 0.5f * (_norm[k] * _level + _offset), 0.0f, 1.0f);
    }
    // -1..1, the value before LEVEL/OFFSET. What the plot draws and what the
    // coupling exchanges.
    float OutNorm(int jack) const { return _norm[constrain(jack, 0, 1)]; }

    uint8_t GetSource(int jack) const { return _src[constrain(jack, 0, 1)]; }
    float GetSpeed() const { return _speed; }
    bool AutoRange() const { return _autoRange; }
    const float *State() const { return _s; }

    // True once since the last call if the orbit had to be re-seeded. The UI
    // shows it: a system that keeps diverging is a parameter problem, and
    // without the cue it just sounds like the module restarting at random.
    bool ConsumeDiverged() {
        const bool d = _diverged;
        _diverged = false;
        return d;
    }

    // ── Trail, oldest first ─────────────────────────────────────────────────
    int TrailCount() const { return _trailCount; }
    // i = 0 is the oldest retained point. x/y come back in -1.27..1.27.
    void TrailPoint(int i, float &x, float &y) const {
        const int n = _trailCount;
        const int idx = (_trailHead - n + i + 2 * ATT_TRAIL_LEN) % ATT_TRAIL_LEN;
        x = (float)_trailX[idx] / 100.0f;
        y = (float)_trailY[idx] / 100.0f;
    }

  private:
    // The window this axis is normalised against: the published constants, or
    // the tracked one under RANGE ▸ AUTO.
    void Window(int axis, float &centre, float &half) const {
        if (_autoRange) {
            centre = 0.5f * (_amax[axis] + _amin[axis]);
            half = 0.5f * (_amax[axis] - _amin[axis]);
            if (half < ATT_AUTO_MIN_SPAN)
                half = ATT_AUTO_MIN_SPAN;
            return;
        }
        const AttractorSpec &sp = AttSpec(_id);
        centre = sp.centre[axis];
        half = sp.halfSpan[axis];
        if (half < ATT_AUTO_MIN_SPAN)
            half = ATT_AUTO_MIN_SPAN;
    }

    void ResetTracker() {
        const AttractorSpec &sp = AttSpec(_id);
        for (int a = 0; a < 3; a++) {
            _amin[a] = sp.centre[a] - sp.halfSpan[a];
            _amax[a] = sp.centre[a] + sp.halfSpan[a];
        }
    }

    // Widen instantly, close in slowly. Instant on the way out because a clipped
    // peak is the one thing auto range exists to prevent; slow on the way in
    // because a chaotic orbit's rare excursions are minutes apart and a fast
    // relax would re-gain the CV between them.
    void TrackRange() {
        if (!_autoRange)
            return;
        for (int a = 0; a < 3; a++) {
            const float span = _amax[a] - _amin[a];
            const float relax = span * ATT_AUTO_RELAX_PER_S * ATT_STEP_DT;
            if (_s[a] < _amin[a])
                _amin[a] = _s[a];
            else
                _amin[a] += relax;
            if (_s[a] > _amax[a])
                _amax[a] = _s[a];
            else
                _amax[a] -= relax;
            if (_amax[a] - _amin[a] < ATT_AUTO_MIN_SPAN) {
                const float c = 0.5f * (_amax[a] + _amin[a]);
                _amin[a] = c - 0.5f * ATT_AUTO_MIN_SPAN;
                _amax[a] = c + 0.5f * ATT_AUTO_MIN_SPAN;
            }
        }
    }

    void PushTrail(float dtAtt) {
        const AttractorSpec &sp = AttSpec(_id);
        _trailArc += dtAtt;
        _trailUs += ATT_STEP_US;

        const float arcStep = sp.rate / ATT_TRAIL_HZ;
        const bool byArc = _trailArc >= arcStep;
        // The time floor keeps a very slow orbit drawing a short live trace
        // rather than a frozen one — without it, SPEED 0.01 leaves the screen
        // unchanged for two seconds at a time and reads as a hung module.
        const bool byTime = _trailUs >= ATT_TRAIL_MAX_MS * 1000UL;
        if (!byArc && !byTime)
            return;
        _trailArc = byArc ? (_trailArc - arcStep) : 0.0f;
        _trailUs = 0;

        _trailX[_trailHead] = (int8_t)constrain((int)lroundf(_norm[0] * 100.0f), -127, 127);
        _trailY[_trailHead] = (int8_t)constrain((int)lroundf(_norm[1] * 100.0f), -127, 127);
        _trailHead = (_trailHead + 1) % ATT_TRAIL_LEN;
        if (_trailCount < ATT_TRAIL_LEN)
            _trailCount++;
    }

    uint8_t _id = AttLorenz;
    float _p[ATT_MAX_PARAMS] = {10.0f, 28.0f, 2.667f, 0.0f};
    float _s[3] = {0.1f, 0.0f, 0.0f};
    float _speed = ATT_SPEED_DEFAULT;
    uint8_t _src[2] = {AxisX, AxisY};
    float _level = 1.0f;
    float _offset = 0.0f;
    float _smooth = 0.0f;
    bool _autoRange = false;
    int8_t _salt = 0;
    bool _diverged = false;

    float _norm[2] = {0.0f, 0.0f};
    float _smoothed[2] = {0.0f, 0.0f};

    float _amin[3] = {-1.0f, -1.0f, -1.0f};
    float _amax[3] = {1.0f, 1.0f, 1.0f};

    int8_t _trailX[ATT_TRAIL_LEN] = {0};
    int8_t _trailY[ATT_TRAIL_LEN] = {0};
    int _trailHead = 0;
    int _trailCount = 0;
    float _trailArc = 0.0f;
    unsigned long _trailUs = 0;
};

// ── Coupling ────────────────────────────────────────────────────────────────
// Strength 1.0 exchanges this fraction of the difference between the two orbits
// per second. 25 is a ~40 ms time constant: fast enough that two copies of one
// system visibly lock together, slow enough that two *different* systems are
// entrained rather than flattened.
#define ATT_COUPLE_RATE_PER_S 25.0f

class ChaosWorld {
  public:
    ChaosWorld() { Reseed(); }

    Generator &Get(int i) { return _g[constrain(i, 0, 1)]; }
    const Generator &Get(int i) const { return _g[constrain(i, 0, 1)]; }

    void SetCouple(float c01) { _couple = constrain(c01, 0.0f, 1.0f); }
    float GetCouple() const { return _couple; }

    // While frozen the orbits hold their exact state and the outputs hold their
    // exact voltage — the accumulator is not allowed to bank the time either, so
    // releasing FREEZE resumes rather than fast-forwarding through the pause.
    void SetFrozen(bool f) { _frozen = f; }
    bool IsFrozen() const { return _frozen; }

    void Reseed() {
        for (int i = 0; i < 2; i++)
            _g[i].Seed(i);
    }
    void Reseed(int i) { _g[constrain(i, 0, 1)].Seed(constrain(i, 0, 1)); }

    unsigned long SimUs() const { return _simUs; }

    // Advance to `nowUs`, in whole 1 ms steps.
    void Advance(unsigned long nowUs) {
        unsigned long elapsed = nowUs - _nowUs; // unsigned: wraps correctly
        _nowUs = nowUs;

        if (_frozen) {
            _accumUs = 0; // see SetFrozen: a pause is not time owed
            return;
        }

        // First call after boot, or a wild jump: do not try to make it up.
        if (elapsed > ATT_STEP_US * 1000UL)
            elapsed = ATT_STEP_US;
        _accumUs += elapsed;

        int steps = 0;
        while (_accumUs >= ATT_STEP_US && steps < ATT_MAX_STEPS_PER_CALL) {
            _accumUs -= ATT_STEP_US;
            steps++;
            StepOnce();
            _simUs += ATT_STEP_US;
        }
        if (steps >= ATT_MAX_STEPS_PER_CALL)
            _accumUs = 0; // give up on the backlog rather than chase it forever
    }

    // Exposed for the host tests, which drive the simulation directly rather
    // than through a clock.
    void StepOnce() {
        for (int i = 0; i < 2; i++)
            _g[i].Step();
        CoupleStep();
    }

  private:
    // Diffusive coupling, applied once per module step and on all three axes.
    //
    // Squared so that the low half of the control does something audible: the
    // interesting region — entrainment without lock — is narrow and sits near
    // the bottom, and a linear taper puts all of it in the first few percent.
    //
    // Symmetric: each orbit is pulled the same distance toward the other, so
    // neither is master. Two copies of one system converge to a single orbit
    // (their four outputs become two), which is the intended extreme and the
    // clearest way to hear what the control does.
    void CoupleStep() {
        if (_couple <= 0.0f)
            return;
        float k = _couple * _couple * ATT_COUPLE_RATE_PER_S * ATT_STEP_DT;
        if (k > 0.5f)
            k = 0.5f; // never overshoot past the midpoint in one step
        for (int a = 0; a < 3; a++) {
            const float d = (_g[1].NormalisedAxis(a) - _g[0].NormalisedAxis(a)) * k;
            _g[0].NudgeAxis(a, +d);
            _g[1].NudgeAxis(a, -d);
        }
    }

    Generator _g[2];
    float _couple = 0.0f;
    bool _frozen = false;
    unsigned long _nowUs = 0;
    unsigned long _accumUs = 0;
    unsigned long _simUs = 0;
};
