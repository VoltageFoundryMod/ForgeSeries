// fw_engine.cpp — ForgeView scope firmware compiled inside VCV Rack via the shim.
//
// A VCV-adapted host for lib/scope.hpp: it pulls in the unchanged scope engine
// through the Arduino shim, defines the shim symbols, and exposes ONLY a POD/
// opaque API (fw_engine.hpp) so it never shares Arduino/rack types with the rest
// of the plugin. State is made per-instance by the same context-swap technique
// ClockForge uses (see engine_state.def + EngineScope below).

// Every system header the shim and lib/ reach transitively must be included
// out here, BEFORE the anonymous namespace below opens — otherwise std:: lands
// inside it with internal linkage.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <math.h>
#include <mutex>
#include <string>
#include <utility> // std::swap
#include <vector>

// ── Translation-unit privacy ─────────────────────────────────────────────────
// Several Forge modules link into a single VCV Rack plugin (see the
// ForgeSeries-VCV meta-repo), and every module's firmware defines the same
// global names — micros(), Serial, display, InitIO(), MENU_ITEMS, cal, ... —
// each backed by its own divergent lib/. So the whole firmware, from the shim
// down to the entry layer, lives in an anonymous namespace: internal linkage
// means sibling modules neither collide at link time nor silently share one
// COMDAT copy of a same-named class. The public fvengine:: API below stays
// outside it, and is the only thing this TU exports.
namespace {

#include "../shim/Arduino.h"
#include "../shim/Wire.h"

// ── Shim symbol definitions ──────────────────────────────────────────────────
HostBridge *g_host = nullptr;
SerialShim Serial;
TwoWire Wire;
TwoWire Wire1;

// Engine time advances by the host's sample time (deterministic; correct under
// faster-than-realtime rendering), not wall-clock. feedSample() advances it.
// g_engineMicrosFrac carries the sub-microsecond remainder so accumulation does
// not drift: rounding each sample's dt to a whole microsecond added a steady
// per-sample error (e.g. +0.32µs at 44.1kHz → a ~1.4% fast clock) that biased
// every time measurement, most visibly the tuner's detected pitch. Both are
// per-instance (swapped in engine_state.def) so N modules keep N independent
// clocks instead of all advancing one shared clock N× too fast.
unsigned long g_engineMicros = 0;
double g_engineMicrosFrac = 0.0;
unsigned long micros() { return g_engineMicros; }
unsigned long millis() { return g_engineMicros / 1000UL; }
void delay(unsigned long) {}             // never block inside Rack
void delayMicroseconds(unsigned long) {} // never block inside Rack

// ── Display geometry (mirrors main.cpp) ──────────────────────────────────────
#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// ── Firmware scope engine (unchanged) ────────────────────────────────────────
#include "../shim/Adafruit_GFX.h"
#include "../shim/Adafruit_SSD1306.h"
#include "scope.hpp" // resolved via -I../lib (Makefile scopes it to this TU)

// Shared, non-swapped scratch display (re-rendered every getFramebuffer under the
// entry-point lock — safe to share across instances).
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

} // namespace — end of the private firmware translation unit

#include "fw_engine.hpp"

namespace fvengine {

// ── Per-instance firmware state (registry: engine_state.def) ─────────────────
// See docs/VCVRack_Plugin_Development.md in the ClockForge repo for the full
// rationale; the mechanics are identical here (swap-in -> run -> swap-out).
struct EngineState {
#define CF_SCALAR(T, n) T n;
#define CF_ARRAY(T, n, N) T n[N];
#include "engine_state.def"
#undef CF_SCALAR
#undef CF_ARRAY

    void swapWithGlobals() {
        using std::swap;
#define CF_SCALAR(T, n)     \
    {                       \
        T _t = (T)::n;      \
        ::n = this->n;      \
        this->n = _t;       \
    }
#define CF_ARRAY(T, n, N)                  \
    for (int _i = 0; _i < (N); ++_i) {     \
        T _t = (T)::n[_i];                 \
        ::n[_i] = this->n[_i];             \
        this->n[_i] = _t;                  \
    }
#include "engine_state.def"
#undef CF_SCALAR
#undef CF_ARRAY
    }

