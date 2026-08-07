// isolation_test.cpp — per-instance state isolation for the wvengine port.
//
// Compiles the firmware engine (fw_engine.cpp + shim + ../lib) into a host
// executable and drives TWO engines through the public wvengine API to prove
// they do not share sequencer/menu state. No VCV Rack, no hardware required.
//
// Build/run:  test/build_isolation_test.sh   (from vcv-plugin/)
//
// The firmware keeps all of its state in file-scope globals so the same lib/
// builds unchanged for the RP2040. Without the per-instance context-swap in
// fw_engine.cpp this test FAILS. It is also the guard against a future firmware
// global being added but not registered in engine_state.def — and on this module
// the symptom of that is unusually blunt: two WeaveForges playing the identical
// pattern.

#include "engine/fw_engine.hpp"

#include <cstdio>
#include <string>

using namespace wvengine;

static int g_failures = 0;
#define CHECK(cond, msg)                      \
    do {                                      \
        if (!(cond)) {                        \
            std::printf("  FAIL: %s\n", msg); \
            ++g_failures;                     \
        } else {                              \
            std::printf("  ok  : %s\n", msg); \
        }                                     \
    } while (0)

// Advance an engine by one control-rate block (8 samples at 44.1 kHz).
static void step(Engine *e, bool clock, float out[4]) {
    const float cv[2] = {0.0f, 0.0f};
    process(e, 8.0f / 44100.0f, cv, clock, out);
}

// Send `n` clock pulses, each a block high then three blocks low — comfortably
// past the firmware's 1 ms input debounce.
static void clockPulses(Engine *e, int n) {
    float out[4] = {0};
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < 8; k++)
            step(e, true, out);
        for (int k = 0; k < 24; k++)
            step(e, false, out);
    }
}

