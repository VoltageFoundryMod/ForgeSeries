#pragma once

// physics.hpp — the gravity sequencer's simulation.
//
// Two circular containers, each holding a handful of balls. Gravity pulls the
// balls down, the container wall rotates and drags them around, and every wall
// strike lands on one of N pegs spaced evenly around the rim. A strike on an
// enabled peg is a note event.
//
// ── Why the pegs are on the wall ─────────────────────────────────────────────
// Free-floating pegs would mean testing every ball against every peg every step
// — O(balls × pegs), and that is the one thing that does not fit on a 133 MHz
// Cortex-M0+ with no FPU. Pegs on the wall are both closer to the original
// Tombola (the notes ring the container) and O(1): the wall collision is
// already being computed, so the contact angle is free and the peg index is one
// division. See docs/Design.md §2.
//
// ── Why float ────────────────────────────────────────────────────────────────
// Steady-state cost is ~14 soft-float ops per ball per step (integrate + the
// `dx²+dy² > R²` wall test). At 8 balls × 2 containers × 1 kHz that is ~8 % of
// one core. The expensive calls — sqrtf, atan2f — run only on an actual
// collision, which is a few events per second per ball, not per step. Fixed
// point would buy speed the module does not need at the cost of readability and
// testability. See docs/Design.md §3.
//
// ── Determinism ──────────────────────────────────────────────────────────────
// Fixed 1 ms timestep, a self-contained PRNG, and time passed in as a parameter
// rather than read from a wall clock. That is what makes this unit-testable on
// the host and what makes the VCV Rack port behave identically under
// faster-than-realtime rendering.

#ifdef UNIT_TEST
#include "ArduinoFake.h"
#else
#include <Arduino.h>
#endif

#include <math.h>
#include <stdint.h>

// ── Geometry (screen pixels — the sim works directly in display space so the
// renderer never has to transform, and what you see is literally the state) ──
#define PHYS_R 20.0f          // container radius
// Ball radius. This is also exactly what the renderer draws, which is the point
// of working in screen pixels: the ball you see is the ball being simulated, so
// it never appears to sink into or float off the wall.
#define PHYS_BALL_R 2.0f
#define PHYS_CY 32.0f         // both containers sit on the screen midline
#define PHYS_CX_CENTER 64.0f  // screen centre

// Centre separation at PROXIMITY 0 % (fully apart) and 100 % (fully merged).
// D_MAX leaves an 8 px gap between the rims so "apart" reads unambiguously.
#define PHYS_D_MAX 48.0f
#define PHYS_D_MIN 0.0f

#define PHYS_MIN_BALLS 1
#define PHYS_MAX_BALLS 8
#define PHYS_MIN_PEGS 3
#define PHYS_MAX_PEGS 16

// Fixed simulation timestep. 1 kHz is comfortably above the ~200 Hz at which a
// bouncing ball at these speeds would start tunnelling through the wall.
#define PHYS_STEP_US 1000UL
#define PHYS_STEP_DT 0.001f

// Hard cap on catch-up steps in one Advance() call. A display stall or a slow
// I2C write must never spiral into an unbounded catch-up loop that starves the
// audio-rate work — better to drop simulated time than to lock up.
#define PHYS_MAX_STEPS_PER_CALL 8

// Re-trigger guards. A ball resting on the wall would otherwise fire its peg
// every single step. Same peg needs the full refractory; moving on to a
// different peg only needs the short one, so a ball skittering along the rim
// still speaks every note it passes.
//
// The "different peg" window is not as generous as it looks: with the container
// rotating, the peg ring sweeps underneath a resting ball, so every contact
// looks like a *new* peg. That path is what turned the GATE jack into a ~33 Hz
// sawtooth, and it is why PEG_MIN_IMPACT_SPEED below exists as the real filter.
#define PEG_REFRACTORY_US 45000UL
#define PEG_MIN_INTERVAL_US 12000UL

