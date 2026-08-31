#pragma once
// Clean POD/opaque API to the ClockForge firmware engine.
// This is the ONLY header the Rack-facing module includes — it deliberately
// exposes no Arduino/firmware types so it can coexist with rack.hpp.
#include <cstdint>
#include <string>

#include "forgevcv/IEngine.hpp"

namespace cfengine {

struct Engine; // opaque

Engine *createEngine();
void destroyEngine(Engine *);

// Advance the engine by dt seconds.
//   cvVolts[3]    : CV input voltages (0..5 V nominal). The third is the
//                   expander's IN 4 and is ignored unless one is enabled.
//   clockGateHigh : external clock input level (rising edges drive ext sync).
//   outVolts[8]   : filled with the output voltages (0..5 V nominal). Only the
//                   first outputCount() entries are written; the rest are left
//                   alone, so an unfitted expander's jacks read whatever the
//                   caller initialised them to.
void process(Engine *, float dt, const float cvVolts[3], bool clockGateHigh, float outVolts[8]);

// Encoder rotation in detents (+clockwise / -counter-clockwise).
void encoderTurn(Engine *, int detents);
// Encoder push-button level; the engine detects press/release edges.
void encoderButton(Engine *, bool pressed);

// Copy the 128x64 monochrome framebuffer (1bpp, row-major, MSB-first = 1024 bytes).
void getFramebuffer(Engine *, uint8_t out[1024]);

// Persistence: the firmware EEPROM blob (presets + calibration) as raw bytes.
// serialize() commits the live state to slot 0 first, so the blob it returns
// always contains what the user is actually hearing.
std::string serialize(Engine *);
void deserialize(Engine *, const std::string &);

// Rack's Initialize / Randomize module actions. reset() restores the factory
// defaults (stored preset slots and calibration are left alone); randomize()
// rolls the per-output waveform, divider, duty, phase, swing, probability and
// euclidean pattern, but not the tempo, the CV matrix or the cross/loop routing.
void reset(Engine *);
void randomize(Engine *);

// Current BPM (for tooltips / future param display).
int bpm(Engine *);

// ── Curated parameter bridge (for the Rack right-click context menu) ──────────
// Absolute get/set of a curated subset of the firmware's parameters, so the
// context menu can offer real option lists / value setters instead of relaying
// relative encoder detents.  Each call that touches an Engine takes the engine
// globals lock internally, so it is safe to call from the UI thread while
// process() runs on the audio thread.  Output indices are 0..3.

// Transport (master play/stop).
bool isRunning(Engine *);
void setRunning(Engine *, bool running);

// BPM (bounds are compile-time constants; setBpm mirrors the menu's setter).
int bpmMin();
int bpmMax();
void setBpm(Engine *, int bpm);

// Waveform — one shared option list across all four outputs.
int waveformCount();
std::string waveformName(int index);
int outputWaveform(Engine *, int out);
void setOutputWaveform(Engine *, int out, int waveform);

// Clock divider — shared option list, per-output selection.  Divider is locked
// to "Env" (and thus not user-settable) while the output is an envelope type.
int dividerCount(Engine *);
std::string dividerName(Engine *, int index);
int outputDivider(Engine *, int out);
void setOutputDivider(Engine *, int out, int index);
bool outputIsEnvelope(Engine *, int out);

// Output enable (per-output on/off).
bool outputEnabled(Engine *, int out);
void setOutputEnabled(Engine *, int out, bool on);

// ── Expander ─────────────────────────────────────────────────────────────────
// Which expander the firmware thinks is fitted: 0 = none, 1 = Expander 1.
// This is the same setting as the module's EXPANDER menu row, and it is the
// authority on both hosts — the Rack module sets it when an expander widget is
// placed beside it, so a patch behaves the way a rack does.
int expanderType(Engine *);
void setExpanderType(Engine *, int type);

// How many outputs are live right now — 4, or 8 with an expander. The context
// menu sizes its per-output list from this.
int outputCount(Engine *);

// ── forgevcv adapter ─────────────────────────────────────────────────────────
// Wraps an Engine as a forgevcv::IEngine so the reusable ForgeModule base can
// drive it through the six core lifecycle calls, while raw() still exposes the
// Engine* for the curated param bridge above (used by the context menu).
class VcvEngine : public forgevcv::IEngine {
    Engine *e_;

  public:
    VcvEngine();
    ~VcvEngine() override;
    Engine *raw() const { return e_; }

    void process(float dt, const float *cv, int nCv,
                 bool clockHigh, float *out, int nOut) override;
    void encoderTurn(int detents) override;
    void encoderButton(bool pressed) override;
    void getFramebuffer(uint8_t out[1024]) override;
    std::string serialize() override;
    void deserialize(const std::string &) override;
    void reset() override;
    void randomize() override;
};

} // namespace cfengine
