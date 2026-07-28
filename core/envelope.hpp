#pragma once

// envelope.hpp — Gate/envelope generator for a quantizer channel's GATE output.
//
// Each channel drives one DAC jack with a shaped envelope, a fixed-width
// trigger, or a straight gate. On the SAMD21 hardware this was an inverted PWM
// pin stepping through a lookup table on a fixed micros() budget; here it is a
// time-based generator writing a 12-bit DAC value, so attack/decay are set in
// real milliseconds instead of opaque 1–26 step units.
//
// The curve table is carried over verbatim from the original firmware so the
// envelope keeps its characteristic snap: a concave (fast-rising) attack and
// the mirrored convex decay with a long tail.

#ifdef UNIT_TEST
#include "ArduinoFake.h"
#else
#include <Arduino.h>
#endif

#define ENVELOPE_MAX 4095       // full-scale 12-bit output (0–5V)
#define ENVELOPE_TRIGGER_MS 10  // pulse width in Trigger mode
#define ENVELOPE_MAX_ATTACK 2000
#define ENVELOPE_MAX_DECAY 4000

// Output behaviour of a channel's GATE jack.
enum GateMode : uint8_t {
    GateEnvelope = 0, // AD envelope — attack then decay, retriggerable
    GateTrigger,      // fixed ENVELOPE_TRIGGER_MS pulse at full level
    GateGate,         // follows the TRIG input level (ignores note-change sync)
    GateModeLength
};

static const char *const GateModeNames[] = {"ENV", "TRIG", "GATE"};

// Envelope shape, 200 points from 0 to 1020. Concave: rises fast, then flattens.
static const uint16_t ADEnvelopeTable[200] = {
    0, 15, 30, 44, 59, 73, 87, 101, 116, 130, 143, 157, 170, 183, 195, 208, 220, 233, 245, 257,
    267, 279, 290, 302, 313, 324, 335, 346, 355, 366, 376, 386, 397, 405, 415, 425, 434, 443, 452, 462,
    470, 479, 488, 495, 504, 513, 520, 528, 536, 544, 552, 559, 567, 573, 581, 589, 595, 602, 609, 616,
    622, 629, 635, 642, 648, 654, 660, 666, 672, 677, 683, 689, 695, 700, 706, 711, 717, 722, 726, 732,
    736, 741, 746, 751, 756, 760, 765, 770, 774, 778, 783, 787, 791, 796, 799, 803, 808, 811, 815, 818,
    823, 826, 830, 834, 837, 840, 845, 848, 851, 854, 858, 861, 864, 866, 869, 873, 876, 879, 881, 885,
    887, 890, 893, 896, 898, 901, 903, 906, 909, 911, 913, 916, 918, 920, 923, 925, 927, 929, 931, 933,
    936, 938, 940, 942, 944, 946, 948, 950, 952, 954, 955, 957, 960, 961, 963, 965, 966, 968, 969, 971,
    973, 975, 976, 977, 979, 980, 981, 983, 984, 986, 988, 989, 990, 991, 993, 994, 995, 996, 997, 999,
    1000, 1002, 1003, 1004, 1005, 1006, 1007, 1008, 1009, 1010, 1012, 1013, 1014, 1014, 1015, 1016, 1017, 1018, 1019, 1020};

// Map a 0..1 phase onto the curve above.
static inline float EnvelopeCurve(float phase) {
    if (phase <= 0.0f) {
        return 0.0f;
    }
    if (phase >= 1.0f) {
        return 1.0f;
    }
    return (float)ADEnvelopeTable[(int)(phase * 199.0f)] / 1020.0f;
}

class Envelope {
    // Parameters
    uint16_t _attackMs = 0;
    uint16_t _decayMs = 360;
    uint8_t _level = 100; // output scaling, percent
    uint8_t _mode = GateEnvelope;

    // Runtime
    enum Phase : uint8_t { Idle = 0, Attack, Decay, Hold };
    uint8_t _phase = Idle;
    unsigned long _phaseStartUs = 0;
    bool _gateHigh = false;
    float _value = 0.0f;

  public:
    // ── Parameters ───────────────────────────────────────────────────────────
    void SetAttack(int ms) { _attackMs = (uint16_t)constrain(ms, 0, ENVELOPE_MAX_ATTACK); }
    void SetDecay(int ms) { _decayMs = (uint16_t)constrain(ms, 0, ENVELOPE_MAX_DECAY); }
    void SetLevel(int percent) { _level = (uint8_t)constrain(percent, 0, 100); }
    void SetMode(int mode) { _mode = (uint8_t)constrain(mode, 0, (int)GateModeLength - 1); }

    int GetAttack() const { return _attackMs; }
    int GetDecay() const { return _decayMs; }
    int GetLevel() const { return _level; }
    int GetMode() const { return _mode; }
    const char *GetModeName() const { return GateModeNames[_mode]; }

    // ── Events ───────────────────────────────────────────────────────────────
    // Fire the envelope (trigger input edge, or a note change in Note sync).
    // No-op in Gate mode, where the output tracks the input level instead.
    void Trigger(unsigned long nowUs) {
        if (_mode == GateGate) {
            return;
        }
        _phaseStartUs = nowUs;
        if (_mode == GateTrigger) {
            _phase = Hold;
            _value = 1.0f;
            return;
        }
        // Envelope mode: a zero attack starts the decay from full immediately,
        // which is what a percussive "no attack" setting should sound like.
        if (_attackMs == 0) {
            _phase = Decay;
            _value = 1.0f;
        } else {
            _phase = Attack;
        }
    }

    // Report the TRIG input level; only Gate mode acts on it.
    void SetGateHigh(bool high) { _gateHigh = high; }

    // Is the envelope currently producing anything? (drives the display's
    // per-channel activity indicator)
    bool IsActive() const { return _phase != Idle || _value > 0.0f; }

    // ── Per-sample update ────────────────────────────────────────────────────
    // Returns the DAC value (0..ENVELOPE_MAX) for this channel's GATE jack.
    uint16_t Update(unsigned long nowUs) {
        if (_mode == GateGate) {
            _value = _gateHigh ? 1.0f : 0.0f;
            _phase = _gateHigh ? Hold : Idle;
            return Scaled();
        }

        unsigned long elapsed = nowUs - _phaseStartUs; // wraps correctly (unsigned)
        switch (_phase) {
        case Attack: {
            unsigned long attackUs = (unsigned long)_attackMs * 1000UL;
            if (elapsed >= attackUs) {
                _value = 1.0f;
                _phase = Decay;
                _phaseStartUs = nowUs;
            } else {
                _value = EnvelopeCurve((float)elapsed / (float)attackUs);
            }
            break;
        }
        case Decay: {
            unsigned long decayUs = (unsigned long)_decayMs * 1000UL;
            if (decayUs == 0 || elapsed >= decayUs) {
                _value = 0.0f;
                _phase = Idle;
            } else {
                _value = 1.0f - EnvelopeCurve((float)elapsed / (float)decayUs);
            }
            break;
        }
        case Hold: { // Trigger mode pulse
            if (elapsed >= (unsigned long)ENVELOPE_TRIGGER_MS * 1000UL) {
                _value = 0.0f;
                _phase = Idle;
            }
            break;
        }
        case Idle:
        default:
            _value = 0.0f;
            break;
        }
        return Scaled();
    }

  private:
    uint16_t Scaled() const {
        float v = _value * (float)_level / 100.0f * (float)ENVELOPE_MAX;
        return (uint16_t)constrain((int)(v + 0.5f), 0, ENVELOPE_MAX);
    }
};