// A peg only speaks when it is actually *struck*. Normal impact speed below this
// is a graze or a ball settling against the rim, not a hit.
//
// This is the difference between a sequencer and a buzzer. Without it a ball
// resting at the bottom of a rotating container registers a contact every step,
// and since rotation keeps changing which peg it is resting on, the short
// re-trigger window lets it fire continuously — the envelope never gets to
// finish and the gate output never returns to zero.
#define PEG_MIN_IMPACT_SPEED 28.0f

// A ball that has lost its energy gets topped back up to this speed, so a patch
// never dies in a silent pile at the bottom.
//
// It has to be comfortably ABOVE PEG_MIN_IMPACT_SPEED, or the floor would
// re-energise balls to a speed that can no longer trigger anything and the
// container would go quiet — the exact failure the floor exists to prevent.
// It also sets the idle rhythm: at gravity 220 a 60 px/s bounce hangs for
// 2*60/220 = 0.55 s, so a settled ball ticks along at about 2 Hz rather than
// vibrating against the wall.
#define PHYS_MIN_BOUNCE_SPEED 60.0f

static const float PHYS_TWO_PI = 6.28318530718f;

// ── Deterministic PRNG ───────────────────────────────────────────────────────
// Self-contained rather than Arduino random(): the sim must produce the same
// sequence on the host test runner, on the RP2040 and inside Rack.
class PhysRandom {
    uint32_t _s = 0x2545F491u;

  public:
    void Seed(uint32_t s) { _s = s ? s : 0x2545F491u; }
    uint32_t Next() {
        _s ^= _s << 13;
        _s ^= _s >> 17;
        _s ^= _s << 5;
        return _s;
    }
    // Uniform in [-1, 1).
    float Bipolar() { return (float)(int32_t)Next() / 2147483648.0f; }
    // Uniform in [0, 1).
    float Unit() { return (float)(Next() >> 8) / 16777216.0f; }
};

struct Ball {
    float x = 0, y = 0;
    float vx = 0, vy = 0;
    int8_t lastPeg = -1;
    unsigned long lastHitUs = 0;
};

// A wall strike, reported out of Container::Step() so PhysicsWorld can decide
// whether it also couples into the neighbouring container.
struct Contact {
    float x, y;   // contact point, world/screen space
    float nx, ny; // outward wall normal at the contact
    float energy; // normal impact speed — how hard the hit was
};

// ─────────────────────────────────────────────────────────────────────────────
// Container — one rotating bowl of balls with a ring of pegs on its rim.
// ─────────────────────────────────────────────────────────────────────────────
class Container {
    // ── Tunables (menu-facing; set via the Set* API) ──
    float _gravity = 220.0f;    // px/s² downward
    float _restitution = 0.72f; // 0 = dead, 1 = lossless
    float _spinGrip = 0.30f;    // how strongly the moving wall drags a ball
    float _omega = 0.0f;        // rad/s, signed
    int _ballCount = 3;
    int _pegCount = 8;
    uint16_t _pegMask = 0xFFFF; // bit i = peg i emits a note

    // ── State ──
    float _cx = PHYS_CX_CENTER, _cy = PHYS_CY;
    float _rotation = 0.0f;
    Ball _balls[PHYS_MAX_BALLS];
    PhysRandom _rng;

    // Last peg hit, consumed by the sequencer layer. Last-wins within a step:
    // two balls landing in the same millisecond is rare, and stacking them would
    // produce a retrigger burst rather than something that sounds like the
    // physics that caused it.
    bool _hitPending = false;
    int _hitPeg = 0;
    float _hitEnergy = 0.0f;

    // Rolling activity measure, used by the display and available as a
    // modulation source. Decays continuously so it reads as "how busy is this
    // container right now".
    float _activity = 0.0f;

    // Peg flash timers for the renderer (frames of inverted drawing left).
    uint8_t _pegFlash[PHYS_MAX_PEGS] = {0};

  public:
    Container() { Reset(0x12345678u); }

    // ── Parameters ───────────────────────────────────────────────────────────
    void SetGravity(float g) { _gravity = constrain(g, 0.0f, 1200.0f); }
    void SetRestitution(float e) { _restitution = constrain(e, 0.05f, 0.99f); }
    void SetSpinGrip(float g) { _spinGrip = constrain(g, 0.0f, 1.0f); }
    void SetOmega(float w) { _omega = constrain(w, -25.0f, 25.0f); }
    void SetCentre(float cx, float cy) { _cx = cx; _cy = cy; }

