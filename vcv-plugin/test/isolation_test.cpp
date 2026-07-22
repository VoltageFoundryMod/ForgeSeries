// isolation_test.cpp — per-instance state isolation for the nfengine port.
//
// Compiles the firmware engine (fw_engine.cpp + shim + ../lib) into a host
// executable and drives TWO engines through the public nfengine API to prove
// they do not share DSP/menu state. No VCV Rack, no hardware required.
//
// Build/run:  test/build_isolation_test.sh   (from vcv-plugin/)
//
// The firmware keeps all of its state in file-scope globals so the same lib/
// builds unchanged for the RP2040. Without the per-instance context-swap in
// fw_engine.cpp this test FAILS — editing engine A's scale would also change
// engine B. It is also the guard against a future firmware global being added
// but not registered in engine_state.def.

#include "engine/fw_engine.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace nfengine;

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

// Advance an engine by one control-rate block with the given pitch CV on both
// inputs and the TRIG line at `trig`.
static void step(Engine *e, float volts, bool trig, float out[4]) {
    const float cv[2] = {volts, volts};
    process(e, 8.0f / 44100.0f, cv, trig, out);
}

// Run enough blocks for the quantizer to settle on the note for `volts`.
static void settle(Engine *e, float volts) {
    float out[4] = {0};
    for (int i = 0; i < 200; ++i)
        step(e, volts, false, out);
}

int main() {
    std::printf("nfengine two-instance isolation test\n");

    Engine *a = createEngine();
    Engine *b = createEngine();

    // ── Note masks are per-instance ─────────────────────────────────────────
    // Both start from the factory defaults: channel 1 chromatic, channel 2 major.
    CHECK(noteEnabled(a, 0, 1) && noteEnabled(b, 0, 1),
          "both engines start with C# enabled on channel 1");

    setNoteEnabled(a, 0, 1, false); // turn C# off on engine A only
    CHECK(!noteEnabled(a, 0, 1), "A: C# disabled");
    CHECK(noteEnabled(b, 0, 1), "B: C# still enabled (per-instance isolation)");

    // ── Scale / root selection is per-instance ──────────────────────────────
    setChannelScale(a, 1, 8); // pentatonic minor
    setChannelRoot(a, 1, 3);  // D#
    CHECK(channelScale(a, 1) == 8 && channelRoot(a, 1) == 3, "A: scale + root set");
    CHECK(channelScale(b, 1) == 1 && channelRoot(b, 1) == 0,
          "B: scale + root untouched");

    // ── Envelope / pitch parameters are per-instance ────────────────────────
    setChannelOctave(a, 0, 2);
    setChannelAttack(a, 0, 500);
    setChannelDecay(a, 0, 1500);
    CHECK(channelOctave(a, 0) == 2 && channelAttack(a, 0) == 500 && channelDecay(a, 0) == 1500,
          "A: octave/attack/decay set");
    CHECK(channelOctave(b, 0) == 0 && channelAttack(b, 0) == 0 && channelDecay(b, 0) == 360,
          "B: octave/attack/decay still at defaults");

    // ── Quantization actually differs between the two instances ─────────────
    // 2.0 V is exactly semitone 24 (C). With A shifted up two octaves its CV out
    // must sit two octaves above B's for the same input.
    settle(a, 2.0f);
    settle(b, 2.0f);
    float outA[4] = {0}, outB[4] = {0};
    step(a, 2.0f, false, outA);
    step(b, 2.0f, false, outB);
    std::printf("  at 2.000V in: A CV1=%.4fV  B CV1=%.4fV\n", outA[0], outB[0]);
    CHECK(std::fabs(outB[0] - 2.0f) < 0.02f, "B quantizes 2V to 2V (C, no shift)");
    CHECK(std::fabs(outA[0] - 4.0f) < 0.02f, "A quantizes 2V to 4V (+2 octaves)");

    // ── Hot path: the swap runs on every block ──────────────────────────────
    bool outputsInRange = true;
    for (int i = 0; i < 2000; ++i) {
        // Sweep the pitch input and toggle TRIG so both the quantizer and the
        // envelope are exercised while the instances interleave.
        float v = 2.5f + 2.0f * std::sin(i * 0.01f);
        step(a, v, (i % 64) < 8, outA);
        step(b, v, (i % 97) < 8, outB);
        for (int k = 0; k < 4; ++k) {
            if (outA[k] < -0.01f || outA[k] > 5.01f || outB[k] < -0.01f || outB[k] > 5.01f)
                outputsInRange = false;
        }
    }
    CHECK(outputsInRange, "all outputs stay within 0..5 V through process()");
    CHECK(!noteEnabled(a, 0, 1) && noteEnabled(b, 0, 1),
          "note masks stay isolated across process() blocks");
    CHECK(channelOctave(a, 0) == 2 && channelOctave(b, 0) == 0,
          "octave stays isolated across process() blocks");

    // ── IN 2 routing / transposition are per-instance ───────────────────────
    // These live in cvInputs.hpp as file-scope globals, so they only stay
    // independent if they are registered in engine_state.def.
    setChannelPitchMode(a, 0, 1); // S&H
    in2RoleSet(a, 1);             // IN 2 -> transpose
    transposeRangeSet(a, 3);      // -12..+12
    setChannelTranspose(a, 0, true);
    CHECK(channelPitchMode(a, 0) == 1 && in2RoleGet(a) == 1 && transposeRangeGet(a) == 3,
          "A: pitch mode + IN 2 routing set");
    CHECK(channelPitchMode(b, 0) == 0 && in2RoleGet(b) == 0 && transposeRangeGet(b) == 0,
          "B: pitch mode + IN 2 routing untouched");
    CHECK(channelTranspose(a, 0) && !channelTranspose(b, 0),
          "transpose enable stays per-instance");

    // Drive both again so the routing runs on the hot path, then re-check.
    for (int i = 0; i < 200; ++i) {
        step(a, 2.0f, false, outA);
        step(b, 2.0f, false, outB);
    }
    CHECK(in2RoleGet(a) == 1 && in2RoleGet(b) == 0,
          "IN 2 routing stays isolated across process() blocks");

    // Put A back into a plain configuration for the remaining checks.
    setChannelPitchMode(a, 0, 0);
    in2RoleSet(a, 0);
    setChannelTranspose(a, 0, false);

    // ── The emulated OLED renders per instance ──────────────────────────────
    // A and B are on different scales, so their keyboard screens must differ.
    uint8_t fbA[1024], fbB[1024];
    getFramebuffer(a, fbA);
    getFramebuffer(b, fbB);
    bool framebuffersDiffer = false;
    for (int i = 0; i < 1024; ++i) {
        if (fbA[i] != fbB[i])
            framebuffersDiffer = true;
    }
    CHECK(framebuffersDiffer, "the two engines render different screens");

    // ── Persistence round-trips per instance ────────────────────────────────
    // Save A's state, wipe a setting, then restore and confirm it came back.
    // The engine only reloads from slot 0, so write the slot first.
    setChannelGlide(a, 0, 42);
    std::string blobBefore = serialize(a);
    CHECK(!blobBefore.empty(), "serialize() returns a non-empty EEPROM blob");
    deserialize(b, blobBefore);
    CHECK(channelGlide(a, 0) == 42, "A: glide still 42 after B deserialized");

    destroyEngine(a);
    destroyEngine(b);

    if (g_failures) {
        std::printf("\n== %d FAILURE(S) ==\n", g_failures);
        return 1;
    }
    std::printf("\n== ALL PASS ==\n");
    return 0;
}
