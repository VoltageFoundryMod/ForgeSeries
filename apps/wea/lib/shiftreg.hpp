#pragma once

// shiftreg.hpp — WeaveForge's bit machinery.
//
// Two 16-bit shift registers and the coupling between them. This file is
// deliberately PURE: no Arduino, no I/O, no display, no menu state, no wall
// clock. Everything it does is a function of its own bits plus the arguments
// passed in, which is what lets the host test runner, the RP2040 and the VCV
// Rack port all produce the same sequence from the same seed.
//
// See docs/Design.md §2 (the register) and §3 (WEAVE).

#include <stdint.h>

// ── Geometry ─────────────────────────────────────────────────────────────────
#define WEA_REG_BITS 16
#define WEA_COMBINED_BITS (WEA_REG_BITS * 2)
#define WEA_MIN_LENGTH 2
#define WEA_MAX_LENGTH WEA_REG_BITS
#define WEA_MAX_DEPTH 8

// Which ring an output reads. AB is the two registers concatenated into one
// 32-position ring — the thing WEAVE at 100 % actually makes them.
//
// Lives here rather than in outputs.hpp because it names a topology this file
// owns; outputs.hpp only chooses between them.
enum RegSource : uint8_t { SrcA = 0, SrcB, SrcAB, RegSourceLength };
static const char *const RegSourceNames[] = {"A", "B", "AB"};

// Who feeds whom. Design.md §3 — one-way coupling is the useful one: with A▸B,
// B becomes a variation on A that cannot contaminate it.
enum WeaveDir : uint8_t { WeaveBoth = 0, WeaveAtoB, WeaveBtoA, WeaveDirLength };
static const char *const WeaveDirNames[] = {"BOTH", "A>B", "B>A"};

// ── Deterministic PRNG ───────────────────────────────────────────────────────
// The same xorshift GravityForge and ChaosForge carry, and for the same reason:
// Arduino random() would give a different sequence on the host, on the board and
// in Rack, and this module's whole premise is that a locked pattern is exactly
// reproducible.
class WeaveRandom {
    uint32_t _s = 0x2545F491u;

  public:
    void Seed(uint32_t s) { _s = s ? s : 0x2545F491u; }
    uint32_t State() const { return _s; }
    void SetState(uint32_t s) { _s = s ? s : 0x2545F491u; }

    uint32_t Next() {
        _s ^= _s << 13;
        _s ^= _s >> 17;
        _s ^= _s << 5;
        return _s;
    }

    // True with probability `pct`.
    //
    // 0 and 100 are answered WITHOUT drawing. That is not an optimisation: it is
    // what makes "CHANCE 0 locks the pattern" and "WEAVE 100 chains the two
    // registers" exact rather than merely very likely, and both are claims the
    // module is sold on. It also keeps a locked pattern from consuming the
    // sequence, so a second register's drift is unaffected by whether the first
    // one happens to be frozen.
    bool Percent(uint8_t pct) {
        if (pct == 0) {
            return false;
        }
        if (pct >= 100) {
            return true;
        }
        return (uint8_t)((Next() >> 8) % 100) < pct;
    }
};

// ── One register ─────────────────────────────────────────────────────────────
//
// The shift is Whitwell's, by way of Hemisphere's ShiftReg: the whole 16 bits
// move left and the fed-back bit enters at position 0. LENGTH chooses which bit
// is fed back, NOT how much of the register moves.
//
// That has a consequence worth understanding before touching this, because it
// looks like a bug and is not (Design.md §2): bits above the feedback point are
// not preserved. They keep marching up and fall off the top at position 15,
// while position N-1 keeps copying what passes through it into the region above.
// So the upper region is a running copy of the loop — at CHANCE 0 it is the
// same loop at a different phase, and with CHANCE up it is the loop's recent
// past. It is a delay line, not an attic. Shortening LENGTH and lengthening it
// again does NOT bring the longer pattern back; that is what preset slots are
// for, and the register contents are part of a preset for exactly this reason.
class ShiftRegister {
    uint16_t _reg = 0xACE1u; // any non-trivial seed; overwritten at Begin()
    uint8_t _length = WEA_MAX_LENGTH;

  public:
    void SetLength(uint8_t n) {
        _length = n < WEA_MIN_LENGTH  ? WEA_MIN_LENGTH
                  : n > WEA_MAX_LENGTH ? WEA_MAX_LENGTH
                                       : n;
    }
    uint8_t Length() const { return _length; }

    uint16_t Value() const { return _reg; }
    void SetValue(uint16_t v) { _reg = v; }

    // The bit about to wrap — the one LENGTH selects and CHANCE may flip.
    bool Tail() const { return (_reg >> (_length - 1)) & 1u; }

    bool Bit(uint8_t i) const { return (_reg >> (i & 15)) & 1u; }
    void FlipBit(uint8_t i) { _reg ^= (uint16_t)(1u << (i & 15)); }

    // Shift left, `bit` enters at position 0. The caller has already decided
    // what that bit is — WeavePair::Clock() owns that decision.
    void ShiftIn(bool bit) {
        _reg = (uint16_t)((_reg << 1) | (bit ? 1u : 0u));
    }

