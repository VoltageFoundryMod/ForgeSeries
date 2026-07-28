#pragma once

// channel.hpp — One complete quantizer voice.
//
// A QuantizerChannel owns everything that makes up one half of the module: the
// 12-note mask, the scale/root selection used to (re)populate it, pitch offsets,
// glide, the sync policy, and the gate/envelope generator. Two of these plus the
// menu system are the whole instrument.
//
// The class is deliberately copyable and default-constructible: the VCV Rack
// port swaps whole channels in and out of the firmware globals to give each
// module instance its own state (see vcv-plugin/src/engine/engine_state.def).

#ifdef UNIT_TEST
#include "ArduinoFake.h"
#else
#include <Arduino.h>
#endif

#include "envelope.hpp"
#include "quantizer.hpp"
#include "scales.hpp"

// When does the gate/envelope fire?
enum SyncMode : uint8_t {
    SyncTrig = 0, // only on a rising edge at the TRIG input
    SyncNote,     // only when the quantized note changes
    SyncBoth,     // either of the above
    SyncModeLength
};

static const char *const SyncModeNames[] = {"TRIG", "NOTE", "BOTH"};

// How the channel decides *when* to move to a new note.
enum PitchMode : uint8_t {
    PitchTrack = 0,  // follow the input continuously (subject to the settle window)
    PitchSampleHold, // latch on a TRIG edge and hold until the next one
    PitchModeLength
};

static const char *const PitchModeNames[] = {"TRACK", "S&H"};

#define CHANNEL_OCTAVE_MIN (-3)
#define CHANNEL_OCTAVE_MAX 3
// Glide is a percentage mapped onto a 0–500 ms one-pole time constant.
#define CHANNEL_GLIDE_MAX_US 500000.0f
// Settle (de-glitch) window: how long a new note must hold before it is played.
#define CHANNEL_SETTLE_MAX_MS 50

class QuantizerChannel {
    // ── Configuration ────────────────────────────────────────────────────────
    bool _activeNotes[12];
    int _scaleIndex = 0; // selection for the "load scale" helper, not the mask
    int _rootIndex = 0;
    int _octave = 0;      // -3..+3, applied after quantization
    int _glide = 0;       // 0..100 percent
    int _settleMs = 5;    // de-glitch window, 0..CHANNEL_SETTLE_MAX_MS
    uint8_t _syncMode = SyncNote;
    uint8_t _pitchMode = PitchTrack;
    bool _transposeEnabled = false; // does this channel follow the transpose CV?

    // ── Runtime ──────────────────────────────────────────────────────────────
    Quantizer _quantizer;
    float _inputSemitone = 0.0f;
    int _quantizedSemitone = -1; // straight from the quantizer; -1 = nothing yet
    int _playedSemitone = 0;     // after degree transposition, still in scale
    int _outputSemitone = 0;     // after octave shift, folded into range
    int _pendingSemitone = -1;   // candidate waiting out the settle window
    unsigned long _pendingSinceUs = 0;
    int _transposeDegrees = 0;   // set from the transpose CV each iteration
    bool _sampleArmed = false;   // S&H: a trigger is waiting out the sample delay
    unsigned long _sampleSinceUs = 0;
    float _cvCounts = 0.0f; // glide-smoothed DAC value
    uint16_t _gateCounts = 0;
    unsigned long _lastUpdateUs = 0;
    bool _noteChanged = false;

    // Apply the transpose CV, in scale degrees, if this channel follows it.
    int Transposed(int semitone) const {
        if (!_transposeEnabled || _transposeDegrees == 0 || semitone < 0) {
            return semitone;
        }
        return _quantizer.TransposeDegrees(semitone, _transposeDegrees);
    }

  public:
    Envelope envelope;

    QuantizerChannel() {
        for (int i = 0; i < 12; i++) {
            _activeNotes[i] = true; // chromatic until a preset says otherwise
        }
        _quantizer.Build(_activeNotes);
    }

    // ── Note mask ────────────────────────────────────────────────────────────
    bool GetActiveNote(int i) const { return (i >= 0 && i < 12) ? _activeNotes[i] : false; }

    void SetActiveNote(int i, bool on) {
        if (i < 0 || i >= 12) {
            return;
        }
        _activeNotes[i] = on;
        _quantizer.Build(_activeNotes);
    }

    void ToggleNote(int i) { SetActiveNote(i, !GetActiveNote(i)); }

    const bool *GetActiveNotes() const { return _activeNotes; }

    void SetActiveNotes(const bool notes[12]) {
        for (int i = 0; i < 12; i++) {
            _activeNotes[i] = notes[i];
        }
        _quantizer.Build(_activeNotes);
    }

    // Populate the mask from the currently selected scale + root.
    void LoadSelectedScale() {
        BuildScale(_scaleIndex, _rootIndex, _activeNotes);
        _quantizer.Build(_activeNotes);
    }

