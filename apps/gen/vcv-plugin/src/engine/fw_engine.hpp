#pragma once
// Clean POD/opaque API to the GravityForge firmware engine.
// This is the ONLY header the Rack-facing module includes — it deliberately
// exposes no Arduino/firmware types so it can coexist with rack.hpp.
#include <cstdint>
#include <string>

#include "forgevcv/IEngine.hpp"

namespace gfengine {

struct Engine; // opaque

Engine *createEngine();
void destroyEngine(Engine *);

// Advance the engine by dt seconds.
//   cvVolts[2]   : the two modulation CV inputs, IN 2 / IN 3 (0..5 V nominal).
//   trigGateHigh : IN 1 level (rising edges act per the jack's configured role).
//   outVolts[4]  : filled with CV A, CV B, GATE A, GATE B (0..5 V nominal).
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
// rolls the physics, note and gate parameters but not the clock, the IN 1 role
// or the CV matrix.
void reset(Engine *);
void randomize(Engine *);

// ── Curated parameter bridge (for the Rack right-click context menu) ──────────
// Absolute get/set of a curated subset of the firmware's parameters, so the
// context menu offers real option lists and value setters instead of relaying
// relative encoder detents. Each call takes the engine globals lock internally,
// so it is safe from the UI thread while process() runs on the audio thread.
//
// Container indices are 0..1 (A/B); note indices are 0..11 (0 = C).
// Everything is exchanged as int/bool/string — no firmware types cross this line.

// ── The signature controls ───────────────────────────────────────────────────
// PROXIMITY slides the two containers together; where they overlap, a wall
// strike in one transmits an impulse into the other. COUPLE scales how much of
// that overlap actually transmits.
int proximityGet(Engine *);
void proximitySet(Engine *, int pct); // 0..100
int couplingGet(Engine *);
void couplingSet(Engine *, int pct); // 0..100

// Gestures, the same ones the COUPLING menu page offers.
void resetBalls(Engine *);
void kickBalls(Engine *);

// ── Clock ────────────────────────────────────────────────────────────────────
int bpmMin();
int bpmMax();
int bpmGet(Engine *);
void bpmSet(Engine *, int bpm);
// The tempo actually in force — differs from bpmGet() while IN 1 is a clock.
int effectiveBpm(Engine *);

int quantizeCount();
std::string quantizeName(int index);
int quantizeGet(Engine *);
void quantizeSet(Engine *, int index);

int ppqnCount();
std::string ppqnName(int index);
int ppqnGet(Engine *);
void ppqnSet(Engine *, int index);

// IN 1's job: CLOCK / RESET / KICK / SPAWN.
int in1RoleCount();
std::string in1RoleName(int index);
int in1RoleGet(Engine *);
void in1RoleSet(Engine *, int index);

// ── Loop / phrase mode ───────────────────────────────────────────────────────
// The simulation is deterministic, so a phrase is a snapshot plus a step count.
// LOOP BEATS sets the length (0 = off); NAP/WAKE mute whole loops per container
// and SHIFT offsets that cycle so A and B can trade phrases.
int loopBeatsMax();
int loopBeatsGet(Engine *);
void loopBeatsSet(Engine *, int beats); // 0 = off
int loopWakeMin();
int loopWakeMax();
int loopWakeGet(Engine *);
void loopWakeSet(Engine *, int loops);
int loopNapMax();
int loopNapGet(Engine *);
void loopNapSet(Engine *, int loops); // 0 = never nap
int loopShiftMax();
int loopShiftGet(Engine *, int c);
void loopShiftSet(Engine *, int c, int loops);
// Throw the captured phrase away and keep whatever is playing now.
void loopNewPhrase(Engine *);

// ── Physics, per container ───────────────────────────────────────────────────
int gravityMin();
int gravityMax();
int gravityGet(Engine *, int c);
void gravitySet(Engine *, int c, int v);

int bounceGet(Engine *, int c); // percent
void bounceSet(Engine *, int c, int pct);
int gripGet(Engine *, int c); // percent
void gripSet(Engine *, int c, int pct);

int spinCount();
std::string spinName(int index); // beats per revolution
int spinGet(Engine *, int c);
void spinSet(Engine *, int c, int index);
bool reverseGet(Engine *, int c);
void reverseSet(Engine *, int c, bool on);

int ballsMin();
int ballsMax();
int ballsGet(Engine *, int c);
void ballsSet(Engine *, int c, int n);

int pegsMin();
int pegsMax();
int pegsGet(Engine *, int c);
void pegsSet(Engine *, int c, int n);
// Muting a peg makes it a silent bounce rather than a note — this is how the
// rhythm is opened up.
bool pegEnabledGet(Engine *, int c, int peg);
void pegEnabledSet(Engine *, int c, int peg, bool on);

// ── Notes, per container ─────────────────────────────────────────────────────
int scaleCount();
std::string scaleName(int index);
int scaleGet(Engine *, int c);
void scaleSet(Engine *, int c, int index); // rebuilds the note mask immediately
std::string noteName(int note);
int rootGet(Engine *, int c);
void rootSet(Engine *, int c, int root);
// SPREAD is how many octaves the peg ring covers; BIAS warps where the notes
// crowd inside that span (-100 low .. 0 even .. +100 high). Together they
// replace what used to be a single OCTAVE offset.
int spreadMin();
int spreadMax();
int spreadGet(Engine *, int c);
void spreadSet(Engine *, int c, int octaves);
int biasMin();
int biasMax();
int biasGet(Engine *, int c);
void biasSet(Engine *, int c, int bias);

// ── Gate, per container ──────────────────────────────────────────────────────
int gateModeCount();
std::string gateModeName(int index);
int gateModeGet(Engine *, int c);
void gateModeSet(Engine *, int c, int index);
int attackMax();
int attackGet(Engine *, int c);
void attackSet(Engine *, int c, int ms);
int decayMax();
int decayGet(Engine *, int c);
void decaySet(Engine *, int c, int ms);
int levelGet(Engine *, int c);
void levelSet(Engine *, int c, int pct);
// ACCENT: how much a ball's impact speed scales the gate level.
int accentGet(Engine *, int c);
void accentSet(Engine *, int c, int pct);

// ── CV modulation matrix ─────────────────────────────────────────────────────
int cvTargetCount();
std::string cvTargetName(int index);
int cvTargetGet(Engine *, int input); // input 0 = IN 2, 1 = IN 3
void cvTargetSet(Engine *, int input, int index);
int cvDepthGet(Engine *, int input);
void cvDepthSet(Engine *, int input, int pct);

// ── Live readouts ────────────────────────────────────────────────────────────
std::string currentNote(Engine *, int c); // e.g. "A#3", or "--" before the first hit

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

} // namespace gfengine
