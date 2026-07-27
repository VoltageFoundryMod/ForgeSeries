#pragma once
// Clean POD/opaque API to the NoteForge firmware engine.
// This is the ONLY header the Rack-facing module includes — it deliberately
// exposes no Arduino/firmware types so it can coexist with rack.hpp.
#include <cstdint>
#include <string>

#include "forgevcv/IEngine.hpp"

namespace nfengine {

struct Engine; // opaque

Engine *createEngine();
void destroyEngine(Engine *);

// Advance the engine by dt seconds.
//   cvVolts[2]   : the two pitch CV inputs (0..5 V nominal).
//   trigGateHigh : TRIG input level (rising edges fire the gate/envelope).
//   outVolts[4]  : filled with CV 1, CV 2, GATE 1, GATE 2 (0..5 V nominal).
void process(Engine *, float dt, const float cvVolts[2], bool trigGateHigh, float outVolts[4]);

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
// rolls the scale, root, octave, glide and gate settings but not the input
// routing (sync mode, IN 2 role, transposition).
void reset(Engine *);
void randomize(Engine *);

// ── Curated parameter bridge (for the Rack right-click context menu) ──────────
// Absolute get/set of a curated subset of the firmware's parameters, so the
// context menu offers real option lists and value setters instead of relaying
// relative encoder detents. Each call takes the engine globals lock internally,
// so it is safe from the UI thread while process() runs on the audio thread.
// Channel indices are 0..1; note indices are 0..11 (0 = C).

// Scale / root selection (the "load scale" helper's state, not the live mask).
int scaleCount();
std::string scaleName(int index);
int channelScale(Engine *, int ch);
// Setting the scale or the root rebuilds that channel's note mask immediately.
void setChannelScale(Engine *, int ch, int scale);
std::string noteName(int note);
int channelRoot(Engine *, int ch);
void setChannelRoot(Engine *, int ch, int root);

// The live 12-note mask — the actual source of truth for quantization.
bool noteEnabled(Engine *, int ch, int note);
void setNoteEnabled(Engine *, int ch, int note, bool on);

// Pitch.
int octaveMin();
int octaveMax();
int channelOctave(Engine *, int ch);
void setChannelOctave(Engine *, int ch, int octave);
int channelGlide(Engine *, int ch);
void setChannelGlide(Engine *, int ch, int glide);
// Settle: how long (ms) a new note must hold before the output follows it.
int settleMax();
int channelSettle(Engine *, int ch);
void setChannelSettle(Engine *, int ch, int ms);

// Pitch tracking mode: TRACK follows the input, S&H latches on a TRIG edge.
int pitchModeCount();
std::string pitchModeName(int index);
int channelPitchMode(Engine *, int ch);
void setChannelPitchMode(Engine *, int ch, int mode);

// ── IN 2 routing / transposition ─────────────────────────────────────────────
// IN 2 is normally channel 2's pitch input. In transpose mode it becomes a
// transposition CV and channel 2 quantizes IN 1 alongside channel 1.
int in2RoleCount();
std::string in2RoleName(int index);
int in2RoleGet(Engine *);
void in2RoleSet(Engine *, int role);
int transposeRangeCount();
std::string transposeRangeName(int index);
int transposeRangeGet(Engine *);
void transposeRangeSet(Engine *, int range);
bool channelTranspose(Engine *, int ch);
void setChannelTranspose(Engine *, int ch, bool on);
// Live transposition applied to a channel, in scale degrees (0 when disabled).
int channelTransposeDegrees(Engine *, int ch);

// Gate / envelope.
int gateModeCount();
std::string gateModeName(int index);
int channelGateMode(Engine *, int ch);
void setChannelGateMode(Engine *, int ch, int mode);
int syncModeCount();
std::string syncModeName(int index);
int channelSyncMode(Engine *, int ch);
void setChannelSyncMode(Engine *, int ch, int mode);
int attackMax();
int channelAttack(Engine *, int ch);
void setChannelAttack(Engine *, int ch, int ms);
int decayMax();
int channelDecay(Engine *, int ch);
void setChannelDecay(Engine *, int ch, int ms);

// Live readout — the note each channel is currently emitting (e.g. "A#3").
std::string currentNote(Engine *, int ch);

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

} // namespace nfengine
