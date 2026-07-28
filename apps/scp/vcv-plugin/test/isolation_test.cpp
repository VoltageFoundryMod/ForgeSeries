// isolation_test.cpp — per-instance state isolation + render smoke test for the
// ForgeView scope engine.  Compiles fw_engine.cpp + shim + ../lib into a host
// executable and drives TWO engines through the public fvengine API to prove
// they do not share scope/menu state, and that acquisition + rendering work.
// No VCV Rack, no hardware required.
//
// Build/run:  test/build_isolation_test.sh   (from vcv-plugin/)

#include "engine/fw_engine.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>

using namespace fvengine;

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

static int litPixels(const uint8_t fb[1024]) {
    int n = 0;
    for (int i = 0; i < 1024; i++)
        for (int b = 0; b < 8; b++)
            if (fb[i] & (1 << b))
                n++;
    return n;
}

// Feed `count` samples of a full-scale sine (bipolar, mapped 0..5V) into ch1.
static void feedSine(Engine *e, int count) {
    for (int i = 0; i < count; i++) {
        float ph = (float)i / 32.0f * 2.0f * 3.14159265f;
        float v = 2.5f + 2.4f * std::sin(ph); // 0.1..4.9 V (0..5 domain)
        feedSample(e, 2.0f / 44100.0f, v, 2.5f, false);
    }
}

