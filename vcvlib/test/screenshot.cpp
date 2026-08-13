// screenshot.cpp — dump a module's emulated OLED to the terminal, on the host.
//
// Every Forge firmware renders to a 128x64 1bpp framebuffer, and until this
// existed the only way to see one was to flash a board or open Rack. That makes
// screen work a guess-and-check loop against a photo of a panel, which is how a
// tap label sat two cells away from the jack it named for a whole release.
// Here the pixels can be READ.
//
// Generic across modules. Most of them wrap their engine in a `VcvEngine`
// implementing forgevcv::IEngine; ScopeForge predates that and exposes free
// functions with a per-sample feedSample() instead. Both shapes are adapted
// below, selected by -DFORGE_SCREENSHOT_FREEFN, and the only other per-module
// knowledge is the namespace, passed as -DFORGE_SCREENSHOT_NS=<ns>. The build
// script reads both out of the module's own header rather than keeping a table
// that would go stale.
//
//   make screen-wea
//   make screen-wea ARGS="--ms 2100 --turn 3 --click"
//
// See test/build_screenshot.sh for the argument list.

#include "engine/fw_engine.hpp" // -I apps/<app>/vcv-plugin/src

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef FORGE_SCREENSHOT_NS
#error "define FORGE_SCREENSHOT_NS to the module's engine namespace (e.g. wvengine)"
#endif

namespace fw = FORGE_SCREENSHOT_NS;

namespace {

// ── The two engine shapes, behind one interface ──────────────────────────────
// Everything below this point is written against Engine and does not care which
// module it is looking at.
#ifdef FORGE_SCREENSHOT_FREEFN
struct Engine {
    fw::Engine *e = fw::createEngine();
    ~Engine() { fw::destroyEngine(e); }

    // Not offered by this shape — main() reports it rather than pretending.
    static bool canRandomize() { return false; }
    void randomize() {}

    void encoderTurn(int detents) { fw::encoderTurn(e, detents); }
    void encoderButton(bool pressed) { fw::encoderButton(e, pressed); }
    void run(float dt, const float cv[2], bool clockHigh, float *, int) {
        fw::feedSample(e, dt, cv[0], cv[1], clockHigh);
    }
    void framebuffer(uint8_t *out) { fw::getFramebuffer(e, out); }
};
#else
struct Engine {
    fw::VcvEngine e;

    static bool canRandomize() { return true; }
    void randomize() { e.randomize(); }

    void encoderTurn(int detents) { e.encoderTurn(detents); }
    void encoderButton(bool pressed) { e.encoderButton(pressed); }
    void run(float dt, const float cv[2], bool clockHigh, float *out, int nOut) {
        e.process(dt, cv, 2, clockHigh, out, nOut);
    }
    void framebuffer(uint8_t *out) { e.getFramebuffer(out); }
};
#endif

const int kWidth = 128;
const int kHeight = 64;

// One control-rate block. 1 ms keeps the arithmetic readable — engine time is
// driven by dt, not by a wall clock, so this is exact rather than approximate
// and a given --ms always produces the same frame.
const float kBlockDt = 1.0f / 1000.0f;

struct Options {
    int ms = 2000;         // engine time to advance before capturing
    int clockHz = 0;       // external clock at IN 1, 0 = leave it unpatched
    float cv[2] = {0, 0};  // held CV on the two modulation inputs
    int turn = 0;          // encoder detents, applied before the run
    int clicks = 0;        // encoder presses, applied before the run
    bool randomize = false;
    bool invert = false; // draw lit pixels as space (for light terminals)
};

void usage() {
    std::printf(
        "usage: screenshot [options]\n"
        "  --ms N         engine time to advance before capturing (default 2000)\n"
        "  --clock HZ     drive IN 1 at HZ pulses/sec (default 0 = internal clock)\n"
        "  --cv A B       hold the two modulation inputs at A and B volts\n"
        "  --turn N       encoder detents before running (negative = anticlockwise)\n"
        "  --click N      encoder presses before running (after --turn)\n"
        "  --randomize    randomize the patch first\n"
        "  --invert       swap ink and paper (for light terminals)\n");
}

bool parse(int argc, char **argv, Options &o) {
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--ms" && i + 1 < argc) {
            o.ms = atoi(argv[++i]);
        } else if (a == "--clock" && i + 1 < argc) {
            o.clockHz = atoi(argv[++i]);
        } else if (a == "--cv" && i + 2 < argc) {
            o.cv[0] = (float)atof(argv[++i]);
            o.cv[1] = (float)atof(argv[++i]);
        } else if (a == "--turn" && i + 1 < argc) {
            o.turn = atoi(argv[++i]);
        } else if (a == "--click" && i + 1 < argc) {
            o.clicks = atoi(argv[++i]);
        } else if (a == "--randomize") {
            o.randomize = true;
        } else if (a == "--invert") {
            o.invert = true;
        } else {
            usage();
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char **argv) {
    Options o;
    if (!parse(argc, argv, o)) {
        return 2;
    }

    Engine engine;

    if (o.randomize) {
        if (Engine::canRandomize()) {
            engine.randomize();
        } else {
            std::printf("note: this module's engine offers no randomize()\n");
        }
    }
    // Encoder input BEFORE the run, so the module has time to settle on whatever
    // page it was driven to — and so a screen-timeout that would bounce it back
    // to home actually gets the chance to fire, exactly as on hardware.
    if (o.turn != 0) {
        engine.encoderTurn(o.turn);
    }
    for (int c = 0; c < o.clicks; c++) {
        engine.encoderButton(true);
        engine.encoderButton(false);
    }

    // A clock pulse is one block high; the rest of its period low. At the 1 ms
    // block that is a 1 ms pulse, comfortably past the firmware's input debounce
    // and short enough not to be mistaken for a gate.
    const int period = (o.clockHz > 0) ? (1000 / o.clockHz) : 0;
    float out[8] = {0};
    for (int t = 0; t < o.ms; t++) {
        const bool clockHigh = (period > 0) && (t % period == 0);
        engine.run(kBlockDt, o.cv, clockHigh, out, 8);
    }

    std::vector<uint8_t> fb(1024, 0);
    engine.framebuffer(fb.data());

    std::printf("t=%dms clock=%dHz cv=%.2f/%.2f  out:", o.ms, o.clockHz,
                (double)o.cv[0], (double)o.cv[1]);
    for (int i = 0; i < 4; i++) {
        std::printf(" %.2f", (double)out[i]);
    }
    std::printf("\n");

    // A tens ruler across the top, so a pixel's x can be read off directly
    // rather than counted — the whole point is to check geometry against the
    // constants in the render source.
    std::printf("    +");
    for (int x = 0; x < kWidth; x++) {
        std::putchar((x % 10 == 0) ? char('0' + (x / 10) % 10) : '.');
    }
    std::printf("+\n");

    const char ink = o.invert ? ' ' : '#';
    const char paper = o.invert ? '#' : '.';
    for (int y = 0; y < kHeight; y++) {
        std::printf("%3d |", y);
        for (int x = 0; x < kWidth; x++) {
            const bool on = fb[(y * kWidth + x) >> 3] & (0x80 >> (x & 7));
            std::putchar(on ? ink : paper);
        }
        std::printf("|\n");
    }
    return 0;
}