    void SetBallCount(int n) {
        int want = constrain(n, PHYS_MIN_BALLS, PHYS_MAX_BALLS);
        // Newly added balls have never been placed, so drop them in rather than
        // leaving them at (0,0) — which is outside the container and would make
        // them teleport to the rim on the first step.
        for (int i = _ballCount; i < want; i++) {
            PlaceBall(i);
        }
        _ballCount = want;
    }

    void SetPegCount(int n) {
        int want = constrain(n, PHYS_MIN_PEGS, PHYS_MAX_PEGS);
        if (want == _pegCount) {
            return;
        }
        _pegCount = want;
        // Peg indices are about to mean something different, so a ball's memory
        // of "the last peg I hit" is stale. Clearing it prevents a spurious
        // suppression on the next strike.
        for (int i = 0; i < PHYS_MAX_BALLS; i++) {
            _balls[i].lastPeg = -1;
        }
    }

    void SetPegMask(uint16_t mask) { _pegMask = mask; }
    void SetPegEnabled(int peg, bool on) {
        if (peg < 0 || peg >= PHYS_MAX_PEGS) {
            return;
        }
        if (on) {
            _pegMask |= (uint16_t)(1u << peg);
        } else {
            _pegMask &= (uint16_t)~(1u << peg);
        }
    }

    float GetGravity() const { return _gravity; }
    float GetRestitution() const { return _restitution; }
    float GetSpinGrip() const { return _spinGrip; }
    float GetOmega() const { return _omega; }
    int GetBallCount() const { return _ballCount; }
    int GetPegCount() const { return _pegCount; }
    uint16_t GetPegMask() const { return _pegMask; }
    bool GetPegEnabled(int peg) const {
        return peg >= 0 && peg < PHYS_MAX_PEGS && ((_pegMask >> peg) & 1u);
    }

    // ── Read-only state for the renderer ─────────────────────────────────────
    float CentreX() const { return _cx; }
    float CentreY() const { return _cy; }
    float Rotation() const { return _rotation; }
    float Activity() const { return _activity; }
    const Ball &GetBall(int i) const { return _balls[constrain(i, 0, PHYS_MAX_BALLS - 1)]; }
    uint8_t PegFlash(int i) const { return (i >= 0 && i < PHYS_MAX_PEGS) ? _pegFlash[i] : 0; }
    void DecayPegFlash() {
        for (int i = 0; i < PHYS_MAX_PEGS; i++) {
            if (_pegFlash[i]) {
                _pegFlash[i]--;
            }
        }
    }

    // World position of peg i, for drawing.
    void PegPosition(int i, float &px, float &py) const {
        float a = _rotation + PHYS_TWO_PI * (float)i / (float)_pegCount;
        px = _cx + cosf(a) * PHYS_R;
        py = _cy + sinf(a) * PHYS_R;
    }

    // ── Events ───────────────────────────────────────────────────────────────
    bool ConsumeHit(int &peg, float &energy) {
        if (!_hitPending) {
            return false;
        }
        _hitPending = false;
        peg = _hitPeg;
        energy = _hitEnergy;
        return true;
    }

    // Re-place every ball and zero the rotation. The RESET gesture.
    void Reset(uint32_t seed) {
        _rng.Seed(seed);
        _rotation = 0.0f;
        _activity = 0.0f;
        _hitPending = false;
        for (int i = 0; i < PHYS_MAX_BALLS; i++) {
            PlaceBall(i);
        }
    }

    // Impulse every ball — the KICK gesture. Deliberately randomised per ball so
    // a kick scatters the population instead of moving it as one rigid clump,
    // which would just repeat the same rhythm louder.
    void Kick(float strength) {
        for (int i = 0; i < _ballCount; i++) {
            _balls[i].vx += _rng.Bipolar() * strength;
            _balls[i].vy -= (0.4f + _rng.Unit() * 0.6f) * strength;
        }
    }

