// isolation_test.cpp — per-instance state isolation for the gfengine port.
//
// Compiles the firmware engine (fw_engine.cpp + shim + ../lib) into a host
// executable and drives TWO engines through the public gfengine API to prove
// they do not share simulation/menu state. No VCV Rack, no hardware required.
//
// Build/run:  test/build_isolation_test.sh   (from vcv-plugin/)
//
// The firmware keeps all of its state in file-scope globals so the same lib/
// builds unchanged for the RP2040. Without the per-instance context-swap in
// fw_engine.cpp this test FAILS — dropping engine A's gravity would also change
// engine B. It is also the guard against a future firmware global being added
// but not registered in engine_state.def.

#include "engine/fw_engine.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace gfengine;

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
    std::printf("gfengine two-instance isolation test\n");

    Engine *a = createEngine();
    Engine *b = createEngine();

    // ── Both start from the factory defaults ────────────────────────────────
    CHECK(proximityGet(a) == 0 && proximityGet(b) == 0,
          "both engines start with the containers apart");
    CHECK(gravityGet(a, 0) == gravityGet(b, 0), "both start with the same gravity");

    // ── The signature control is per-instance ───────────────────────────────
    proximitySet(a, 100);
    couplingSet(a, 25);
    CHECK(proximityGet(a) == 100 && couplingGet(a) == 25, "A: proximity + couple set");
    CHECK(proximityGet(b) == 0, "B: proximity untouched (per-instance isolation)");
    CHECK(couplingGet(b) == 60, "B: couple still at its default");

    // ── Loop / phrase mode is per-instance ──────────────────────────────────
    loopBeatsSet(a, 4);
    loopWakeSet(a, 2);
    loopNapSet(a, 1);
    loopShiftSet(a, 1, 3);
    CHECK(loopBeatsGet(a) == 4 && loopWakeGet(a) == 2 && loopNapGet(a) == 1 &&
              loopShiftGet(a, 1) == 3,
          "A: loop length + nap/wake + shift set");
    CHECK(loopBeatsGet(b) == 0 && loopNapGet(b) == 0 && loopShiftGet(b, 1) == 0,
          "B: loop settings untouched (per-instance isolation)");

    // ── Physics parameters are per-instance ─────────────────────────────────
    gravitySet(a, 0, 700);
    bounceSet(a, 0, 90);
    ballsSet(a, 0, 8);
    pegsSet(a, 0, 16);
    spinSet(a, 1, 0); // container B of engine A: fastest ratio
    CHECK(gravityGet(a, 0) == 700 && bounceGet(a, 0) == 90, "A: gravity + bounce set");
    CHECK(ballsGet(a, 0) == 8 && pegsGet(a, 0) == 16, "A: balls + pegs set");
    CHECK(gravityGet(b, 0) == 220 && ballsGet(b, 0) == 3,
          "B: physics parameters still at defaults");

    // ── Musical parameters are per-instance ─────────────────────────────────
    scaleSet(a, 0, 8); // pentatonic minor
    rootSet(a, 0, 3);  // D#
    spreadSet(a, 0, 4);
    biasSet(a, 0, -60);
    CHECK(scaleGet(a, 0) == 8 && rootGet(a, 0) == 3 && spreadGet(a, 0) == 4 &&
              biasGet(a, 0) == -60,
          "A: scale + root + spread + bias set");

    // ── Note thinning is per-instance ───────────────────────────────────────
    densitySet(a, 0, 35);
    spaceSet(a, 0, 3); // one beat
    CHECK(densityGet(a, 0) == 35 && spaceGet(a, 0) == 3, "A: density + space set");
    CHECK(densityGet(b, 0) == 100 && spaceGet(b, 0) == 0,
          "B: density + space still at their defaults");
    CHECK(scaleGet(b, 0) == 1 && rootGet(b, 0) == 0 && spreadGet(b, 0) == 2 &&
              biasGet(b, 0) == 0,
          "B: scale + root + spread + bias untouched");

    // ── cvInputs.hpp file-scope globals are per-instance ────────────────────
    // These only stay independent if they are registered in engine_state.def —
    // this is the check that catches a forgotten registration.
    in1RoleSet(a, 2); // KICK
    cvTargetSet(a, 0, 3);
    cvDepthSet(a, 0, 77);
    CHECK(in1RoleGet(a) == 2 && cvTargetGet(a, 0) == 3 && cvDepthGet(a, 0) == 77,
          "A: IN 1 role + CV matrix set");
    CHECK(in1RoleGet(b) == 0 && cvDepthGet(b, 0) == 0,
          "B: IN 1 role + CV matrix untouched");

    // ── Clock state is per-instance ─────────────────────────────────────────
    bpmSet(a, 200);
    quantizeSet(a, 3); // 1/16
    CHECK(bpmGet(a) == 200 && quantizeGet(a) == 3, "A: tempo + quantize set");
    CHECK(bpmGet(b) == 120 && quantizeGet(b) == 0, "B: tempo + quantize untouched");

    // ── The simulation actually runs and stays in range ─────────────────────
    float outA[4] = {0}, outB[4] = {0};
    bool outputsInRange = true;
    for (int i = 0; i < 20000; ++i) {
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

    // ── State stays isolated across the hot path ────────────────────────────
    CHECK(gravityGet(a, 0) == 700 && gravityGet(b, 0) == 220,
          "gravity stays isolated across process() blocks");
    CHECK(proximityGet(a) == 100 && proximityGet(b) == 0,
          "proximity stays isolated across process() blocks");
    CHECK(in1RoleGet(a) == 2 && in1RoleGet(b) == 0,
          "IN 1 role stays isolated across process() blocks");
    CHECK(scaleGet(a, 0) == 8 && scaleGet(b, 0) == 1,
          "scale stays isolated across process() blocks");

    // ── The sequencer is actually producing notes ───────────────────────────
    // B is on defaults (quantize off), so after several seconds of falling balls
    // it must have emitted something.
    CHECK(currentNote(b, 0) != "--", "B container A has played a note");
    CHECK(currentNote(b, 1) != "--", "B container B has played a note");

    // ── The emulated OLED renders per instance ──────────────────────────────
    // A and B differ in proximity, scale and ball count, so their physics screens
    // cannot be identical.
    uint8_t fbA[1024], fbB[1024];
    getFramebuffer(a, fbA);
    getFramebuffer(b, fbB);
    bool framebuffersDiffer = false;
    for (int i = 0; i < 1024; ++i) {
        if (fbA[i] != fbB[i])
            framebuffersDiffer = true;
    }
    CHECK(framebuffersDiffer, "the two engines render different screens");

    // ── The physics view actually animates ──────────────────────────────────
    // Regression guard for the freeze: RedrawDisplay() clears displayRefresh
    // after each frame, and HandleDisplay() needs displayRefresh AND
    // ShouldUpdate(), so without the home screen re-arming displayRefresh the
    // view rendered a couple of frames and then stopped dead.
    //
    // Comparing just two consecutive frames is NOT enough — it passes even with
    // the bug, because a render still happens early on and two samples can
    // straddle it. Count distinct frames over a stretch of time instead:
    // measured, the bug yields 2 of 20 and a healthy screen yields most of them.
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
    CHECK(distinctFrames > 8, "the physics view animates rather than freezing");

    // ── Gestures act on one instance only ───────────────────────────────────
    resetBalls(a);
    kickBalls(a);
    CHECK(gravityGet(b, 0) == 220, "B unaffected by A's reset/kick");

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
    CHECK(gravityGet(b, 0) == 700 && bounceGet(b, 0) == 90,
          "B: physics restored from A's blob");
    CHECK(ballsGet(b, 0) == 8 && pegsGet(b, 0) == 16,
          "B: balls + pegs restored from A's blob");
    CHECK(scaleGet(b, 0) == 8 && rootGet(b, 0) == 3 && spreadGet(b, 0) == 4 &&
              biasGet(b, 0) == -60,
          "B: scale + root + spread + bias restored from A's blob");
    CHECK(densityGet(b, 0) == 35 && spaceGet(b, 0) == 3,
          "B: density + space restored from A's blob");
    CHECK(proximityGet(b) == 100 && couplingGet(b) == 25,
          "B: proximity + couple restored from A's blob");
    CHECK(bpmGet(b) == 200 && quantizeGet(b) == 3,
          "B: tempo + quantize restored from A's blob");
    // A loop setting missing from CollectParams() would be silently dropped here
    // and the patch would reload not looping.
    CHECK(loopBeatsGet(b) == 4 && loopWakeGet(b) == 2 && loopNapGet(b) == 1 &&
              loopShiftGet(b, 1) == 3,
          "B: loop settings restored from A's blob");
    CHECK(in1RoleGet(b) == 2 && cvTargetGet(b, 0) == 3 && cvDepthGet(b, 0) == 77,
          "B: IN 1 role + CV matrix restored from A's blob");

    CHECK(gravityGet(a, 0) == 700, "A: gravity still 700 after B deserialized");
    CHECK(proximityGet(a) == 100, "A: proximity still 100 after B deserialized");

    // ── Initialize / Randomize act on one instance only ─────────────────────
    // Rack's module actions. reset() is the factory defaults; randomize() must
    // stay inside the parameters' legal ranges and must leave the clock and the
    // CV routing alone (those are patch wiring, not sound design).
    reset(a);
    CHECK(gravityGet(a, 0) == 220 && ballsGet(a, 0) == 3 && proximityGet(a) == 0,
          "A: reset() restores the factory defaults");

    // ── The factory patch ships two worked examples ─────────────────────────
    // Container 0 is the busy sequencer the module has always been; container 1
    // is a slow ambient voice. They are deliberately far apart so the first patch
    // cable demonstrates both ends of the range — see LoadDefaultParams().
    CHECK(gravityGet(a, 0) == 220 && ballsGet(a, 0) == 3 && pegsGet(a, 0) == 8 &&
              densityGet(a, 0) == 100 && spaceGet(a, 0) == 0 && decayGet(a, 0) == 100,
          "factory A: busy sequencer, no thinning");
    CHECK(gravityGet(a, 1) == 20 && ballsGet(a, 1) == 1 && pegsGet(a, 1) == 5 &&
              densityGet(a, 1) == 85 && spaceGet(a, 1) == 4 /* 2 beats */,
          "factory B: slow, thinned");
    // B's whole point is an envelope the module could not previously hold. It
    // only works because SPACE floors the gap, so it has to fit inside that
    // floor — 2 beats at the factory 120 BPM is 1000 ms.
    {
        int envelope = attackGet(a, 1) + decayGet(a, 1);
        int floorMs = 2 * 60000 / bpmGet(a);
        CHECK(envelope < floorMs,
              "factory B: envelope fits inside the SPACE floor");
    }
    // Independent containers, or the two examples would not be separable.
    CHECK(proximityGet(a) == 0, "factory: containers start apart");
    CHECK(gravityGet(b, 0) == 700 && proximityGet(b) == 100,
          "B: unaffected by A's reset()");

    bool randomInRange = true, randomKeptRouting = true;
    int bpmBefore = bpmGet(b), roleBefore = in1RoleGet(b), depthBefore = cvDepthGet(b, 0);
    for (int i = 0; i < 200; ++i) {
        randomize(b);
        // Run a little so the physics has to survive whatever was rolled.
        for (int k = 0; k < 200; ++k) {
            const float cv[2] = {2.5f, 2.5f};
            process(b, 8.0f / 44100.0f, cv, false, outB);
            for (int j = 0; j < 4; ++j) {
                if (std::isnan(outB[j]) || outB[j] < -0.01f || outB[j] > 5.01f)
                    randomInRange = false;
            }
        }
        for (int c = 0; c < 2; ++c) {
            if (gravityGet(b, c) < gravityMin() || gravityGet(b, c) > gravityMax() ||
                ballsGet(b, c) < ballsMin() || ballsGet(b, c) > ballsMax() ||
                pegsGet(b, c) < pegsMin() || pegsGet(b, c) > pegsMax() ||
                scaleGet(b, c) < 0 || scaleGet(b, c) >= scaleCount() ||
                spreadGet(b, c) < spreadMin() || spreadGet(b, c) > spreadMax() ||
                biasGet(b, c) < biasMin() || biasGet(b, c) > biasMax())
                randomInRange = false;
            // A container whose whole peg ring is muted is silent — the
            // randomizer tops the mask back up specifically to prevent that.
            int live = 0;
            for (int p = 0; p < pegsGet(b, c); ++p)
                live += pegEnabledGet(b, c, p) ? 1 : 0;
            if (live == 0)
                randomInRange = false;
        }
        if (bpmGet(b) != bpmBefore || in1RoleGet(b) != roleBefore ||
            cvDepthGet(b, 0) != depthBefore)
            randomKeptRouting = false;
    }
    CHECK(randomInRange, "randomize() stays in range and keeps every container audible");
    CHECK(randomKeptRouting, "randomize() leaves the clock and CV routing alone");
    CHECK(gravityGet(a, 0) == 220 && ballsGet(a, 0) == 3,
          "A: unaffected by B's randomize()");

    destroyEngine(a);
    destroyEngine(b);

    if (g_failures) {
        std::printf("\n== %d FAILURE(S) ==\n", g_failures);
        return 1;
    }
    std::printf("\n== ALL PASS ==\n");
    return 0;
}
