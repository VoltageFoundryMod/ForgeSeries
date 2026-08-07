#pragma once

// outputs.hpp — the output matrix (Design.md §5).
//
// Four jacks, each a slot naming a SOURCE, a TYPE and how to read the ring.
// This is the part of Enigma worth having: registers and outputs are decoupled,
// so the same two shift registers can be two voices, one voice plus two
// modulations, or a four-track drum machine, without any of those being a mode
// with its own code path.
//
// ROUTING is a macro over these slots, not a mode — see ROUTING_TEMPLATES
// below.

#ifdef UNIT_TEST
#include "ArduinoFake.h"
#else
#include <Arduino.h>
#endif

#include <stdint.h>

#include "boardPinouts.hpp" // MAXDAC
#include "quantizer.hpp"    // core
#include "shiftreg.hpp"

#define WEA_NUM_OUTS 4

// ── The jacks, as the panel names them ───────────────────────────────────────
// Four jacks in two rows of two. Index order is the physical reading order —
// top-left, top-right, bottom-left, bottom-right — and the panel labels run down
// the COLUMNS, matching ChaosForge:
//
//     A1   B1
//     A2   B2
//
// So the DUO routing, which puts register A on the left column and B on the
// right, has the panel spell out which register you are patching.
//
// NOTE the deliberate collision: "A" in a jack name is a COLUMN, while "A" in a
// SOURCE is a REGISTER. They agree in DUO, which is the point of that routing,
// and they are free to disagree in any other — a jack named A2 may perfectly
// well read register B. The jack name says where the cable goes; SOURCE says
// what comes out of it.
static const char *const OutJackNames[] = {"A1", "B1", "A2", "B2"};

enum OutType : uint8_t { OutNote = 0, OutMod, OutGate, OutTrig, OutTypeLength };
static const char *const OutTypeNames[] = {"NOTE", "MOD", "GATE", "TRIG"};

// One jack. Six fields, which is exactly the six rows a menu page holds — see
// Design.md §5; the last two are contextual on TYPE.
struct OutSlot {
    uint8_t source = SrcA;  // A / B / AB
    uint8_t type = OutNote; // NOTE / MOD / GATE / TRIG
    uint8_t depth = 5;      // bits read out of the ring, 1..8
    uint8_t rotate = 0;     // where in the ring the window sits
    // NOTE ▸ RANGE (octaves 1..5) · MOD ▸ LEVEL (%) · GATE/TRIG ▸ THRESH (%)
    uint8_t param = 2;
    // NOTE/MOD ▸ SLEW (%) · TRIG ▸ WIDTH (ms/2, so 1..255 covers 2..510 ms)
    uint8_t param2 = 0;
};

// ── ROUTING ──────────────────────────────────────────────────────────────────
// The three configurations the matrix is for. Selecting one stamps the four
// slots and nothing more: they stay individually editable afterwards, exactly
// the way a scale populates a note mask in core/scales.hpp and then steps out of
// the way.
enum Routing : uint8_t { RouteDuo = 0, RouteMono, RoutePulse, RoutingLength };
static const char *const RoutingNames[] = {"DUO", "MONO", "PULSE"};
#define WEA_ROUTING_CUSTOM 255

// Indexed [routing][jack].
static const OutSlot ROUTING_TEMPLATES[RoutingLength][WEA_NUM_OUTS] = {
    // DUO — two voices. Mirrors NoteForge's and GravityForge's jack map, so the
    // muscle memory survives across the series.
    {{SrcA, OutNote, 5, 0, 2, 0},
     {SrcB, OutNote, 5, 0, 2, 0},
     {SrcA, OutGate, 1, 0, 50, 0},
     {SrcB, OutGate, 1, 0, 50, 0}},
    // MONO — one voice plus two modulations. OUT 3 is rotated off the melody it
    // shares a register with; OUT 4 comes from B, so WEAVE slides it from
    // unrelated to locked with the voice (Design.md §4).
    {{SrcA, OutNote, 5, 0, 2, 0},
     {SrcA, OutGate, 1, 0, 50, 0},
     {SrcA, OutMod, 8, 8, 100, 20},
     {SrcB, OutMod, 8, 0, 100, 20}},
    // PULSE — the Turing Machine's Pulses expander, in software. Two tracks off
    // each register at different phases, with different densities.
    {{SrcA, OutTrig, 3, 0, 50, 5},
     {SrcA, OutTrig, 3, 8, 35, 5},
     {SrcB, OutTrig, 3, 0, 50, 5},
     {SrcB, OutTrig, 3, 8, 25, 5}},
};