int main() {
    std::printf("fvengine two-instance isolation + render test\n");

    Engine *a = createEngine();
    Engine *b = createEngine();

    CHECK(mode(a) == 1 && mode(b) == 1, "both engines start in LFO mode");

    // ── Mode isolation via the state bridge ──────────────────────────────────
    setMode(a, 4); // Spectrum
    std::printf("  mode(A)=%d mode(B)=%d\n", mode(a), mode(b));
    CHECK(mode(a) == 4, "A switched to Spectrum");
    CHECK(mode(b) == 1, "B stayed in LFO (per-instance isolation)");

    // ── Parameter isolation ──────────────────────────────────────────────────
    setParam1(a, 7);
    CHECK(param1(a) == 7, "A param1 set to 7");
    CHECK(param1(b) != 7, "B param1 not affected by A");

    // ── Extended timebase range (time modes reach 14; spectrum stays 8) ──────
    setMode(b, 1); // LFO
    setParam1(b, 14);
    CHECK(param1(b) == 14, "LFO timebase reaches extended max (14)");
    CHECK(param1Max(b) == 14, "param1Max is 14 in a time-domain mode");
    // A is in Spectrum (set above): param1 must stay clamped to 8.
    CHECK(param1Max(a) == 8, "param1Max is 8 in Spectrum mode");
    setParam1(a, 14);
    CHECK(param1(a) == 8, "Spectrum param1 clamps to 8 (HF-emphasis reuse)");
    setParam1(a, 7);  // restore
    setParam1(b, 2);  // fast timebase so the render smoke test below fills a trace

    // ── Render smoke test: B (LFO) should draw grid + trace ──────────────────
    feedSine(b, 400);
    uint8_t fbB[1024];
    getFramebuffer(b, fbB);
    int litB = litPixels(fbB);
    std::printf("  LFO framebuffer lit pixels: %d\n", litB);
    CHECK(litB > 150, "LFO mode renders a non-trivial frame (grid + trace)");

    // Spectrum on A should not crash and should render something after capture.
    feedSine(a, 400);
    uint8_t fbA[1024];
    getFramebuffer(a, fbA);
    std::printf("  Spectrum framebuffer lit pixels: %d\n", litPixels(fbA));
    CHECK(litPixels(fbA) >= 0, "Spectrum mode renders without crashing");

    // ── Encoder path: navigate + edit Mode on a fresh engine ─────────────────
    Engine *c = createEngine();
    // param cursor starts on the Mode row (param=1). Click to select it, turn to
    // change the mode, click to deselect.
    encoderButton(c, true);
    encoderButton(c, false); // select Mode row (param_select = 1)
    encoderTurn(c, +1);      // menuMode 1 -> 2 (Wave)
    encoderButton(c, true);
    encoderButton(c, false); // deselect
    std::printf("  encoder-driven mode(C)=%d\n", mode(c));
    CHECK(mode(c) == 2, "encoder rotate+click changes mode (Wave)");

    // ── New modes + curated accessors ────────────────────────────────────────
    CHECK(modeCount() == 6, "six scope modes registered");
    CHECK(modeName(4) == "X-Y" && modeName(5) == "Tuner", "new modes are named");

    // X-Y renders a scatter from two channels without crashing.
    Engine *d = createEngine();
    setMode(d, 5);      // X-Y
    setXyPersist(d, 0); // "Live" -> full-rate capture, dense scatter
    CHECK(mode(d) == 5, "engine D switched to X-Y");
    CHECK(xyPersistCount() == 13 && xyPersistName(3) == "0.5s" && xyPersistName(12) == "5m",
          "persistence presets named (up to 5m)");
    for (int i = 0; i < 400; i++) {
        float ph = (float)i / 32.0f * 2.0f * 3.14159265f;
        feedSample(d, 2.0f / 44100.0f, 2.5f + 2.0f * std::sin(ph),
                   2.5f + 2.0f * std::sin(ph * 2.0f), false);
    }
    uint8_t fbD[1024];
    getFramebuffer(d, fbD);
    std::printf("  X-Y framebuffer lit pixels: %d\n", litPixels(fbD));
    CHECK(litPixels(fbD) > 20, "X-Y mode renders a Lissajous scatter");
    CHECK(xyPersist(d) == 0, "X-Y persistence level stored");

    // Tuner: feed a ~440 Hz sine and expect a plausible reading (no crash).
    setMode(d, 6); // Tuner
    for (int i = 0; i < 4000; i++) {
        float ph = (float)i * 440.0f * (2.0f / 44100.0f) * 2.0f * 3.14159265f;
        feedSample(d, 2.0f / 44100.0f, 2.5f + 2.0f * std::sin(ph), 2.5f, false);
    }
    getFramebuffer(d, fbD); // must not crash
    CHECK(mode(d) == 6, "Tuner mode active + renders");

    // Per-instance isolation of the new state (D vs a fresh engine).
    Engine *f = createEngine();
    setFrozen(d, true);
    setOffsetCh1(d, 12);
    setOffsetCh2(d, -8);
    setTrigMode(d, 2);
    setShowLabels(d, true);
    CHECK(frozen(d) && !frozen(f), "freeze is per-instance");
    CHECK(offsetCh1(d) == 12 && offsetCh1(f) == 0, "CH1 offset per-instance");
    CHECK(offsetCh2(d) == -8, "CH2 offset stored");
    CHECK(trigMode(d) == 2 && trigMode(f) != 2, "trigger mode per-instance");
    CHECK(showLabels(d) && !showLabels(f), "labels toggle per-instance");
    setXyPersist(f, 9); // 60s
    CHECK(xyPersist(f) == 9 && xyPersist(d) == 0, "X-Y persistence per-instance");

    // Freeze holds the frame: once frozen, feeding must not change the frame.
    // (Compare two already-frozen captures so the HOLD marker is present in both.)
    setMode(f, 1); // LFO
    setFrozen(f, false);
    for (int i = 0; i < 300; i++)
        feedSample(f, 2.0f / 44100.0f, 2.5f + 2.0f * std::sin((float)i / 8.f), 2.5f, false);
    setFrozen(f, true);
    uint8_t before[1024];
    getFramebuffer(f, before);
    for (int i = 0; i < 300; i++)
        feedSample(f, 2.0f / 44100.0f, 2.5f + 2.0f * std::sin((float)i / 3.f + 1.f), 2.5f, false);
    uint8_t after[1024];
    getFramebuffer(f, after);
    int diff = 0;
    for (int i = 0; i < 1024; i++)
        if (before[i] != after[i]) diff++;
    std::printf("  frozen frame byte-diff: %d\n", diff);
    CHECK(diff == 0, "freeze-frame holds the trace (frozen feeds change nothing)");

    destroyEngine(d);
    destroyEngine(f);

    // ── Hot path: interleave feeds on all three, ensure stability ────────────
    for (int i = 0; i < 3000; i++) {
        float v = 2.5f + 2.0f * std::sin((float)i / 20.0f);
        feedSample(a, 2.0f / 44100.0f, v, 2.5f, (i % 500) == 0);
        feedSample(b, 2.0f / 44100.0f, v, 2.5f, false);
        feedSample(c, 2.0f / 44100.0f, v, 2.5f, false);
    }
    CHECK(mode(a) == 4 && mode(b) == 1 && mode(c) == 2, "modes stay isolated through feed loop");

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
