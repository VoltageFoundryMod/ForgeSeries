#pragma once
// Clean POD/opaque API to the WeaveForge firmware engine.
// This is the ONLY header the Rack-facing module includes — it deliberately
// exposes no Arduino/firmware types so it can coexist with rack.hpp.
#include <cstdint>
#include <string>

#include "forgevcv/IEngine.hpp"

namespace wvengine {

struct Engine; // opaque

Engine *createEngine();
void destroyEngine(Engine *);

// Advance the engine by dt seconds.
//   cvVolts[2]   : the two modulation CV inputs, IN 2 / IN 3 (0..5 V nominal).
//   clockGateHigh: IN 1 level. Rising edges clock the registers; the level is
//                  passed rather than an edge so the engine owns edge detection
//                  exactly as the ISR does on hardware.
//   outVolts[4]  : filled with the four jacks (0..5 V nominal).
void process(Engine *, float dt, const float cvVolts[2], bool clockGateHigh, float outVolts[4]);

// Encoder rotation in detents (+clockwise / -counter-clockwise).
void encoderTurn(Engine *, int detents);
// Encoder push-button level; the engine detects press/release edges.
void encoderButton(Engine *, bool pressed);

// Copy the 128x64 monochrome framebuffer (1bpp, row-major, MSB-first = 1024 bytes).
void getFramebuffer(Engine *, uint8_t out[1024]);

// Persistence: the firmware EEPROM blob (presets + calibration) as raw bytes.
// serialize() commits the live state to slot 0 first, so the blob it returns
// always contains what the user is actually hearing — which on this module
// includes the register contents, i.e. the pattern itself.
std::string serialize(Engine *);
void deserialize(Engine *, const std::string &);

// Rack's Initialize / Randomize module actions.
void reset(Engine *);
void randomize(Engine *);

// ── Curated parameter bridge (for the Rack right-click context menu) ──────────
// Absolute get/set of the firmware's parameters, so the context menu offers real
// option lists and value setters instead of relaying relative encoder detents.
// Each call takes the engine globals lock internally, so it is safe from the UI
// thread while process() runs on the audio thread.
//
// Register indices are 0..1 (A/B); jack indices are 0..3.

// ── The registers ────────────────────────────────────────────────────────────
int lengthMin();
int lengthMax();
int lengthGet(Engine *, int reg);
void lengthSet(Engine *, int reg, int n);
int chanceGet(Engine *, int reg); // percent
void chanceSet(Engine *, int reg, int pct);
// The four editing actions from the register page.
void regRandomize(Engine *, int reg);
void regInvert(Engine *, int reg);
void regClear(Engine *, int reg);
void regFill(Engine *, int reg);
// The live 16-bit pattern, for anything that wants to show it.
uint32_t regValue(Engine *, int reg);

// ── WEAVE ────────────────────────────────────────────────────────────────────
int weaveGet(Engine *); // percent
void weaveSet(Engine *, int pct);
int weaveDirCount();
std::string weaveDirName(int index);
int weaveDirGet(Engine *);
void weaveDirSet(Engine *, int index);

// ── The output matrix ────────────────────────────────────────────────────────
// The panel's name for a jack: A1 / B1 / A2 / B2, in index order (top-left,
// top-right, bottom-left, bottom-right). Register A owns the left column and B
// the right in the DUO routing, which is what the naming is for.
std::string jackName(int jack);

// The jacks in PANEL order — down the left column, then down the right. Pass a
// SLOT 0..3 and get the jack index to use; jackAt(1) is A2, not B1.
//
// Any UI listing the four jacks walks them through this, so the context menu
// stays in the same order as the module's own OUT pages. The firmware owns the
// order (WEA_JACK_COLUMN_ORDER in lib/outputs.hpp) and this is how the plugin
// reads it, rather than repeating {0,2,1,3} on the Rack side where it could
// quietly drift.
int jackAt(int slot);

int routingCount();
std::string routingName(int index);
// Returns -1 when the four slots match no template — the CUSTOM state, which is
// recomputed rather than stored (see lib/outputs.hpp).
int routingGet(Engine *);
void routingSet(Engine *, int index);

int outSourceCount();
std::string outSourceName(int index);
int outSourceGet(Engine *, int jack);
void outSourceSet(Engine *, int jack, int index);

int outTypeCount();
std::string outTypeName(int index);
int outTypeGet(Engine *, int jack);
void outTypeSet(Engine *, int jack, int index);

int outDepthGet(Engine *, int jack);
void outDepthSet(Engine *, int jack, int bits);
int outRotateGet(Engine *, int jack);
void outRotateSet(Engine *, int jack, int pos);
// How far ROTATE goes for this jack's current SOURCE: 16 for A or B, 32 for AB.
int outRotateSpan(Engine *, int jack);

// The two contextual fields. What they are called and what they range over
// follows TYPE, so the labels and bounds are queried per jack rather than
// assumed.
std::string outParamLabel(Engine *, int jack); // RANGE / LEVEL / THRESH
int outParamMin(Engine *, int jack);
int outParamMax(Engine *, int jack);
int outParamGet(Engine *, int jack);
void outParamSet(Engine *, int jack, int v);

std::string outParam2Label(Engine *, int jack); // SLEW / WIDTH, "" for GATE
int outParam2Min(Engine *, int jack);
int outParam2Max(Engine *, int jack);
int outParam2Get(Engine *, int jack);
void outParam2Set(Engine *, int jack, int v);

// ── Clock ────────────────────────────────────────────────────────────────────
int bpmMin();
int bpmMax();
int bpmGet(Engine *);
void bpmSet(Engine *, int bpm);
bool clockIsExternal(Engine *); // a clock is genuinely arriving right now
int ppqnCount();
std::string ppqnName(int index);
int ppqnGet(Engine *);
void ppqnSet(Engine *, int index);
// Steps per beat, on both clock sources: "/4" is one step every four beats,
// "x4" is four steps a beat. Same names ClockForge uses.
int rateCount();
std::string rateName(int index);
int rateGet(Engine *);
void rateSet(Engine *, int index);

// ── Pitch ────────────────────────────────────────────────────────────────────
std::string noteName(int index); // 0=C … 11=B
int rootGet(Engine *);
void rootSet(Engine *, int index);
int scaleCount();
std::string scaleName(int index);
int scaleGet(Engine *);
void scaleSet(Engine *, int index);
int transposeGet(Engine *); // semitones
void transposeSet(Engine *, int semitones);

// ── CV modulation matrix ─────────────────────────────────────────────────────
int cvTargetCount();
std::string cvTargetName(int index);
int cvTargetGet(Engine *, int input); // input 0 = IN 2, 1 = IN 3
void cvTargetSet(Engine *, int input, int index);
int cvDepthGet(Engine *, int input);
void cvDepthSet(Engine *, int input, int pct);

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

} // namespace wvengine