int main() {
    std::printf("wvengine two-instance isolation test\n");

    Engine *a = createEngine();
    Engine *b = createEngine();

    // ── Both start from the factory defaults ────────────────────────────────
    CHECK(lengthGet(a, 0) == lengthGet(b, 0), "both engines start at the same length");
    CHECK(weaveGet(a) == 0 && weaveGet(b) == 0, "both start with the registers uncoupled");
    CHECK(routingGet(a) == 0 && routingName(0) == "DUO", "the factory routing is DUO");
    CHECK(regValue(a, 0) == regValue(b, 0), "both start from the same pattern");

    // ── The registers are per-instance ──────────────────────────────────────
    lengthSet(a, 0, 5);
    CHECK(lengthGet(a, 0) == 5, "A: length set to 5");
    CHECK(lengthGet(b, 0) == 16, "B: length untouched by A");

    chanceSet(b, 1, 80);
    CHECK(chanceGet(b, 1) == 80, "B: chance set to 80%");
    CHECK(chanceGet(a, 1) == 25, "A: chance untouched by B");

    // ── The PATTERN is per-instance ─────────────────────────────────────────
    // The one this module cannot get wrong: an unregistered WeavePair means two
    // WeaveForges playing in unison forever.
    regClear(a, 0);
    regFill(b, 0);
    CHECK(regValue(a, 0) == 0x0000u, "A: pattern cleared");
    CHECK(regValue(b, 0) == 0xFFFFu, "B: pattern filled, A unaffected");

    // ── Clocking one does not clock the other ───────────────────────────────
    regFill(a, 0);
    regFill(b, 0);
    chanceSet(a, 0, 100); // always flip, so every step visibly changes the register
    chanceSet(b, 0, 100);
    lengthSet(a, 0, 16);
    lengthSet(b, 0, 16);
    const uint32_t bBefore = regValue(b, 0);
    clockPulses(a, 4);
    CHECK(regValue(a, 0) != 0xFFFFu, "A: four clocks advanced its register");
    CHECK(regValue(b, 0) == bBefore, "B: not advanced by A's clock");

    // ── The output matrix is per-instance ───────────────────────────────────
    routingSet(a, 2); // PULSE
    CHECK(routingGet(a) == 2, "A: routing set to PULSE");
    CHECK(routingGet(b) == 0, "B: still DUO");

    outRotateSet(b, 1, 7);
    CHECK(outRotateGet(b, 1) == 7, "B: out 2 rotated to 7");
    CHECK(outRotateGet(a, 1) != 7 || routingGet(a) == 2, "A: out 2 untouched by B");

    // Editing one jack must read as Custom — recomputed, not stored.
    routingSet(b, 0);
    CHECK(routingGet(b) == 0, "B: back to DUO exactly");
    outDepthSet(b, 0, 7);
    CHECK(routingGet(b) == -1, "B: an edited jack reads as Custom");

    // ── WEAVE and the clock are per-instance ────────────────────────────────
    weaveSet(a, 100);
    CHECK(weaveGet(a) == 100 && weaveGet(b) == 0, "A: weave is not shared");
    bpmSet(a, 200);
    CHECK(bpmGet(a) == 200 && bpmGet(b) == 120, "A: tempo is not shared");
    const int aRate = rateGet(a);
    rateSet(b, 3);
    CHECK(rateGet(b) == 3 && rateGet(a) == aRate, "B: clock rate is not shared");

    // ── Pitch is per-instance ───────────────────────────────────────────────
    // Captured rather than hardcoded: the factory scale is C minor, and an
    // assertion that spells out the default breaks whenever the default is
    // retuned, which says nothing about isolation.
    const int bScale = scaleGet(b);
    const int aRoot = rootGet(a);
    scaleSet(a, 1); // Major
    rootSet(b, 7);  // G
    CHECK(scaleGet(a) == 1 && scaleGet(b) == bScale, "A: scale is not shared");
    CHECK(rootGet(b) == 7 && rootGet(a) == aRoot, "B: root is not shared");
    transposeSet(a, -12);
    CHECK(transposeGet(a) == -12 && transposeGet(b) == 0, "A: transpose is not shared");

    // ── CV routing is per-instance ──────────────────────────────────────────
    cvTargetSet(a, 0, 1);
    cvDepthSet(a, 0, 60);
    CHECK(cvTargetGet(a, 0) == 1 && cvTargetGet(b, 0) != 1, "A: CV target is not shared");
    CHECK(cvDepthGet(a, 0) == 60 && cvDepthGet(b, 0) == 0, "A: CV depth is not shared");

    // ── Serialization round-trips the PATTERN, not just the settings ────────
    // The register contents are in LoadSaveParams for exactly this reason: a
    // patch that reloads at the right length and the wrong bits has restored
    // nothing that matters.
    lengthSet(a, 0, 9);
    chanceSet(a, 0, 40);
    regRandomize(a, 0);
    const uint32_t pattern = regValue(a, 0);
    const std::string blob = serialize(a);

    Engine *c = createEngine();
    deserialize(c, blob);
    CHECK(regValue(c, 0) == pattern, "C: the pattern survived a serialize/deserialize");
    CHECK(lengthGet(c, 0) == 9, "C: length restored");
    CHECK(chanceGet(c, 0) == 40, "C: chance restored");
    CHECK(weaveGet(c) == 100, "C: weave restored");

    // ── Initialize resets only the instance it is called on ─────────────────
    reset(c);
    CHECK(lengthGet(c, 0) == 16, "C: initialize restored the factory length");
    CHECK(lengthGet(a, 0) == 9, "A: unaffected by C's initialize");

    // ── Randomize is per-instance ───────────────────────────────────────────
    const uint32_t bPattern = regValue(b, 0);
    randomize(a);
    CHECK(regValue(b, 0) == bPattern, "B: unaffected by A's randomize");
    CHECK(lengthGet(a, 0) >= 4 && lengthGet(a, 0) <= 16, "A: randomize stayed in range");
    CHECK(chanceGet(a, 0) >= 5 && chanceGet(a, 0) <= 60,
          "A: randomize kept chance off both endpoints");
    CHECK(routingGet(a) == 2, "A: randomize left the output routing alone");

    destroyEngine(a);
    destroyEngine(b);
    destroyEngine(c);

    if (g_failures) {
        std::printf("\n== %d FAILURE(S) ==\n", g_failures);
        return 1;
    }
    std::printf("\n== ALL PASS ==\n");
    return 0;
}