    // ── Parameters ───────────────────────────────────────────────────────────
    // SetScaleIndex/SetRootIndex only record the selection; SelectScale/SelectRoot
    // also rebuild the note mask from it.
    //
    // The split matters for preset loading. A preset stores the mask *and* the
    // scale/root that were last chosen, and the mask is the source of truth
    // because it may have been hand-edited afterwards. If the plain setters
    // rebuilt the mask, restoring a preset would overwrite the saved mask with a
    // freshly generated scale and quietly discard those edits. UpdateParameters()
    // therefore uses the plain setters; anything driven by the user uses the
    // Select* pair.
    int GetScaleIndex() const { return _scaleIndex; }
    void SetScaleIndex(int i) { _scaleIndex = ((i % numScales) + numScales) % numScales; }
    int GetRootIndex() const { return _rootIndex; }
    void SetRootIndex(int i) { _rootIndex = ((i % 12) + 12) % 12; }

    void SelectScale(int i) {
        SetScaleIndex(i);
        LoadSelectedScale();
    }
    void SelectRoot(int i) {
        SetRootIndex(i);
        LoadSelectedScale();
    }

    int GetOctave() const { return _octave; }
    void SetOctave(int o) { _octave = constrain(o, CHANNEL_OCTAVE_MIN, CHANNEL_OCTAVE_MAX); }

    int GetGlide() const { return _glide; }
    void SetGlide(int g) { _glide = constrain(g, 0, 100); }

    int GetSettle() const { return _settleMs; }
    void SetSettle(int ms) { _settleMs = constrain(ms, 0, CHANNEL_SETTLE_MAX_MS); }

    int GetSyncMode() const { return _syncMode; }
    void SetSyncMode(int m) { _syncMode = (uint8_t)constrain(m, 0, (int)SyncModeLength - 1); }
    const char *GetSyncModeName() const { return SyncModeNames[_syncMode]; }

    int GetPitchMode() const { return _pitchMode; }
    void SetPitchMode(int m) { _pitchMode = (uint8_t)constrain(m, 0, (int)PitchModeLength - 1); }
    const char *GetPitchModeName() const { return PitchModeNames[_pitchMode]; }

    bool GetTransposeEnabled() const { return _transposeEnabled; }
    void SetTransposeEnabled(bool on) { _transposeEnabled = on; }

    // Transposition in scale degrees, refreshed from the transpose CV before
    // each Process(). Ignored unless this channel has transposition enabled.
    void SetTransposeDegrees(int degrees) { _transposeDegrees = degrees; }
    int GetTransposeDegrees() const { return _transposeEnabled ? _transposeDegrees : 0; }

    // ── Readouts for the display ─────────────────────────────────────────────
    int GetNoteIndex() const { return SemitoneToNoteIndex(_outputSemitone); }
    int GetOctaveOut() const { return SemitoneToOctave(_outputSemitone); }
    int GetQuantizedSemitone() const { return _quantizedSemitone; }
    int GetSoundingSemitone() const { return _playedSemitone; }
    uint16_t GetCVOutput() const { return (uint16_t)constrain((int)(_cvCounts + 0.5f), 0, 4095); }
    uint16_t GetGateOutput() const { return _gateCounts; }
    bool IsGateActive() const { return envelope.IsActive(); }

    // Consume the "note changed since last call" flag (drives display refresh).
    bool ConsumeNoteChanged() {
        bool changed = _noteChanged;
        _noteChanged = false;
        return changed;
    }

