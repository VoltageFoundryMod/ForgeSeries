// fw_engine.cpp — GravityForge firmware compiled inside VCV Rack via the shim.
//
// This is a VCV-adapted port of src/main.cpp: it pulls in the unchanged firmware
// lib/ (the valuable physics + sequencer + menu + render code) through the
// Arduino shim, defines the same set of globals, and replaces the
// hardware/dual-core integration glue with host-driven entry points. It exposes
// ONLY a POD/opaque API (fw_engine.hpp) so it never shares Arduino/rack types
// with the rest of the plugin.

// Every system header the shim and lib/ reach transitively must be included out
// here, BEFORE the anonymous namespace below opens — otherwise std:: lands
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
// COMDAT copy of a same-named class. The public gfengine:: API below stays
// outside it, and is the only thing this TU exports.
namespace {

#include "Arduino.h"
#include "EEPROM.h"
#include "Wire.h"

// ── Shim symbol definitions ──────────────────────────────────────────────────
HostBridge *g_host = nullptr;
SerialShim Serial;
TwoWire Wire;
TwoWire Wire1;
EEPROMClass EEPROM;

// Engine time advances by the host's sample time (deterministic; correct under
// faster-than-realtime rendering), not wall-clock. process() advances it.
//
// This matters more here than on any other Forge module: the physics integrates
// on a fixed 1 ms timestep driven entirely by micros(), so a wall-clock reading
// would make the simulation run at a different speed under a rendered mixdown
// than it does live.
unsigned long g_engineMicros = 0;
unsigned long micros() { return g_engineMicros; }
unsigned long millis() { return g_engineMicros / 1000UL; }
void delay(unsigned long) {}             // never block inside Rack
void delayMicroseconds(unsigned long) {} // never block inside Rack

// IN 1 ISR pointer (the firmware calls attachInterrupt for CLK_IN rising).
static void (*_trigIsr)() = nullptr;
void attachInterrupt(int, void (*isr)(), int) { _trigIsr = isr; }

// ── Display geometry + refresh macro (mirrors main.cpp) ───────────────────────
#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define REQUEST_DISPLAY_REFRESH()     \
    do {                              \
        displayRefresh = 1;           \
        displayMgr.MarkInteraction(); \
    } while (0)

// ── Firmware library (unchanged) ──────────────────────────────────────────────
#include "Adafruit_GFX.h"
#include "Adafruit_SSD1306.h"

#include "boardIO.hpp"
#include "clock.hpp"
#include "cvInputs.hpp" // must precede presetManager.hpp (In1Role/CVTarget types)
#include "displayManager.hpp"
#include "params.hpp"
#include "physics.hpp"
#include "pinouts.hpp"
#include "presetManager.hpp"
#include "randomize.hpp" // RandomizeParams() — shared with the hardware menu
#include "sequencer.hpp"
#include "splash.hpp"
#include "storage.hpp"
#include "utils.hpp"

#include "menuDefinitions.hpp"
#include "menuHandlers.hpp"
#include "menuDisplay.hpp"
#include "menuRender.hpp"

// ── Global objects (mirror main.cpp, minus hardware/dual-core bits) ───────────
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DisplayManager displayMgr(display);

PhysicsWorld physicsWorld;
GravityChannel channels[NUM_CHANNELS];
ContainerParams containerParams[2];
WorldParams worldParams;
Clock clockEngine;
ModBus modBus;

int menuItem = 1;
bool switchState = 1;
bool oldSwitchState = 1;
int menuMode = 0;
bool displayRefresh = 1;
bool unsavedChanges = false;
int menuScreenTimeout = 2;

CalibrationData cal;

// Brief on-screen message overlay (replaces main.cpp's blocking ShowTemporaryMessage).
static char _tempMsg[32] = {0};
static unsigned long _tempMsgUntil = 0;

// ─────────────────────────────────────────────────────────────────────────────
// main.cpp functions the firmware lib references — ported, non-blocking.
// ─────────────────────────────────────────────────────────────────────────────
void RedrawDisplay() {
    displayMgr.PrepareFrame(); // DrawOverlays + CommitFrame
    displayRefresh = 0;
    display.display(); // pack into the HostBridge framebuffer
}

// Act on an IN 1 edge according to the jack's configured role.
static void HandleTriggerRole(unsigned long edgeUs) {
    switch (in1Role) {
    case In1Clock:
        clockEngine.ExternalEdge(edgeUs);
        break;
    case In1Reset:
        physicsWorld.Reset();
        break;
    case In1Kick:
        physicsWorld.Kick(180.0f);
        break;
    case In1Spawn:
        for (int i = 0; i < 2; i++) {
            int n = containerParams[i].balls + 1;
            containerParams[i].balls =
                (unsigned char)(n > PHYS_MAX_BALLS ? PHYS_MIN_BALLS : n);
        }
        MarkUnsaved();
        REQUEST_DISPLAY_REFRESH();
        break;
    default:
        break;
    }
}

// Advance the whole instrument and push all four DAC outputs.
// Jack map: 1 = CV A, 2 = CV B, 3 = GATE A, 4 = GATE B.
void HandleOutputs() {
    unsigned long now = micros();

    unsigned long edgeUs = now;
    if (ConsumeTrigger(&edgeUs)) {
        HandleTriggerRole(edgeUs);
    }
    HandleTriggerLevel();

    clockEngine.Update(now);

    BuildModBus(modBus);
    ApplyParams(physicsWorld, clockEngine, containerParams, worldParams, modBus);

    physicsWorld.Advance(now);

    // Consumed once and handed to both channels: two calls would give the
    // boundary to channel A and nothing to channel B.
    bool boundary = clockEngine.ConsumeBoundary();

    for (int i = 0; i < NUM_CHANNELS; i++) {
        channels[i].SetGateHigh(trigLevel);
        channels[i].Process(physicsWorld.Get(i), now, clockEngine, boundary);
    }

    DACWriteAll(channels[0].GetCVOutput(), channels[1].GetCVOutput(),
                channels[0].GetGateOutput(), channels[1].GetGateOutput());
}

// Non-blocking: stash the message + expiry; the renderer draws it as an overlay.
void ShowTemporaryMessage(const char *msg, uint32_t durationMs) {
    strncpy(_tempMsg, msg, sizeof(_tempMsg) - 1);
    _tempMsg[sizeof(_tempMsg) - 1] = 0;
    _tempMsgUntil = millis() + durationMs;
    REQUEST_DISPLAY_REFRESH();
}

} // namespace — end of the private firmware translation unit