    void copyFromGlobals() {
#define CF_SCALAR(T, n) this->n = (T)::n;
#define CF_ARRAY(T, n, N) \
    for (int _i = 0; _i < (N); ++_i) this->n[_i] = (T)::n[_i];
#include "engine_state.def"
#undef CF_SCALAR
#undef CF_ARRAY
    }
};

struct Engine {
    HostBridge host;
    EngineState state;
    bool lastButton = false; // true = currently pressed
};

// One mutex guards the single shared set of firmware globals; every entry point
// serializes on it (feedSample on the audio thread, getFramebuffer on the draw
// thread), so the swap-in -> run -> swap-out region is never interleaved.
static std::mutex g_globalsMutex;

struct EngineScope {
    std::lock_guard<std::mutex> _lock;
    Engine *_e;
    explicit EngineScope(Engine *e) : _lock(g_globalsMutex), _e(e) {
        g_host = &_e->host;
        _e->state.swapWithGlobals(); // swap in
    }
    ~EngineScope() {
        _e->state.swapWithGlobals(); // swap out
    }
};

static uint16_t voltsToAdc(float v) {
    int a = (int)(v / 5.0f * 4095.0f + 0.5f);
    return (uint16_t)constrain(a, 0, 4095);
}

Engine *createEngine() {
    Engine *e = new Engine();
    std::lock_guard<std::mutex> lock(g_globalsMutex);

    // The scope's power-on defaults live in the globals' static initializers plus
    // ScopeInit(). Capture that once as the pristine template for every instance.
    static bool havePristine = false;
    static EngineState pristine;
    if (!havePristine) {
        g_host = &e->host;
        display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
        ScopeInit();
        pristine.copyFromGlobals();
        havePristine = true;
    }
    e->state = pristine; // seed this instance (independent copy)
    return e;
}

void destroyEngine(Engine *e) {
    std::lock_guard<std::mutex> lock(g_globalsMutex);
    if (g_host == &e->host) g_host = nullptr;
    delete e;
}

void feedSample(Engine *e, float dt, float cv1Volts, float cv2Volts, bool clkHigh) {
    EngineScope scope(e);
    double us = (double)g_engineMicros + g_engineMicrosFrac + (double)dt * 1.0e6;
    g_engineMicros = (unsigned long)us;
    g_engineMicrosFrac = us - (double)g_engineMicros;
    ScopeFeedSample(voltsToAdc(cv1Volts), voltsToAdc(cv2Volts), clkHigh);
}

void getFramebuffer(Engine *e, uint8_t out[1024]) {
    EngineScope scope(e);
    ScopeRender(display);
    display.display(); // pack into g_host->fb
    for (int i = 0; i < 1024; i++) out[i] = e->host.fb[i];
}

void encoderTurn(Engine *e, int detents) {
    EngineScope scope(e);
    int dir = detents > 0 ? 1 : -1;
    int n = detents > 0 ? detents : -detents;
    for (int k = 0; k < n; k++)
        ScopeEncoderTurn(dir);
}

void encoderButton(Engine *e, bool pressed) {
    EngineScope scope(e);
    ScopeEncoderButton(pressed);
    e->lastButton = pressed;
}

// Rack's "Initialize" action. ScopeInit() is the firmware's own power-on path,
// so this lands on exactly the state a freshly-added module has — no separate
// list of defaults to keep in sync with scope.hpp.
void reset(Engine *e) {
    EngineScope scope(e);
    ScopeInit();
}

// ── Curated state bridge ─────────────────────────────────────────────────────
int modeCount() { return SCOPE_MODE_COUNT; }
std::string modeName(int index) {
    if (index < 0 || index >= SCOPE_MODE_COUNT) return "";
    return ScopeModeName(index + 1); // firmware modes are 1-based
}
int mode(Engine *e) {
    EngineScope scope(e);
    return menuMode;
}
void setMode(Engine *e, int m) {
    EngineScope scope(e);
    m = constrain(m, 1, SCOPE_MODE_COUNT);
    if (m != menuMode) {
        menuMode = m;
        ScopeApplyModeDefaults();
        oldMenuMode = menuMode;
        if (param > ScopeParamCount() - 1) // new mode may have a shorter list
            param = ScopeParamCount() - 1;
        hideTimer = millis();
    }
}

// NB: these accessor names collide with the scope globals of the same name, so
// the globals must be reached with the ::-qualified form inside these functions.
int param1(Engine *e) {
    EngineScope scope(e);
    return ::param1;
}
void setParam1(Engine *e, int v) {
    EngineScope scope(e);
    ::param1 = constrain(v, 1, ScopeParam1Max());
    hideTimer = millis();
}
int param1Max(Engine *e) {
    EngineScope scope(e);
    return ScopeParam1Max();
}
int param2(Engine *e) {
    EngineScope scope(e);
    return ::param2;
}
void setParam2(Engine *e, int v) {
    EngineScope scope(e);
    ::param2 = constrain(v, 1, 8);
    hideTimer = millis();
}

float verticalScale(Engine *e) {
    EngineScope scope(e);
    return ::scale;
}
void setVerticalScale(Engine *e, float v) {
    EngineScope scope(e);
    ::scale = constrain(v, 0.05f, 20.0f);
    hideTimer = millis();
}

bool frozen(Engine *e) {
    EngineScope scope(e);
    return ::frozen;
}
void setFrozen(Engine *e, bool f) {
    EngineScope scope(e);
    ::frozen = f;
    hideTimer = millis();
}

int trigMode(Engine *e) {
    EngineScope scope(e);
    return ::trigMode;
}
void setTrigMode(Engine *e, int v) {
    EngineScope scope(e);
    ::trigMode = constrain(v, 0, 3);
    hideTimer = millis();
}

int offsetCh1(Engine *e) {
    EngineScope scope(e);
    return ::offset0;
}
void setOffsetCh1(Engine *e, int v) {
    EngineScope scope(e);
    ::offset0 = constrain(v, -24, 24);
    hideTimer = millis();
}
int offsetCh2(Engine *e) {
    EngineScope scope(e);
    return ::offset1;
}
void setOffsetCh2(Engine *e, int v) {
    EngineScope scope(e);
    ::offset1 = constrain(v, -24, 24);
    hideTimer = millis();
}

bool showLabels(Engine *e) {
    EngineScope scope(e);
    return ::showLabels;
}
void setShowLabels(Engine *e, bool v) {
    EngineScope scope(e);
    ::showLabels = v;
    hideTimer = millis();
}

int tunerChannel(Engine *e) {
    EngineScope scope(e);
    return ::tunerChan;
}
void setTunerChannel(Engine *e, int ch) {
    EngineScope scope(e);
    ::tunerChan = constrain(ch, 1, 2);
    hideTimer = millis();
}

int xyPersist(Engine *e) {
    EngineScope scope(e);
    return ::xyPersist;
}
void setXyPersist(Engine *e, int v) {
    EngineScope scope(e);
    ::xyPersist = constrain(v, 0, XY_PERSIST_COUNT - 1);
    hideTimer = millis();
}
int xyPersistCount() { return XY_PERSIST_COUNT; }
std::string xyPersistName(int index) {
    if (index < 0 || index >= XY_PERSIST_COUNT) return "";
    return kXYPersistNames[index];
}

void setInputSpanVolts(Engine *e, float v) {
    EngineScope scope(e);
    if (v > 0.1f)
        ::scopeInputSpanV = v;
}

} // namespace fvengine
