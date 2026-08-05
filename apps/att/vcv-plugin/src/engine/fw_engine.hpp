#pragma once
// Clean POD/opaque API to the ChaosForge firmware engine.
// This is the ONLY header the Rack-facing module includes — it deliberately
// exposes no Arduino/firmware types so it can coexist with rack.hpp.
#include <cstdint>
#include <string>

#include "forgevcv/IEngine.hpp"

namespace chengine {

struct Engine; // opaque

Engine *createEngine();
void destroyEngine(Engine *);

// Advance the engine by dt seconds.
//   cvVolts[2]   : the two modulation CV inputs, IN 2 / IN 3 (0..5 V nominal).
//   trigGateHigh : IN 1 level (rising edges re-seed; the FREEZE role reads the
//                  level itself, which is why this is a level and not an edge).
//   outVolts[4]  : filled with A1, A2, B1, B2 (0..5 V nominal).
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

// Rack's Initialize / Randomize module actions.
void reset(Engine *);
void randomize(Engine *);

// ── Curated parameter bridge (for the Rack right-click context menu) ──────────
// Absolute get/set of the firmware's parameters, so the context menu offers real
// option lists and value setters instead of relaying relative encoder detents.
// Each call takes the engine globals lock internally, so it is safe from the UI
// thread while process() runs on the audio thread.
//
// Generator indices are 0..1 (A/B); jack indices are 0..1 within a generator.
// Everything is exchanged as int/float/bool/string — no firmware types cross
// this line.

// ── The systems ──────────────────────────────────────────────────────────────
int systemCount();
std::string systemName(int index);
int systemGet(Engine *, int g);
// Loads that system's published parameter values, exactly as the panel does:
// parameter 1 means something different in every equation.
void systemSet(Engine *, int g, int index);

// A system's parameters. The count, the names and the ranges all depend on which
// system the generator is currently running, so they are queried per generator
// rather than assumed.
int paramCount(Engine *, int g);
std::string paramName(Engine *, int g, int k);
float paramMin(Engine *, int g, int k);
float paramMax(Engine *, int g, int k);
float paramDefault(Engine *, int g, int k);
float paramGet(Engine *, int g, int k);
void paramSet(Engine *, int g, int k, float v);

// ── Rate ─────────────────────────────────────────────────────────────────────
// A multiplier on the rate the system was catalogued at, so 1.00x means the same
// kind of motion whichever system is selected.
float speedMin();
float speedMax();
float speedGet(Engine *, int g);
void speedSet(Engine *, int g, float x);

// ── Output shaping, per generator ────────────────────────────────────────────
int axisCount();
std::string axisName(int axis); // "X" / "Y" / "Z"
int srcGet(Engine *, int g, int jack);
void srcSet(Engine *, int g, int jack, int axis);

int levelGet(Engine *, int g); // percent
void levelSet(Engine *, int g, int pct);
int offsetGet(Engine *, int g); // -100..100 percent
void offsetSet(Engine *, int g, int pct);
int smoothGet(Engine *, int g); // percent
void smoothSet(Engine *, int g, int pct);
// false = the published window measured at the system's defaults; true = a
// window tracked from the orbit itself.
bool autoRangeGet(Engine *, int g);
void autoRangeSet(Engine *, int g, bool on);

// ── The link between the two generators ─────────────────────────────────────
int coupleGet(Engine *); // percent
void coupleSet(Engine *, int pct);
void reseedAll(Engine *);
void reseedGen(Engine *, int g);

// IN 1's job: RESET / RESET A / RESET B / FREEZE.
int in1RoleCount();
std::string in1RoleName(int index);
int in1RoleGet(Engine *);
void in1RoleSet(Engine *, int index);

// ── CV modulation matrix ─────────────────────────────────────────────────────
int cvTargetCount();
std::string cvTargetName(int index);
int cvTargetGet(Engine *, int input); // input 0 = IN 2, 1 = IN 3
void cvTargetSet(Engine *, int input, int index);
int cvDepthGet(Engine *, int input);
void cvDepthSet(Engine *, int input, int pct);

// ── Display ──────────────────────────────────────────────────────────────────
int viewCount();
std::string viewName(int index); // "A+B" / "A" / "B"
int viewGet(Engine *);
void viewSet(Engine *, int index);

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

} // namespace chengine
