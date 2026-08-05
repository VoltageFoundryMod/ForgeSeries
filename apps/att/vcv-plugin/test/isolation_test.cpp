// isolation_test.cpp — per-instance state isolation for the chengine port.
//
// Compiles the firmware engine (fw_engine.cpp + shim + ../lib) into a host
// executable and drives TWO engines through the public chengine API to prove
// they do not share simulation/menu state. No VCV Rack, no hardware required.
//
// Build/run:  test/build_isolation_test.sh   (from vcv-plugin/)
//
// The firmware keeps all of its state in file-scope globals so the same lib/
// builds unchanged for the RP2040. Without the per-instance context-swap in
// fw_engine.cpp this test FAILS — changing engine A's system would also change
// engine B's. It is also the guard against a future firmware global being added
// but not registered in engine_state.def.

#include "engine/fw_engine.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace chengine;

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
static void step(Engine *e, float volts, bool trig, float out[4]) {
    const float cv[2] = {volts, volts};
    process(e, 8.0f / 44100.0f, cv, trig, out);
}

// Run the simulation for roughly `ms` milliseconds of engine time.
static void run(Engine *e, int ms) {
    float out[4] = {0};
    int blocks = (int)(ms / 1000.0f * 44100.0f / 8.0f);
    for (int i = 0; i < blocks; ++i)
        step(e, 0.0f, false, out);
}