    // `depth` bits read out of the ring starting at `rotate`, wrapping. Window
    // bit 0 is register bit `rotate`, so the low end of the window is always the
    // newer end whatever the rotation.
    uint8_t Window(uint8_t rotate, uint8_t depth) const {
        return (uint8_t)(RotatedRight(rotate) & DepthMask(depth));
    }

    // Register editing, backing the home-screen actions (Design.md §2).
    void Clear() { _reg = 0; }
    void Fill() { _reg = 0xFFFFu; }
    void Invert() { _reg = (uint16_t)~_reg; }
    void Randomize(WeaveRandom &rng) { _reg = (uint16_t)(rng.Next() >> 8); }

  private:
    uint16_t RotatedRight(uint8_t rotate) const {
        const uint8_t r = rotate & 15;
        const uint32_t v = _reg;
        return (uint16_t)((v >> r) | (v << (WEA_REG_BITS - r)));
    }

    static uint8_t DepthMask(uint8_t depth) {
        const uint8_t d = depth == 0                ? 1
                          : depth > WEA_MAX_DEPTH ? WEA_MAX_DEPTH
                                                    : depth;
        return (uint8_t)((1u << d) - 1u);
    }
};

// ── The pair, and the weave ──────────────────────────────────────────────────
class WeavePair {
    ShiftRegister _reg[2];
    WeaveRandom _rng;

    // Bit i: register i took the OTHER register's bit on the last Clock().
    // Observation only — nothing in here reads it back, so it cannot change a
    // sequence, and this file stays a pure function of its inputs.
    uint8_t _crossed = 0;

  public:
    ShiftRegister &Reg(uint8_t i) { return _reg[i & 1]; }
    const ShiftRegister &Reg(uint8_t i) const { return _reg[i & 1]; }
    WeaveRandom &Rng() { return _rng; }

    void Seed(uint32_t s) { _rng.Seed(s); }

    // Did register `i` receive a foreign bit on the last clock? WEAVE is a
    // probability, so the screen showing the SETTING says what is likely and the
    // screen showing this says what happened — and at 35 % those are different
    // pictures on most steps. The loom draws a courier crossing the channel from
    // it (menuRender.hpp).
    bool Crossed(uint8_t i) const { return (_crossed >> (i & 1)) & 1u; }

    // One clock edge for both registers.
    //
    // `weavePct` is the probability that a register takes the OTHER register's
    // outgoing bit instead of its own; `chancePct` is the per-register
    // probability that whichever bit was chosen is then flipped.
    void Clock(uint8_t weavePct, uint8_t dir, const uint8_t chancePct[2]) {
        // BOTH tails are sampled before EITHER register shifts. Shifting A first
        // and then reading its tail for B would hand B a bit one clock too new,
        // and the 2N chain at WEAVE 100 % would collapse to something shorter
        // and asymmetric. This is the one ordering bug this file can have.
        const bool tail[2] = {_reg[0].Tail(), _reg[1].Tail()};

        bool incoming[2];
        _crossed = 0;
        for (uint8_t i = 0; i < 2; i++) {
            const uint8_t other = i ^ 1;

            // Can this register receive at all? A▸B means only B receives.
            const bool receives = (dir == WeaveBoth) ||
                                  (dir == WeaveAtoB && i == 1) ||
                                  (dir == WeaveBtoA && i == 0);

            // No draw at all when the register cannot receive, so a one-way
            // weave leaves the sender's stream bit-identical to running alone.
            const bool foreign = receives && _rng.Percent(weavePct);
            if (foreign) {
                _crossed |= (uint8_t)(1u << i);
            }

            bool bit = foreign ? tail[other] : tail[i];
            if (_rng.Percent(chancePct[i])) {
                bit = !bit;
            }
            incoming[i] = bit;
        }

        _reg[0].ShiftIn(incoming[0]);
        _reg[1].ShiftIn(incoming[1]);
    }

    // The two registers as one 32-position ring: A in the low half, B in the
    // high half. This is the ring WEAVE at 100 % actually creates, and it is
    // what an output sourced from AB reads.
    uint32_t Combined() const {
        return (uint32_t)_reg[0].Value() |
               ((uint32_t)_reg[1].Value() << WEA_REG_BITS);
    }

    // `depth` bits out of the combined ring, starting at `rotate`, wrapping at
    // 32 rather than 16 — so a window near the top spans the A/B boundary, which
    // is the point.
    uint8_t CombinedWindow(uint8_t rotate, uint8_t depth) const {
        const uint8_t r = rotate & 31;
        const uint64_t v = Combined();
        const uint64_t rotated =
            (v >> r) | (v << (WEA_COMBINED_BITS - r));
        const uint8_t d = depth == 0                ? 1
                          : depth > WEA_MAX_DEPTH ? WEA_MAX_DEPTH
                                                    : depth;
        return (uint8_t)(rotated & ((1u << d) - 1u));
    }

    // What an output slot reads: the window its SOURCE / ROTATE / DEPTH select.
    uint8_t Window(uint8_t source, uint8_t rotate, uint8_t depth) const {
        if (source == SrcAB) {
            return CombinedWindow(rotate, depth);
        }
        return _reg[source == SrcB ? 1 : 0].Window(rotate, depth);
    }
};