// ── Touch points used by the engine entry layer (below) ───────────────────────
#include "fw_engine.hpp"

namespace gfengine {

// ── Per-instance firmware state (registry: engine_state.def) ──────────────────
// The firmware keeps its DSP/menu state in file-scope globals so the same lib/
// builds unchanged for the RP2040.  To let multiple Rack instances coexist, each
// Engine owns an EngineState snapshot that is *swapped* with the live globals
// around every entry point (swap-in → run firmware → swap-out).  Swapping rather
// than copying keeps every heap-backed String/std::vector living in exactly one
// place at a time, so it is allocation-free and safe.  The swap is symmetric:
// the same call swaps in and swaps out (swap∘swap = identity), and it restores
// the globals to whatever they held before, so the resting global values are
// irrelevant scratch.
struct EngineState {
#define CF_SCALAR(T, n) T n;
#define CF_ARRAY(T, n, N) T n[N];
#define CF_OBJECT(T, n) T n;
#define CF_OBJARRAY(T, n, N) T n[N];
#include "engine_state.def"
#undef CF_SCALAR
#undef CF_ARRAY
#undef CF_OBJECT
#undef CF_OBJARRAY

    // Exchange this snapshot with the live firmware globals (symmetric).
    void swapWithGlobals() {
        using std::swap;
#define CF_SCALAR(T, n) \
    {                   \
        T _t = (T)::n;  \
        ::n = this->n;  \
        this->n = _t;   \
    }
#define CF_ARRAY(T, n, N)              \
    for (int _i = 0; _i < (N); ++_i) { \
        T _t = (T)::n[_i];             \
        ::n[_i] = this->n[_i];         \
        this->n[_i] = _t;              \
    }
#define CF_OBJECT(T, n) swap(::n, this->n);
#define CF_OBJARRAY(T, n, N)           \
    for (int _i = 0; _i < (N); ++_i) { \
        swap(::n[_i], this->n[_i]);    \
    }
#include "engine_state.def"
#undef CF_SCALAR
#undef CF_ARRAY
#undef CF_OBJECT
#undef CF_OBJARRAY
    }

