// isolation_test.cpp — per-instance state isolation for the cfengine port.
//
// Compiles the firmware engine (fw_engine.cpp + shim + ../lib) into a host
// executable and drives TWO engines through the public cfengine API to prove
// they do not share DSP/menu state.  No VCV Rack, no hardware required.
//
// Build/run:  test/build_isolation_test.sh   (from vcv-plugin/)
//
// Before the per-instance context-swap this test FAILS (turning engine A's
// encoder also moves engine B, because the firmware globals are shared).
// After the fix it PASSES.

#include "engine/fw_engine.hpp"

#include <cstdio>

using namespace cfengine;

static int g_failures = 0;
#define CHECK(cond, msg)                              \
    do {                                              \
        if (!(cond)) {                                \
            std::printf("  FAIL: %s\n", msg);         \
            ++g_failures;                             \
        } else {                                      \
            std::printf("  ok  : %s\n", msg);         \
        }                                             \
    } while (0)

// Drive an engine's encoder to change its BPM by `steps` detents (+/-).
// Menu item 1 is BPM (a MENU_EDIT with the setBPM setter); the engine starts
// on item 2, so step back to 1, click to enter edit, turn, click to exit.
static void bumpBPM(Engine *e, int steps) {
    encoderTurn(e, -1);      // menuItem 2 -> 1 (BPM)
    encoderButton(e, true);  // press
    encoderButton(e, false); // release edge -> enter edit mode
    encoderTurn(e, steps);   // setBPM(+/-1) per detent
    encoderButton(e, true);  // press
    encoderButton(e, false); // release edge -> exit edit mode
}

int main() {
    std::printf("cfengine two-instance isolation test\n");

    Engine *a = createEngine();
    Engine *b = createEngine();

    CHECK(bpm(a) == 120 && bpm(b) == 120, "both engines start at 120 BPM");

    // Raise A's BPM only. B is never touched.
    bumpBPM(a, +20);

    std::printf("  after bumping A by +20: bpm(A)=%d bpm(B)=%d\n", bpm(a), bpm(b));
    CHECK(bpm(a) == 140, "A changed to 140 BPM");
    CHECK(bpm(b) == 120, "B stayed at 120 BPM (per-instance isolation)");

    // And the reverse direction on B only, to be sure the coupling isn't one-way.
    bumpBPM(b, -30);
    std::printf("  after bumping B by -30: bpm(A)=%d bpm(B)=%d\n", bpm(a), bpm(b));
    CHECK(bpm(a) == 140, "A still 140 BPM after moving B");
    CHECK(bpm(b) == 90, "B changed to 90 BPM");

    // Hot path: run the audio-rate entry point on both instances (this is where
    // the swap runs on every block).  State must stay isolated and outputs sane.
    const float cv[2] = {0.0f, 0.0f};
    float outA[4] = {0}, outB[4] = {0};
    bool outputsInRange = true;
    for (int i = 0; i < 2000; ++i) {
        process(a, 8.0f / 44100.0f, cv, false, outA);
        process(b, 8.0f / 44100.0f, cv, false, outB);
        for (int k = 0; k < 4; ++k)
            if (outA[k] < -0.01f || outA[k] > 5.01f || outB[k] < -0.01f || outB[k] > 5.01f)
                outputsInRange = false;
    }
    CHECK(outputsInRange, "all outputs stay within 0..5 V through process()");
    CHECK(bpm(a) == 140 && bpm(b) == 90, "BPM stays isolated across process() blocks");

    destroyEngine(a);
    destroyEngine(b);

    if (g_failures) {
        std::printf("\n== %d FAILURE(S) ==\n", g_failures);
        return 1;
    }
    std::printf("\n== ALL PASS ==\n");
    return 0;
}