inline bool SlotsEqual(const OutSlot &a, const OutSlot &b) {
    return a.source == b.source && a.type == b.type && a.depth == b.depth &&
           a.rotate == b.rotate && a.param == b.param && a.param2 == b.param2;
}

// Which ROUTING the four slots currently are, or WEA_ROUTING_CUSTOM.
//
// Recomputed rather than stored. A stored index goes stale the moment a slot is
// edited — from the menu, from a preset load, from Rack's randomiser — and a
// preset that happens to match DUO should say DUO rather than CUSTOM.
inline uint8_t RoutingOf(const OutSlot slots[WEA_NUM_OUTS]) {
    for (uint8_t r = 0; r < RoutingLength; r++) {
        bool match = true;
        for (int i = 0; i < WEA_NUM_OUTS && match; i++) {
            match = SlotsEqual(slots[i], ROUTING_TEMPLATES[r][i]);
        }
        if (match) {
            return r;
        }
    }
    return WEA_ROUTING_CUSTOM;
}

inline void ApplyRouting(OutSlot slots[WEA_NUM_OUTS], uint8_t routing) {
    if (routing >= RoutingLength) {
        return;
    }
    for (int i = 0; i < WEA_NUM_OUTS; i++) {
        slots[i] = ROUTING_TEMPLATES[routing][i];
    }
}

// ── The bank ─────────────────────────────────────────────────────────────────
// Slots plus the runtime state each jack carries between steps. One object, so
// the Rack port's engine_state.def has one entry to track rather than eight.
class OutputBank {
    OutSlot _slot[WEA_NUM_OUTS];

    // Runtime, not saved.
    float _value[WEA_NUM_OUTS] = {0, 0, 0, 0};  // smoothed DAC counts
    float _target[WEA_NUM_OUTS] = {0, 0, 0, 0}; // where SLEW is heading
    bool _gate[WEA_NUM_OUTS] = {false, false, false, false};
    unsigned long _trigUntilUs[WEA_NUM_OUTS] = {0, 0, 0, 0};
    int _lastNote[WEA_NUM_OUTS] = {-1, -1, -1, -1};

  public:
    OutSlot &Slot(int i) { return _slot[i & 3]; }
    const OutSlot &Slot(int i) const { return _slot[i & 3]; }
    OutSlot *Slots() { return _slot; }
    const OutSlot *Slots() const { return _slot; }

    bool GateHigh(int i) const { return _gate[i & 3]; }

    // Does this jack fire on this step? The DEPTH-bit window compared against
    // THRESH, which is what gives a gate output a density control instead of the
    // fixed ~50 % that reading bit 0 alone would give (Design.md §5).
    static bool Fires(uint8_t window, uint8_t depth, uint8_t thresh) {
        const uint16_t span = (uint16_t)1u << (depth == 0 ? 1 : depth);
        const uint16_t limit = (uint16_t)(((uint32_t)thresh * span) / 100u);
        return window < limit;
    }