    // Push balls away from a point — how a collision in the neighbouring
    // container transmits through the overlap region.
    void ApplyImpulseNear(float px, float py, float strength, float radius) {
        if (strength <= 0.0f || radius <= 0.0f) {
            return;
        }
        for (int i = 0; i < _ballCount; i++) {
            float dx = _balls[i].x - px;
            float dy = _balls[i].y - py;
            float d2 = dx * dx + dy * dy;
            if (d2 > radius * radius) {
                continue;
            }
            float d = sqrtf(d2);
            // Falls off linearly to zero at `radius`. A ball sitting exactly on
            // the contact point has no defined direction, so nudge it upward
            // rather than dividing by zero.
            float falloff = 1.0f - (d / radius);
            if (d < 0.0001f) {
                _balls[i].vy -= strength * falloff;
            } else {
                _balls[i].vx += (dx / d) * strength * falloff;
                _balls[i].vy += (dy / d) * strength * falloff;
            }
        }
    }

    // Ring the peg nearest a point on the rim, as if the wall had been struck
    // there — used when a strike in the neighbouring container transmits through
    // the overlap. The note is entirely this container's own: its peg ring, its
    // scale, its muted pegs. Nothing is copied across, so a transmitted strike
    // can never sound out of key.
    //
    // The refractory is tracked separately from the per-ball one, because there
    // is no ball behind this hit; without its own guard a burst of transmitted
    // strikes could retrigger the same peg every step.
    void RingPegNear(float px, float py, float energy, unsigned long nowUs) {
        float dx = px - _cx;
        float dy = py - _cy;
        if (dx * dx + dy * dy < 0.0001f) {
            return; // dead centre — no direction, so no peg to pick
        }
        int peg = PegAtContact(dx, dy);

        unsigned long since = nowUs - _coupleHitUs; // unsigned: wraps correctly
        unsigned long needed = (peg == _couplePeg) ? PEG_REFRACTORY_US : PEG_MIN_INTERVAL_US;
        if (_couplePeg >= 0 && since < needed) {
            return;
        }
        _couplePeg = (int8_t)peg;
        _coupleHitUs = nowUs;

        _activity += 1.0f;
        if (_activity > 20.0f) {
            _activity = 20.0f;
        }

        if (!GetPegEnabled(peg)) {
            return; // a muted peg absorbs the transfer silently
        }
        _pegFlash[peg] = 3;
        _hitPending = true;
        _hitPeg = peg;
        _hitEnergy = energy;
    }