    // ── Per-iteration processing ─────────────────────────────────────────────
    // pitchSemitones : calibrated, filtered pitch CV as fractional semitones at
    //                  1 V/oct (see CvSemitonesFromMv()). Takes semitones rather
    //                  than ADC counts because counts cannot express a negative
    //                  CV, so a counts-based input would silently lose every
    //                  note below 0 V on the +/-5 V hardware.
    // trigEdge       : a rising edge arrived at the TRIG input this iteration.
    // trigHigh       : current TRIG input level (used by Gate mode).
    void Process(float pitchSemitones, unsigned long nowUs, bool trigEdge, bool trigHigh) {
        // The quantizer's note table spans 0..QUANT_MAX_SEMITONE, so the input
        // is clamped to that regardless of what the jack can deliver. On the
        // bipolar hardware this means negative CV rests on the bottom note —
        // whether -5..+5 V should instead span ten octaves is a musical
        // decision for that hardware revision, not something to assume here.
        _inputSemitone =
            constrain(pitchSemitones, 0.0f, (float)QUANT_MAX_SEMITONE);

        int candidate = _quantizer.Quantize(_inputSemitone, _quantizedSemitone);
        const unsigned long settleUs = (unsigned long)_settleMs * 1000UL;
        bool noteChanged = false;

        if (_pitchMode == PitchSampleHold) {
            // ── Sample & hold ────────────────────────────────────────────────
            // The output only ever moves on a TRIG edge, so however the input
            // behaves between triggers — slewed, noisy, swept — nothing reaches
            // the jack. This is the unconditional answer to intermediate notes.
            //
            // Sampling exactly on the edge would be wrong: a sequencer puts out
            // its pitch and its gate at the same instant, so at the edge the
            // pitch CV may still be in transit. SETTLE doubles as the sample
            // delay here, which is the same idea it serves in TRACK mode —
            // "how long to wait before believing the input".
            if (trigEdge) {
                _sampleArmed = true;
                _sampleSinceUs = nowUs;
            }
            bool first = (_quantizedSemitone < 0); // nothing latched yet
            if (first || (_sampleArmed && (nowUs - _sampleSinceUs) >= settleUs)) {
                _sampleArmed = false;
                if (candidate != _quantizedSemitone) {
                    noteChanged = true;
                    _noteChanged = true;
                }
                _quantizedSemitone = candidate;
                _playedSemitone = Transposed(candidate);
            } else if (!_quantizer.Emits(_quantizedSemitone)) {
                // The scale changed under a held note. Re-snap the *held* note
                // into the new scale rather than re-reading the input: the
                // output must still not follow the input between triggers.
                _quantizedSemitone = _quantizer.Quantize((float)_quantizedSemitone, -1);
                _playedSemitone = Transposed(_quantizedSemitone);
                _noteChanged = true;
            }
        } else {
            // ── Track, with the settle (de-glitch) window ─────────────────────
            // A step between two distant notes does not arrive at the ADC as a
            // step: the input smoothing in cvInputs.hpp takes a few loop
            // iterations to converge, and a plain "follow the input" quantizer
            // plays every note it crosses on the way, which is heard as a short
            // sweep up to the target. Requiring a candidate to hold for
            // _settleMs before it is committed removes those transients — none
            // of them is ever held long enough. An input that really is moving
            // slowly still plays every note it passes through, because each one
            // holds for longer than the window. Setting it to 0 restores the
            // immediate follow behaviour.
            if (candidate != _quantizedSemitone) {
                if (candidate != _pendingSemitone) {
                    _pendingSemitone = candidate;
                    _pendingSinceUs = nowUs;
                }
                // Two cases must never wait: the very first note after power-on,
                // and a held note that has just left the scale (editing the
                // keyboard or loading a scale has to take effect at once).
                bool immediate = (_quantizedSemitone < 0) || !_quantizer.Emits(_quantizedSemitone);
                if (immediate || (nowUs - _pendingSinceUs) >= settleUs) {
                    _quantizedSemitone = candidate;
                    _noteChanged = true;
                    noteChanged = true;
                }
            } else {
                _pendingSemitone = candidate; // input returned before the window expired
            }
            // Transposition is live in TRACK mode: sweeping the transpose CV
            // moves the output without waiting for the note to change.
            _playedSemitone = Transposed(_quantizedSemitone);
        }

        // Octave shift is applied after quantization, so the result is always
        // still a note of the scale. When the shift would run past the end of the
        // 0..60 output range, fold back by whole octaves instead of clamping:
        // clamping lands on whatever semitone sits at the limit, so a +3 shift on
        // a high B would silently output C — a note the scale may not even
        // contain. Folding keeps the pitch class the quantizer chose.
        int shifted = _quantizedSemitone < 0 ? 0 : _playedSemitone + _octave * 12;
        while (shifted > QUANT_MAX_SEMITONE) {
            shifted -= 12;
        }
        while (shifted < 0) {
            shifted += 12;
        }
        _outputSemitone = constrain(shifted, 0, QUANT_MAX_SEMITONE);

        // Fire the gate/envelope per the sync policy.
        bool fire = (trigEdge && (_syncMode == SyncTrig || _syncMode == SyncBoth)) ||
                    (noteChanged && (_syncMode == SyncNote || _syncMode == SyncBoth));
        if (fire) {
            envelope.Trigger(nowUs);
        }
        envelope.SetGateHigh(trigHigh);
        _gateCounts = envelope.Update(nowUs);

        // Glide: one-pole toward the target pitch. The coefficient is derived
        // from the real elapsed time so the glide rate does not drift with loop
        // speed (the VCV port runs the engine far slower than the hardware).
        float target = SemitonesToCounts((float)_outputSemitone);
        unsigned long dtUs = nowUs - _lastUpdateUs;
        _lastUpdateUs = nowUs;
        if (_glide == 0 || dtUs == 0) {
            _cvCounts = target;
        } else {
            float tauUs = (float)_glide / 100.0f * CHANNEL_GLIDE_MAX_US;
            float alpha = (float)dtUs / tauUs;
            if (alpha >= 1.0f) {
                _cvCounts = target;
            } else {
                _cvCounts += alpha * (target - _cvCounts);
            }
        }
    }
};