    // One clock step: latch every jack's new value from the registers.
    void Step(const WeavePair &pair, const Quantizer &quant, int8_t transpose,
              uint8_t rotateMod, unsigned long nowUs) {
        for (int i = 0; i < WEA_NUM_OUTS; i++) {
            const OutSlot &s = _slot[i];
            const uint8_t rot = (uint8_t)(s.rotate + rotateMod);
            const uint8_t win = pair.Window(s.source, rot, s.depth);

            switch (s.type) {
            case OutNote: {
                // DEPTH is resolution, RANGE is span, and they stay independent:
                // 3 bits over 2 octaves is a singable line, 7 bits over the same
                // 2 octaves wanders finely through it.
                const int span = quant.DegreesPerOctave() * (int)(s.param < 1 ? 1 : s.param);
                const int idx = (int)(((uint32_t)win * (uint32_t)span) >>
                                      (s.depth == 0 ? 1 : s.depth));
                int semi = quant.SemitoneAt(idx) + transpose;
                semi = constrain(semi, 0, QUANT_MAX_SEMITONE);
                _lastNote[i] = semi;
                _target[i] = SemitonesToCounts((float)semi);
                break;
            }
            case OutMod: {
                const uint16_t span = (uint16_t)1u << (s.depth == 0 ? 1 : s.depth);
                const float unit = (float)win / (float)(span - 1 > 0 ? span - 1 : 1);
                _target[i] = unit * ((float)s.param / 100.0f) * (float)MAXDAC;
                break;
            }
            case OutGate:
                _gate[i] = Fires(win, s.depth, s.param);
                _target[i] = _gate[i] ? (float)MAXDAC : 0.0f;
                break;
            case OutTrig:
                if (Fires(win, s.depth, s.param)) {
                    // param2 is half-milliseconds so a byte reaches 510 ms; 0
                    // would be a pulse no downstream module could see.
                    const unsigned long widthUs =
                        (unsigned long)(s.param2 < 1 ? 1 : s.param2) * 2000UL;
                    _trigUntilUs[i] = nowUs + widthUs;
                }
                break;
            default:
                break;
            }
        }
    }

    // Every loop, whether or not a step landed: advances slew and expires
    // triggers. Trigger width is a wall-clock duration, not a number of steps,
    // so it has to be serviced far more often than the clock ticks.
    void Update(unsigned long nowUs) {
        for (int i = 0; i < WEA_NUM_OUTS; i++) {
            const OutSlot &s = _slot[i];

            if (s.type == OutTrig) {
                // Unsigned compare against a deadline in the past wraps
                // correctly; comparing the other way round does not.
                const bool high = _trigUntilUs[i] != 0 &&
                                  (long)(nowUs - _trigUntilUs[i]) < 0;
                _gate[i] = high;
                _value[i] = high ? (float)MAXDAC : 0.0f;
                continue;
            }

            if (s.type == OutGate) {
                _value[i] = _target[i]; // gates never slew — that is the point
                continue;
            }

            // NOTE / MOD: SLEW 0 is a jump, 100 is a long glide.
            if (s.param2 == 0) {
                _value[i] = _target[i];
            } else {
                const float coeff = 1.0f - ((float)s.param2 / 101.0f);
                _value[i] += coeff * (_target[i] - _value[i]);
            }
        }
    }

    uint16_t DacValue(int i) const {
        const float v = _value[i & 3];
        if (v <= 0.0f) {
            return 0;
        }
        if (v >= (float)MAXDAC) {
            return MAXDAC;
        }
        return (uint16_t)lroundf(v);
    }

    int LastNote(int i) const { return _lastNote[i & 3]; }

    // Where jack `i` reads, in combined-ring positions — what the home screen
    // draws its tap markers from.
    uint8_t TapPosition(int i, uint8_t rotateMod) const {
        const OutSlot &s = _slot[i & 3];
        const uint8_t rot = (uint8_t)(s.rotate + rotateMod);
        if (s.source == SrcB) {
            return (uint8_t)(WEA_REG_BITS + (rot & 15));
        }
        if (s.source == SrcAB) {
            return (uint8_t)(rot & 31);
        }
        return (uint8_t)(rot & 15);
    }
};
