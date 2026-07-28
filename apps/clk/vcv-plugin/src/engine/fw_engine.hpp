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
//   cvVolts[2]    : the two CV input voltages (0..5 V nominal).
//   clockGateHigh : external clock input level (rising edges drive ext sync).
//   outVolts[4]   : filled with the four output voltages (0..5 V nominal).
void process(Engine *, float dt, const float cvVolts[2], bool clockGateHigh, float outVolts[4]);

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