    // Deep-copy the live globals into this snapshot (globals unchanged).  Used
    // once to capture the firmware's initialised power-on defaults — the scalar
    // globals get their defaults from their *initializers* (e.g. `menuItem = 1`),
    // which a default-constructed EngineState would not have.
    void copyFromGlobals() {
#define CF_SCALAR(T, n) this->n = (T)::n;
#define CF_ARRAY(T, n, N) \
    for (int _i = 0; _i < (N); ++_i) this->n[_i] = (T)::n[_i];
#define CF_OBJECT(T, n) this->n = ::n;
#define CF_OBJARRAY(T, n, N) \
    for (int _i = 0; _i < (N); ++_i) this->n[_i] = ::n[_i];
#include "engine_state.def"
#undef CF_SCALAR
#undef CF_ARRAY
#undef CF_OBJECT
#undef CF_OBJARRAY
    }
};

struct Engine {
    HostBridge host;
    EngineState state;
    bool lastTrig = false;
    bool lastButton = false; // true = currently pressed
};

// One mutex guards the single shared set of firmware globals: all engine entry
// points serialize on it (process() on the audio thread, getFramebuffer()/UI on
// the draw thread), so the swap-in→run→swap-out region is never interleaved.
static std::mutex g_globalsMutex;

// RAII: lock the globals, point IO at this instance, swap its state into the live
// globals for the duration of the call, then swap it back out and unlock.
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

static int clampC(int c) { return c < 0 ? 0 : (c > 1 ? 1 : c); }
static int clampIn(int i) { return i < 0 ? 0 : (i > NUM_CV_INS - 1 ? NUM_CV_INS - 1 : i); }

// One-time setup of the *shared* shim objects — the IN 1 interrupt vector, the
// SSD1306 and the MCP4728.  These are process-global (not part of any instance's
// swapped state), so they run exactly once regardless of how many modules are
// instantiated.  Wiring the rising-edge ISR here mirrors main.cpp setup();
// without it IN 1 would do nothing whatever its role is set to.
static void globalOneTimeInit() {
    static bool done = false;
    if (done)
        return;
    done = true;
    attachInterrupt(digitalPinToInterrupt(CLK_IN_PIN), TriggerReceived, RISING);
    display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
    InitDAC();
}

// Per-instance boot: runs against whatever EngineState is currently swapped into
// the globals (the caller must hold an EngineScope).  Mirrors main.cpp setup()
// minus the shared one-time bits.  A fresh instance starts from a blank EEPROM,
// so Load(0)/LoadCalibration fall back to defaults.
static void engineInstanceInit() {
    EEPROMInit();
    InitIO();
    cal = LoadCalibration();
    LoadSaveParams p = Load(0);
    UpdateParameters(p);
    // Seed the simulation from the loaded parameters before the first step, so
    // ball counts and peg rings are right on the very first frame.
    ApplyParams(physicsWorld, clockEngine, containerParams, worldParams, modBus);
    physicsWorld.Reset();
    REQUEST_DISPLAY_REFRESH();
    displayMgr.MarkDirty();
}

// Encoder click — ported from main.cpp HandleEncoderClick (fires on press edge).
static void doEncoderClick() {
    REQUEST_DISPLAY_REFRESH();
    if (menuMode == 0) {
        if (menuItem >= 1 && menuItem <= MENU_ITEM_COUNT) {
            const MenuItem &mi = MENU_ITEMS[menuItem - 1];
            if (mi.type == MENU_ACTION || mi.type == MENU_TOGGLE) {
                if (mi.action)
                    mi.action();
            } else { // MENU_EDIT
                menuMode = menuItem;
            }
        }
    } else {
        menuMode = 0;
    }
}

Engine *createEngine() {
    Engine *e = new Engine();
    std::lock_guard<std::mutex> lock(g_globalsMutex);

    // The firmware's power-on defaults live in the globals' static initializers
    // (e.g. `menuItem = 1`), not in EngineState's members.  On the first instance
    // the globals are still at those defaults, so run the one-time shared setup
    // plus a full instance init against them and capture the result as the
    // template every instance is seeded from.
    static bool havePristine = false;
    static EngineState pristine;
    if (!havePristine) {
        g_host = &e->host; // InitDAC / display.begin write through g_host
        globalOneTimeInit();
        engineInstanceInit();
        pristine.copyFromGlobals();
        havePristine = true;
    }

    e->state = pristine; // seed this instance (independent deep copy)
    return e;
}

void destroyEngine(Engine *e) {
    std::lock_guard<std::mutex> lock(g_globalsMutex);
    if (g_host == &e->host)
        g_host = nullptr;
    delete e;
}

void process(Engine *e, float dt, const float cvVolts[2], bool trigGateHigh, float outVolts[4]) {
    EngineScope scope(e);

    // Advance engine time by the elapsed block. The physics catches up in fixed
    // 1 ms steps inside PhysicsWorld::Advance(), capped so a big block can never
    // turn into an unbounded catch-up loop on the audio thread.
    g_engineMicros += (unsigned long)(dt * 1.0e6f + 0.5f);

    // Feed the modulation CVs (volts -> 12-bit ADC) and the IN 1 level.
    e->host.adc[CV_1_IN_PIN] = voltsToAdc(cvVolts[0]);
    e->host.adc[CV_2_IN_PIN] = voltsToAdc(cvVolts[1]);
    e->host.gpio[CLK_IN_PIN] = trigGateHigh ? 1 : 0;
    if (trigGateHigh && !e->lastTrig && _trigIsr)
        _trigIsr();
    e->lastTrig = trigGateHigh;

    HandleCVInputs();
    HandleOutputs();

    if (displayRefresh)
        displayMgr.MarkDirty();

    for (int i = 0; i < NUM_OUTPUTS; i++)
        outVolts[i] = e->host.dac[i] / (float)MAXDAC * 5.0f;
}

void encoderTurn(Engine *e, int detents) {
    EngineScope scope(e);
    int dir = detents > 0 ? 1 : -1;
    int n = detents > 0 ? detents : -detents;
    for (int k = 0; k < n; k++) {
        REQUEST_DISPLAY_REFRESH();
        if (menuMode == 0) {
            menuItem += dir;
            if (menuItem < 1)
                menuItem = MENU_ITEM_COUNT;
            else if (menuItem > MENU_ITEM_COUNT)
                menuItem = 1;
        } else if (menuMode >= 1 && menuMode <= MENU_ITEM_COUNT) {
            if (MENU_ITEMS[menuMode - 1].setter)
                MENU_ITEMS[menuMode - 1].setter(dir);
        }
    }
}

void encoderButton(Engine *e, bool pressed) {
    EngineScope scope(e);
    if (pressed && !e->lastButton)
        doEncoderClick(); // press edge
    e->lastButton = pressed;
    e->host.gpio[ENCODER_SW] = pressed ? 0 : 1;
}

void getFramebuffer(Engine *e, uint8_t out[1024]) {
    EngineScope scope(e);
    bool msgActive = _tempMsg[0] && (long)(millis() - _tempMsgUntil) < 0;
    if (msgActive) {
        display.clearDisplay();
        display.setTextSize(2);
        int x = (SCREEN_WIDTH - (int)strlen(_tempMsg) * 12) / 2;
        display.setCursor(x < 0 ? 0 : x, SCREEN_HEIGHT / 2 - 8);
        display.print(_tempMsg);
        display.display();
    } else {
        if (_tempMsg[0]) {
            _tempMsg[0] = 0;
            REQUEST_DISPLAY_REFRESH();
        }
        if (displayRefresh)
            displayMgr.MarkDirty();
        HandleDisplay();
    }
    for (int i = 0; i < 1024; i++)
        out[i] = e->host.fb[i];
}

std::string serialize(Engine *e) {
    EngineScope scope(e);
    // Commit the live state to slot 0 before handing the blob to Rack. The
    // firmware only writes EEPROM on an explicit SAVE, so without this the blob
    // still holds whatever was last saved — on a fresh instance, nothing at all,
    // which is why a patch used to reload at factory defaults. Slot 0 is the slot
    // the firmware auto-loads at boot and the one deserialize() reads back, so
    // this makes a Rack patch round-trip the live state. Slots 1..NUM_SLOTS-1
    // (the user's own presets) and the calibration block are untouched.
    Save(CollectParams(), 0);
    return std::string((const char *)EEPROM.data.data(), EEPROM.data.size());
}

void deserialize(Engine *e, const std::string &blob) {
    EngineScope scope(e);
    EEPROM.data.assign(blob.begin(), blob.end());
    // Reload settings from slot 0 into the live state.
    cal = LoadCalibration();
    LoadSaveParams p = Load(0);
    UpdateParameters(p);
    ApplyParams(physicsWorld, clockEngine, containerParams, worldParams, modBus);
    REQUEST_DISPLAY_REFRESH();
}

// ── Initialize / Randomize ────────────────────────────────────────────────────
// Back Rack's module actions. Both run entirely against the live firmware state
// under one EngineScope, so they must NOT call the public bridge helpers below
// (those take the same non-recursive lock).

void reset(Engine *e) {
    EngineScope scope(e);
    // Factory defaults, exactly what a fresh instance boots into. The stored
    // preset slots and the calibration block are deliberately left alone:
    // Initialize resets the patch you are playing, not your saved presets.
    UpdateParameters(LoadDefaultParams());
    ApplyParams(physicsWorld, clockEngine, containerParams, worldParams, modBus);
    physicsWorld.Reset();
    menuItem = 1; // back to the top of the menu, like a power cycle
    menuMode = 0;
    MarkUnsaved(); // live state now differs from whatever slot 0 holds
    REQUEST_DISPLAY_REFRESH();
}

void randomize(Engine *e) {
    EngineScope scope(e);
    // Same implementation the hardware's SETTINGS > RANDOM action runs, so the
    // panel and the host roll the same kind of patch. lib/randomize.hpp documents
    // the ranges and what is deliberately left alone (tempo, IN 1 role, CV matrix).
    // Engine time is the entropy source here — it is per-instance and always
    // moving, so two modules randomized in the same frame do not agree.
    RandomizeParams((uint32_t)micros());
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

// ── Curated parameter bridge ──────────────────────────────────────────────────
// Absolute get/set helpers backing the Rack context menu.  Each mutates the live
// firmware state under an EngineScope (globals lock + swap-in), then requests a
// display refresh so the emulated OLED tracks changes made from the menu.
//
// Note these write the *base* parameter block (containerParams/worldParams), not
// the live Container — exactly as the on-panel menu does. Writing the container
// directly would be overwritten by the next ApplyParams() and would not persist.

// ── Proximity / coupling ──
int proximityGet(Engine *e) {
    EngineScope scope(e);
    return (int)lroundf(worldParams.proximity * 100.0f);
}
void proximitySet(Engine *e, int pct) {
    EngineScope scope(e);
    worldParams.proximity = constrain((float)pct / 100.0f, 0.0f, 1.0f);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}
int couplingGet(Engine *e) {
    EngineScope scope(e);
    return (int)lroundf(worldParams.coupling * 100.0f);
}
void couplingSet(Engine *e, int pct) {
    EngineScope scope(e);
    worldParams.coupling = constrain((float)pct / 100.0f, 0.0f, 1.0f);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

void resetBalls(Engine *e) {
    EngineScope scope(e);
    physicsWorld.Reset();
    REQUEST_DISPLAY_REFRESH();
}
void kickBalls(Engine *e) {
    EngineScope scope(e);
    physicsWorld.Kick(180.0f);
    REQUEST_DISPLAY_REFRESH();
}

// ── Clock ──
int bpmMin() { return CLOCK_MIN_BPM; }
int bpmMax() { return CLOCK_MAX_BPM; }
int bpmGet(Engine *e) {
    EngineScope scope(e);
    return clockEngine.GetBpm();
}
void bpmSet(Engine *e, int bpm) {
    EngineScope scope(e);
    clockEngine.SetBpm(bpm);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}
int effectiveBpm(Engine *e) {
    EngineScope scope(e);
    return (int)lroundf(clockEngine.GetEffectiveBpm());
}

int quantizeCount() { return (int)QuantizeDivLength; }
std::string quantizeName(int index) {
    if (index < 0 || index >= (int)QuantizeDivLength)
        return "";
    return QuantizeDivNames[index];
}
int quantizeGet(Engine *e) {
    EngineScope scope(e);
    return clockEngine.GetQuantize();
}
void quantizeSet(Engine *e, int index) {
    EngineScope scope(e);
    clockEngine.SetQuantize(index);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

int ppqnCount() { return (int)ClockPPQNLength; }
std::string ppqnName(int index) {
    if (index < 0 || index >= (int)ClockPPQNLength)
        return "";
    return ClockPPQNNames[index];
}
int ppqnGet(Engine *e) {
    EngineScope scope(e);
    return clockEngine.GetPpqn();
}
void ppqnSet(Engine *e, int index) {
    EngineScope scope(e);
    clockEngine.SetPpqn(index);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

int in1RoleCount() { return (int)In1RoleLength; }
std::string in1RoleName(int index) {
    if (index < 0 || index >= (int)In1RoleLength)
        return "";
    return In1RoleNames[index];
}
int in1RoleGet(Engine *e) {
    EngineScope scope(e);
    return (int)in1Role;
}
void in1RoleSet(Engine *e, int index) {
    EngineScope scope(e);
    in1Role = (unsigned char)constrain(index, 0, (int)In1RoleLength - 1);
    // The clock only follows the jack while the jack is a clock.
    clockEngine.SetExternal(in1Role == In1Clock);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

// ── Loop / phrase mode ──
// These write worldParams like every other setter here; ApplyParams() turns the
// beat count into an exact step count and PhysicsWorld does the rewind.
int loopBeatsMax() { return PARAM_LOOP_BEATS_MAX; }
int loopBeatsGet(Engine *e) {
    EngineScope scope(e);
    return (int)worldParams.loopBeats;
}
void loopBeatsSet(Engine *e, int beats) {
    EngineScope scope(e);
    worldParams.loopBeats = (unsigned char)constrain(beats, 0, PARAM_LOOP_BEATS_MAX);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

int loopWakeMin() { return PARAM_LOOP_WAKE_MIN; }
int loopWakeMax() { return PARAM_LOOP_WAKE_MAX; }
int loopWakeGet(Engine *e) {
    EngineScope scope(e);
    return (int)worldParams.loopWake;
}
void loopWakeSet(Engine *e, int loops) {
    EngineScope scope(e);
    worldParams.loopWake =
        (unsigned char)constrain(loops, PARAM_LOOP_WAKE_MIN, PARAM_LOOP_WAKE_MAX);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

int loopNapMax() { return PARAM_LOOP_NAP_MAX; }
int loopNapGet(Engine *e) {
    EngineScope scope(e);
    return (int)worldParams.loopNap;
}
void loopNapSet(Engine *e, int loops) {
    EngineScope scope(e);
    worldParams.loopNap = (unsigned char)constrain(loops, 0, PARAM_LOOP_NAP_MAX);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

int loopShiftMax() { return PARAM_LOOP_SHIFT_MAX; }
int loopShiftGet(Engine *e, int c) {
    EngineScope scope(e);
    return (int)worldParams.loopShift[clampC(c)];
}
void loopShiftSet(Engine *e, int c, int loops) {
    EngineScope scope(e);
    worldParams.loopShift[clampC(c)] =
        (unsigned char)constrain(loops, 0, PARAM_LOOP_SHIFT_MAX);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

void loopNewPhrase(Engine *e) {
    EngineScope scope(e);
    // Not a parameter change — the settings are untouched, only the captured
    // phrase — so this deliberately does not mark the patch unsaved.
    physicsWorld.ArmLoop();
    REQUEST_DISPLAY_REFRESH();
}

// ── Physics, per container ──
int gravityMin() { return (int)PARAM_GRAVITY_MIN; }
int gravityMax() { return (int)PARAM_GRAVITY_MAX; }
int gravityGet(Engine *e, int c) {
    EngineScope scope(e);
    return (int)lroundf(containerParams[clampC(c)].gravity);
}
void gravitySet(Engine *e, int c, int v) {
    EngineScope scope(e);
    containerParams[clampC(c)].gravity =
        constrain((float)v, PARAM_GRAVITY_MIN, PARAM_GRAVITY_MAX);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

int bounceGet(Engine *e, int c) {
    EngineScope scope(e);
    return (int)lroundf(containerParams[clampC(c)].bounce * 100.0f);
}
void bounceSet(Engine *e, int c, int pct) {
    EngineScope scope(e);
    containerParams[clampC(c)].bounce =
        constrain((float)pct / 100.0f, PARAM_BOUNCE_MIN, PARAM_BOUNCE_MAX);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

int gripGet(Engine *e, int c) {
    EngineScope scope(e);
    return (int)lroundf(containerParams[clampC(c)].grip * 100.0f);
}
void gripSet(Engine *e, int c, int pct) {
    EngineScope scope(e);
    containerParams[clampC(c)].grip =
        constrain((float)pct / 100.0f, PARAM_GRIP_MIN, PARAM_GRIP_MAX);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

// Only the clock-locked ratios are offered; SpinFree needs a rate control that
// the six-row hardware page has no space for.
int spinCount() { return (int)SpinFree; }
std::string spinName(int index) {
    if (index < 0 || index >= (int)SpinRateLength)
        return "";
    return SpinRateNames[index];
}
int spinGet(Engine *e, int c) {
    EngineScope scope(e);
    return containerParams[clampC(c)].spin;
}
void spinSet(Engine *e, int c, int index) {
    EngineScope scope(e);
    containerParams[clampC(c)].spin =
        (unsigned char)constrain(index, 0, (int)SpinRateLength - 1);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}
bool reverseGet(Engine *e, int c) {
    EngineScope scope(e);
    return containerParams[clampC(c)].reverse;
}
void reverseSet(Engine *e, int c, bool on) {
    EngineScope scope(e);
    containerParams[clampC(c)].reverse = on;
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

int ballsMin() { return PHYS_MIN_BALLS; }
int ballsMax() { return PHYS_MAX_BALLS; }
int ballsGet(Engine *e, int c) {
    EngineScope scope(e);
    return containerParams[clampC(c)].balls;
}
void ballsSet(Engine *e, int c, int n) {
    EngineScope scope(e);
    containerParams[clampC(c)].balls =
        (unsigned char)constrain(n, PHYS_MIN_BALLS, PHYS_MAX_BALLS);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

int pegsMin() { return PHYS_MIN_PEGS; }
int pegsMax() { return PHYS_MAX_PEGS; }
int pegsGet(Engine *e, int c) {
    EngineScope scope(e);
    return containerParams[clampC(c)].pegs;
}
void pegsSet(Engine *e, int c, int n) {
    EngineScope scope(e);
    containerParams[clampC(c)].pegs =
        (unsigned char)constrain(n, PHYS_MIN_PEGS, PHYS_MAX_PEGS);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

bool pegEnabledGet(Engine *e, int c, int peg) {
    EngineScope scope(e);
    if (peg < 0 || peg >= PHYS_MAX_PEGS)
        return false;
    return (containerParams[clampC(c)].pegMask >> peg) & 1u;
}
void pegEnabledSet(Engine *e, int c, int peg, bool on) {
    EngineScope scope(e);
    if (peg < 0 || peg >= PHYS_MAX_PEGS)
        return;
    unsigned short &m = containerParams[clampC(c)].pegMask;
    if (on)
        m = (unsigned short)(m | (1u << peg));
    else
        m = (unsigned short)(m & ~(1u << peg));
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

// ── Notes, per container ──
int scaleCount() { return numScales; }
std::string scaleName(int index) {
    if (index < 0 || index >= numScales)
        return "";
    return scaleNames[index];
}
int scaleGet(Engine *e, int c) {
    EngineScope scope(e);
    return channels[clampC(c)].GetScaleIndex();
}
void scaleSet(Engine *e, int c, int index) {
    EngineScope scope(e);
    channels[clampC(c)].SelectScale(index);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

std::string noteName(int note) {
    if (note < 0 || note >= 12)
        return "";
    return noteNames[note];
}
int rootGet(Engine *e, int c) {
    EngineScope scope(e);
    return channels[clampC(c)].GetRootIndex();
}
void rootSet(Engine *e, int c, int root) {
    EngineScope scope(e);
    channels[clampC(c)].SelectRoot(root);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

int spreadMin() { return CHANNEL_SPREAD_MIN; }
int spreadMax() { return CHANNEL_SPREAD_MAX; }
int spreadGet(Engine *e, int c) {
    EngineScope scope(e);
    return channels[clampC(c)].GetSpread();
}
void spreadSet(Engine *e, int c, int octaves) {
    EngineScope scope(e);
    channels[clampC(c)].SetSpread(octaves);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

int biasMin() { return CHANNEL_BIAS_MIN; }
int biasMax() { return CHANNEL_BIAS_MAX; }
int biasGet(Engine *e, int c) {
    EngineScope scope(e);
    return channels[clampC(c)].GetBias();
}
void biasSet(Engine *e, int c, int bias) {
    EngineScope scope(e);
    channels[clampC(c)].SetBias(bias);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

// ── Gate, per container ──
int gateModeCount() { return (int)GateModeLength; }
std::string gateModeName(int index) {
    if (index < 0 || index >= (int)GateModeLength)
        return "";
    return GateModeNames[index];
}
int gateModeGet(Engine *e, int c) {
    EngineScope scope(e);
    return channels[clampC(c)].envelope.GetMode();
}
void gateModeSet(Engine *e, int c, int index) {
    EngineScope scope(e);
    channels[clampC(c)].envelope.SetMode(index);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

int attackMax() { return ENVELOPE_MAX_ATTACK; }
int attackGet(Engine *e, int c) {
    EngineScope scope(e);
    return channels[clampC(c)].envelope.GetAttack();
}
void attackSet(Engine *e, int c, int ms) {
    EngineScope scope(e);
    channels[clampC(c)].envelope.SetAttack(ms);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

int decayMax() { return ENVELOPE_MAX_DECAY; }
int decayGet(Engine *e, int c) {
    EngineScope scope(e);
    return channels[clampC(c)].envelope.GetDecay();
}
void decaySet(Engine *e, int c, int ms) {
    EngineScope scope(e);
    channels[clampC(c)].envelope.SetDecay(ms);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

int levelGet(Engine *e, int c) {
    EngineScope scope(e);
    return channels[clampC(c)].GetGateLevel();
}
void levelSet(Engine *e, int c, int pct) {
    EngineScope scope(e);
    channels[clampC(c)].SetGateLevel(pct);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

int accentGet(Engine *e, int c) {
    EngineScope scope(e);
    return channels[clampC(c)].GetAccent();
}
void accentSet(Engine *e, int c, int pct) {
    EngineScope scope(e);
    channels[clampC(c)].SetAccent(pct);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

// ── CV modulation matrix ──
int cvTargetCount() { return (int)CVTargetLength; }
std::string cvTargetName(int index) {
    if (index < 0 || index >= (int)CVTargetLength)
        return "";
    return CVTargetNames[index];
}
int cvTargetGet(Engine *e, int input) {
    EngineScope scope(e);
    return cvTarget[clampIn(input)];
}
void cvTargetSet(Engine *e, int input, int index) {
    EngineScope scope(e);
    cvTarget[clampIn(input)] = (unsigned char)constrain(index, 0, (int)CVTargetLength - 1);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}
int cvDepthGet(Engine *e, int input) {
    EngineScope scope(e);
    return cvDepth[clampIn(input)];
}
void cvDepthSet(Engine *e, int input, int pct) {
    EngineScope scope(e);
    cvDepth[clampIn(input)] = (unsigned char)constrain(pct, 0, 100);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

// ── Live readouts ──
std::string currentNote(Engine *e, int c) {
    EngineScope scope(e);
    int i = clampC(c);
    if (channels[i].GetSemitone() < 0)
        return "--";
    return std::string(noteNames[channels[i].GetNoteIndex()]) +
           std::to_string(channels[i].GetOctaveOut());
}

// ── forgevcv::IEngine adapter ─────────────────────────────────────────────────
// Thin forwards to the free-function bridge above. The firmware is fixed at
// 2 CV inputs / 4 outputs, so the nCv/nOut counts are informational here.
VcvEngine::VcvEngine() : e_(createEngine()) {}
VcvEngine::~VcvEngine() { destroyEngine(e_); }

void VcvEngine::process(float dt, const float *cv, int /*nCv*/,
                        bool clockHigh, float *out, int /*nOut*/) {
    gfengine::process(e_, dt, cv, clockHigh, out);
}
void VcvEngine::encoderTurn(int detents) { gfengine::encoderTurn(e_, detents); }
void VcvEngine::encoderButton(bool pressed) { gfengine::encoderButton(e_, pressed); }
void VcvEngine::getFramebuffer(uint8_t out[1024]) { gfengine::getFramebuffer(e_, out); }
std::string VcvEngine::serialize() { return gfengine::serialize(e_); }
void VcvEngine::deserialize(const std::string &blob) { gfengine::deserialize(e_, blob); }
void VcvEngine::reset() { gfengine::reset(e_); }
void VcvEngine::randomize() { gfengine::randomize(e_); }

} // namespace gfengine