    // ── Simulation ───────────────────────────────────────────────────────────
    // One fixed 1 ms step. Contacts are written out so the caller (PhysicsWorld)
    // can decide whether they also couple into the other container.
    void Step(unsigned long nowUs, Contact *contacts, int &contactCount, int maxContacts) {
        const float dt = PHYS_STEP_DT;

        _rotation += _omega * dt;
        // Keep the angle bounded — it feeds atan2f/cosf and would slowly lose
        // precision if left to grow without limit over hours of running.
        if (_rotation >= PHYS_TWO_PI) {
            _rotation -= PHYS_TWO_PI;
        } else if (_rotation < 0.0f) {
            _rotation += PHYS_TWO_PI;
        }

        // Activity decays toward zero with a ~0.5 s time constant.
        _activity -= _activity * 2.0f * dt;
        if (_activity < 0.0f) {
            _activity = 0.0f;
        }

        const float limit = PHYS_R - PHYS_BALL_R;

        for (int i = 0; i < _ballCount; i++) {
            Ball &b = _balls[i];

            // Semi-implicit Euler: velocity first, then position. More stable
            // than explicit Euler for this kind of bounded oscillatory motion.
            b.vy += _gravity * dt;
            b.x += b.vx * dt;
            b.y += b.vy * dt;

            float dx = b.x - _cx;
            float dy = b.y - _cy;
            float d2 = dx * dx + dy * dy;
            if (d2 <= limit * limit) {
                continue; // still in free flight — the common case, kept cheap
            }

            float d = sqrtf(d2);
            if (d < 0.0001f) {
                continue; // degenerate; cannot define a normal
            }
            float nx = dx / d;
            float ny = dy / d;

            // Project back onto the wall so the ball cannot sink into it and
            // stick there re-colliding every step.
            b.x = _cx + nx * limit;
            b.y = _cy + ny * limit;

            // The wall is rotating, so the contact point is moving. Working in
            // the wall's frame is what lets rotation actually stir the balls
            // rather than spin a decorative ring behind them.
            float wvx = -_omega * dy;
            float wvy = _omega * dx;
            float rvx = b.vx - wvx;
            float rvy = b.vy - wvy;

            float vn = rvx * nx + rvy * ny; // >0 means moving out through the wall
            if (vn > 0.0f) {
                // Reflect the normal component, losing (1-restitution) of it.
                rvx -= (1.0f + _restitution) * vn * nx;
                rvy -= (1.0f + _restitution) * vn * ny;

                // Grip: bleed off tangential velocity relative to the wall, so
                // the wall drags the ball along with it.
                float tvx = rvx - (rvx * nx + rvy * ny) * nx;
                float tvy = rvy - (rvx * nx + rvy * ny) * ny;
                rvx -= _spinGrip * tvx;
                rvy -= _spinGrip * tvy;

                b.vx = rvx + wvx;
                b.vy = rvy + wvy;

                // Energy floor — see PHYS_MIN_BOUNCE_SPEED.
                float sp = sqrtf(b.vx * b.vx + b.vy * b.vy);
                if (sp < PHYS_MIN_BOUNCE_SPEED) {
                    b.vx -= nx * PHYS_MIN_BOUNCE_SPEED;
                    b.vy -= ny * PHYS_MIN_BOUNCE_SPEED;
                }

                // Only a real strike speaks. Grazes and settling contacts are
                // still resolved as collisions above (and still transmit through
                // the coupling below), they just do not make a note.
                float energy = vn;
                if (energy >= PEG_MIN_IMPACT_SPEED) {
                    RegisterPegHit(b, dx, dy, energy, nowUs);
                }

                if (contactCount < maxContacts) {
                    contacts[contactCount].x = b.x;
                    contacts[contactCount].y = b.y;
                    contacts[contactCount].nx = nx;
                    contacts[contactCount].ny = ny;
                    contacts[contactCount].energy = energy;
                    contactCount++;
                }
            } else {
                // Moving inward already (usually just been pushed by coupling) —
                // reposition only, do not fire a peg.
                b.vx = rvx + wvx;
                b.vy = rvy + wvy;
            }
        }
    }

    // Which peg is at this contact angle? Exposed for unit testing.
    int PegAtContact(float dx, float dy) const {
        float a = atan2f(dy, dx) - _rotation;
        a = fmodf(a, PHYS_TWO_PI);
        if (a < 0.0f) {
            a += PHYS_TWO_PI;
        }
        int peg = (int)(a / (PHYS_TWO_PI / (float)_pegCount));
        return constrain(peg, 0, _pegCount - 1);
    }

  private:
    // Refractory state for transmitted strikes, kept apart from the per-ball one
    // because a transmitted hit has no ball behind it.
    int8_t _couplePeg = -1;
    unsigned long _coupleHitUs = 0;

    void PlaceBall(int i) {
        // Drop from the upper half, inside the rim, with a small random sideways
        // push so identical containers do not evolve identically.
        float a = -PHYS_TWO_PI * 0.25f + _rng.Bipolar() * 1.0f;
        float r = (PHYS_R - PHYS_BALL_R) * (0.25f + _rng.Unit() * 0.45f);
        _balls[i].x = _cx + cosf(a) * r;
        _balls[i].y = _cy + sinf(a) * r;
        _balls[i].vx = _rng.Bipolar() * 30.0f;
        _balls[i].vy = _rng.Unit() * 10.0f;
        _balls[i].lastPeg = -1;
        _balls[i].lastHitUs = 0;
    }