int main() {
    std::printf("chengine two-instance isolation test\n");

    Engine *a = createEngine();
    Engine *b = createEngine();

    // ── Both start from the factory defaults ────────────────────────────────
    CHECK(systemGet(a, 0) == systemGet(b, 0), "both engines start on the same system");
    CHECK(coupleGet(a) == 0 && coupleGet(b) == 0, "both start with the generators unlinked");
    CHECK(systemName(systemGet(a, 0)) == "LORENZ" && systemName(systemGet(a, 1)) == "ROSSLER",
          "the factory patch is Lorenz + Rössler");

    // ── The systems are per-instance ────────────────────────────────────────
    systemSet(a, 0, 3); // Chua
    systemSet(a, 1, 7); // Aizawa
    CHECK(systemGet(a, 0) == 3 && systemGet(a, 1) == 7, "A: both systems set");
    CHECK(systemGet(b, 0) == 0 && systemGet(b, 1) == 1,
          "B: systems untouched (per-instance isolation)");

    // Changing the system must load THAT system's published parameters — the
    // parameter slots mean different things in different equations.
    CHECK(paramCount(a, 0) == 4, "A: Chua exposes four parameters");
    CHECK(paramName(a, 0, 0) == "ALPHA", "A: parameter 1 is named for Chua");
    CHECK(std::fabs(paramGet(a, 0, 0) - 15.6f) < 1e-4f, "A: Chua's alpha is at its default");

    // ── Rate, output shaping and the link are per-instance ──────────────────
    speedSet(a, 0, 6.0f);
    speedSet(a, 1, 0.05f);
    levelSet(a, 0, 40);
    offsetSet(a, 0, -25);
    smoothSet(a, 1, 80);
    autoRangeSet(a, 1, true);
    srcSet(a, 0, 0, 2); // Z
    srcSet(a, 0, 1, 1); // Y
    coupleSet(a, 65);

    CHECK(std::fabs(speedGet(a, 0) - 6.0f) < 1e-4f && std::fabs(speedGet(a, 1) - 0.05f) < 1e-4f,
          "A: speeds set");
    CHECK(levelGet(a, 0) == 40 && offsetGet(a, 0) == -25 && smoothGet(a, 1) == 80,
          "A: level + offset + smooth set");
    CHECK(autoRangeGet(a, 1) && !autoRangeGet(a, 0), "A: auto range set on B only");
    CHECK(srcGet(a, 0, 0) == 2 && srcGet(a, 0, 1) == 1, "A: output axes set");
    CHECK(coupleGet(a) == 65, "A: couple set");

    CHECK(std::fabs(speedGet(b, 0) - 1.0f) < 1e-4f, "B: speed still at its default");
    CHECK(levelGet(b, 0) == 100 && offsetGet(b, 0) == 0, "B: output shaping untouched");
    CHECK(!autoRangeGet(b, 1), "B: auto range untouched");
    CHECK(coupleGet(b) == 0, "B: couple untouched");

    // ── cvInputs.hpp file-scope globals are per-instance ────────────────────
    // These only stay independent if they are registered in engine_state.def —
    // this is the check that catches a forgotten registration.
    in1RoleSet(a, 3); // FREEZE
    cvTargetSet(a, 0, 4);
    cvDepthSet(a, 0, 77);
    viewSet(a, 2);
    CHECK(in1RoleGet(a) == 3 && cvTargetGet(a, 0) == 4 && cvDepthGet(a, 0) == 77,
          "A: IN 1 role + CV matrix set");
    CHECK(viewGet(a) == 2, "A: home view set");
    CHECK(in1RoleGet(b) == 0 && cvDepthGet(b, 0) == 0 && viewGet(b) == 0,
          "B: IN 1 role, CV matrix and view untouched");

    // ── The simulation runs and stays in range ──────────────────────────────
    float outA[4] = {0}, outB[4] = {0};
    bool outputsInRange = true;
    for (int i = 0; i < 40000; ++i) {
        float v = 2.5f + 2.0f * std::sin(i * 0.003f);
        const float cvA[2] = {v, v};
        const float cvB[2] = {v, v};
        process(a, 8.0f / 44100.0f, cvA, (i % 64) < 8, outA);
        process(b, 8.0f / 44100.0f, cvB, (i % 97) < 8, outB);
        for (int k = 0; k < 4; ++k) {
            if (outA[k] < -0.01f || outA[k] > 5.01f || outB[k] < -0.01f || outB[k] > 5.01f)
                outputsInRange = false;
            if (std::isnan(outA[k]) || std::isnan(outB[k]))
                outputsInRange = false;
        }
    }
    CHECK(outputsInRange, "all outputs stay within 0..5 V through process()");

    // The two jacks of one generator follow different axes of one orbit, so they
    // must be related but never the same voltage. Two identical outputs here is
    // the failure mode the whole module is built to avoid.
    CHECK(std::fabs(outB[0] - outB[1]) > 1e-4f, "B: the two jacks of generator A differ");
    CHECK(std::fabs(outB[2] - outB[3]) > 1e-4f, "B: the two jacks of generator B differ");

    // ── State stays isolated across the hot path ────────────────────────────
    CHECK(systemGet(a, 0) == 3 && systemGet(b, 0) == 0,
          "system stays isolated across process() blocks");
    CHECK(coupleGet(a) == 65 && coupleGet(b) == 0,
          "couple stays isolated across process() blocks");
    CHECK(in1RoleGet(a) == 3 && in1RoleGet(b) == 0,
          "IN 1 role stays isolated across process() blocks");

    // ── FREEZE really freezes (engine A's IN 1 role is FREEZE) ──────────────
    {
        float held[4] = {0};
        step(a, 0.0f, true, held);
        float again[4] = {0};
        for (int i = 0; i < 500; ++i)
            step(a, 0.0f, true, again);
        bool frozen = true;
        for (int k = 0; k < 4; ++k)
            if (std::fabs(held[k] - again[k]) > 1e-6f)
                frozen = false;
        CHECK(frozen, "A: IN 1 held high freezes every output");

        float moved[4] = {0};
        for (int i = 0; i < 500; ++i)
            step(a, 0.0f, false, moved);
        bool moving = false;
        for (int k = 0; k < 4; ++k)
            if (std::fabs(held[k] - moved[k]) > 1e-6f)
                moving = true;
        CHECK(moving, "A: releasing IN 1 resumes the orbits");
    }

    // ── The emulated OLED renders per instance ──────────────────────────────
    uint8_t fbA[1024], fbB[1024];
    getFramebuffer(a, fbA);
    getFramebuffer(b, fbB);
    bool framebuffersDiffer = false;
    for (int i = 0; i < 1024; ++i) {
        if (fbA[i] != fbB[i])
            framebuffersDiffer = true;
    }
    CHECK(framebuffersDiffer, "the two engines render different screens");

    // ── The plot actually animates ──────────────────────────────────────────
    // Regression guard for the freeze: RedrawDisplay() clears displayRefresh
    // after each frame, and HandleDisplay() needs displayRefresh AND
    // ShouldUpdate(), so without the home screen re-arming displayRefresh the
    // view renders a couple of frames and then stops dead.
    //
    // Comparing two consecutive frames is NOT enough — it passes even with the
    // bug, because a render still happens early on. Count distinct frames over a
    // stretch of time instead.
    {
        unsigned long prevHash = 0;
        int distinctFrames = 0;
        for (int i = 0; i < 20; ++i) {
            run(b, 50); // 50 ms per sample; the renderer is rate-limited to 20 fps
            uint8_t frame[1024];
            getFramebuffer(b, frame);
            unsigned long h = 1469598103934665603ULL;
            for (int k = 0; k < 1024; ++k) {
                h ^= frame[k];
                h *= 1099511628211ULL;
            }
            if (h != prevHash) {
                distinctFrames++;
                prevHash = h;
            }
        }
        std::printf("  (distinct frames over 1 s: %d / 20)\n", distinctFrames);
        CHECK(distinctFrames > 8, "the plot animates rather than freezing");
    }

    // ── Gestures act on one instance only ───────────────────────────────────
    reseedAll(a);
    reseedGen(a, 1);
    CHECK(systemGet(b, 0) == 0, "B unaffected by A's re-seed");

    // ── Persistence round-trips per instance ────────────────────────────────
    // serialize() has to commit the *live* state to slot 0 first. The firmware
    // only writes EEPROM on an explicit SAVE, so an implementation that just
    // dumps the buffer returns a blank blob and a Rack patch silently reloads at
    // factory defaults — which is exactly the bug these next checks guard.
    std::string blob = serialize(a);
    CHECK(!blob.empty(), "serialize() returns a non-empty EEPROM blob");
    bool blobHasContent = false;
    for (unsigned char ch : blob) {
        if (ch != 0xFF) { // 0xFF is erased flash: nothing was ever written
            blobHasContent = true;
            break;
        }
    }
    CHECK(blobHasContent, "serialize() commits the live state, not a blank blob");

    deserialize(b, blob);
    CHECK(systemGet(b, 0) == 3 && systemGet(b, 1) == 7, "B: systems restored from A's blob");
    CHECK(std::fabs(paramGet(b, 0, 0) - paramGet(a, 0, 0)) < 1e-4f,
          "B: system parameters restored from A's blob");
    CHECK(std::fabs(speedGet(b, 0) - 6.0f) < 1e-4f, "B: speed restored from A's blob");
    CHECK(levelGet(b, 0) == 40 && offsetGet(b, 0) == -25 && smoothGet(b, 1) == 80,
          "B: output shaping restored from A's blob");
    CHECK(autoRangeGet(b, 1), "B: auto range restored from A's blob");
    CHECK(srcGet(b, 0, 0) == 2 && srcGet(b, 0, 1) == 1, "B: output axes restored from A's blob");
    CHECK(coupleGet(b) == 65, "B: couple restored from A's blob");
    // A field missing from CollectParams() would be silently dropped here and
    // the patch would reload with the wrong routing.
    CHECK(in1RoleGet(b) == 3 && cvTargetGet(b, 0) == 4 && cvDepthGet(b, 0) == 77,
          "B: IN 1 role + CV matrix restored from A's blob");
    CHECK(viewGet(b) == 2, "B: home view restored from A's blob");

    CHECK(systemGet(a, 0) == 3, "A: system unchanged after B deserialized");
    CHECK(coupleGet(a) == 65, "A: couple unchanged after B deserialized");

    // ── Initialize / Randomize act on one instance only ─────────────────────
    reset(a);
    CHECK(systemGet(a, 0) == 0 && systemGet(a, 1) == 1 && coupleGet(a) == 0,
          "A: reset() restores the factory defaults");
    CHECK(std::fabs(speedGet(a, 1) - 0.30f) < 1e-4f && smoothGet(a, 1) == 15,
          "A: reset() restores the factory patch's slow B side");
    CHECK(systemGet(b, 0) == 3 && coupleGet(b) == 65, "B: unaffected by A's reset()");

    // Checked as separate invariants rather than one flag: a single "in range"
    // boolean tells you a roll was bad but not which of five ways, and the
    // interesting failures here are the ones a chaotic system finds on its own.
    bool outputsOk = true, systemsOk = true, speedsOk = true, axesOk = true, paramsOk = true;
    bool randomKeptRouting = true;
    int roleBefore = in1RoleGet(b), depthBefore = cvDepthGet(b, 0);
    int targetBefore = cvTargetGet(b, 0), viewBefore = viewGet(b);
    for (int i = 0; i < 100; ++i) {
        randomize(b);
        // Run a little so the orbits have to survive whatever was rolled.
        for (int k = 0; k < 300; ++k) {
            const float cv[2] = {2.5f, 2.5f};
            process(b, 8.0f / 44100.0f, cv, false, outB);
            for (int j = 0; j < 4; ++j) {
                if (std::isnan(outB[j]) || outB[j] < -0.01f || outB[j] > 5.01f) {
                    if (outputsOk)
                        std::printf("       roll %d: OUT %d = %g (systems %s / %s)\n", i, j,
                                    outB[j], systemName(systemGet(b, 0)).c_str(),
                                    systemName(systemGet(b, 1)).c_str());
                    outputsOk = false;
                }
            }
        }
        for (int g = 0; g < 2; ++g) {
            if (systemGet(b, g) < 0 || systemGet(b, g) >= systemCount())
                systemsOk = false;
            if (speedGet(b, g) < speedMin() || speedGet(b, g) > speedMax()) {
                if (speedsOk)
                    std::printf("       roll %d: speed %g\n", i, speedGet(b, g));
                speedsOk = false;
            }
            // A generator whose two jacks read the same axis is two copies of one
            // voltage — the one roll the randomizer must never produce.
            if (srcGet(b, g, 0) == srcGet(b, g, 1))
                axesOk = false;
            for (int k = 0; k < paramCount(b, g); ++k) {
                if (paramGet(b, g, k) < paramMin(b, g, k) ||
                    paramGet(b, g, k) > paramMax(b, g, k)) {
                    if (paramsOk)
                        std::printf("       roll %d: %s P%d = %g (%g..%g)\n", i,
                                    systemName(systemGet(b, g)).c_str(), k + 1,
                                    paramGet(b, g, k), paramMin(b, g, k), paramMax(b, g, k));
                    paramsOk = false;
                }
            }
        }
        if (in1RoleGet(b) != roleBefore || cvDepthGet(b, 0) != depthBefore ||
            cvTargetGet(b, 0) != targetBefore || viewGet(b) != viewBefore)
            randomKeptRouting = false;
    }
    CHECK(outputsOk, "randomize() never produces an out-of-range or NaN output");
    CHECK(systemsOk, "randomize() picks a legal system");
    CHECK(speedsOk, "randomize() picks a legal speed");
    CHECK(axesOk, "randomize() keeps both jacks of a pair on different axes");
    CHECK(paramsOk, "randomize() keeps every parameter inside its system's range");
    CHECK(randomKeptRouting, "randomize() leaves the routing and the view alone");
    CHECK(systemGet(a, 0) == 0, "A: unaffected by B's randomize()");

    destroyEngine(a);
    destroyEngine(b);

    if (g_failures) {
        std::printf("\n== %d FAILURE(S) ==\n", g_failures);
        return 1;
    }
    std::printf("\n== ALL PASS ==\n");
    return 0;
}