    void RegisterPegHit(Ball &b, float dx, float dy, float energy, unsigned long nowUs) {
        int peg = PegAtContact(dx, dy);

        unsigned long since = nowUs - b.lastHitUs; // unsigned: wraps correctly
        unsigned long needed = (peg == b.lastPeg) ? PEG_REFRACTORY_US : PEG_MIN_INTERVAL_US;
        if (b.lastPeg >= 0 && since < needed) {
            return;
        }
        b.lastPeg = (int8_t)peg;
        b.lastHitUs = nowUs;

        _activity += 1.0f;
        if (_activity > 20.0f) {
            _activity = 20.0f;
        }

        if (!GetPegEnabled(peg)) {
            return; // a muted peg is a silent bounce, not a note
        }
        _pegFlash[peg] = 3;
        _hitPending = true;
        _hitPeg = peg;
        _hitEnergy = energy;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// PhysicsWorld — both containers plus the proximity coupling between them.
//
// PROXIMITY slides the two centres together. Apart, the containers are wholly
// independent. Overlapping, a wall strike whose contact point falls inside the
// *other* container transmits an impulse into it, scaled by how much they
// overlap. Merged, they share one space. See docs/Design.md §5.
//
// This is energy transfer, not ball transfer: ball counts stay stable and each
// container keeps its own scale and peg layout. Ball transfer ("portals") is a
// natural extension of the same geometry and is deliberately deferred —
// OverlapArc() below is what it would build on.
// ─────────────────────────────────────────────────────────────────────────────
class PhysicsWorld {
    Container _c[2];
    float _proximity = 0.0f; // 0..1
    float _coupling = 0.6f;  // 0..1, how much of the overlap actually transmits
    unsigned long _accumUs = 0;
    unsigned long _nowUs = 0;
    uint32_t _resetSeed = 0x9E3779B9u;

  public:
    PhysicsWorld() { Reset(); }

    Container &Get(int i) { return _c[constrain(i, 0, 1)]; }
    const Container &Get(int i) const { return _c[constrain(i, 0, 1)]; }

    void SetProximity(float p) {
        _proximity = constrain(p, 0.0f, 1.0f);
        UpdateCentres();
    }
    void SetCoupling(float c) { _coupling = constrain(c, 0.0f, 1.0f); }
    float GetProximity() const { return _proximity; }
    float GetCoupling() const { return _coupling; }

    // Centre separation at the current proximity.
    float Separation() const {
        return PHYS_D_MAX - _proximity * (PHYS_D_MAX - PHYS_D_MIN);
    }

    // How much the two containers overlap, 0 (apart) .. 1 (concentric). This is
    // the coupling strength before the user's COUPLE amount is applied.
    float Overlap() const {
        float k = (2.0f * PHYS_R - Separation()) / (2.0f * PHYS_R);
        return constrain(k, 0.0f, 1.0f);
    }

    // Half-angle of the lens where the two rims cross, measured at either
    // centre. Zero when they do not reach each other. Unused by the coupling
    // itself — it is here because it is exactly what a future ball-transfer
    // "portal" needs to know, and it is cheap to keep correct while the
    // geometry is fresh.
    float OverlapArc() const {
        float d = Separation();
        if (d >= 2.0f * PHYS_R) {
            return 0.0f; // rims do not reach
        }
        if (d <= 0.0f) {
            return PHYS_TWO_PI * 0.5f; // concentric — the whole rim is shared
        }
        // Equal radii: the crossing points sit at half the separation, so the
        // half-angle is acos((d/2)/R).
        float c = (d * 0.5f) / PHYS_R;
        return acosf(constrain(c, -1.0f, 1.0f));
    }

    void Reset() {
        _resetSeed = _resetSeed * 1664525u + 1013904223u;
        _c[0].Reset(_resetSeed);
        _c[1].Reset(_resetSeed ^ 0xA5A5A5A5u);
        UpdateCentres();
    }

    void Kick(float strength) {
        _c[0].Kick(strength);
        _c[1].Kick(strength);
    }

    // Advance the simulation to `nowUs`, in fixed 1 ms steps.
    //
    // Catch-up is capped: if the caller was starved (a long display flush, a
    // slow I2C write), dropping simulated time is strictly better than an
    // unbounded loop that starves everything else in turn.
    void Advance(unsigned long nowUs) {
        unsigned long elapsed = nowUs - _nowUs; // unsigned: wraps correctly
        _nowUs = nowUs;

        // First call after boot, or a wild jump: do not try to make it up.
        if (elapsed > PHYS_STEP_US * 1000UL) {
            elapsed = PHYS_STEP_US;
        }
        _accumUs += elapsed;

        int steps = 0;
        while (_accumUs >= PHYS_STEP_US && steps < PHYS_MAX_STEPS_PER_CALL) {
            _accumUs -= PHYS_STEP_US;
            steps++;
            StepOnce(_nowUs);
        }
        if (steps >= PHYS_MAX_STEPS_PER_CALL) {
            _accumUs = 0; // give up on the backlog rather than chase it forever
        }
    }

  private:
    void UpdateCentres() {
        float half = Separation() * 0.5f;
        _c[0].SetCentre(PHYS_CX_CENTER - half, PHYS_CY);
        _c[1].SetCentre(PHYS_CX_CENTER + half, PHYS_CY);
    }

  public:
    // How many cross-container impulses have actually been transmitted. Purely
    // diagnostic — it is what tells the difference between "coupling is wired up
    // but the effect is too weak to feel" and "coupling never fires at all".
    unsigned long CouplingEvents() const { return _couplingEvents; }

    // ── Coupling spark, for the renderer ──
    // Where the last transmitted strike landed, and a short countdown so it can
    // be drawn for a few frames.
    //
    // Without this the coupling is real but invisible: it fires a handful of
    // times a second while both containers are already bouncing several times a
    // second, so there is nothing to tie the cause to the effect and it reads as
    // "proximity does nothing". Measured, it displaces balls by ~13 px — the
    // effect was never weak, only unattributable.
    bool CoupleFlashActive() const { return _coupleFlash > 0; }
    uint8_t CoupleFlash() const { return _coupleFlash; }
    float CoupleX() const { return _coupleX; }
    float CoupleY() const { return _coupleY; }
    void DecayCoupleFlash() {
        if (_coupleFlash) {
            _coupleFlash--;
        }
    }

  private:
    unsigned long _couplingEvents = 0;
    float _coupleX = 0.0f, _coupleY = 0.0f;
    uint8_t _coupleFlash = 0;

    void StepOnce(unsigned long nowUs) {
        Contact contacts[2][PHYS_MAX_BALLS];
        int counts[2] = {0, 0};

        _c[0].Step(nowUs, contacts[0], counts[0], PHYS_MAX_BALLS);
        _c[1].Step(nowUs, contacts[1], counts[1], PHYS_MAX_BALLS);

        float k = Overlap() * _coupling;
        if (k <= 0.0f) {
            return; // containers apart (or coupling off) — nothing to transmit
        }

        // A strike only transmits if it happened on the part of the wall the two
        // containers share, i.e. the contact point is inside the other circle.
        for (int src = 0; src < 2; src++) {
            int dst = 1 - src;
            for (int i = 0; i < counts[src]; i++) {
                const Contact &ct = contacts[src][i];
                float dx = ct.x - _c[dst].CentreX();
                float dy = ct.y - _c[dst].CentreY();
                if (dx * dx + dy * dy > PHYS_R * PHYS_R) {
                    continue; // struck the far wall — the neighbour never feels it
                }
                _couplingEvents++;
                _coupleX = ct.x;
                _coupleY = ct.y;
                _coupleFlash = 4;
                _c[dst].ApplyImpulseNear(ct.x, ct.y, ct.energy * k * 0.75f, PHYS_R);

                // ...and the receiving rim rings where the energy arrived, so the
                // transfer is heard as well as seen.
                //
                // Scaling by k before the impact test gives COUPLE a musical
                // gradient rather than an on/off: a weak coupling only nudges the
                // trajectories, and the containers start answering each other
                // audibly only as it is turned up.
                float transmitted = ct.energy * k;
                if (transmitted >= PEG_MIN_IMPACT_SPEED) {
                    _c[dst].RingPegNear(ct.x, ct.y, transmitted, nowUs);
                }
            }
        }
    }
};
