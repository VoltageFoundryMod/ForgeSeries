// fw_engine.cpp — WeaveForge firmware compiled inside VCV Rack via the shim.
//
// This is a VCV-adapted port of the app TU (../../../src/wea_app.cpp): it pulls
// in the unchanged firmware lib/ (the shift registers, the output matrix, the
// menu and render code) through the Arduino shim, defines the same set of
// globals, and replaces the hardware/dual-core integration glue with
// host-driven entry points. It exposes ONLY a POD/opaque API (fw_engine.hpp) so
// it never shares Arduino/rack types with the rest of the plugin.

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
// Several Forge modules link into a single VCV Rack plugin, and every module's
// firmware defines the same global names — micros(), Serial, display, InitIO(),
// MENU_ITEMS, cal, ... — each backed by its own divergent lib/. So the whole
// firmware, from the shim down to the entry layer, lives in an anonymous
// namespace: internal linkage means sibling modules neither collide at link time
// nor silently share one COMDAT copy of a same-named class. The public
// wvengine:: API below stays outside it, and is the only thing this TU exports.
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
// faster-than-realtime rendering), not wall-clock.
//
// It matters more here than on a modulation source: this module's internal
// clock derives its step period from this, and its trigger widths are wall-clock
// durations measured against it. A real-time reading would make a rendered
// mixdown come out at a different tempo than the live patch.
unsigned long g_engineMicros = 0;
unsigned long micros() { return g_engineMicros; }
unsigned long millis() { return g_engineMicros / 1000UL; }
void delay(unsigned long) {}             // never block inside Rack
void delayMicroseconds(unsigned long) {} // never block inside Rack

// IN 1 ISR pointer (the firmware calls attachInterrupt for CLK_IN rising).
static void (*_trigIsr)() = nullptr;
void attachInterrupt(int, void (*isr)(), int) { _trigIsr = isr; }

// ── Display geometry + refresh macro (mirrors the app TU) ────────────────────
#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define REQUEST_DISPLAY_REFRESH()     \
    do {                              \
        displayRefresh = 1;           \
        displayMgr.MarkInteraction(); \
    } while (0)

// ── Firmware library (unchanged) ─────────────────────────────────────────────
#include "Adafruit_GFX.h"
#include "Adafruit_SSD1306.h"

// shellObjects.hpp declares the board-owned display/displayMgr/encoder/cal that
// the shared headers reference. Must precede all of them.
#include "shellObjects.hpp"

#include "boardIO.hpp"
#include "boardPinouts.hpp"
#include "clockSource.hpp"
#include "displayManager.hpp"
#include "quantizer.hpp"
#include "scales.hpp"
#include "splash.hpp"
#include "utils.hpp"

#include "shiftreg.hpp"
#include "params.hpp"
#include "clock.hpp"
#include "outputs.hpp"
#include "cvInputs.hpp" // must precede presetManager.hpp (CVTarget types)
#include "presetManager.hpp"
#include "randomize.hpp" // RandomizeParams() — shared with the hardware menu
#include "storage.hpp"

#include "menuDefinitions.hpp"
#include "menuDisplay.hpp"
#include "menuHandlers.hpp"
#include "menuRender.hpp"

// ── Global objects (mirror the app TU, minus hardware/dual-core bits) ────────
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DisplayManager displayMgr(display);

WeavePair registers;
OutputBank outputs;
StepClock clockEngine;
Quantizer quantizer;
RegParams regParams[WEA_NUM_REGS];
GlobalParams globalParams;
ModBus modBus;
LiveParams liveParams;
bool resetArmed = false;

uint16_t noteMask = 0x0FFF;
uint8_t scaleIndex = 0;
uint8_t rootIndex = 0;

int menuItem = 1;
bool switchState = 1;
bool oldSwitchState = 1;
int menuMode = 0;
bool displayRefresh = 1;
bool unsavedChanges = false;
int menuScreenTimeout = 2;

CalibrationData cal;

// Brief on-screen message overlay (replaces the blocking ShowTemporaryMessage).
static char _tempMsg[32] = {0};
static unsigned long _tempMsgUntil = 0;

// ─────────────────────────────────────────────────────────────────────────────
// App-TU functions the firmware lib references — ported, non-blocking.
// ─────────────────────────────────────────────────────────────────────────────
void RedrawDisplay() {
    displayMgr.PrepareFrame(); // DrawOverlays + CommitFrame
    displayRefresh = 0;
    display.display(); // pack into the HostBridge framebuffer
}

// Non-blocking: stash the message + expiry; the renderer draws it as an overlay.
void ShowTemporaryMessage(const char *msg, uint32_t durationMs) {
    strncpy(_tempMsg, msg, sizeof(_tempMsg) - 1);
    _tempMsg[sizeof(_tempMsg) - 1] = 0;
    _tempMsgUntil = millis() + durationMs;
    REQUEST_DISPLAY_REFRESH();
}

// HandleOutputs() — shared with the firmware rather than written out again here.
// Included after the globals it references. Duplicating it instead is how
// GravityForge's Rack port came to be missing its LOOP▸NAP muting.
#include "engine.hpp"

} // namespace

// ── Touch points used by the engine entry layer (below) ──────────────────────
#include "fw_engine.hpp"

namespace wvengine {

// ── Per-instance firmware state (registry: engine_state.def) ─────────────────
// The firmware keeps its DSP/menu state in file-scope globals so the same lib/
// builds unchanged for the RP2040. To let multiple Rack instances coexist, each
// Engine owns an EngineState snapshot that is *swapped* with the live globals
// around every entry point (swap-in → run firmware → swap-out). Swapping rather
// than copying keeps every heap-backed String/std::vector living in exactly one
// place at a time, so it is allocation-free and safe. The swap is symmetric:
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

    // Deep-copy the live globals into this snapshot (globals unchanged). Used
    // once to capture the firmware's initialised power-on defaults — the scalar
    // globals get their defaults from their *initializers* (e.g. `menuItem = 1`),
    // which a default-constructed EngineState would not have.
    void copyFromGlobals() {
#define CF_SCALAR(T, n) this->n = (T)::n;
#define CF_ARRAY(T, n, N)            \
    for (int _i = 0; _i < (N); ++_i) \
        this->n[_i] = (T)::n[_i];
#define CF_OBJECT(T, n) this->n = ::n;
#define CF_OBJARRAY(T, n, N)         \
    for (int _i = 0; _i < (N); ++_i) \
        this->n[_i] = ::n[_i];
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
    bool lastClock = false;
    bool lastButton = false; // true = currently pressed
};

// One mutex guards the single shared set of firmware globals: all engine entry
// points serialize on it (process() on the audio thread, getFramebuffer()/UI on
// the draw thread), so the swap-in→run→swap-out region is never interleaved.
static std::mutex g_globalsMutex;

// RAII: lock the globals, point IO at this instance, swap its state into the
// live globals for the duration of the call, then swap it back out and unlock.
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

static int clampR(int r) { return r < 0 ? 0 : (r > WEA_NUM_REGS - 1 ? WEA_NUM_REGS - 1 : r); }
static int clampJ(int j) { return j < 0 ? 0 : (j > WEA_NUM_OUTS - 1 ? WEA_NUM_OUTS - 1 : j); }
static int clampIn(int i) { return i < 0 ? 0 : (i > NUM_CV_INS - 1 ? NUM_CV_INS - 1 : i); }

// One-time setup of the *shared* shim objects — the IN 1 interrupt vector, the
// SSD1306 and the MCP4728. These are process-global (not part of any instance's
// swapped state), so they run exactly once regardless of how many modules are
// instantiated. Wiring the rising-edge ISR here mirrors the firmware's Begin();
// without it IN 1 would do nothing and the module would never advance.
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
// the globals (the caller must hold an EngineScope). Mirrors the firmware's
// Begin() minus the shared one-time bits. A fresh instance starts from a blank
// EEPROM, so Load(0)/LoadCalibration fall back to defaults.
static void engineInstanceInit() {
    EEPROMInit();
    InitIO();
    displayMgr.SetUpdateInterval(WEA_DISPLAY_INTERVAL_MS);
    cal = LoadCalibration();
    UpdateParameters(Load(0));
    // The drift PRNG is seeded from engine time rather than the wall clock, so
    // a patch reload is reproducible. The PATTERN itself comes from the preset,
    // which is what makes a saved phrase come back exactly.
    registers.Seed(0x2545F491u);
    ApplyParams(registers, regParams, globalParams, modBus, liveParams);
    REQUEST_DISPLAY_REFRESH();
    displayMgr.MarkDirty();
}

// Encoder click — fires on the press edge, as the hardware's release-edge
// handler cannot be reused here (Rack delivers a click as a press/release pair
// inside one block).
static void doEncoderClick() {
    REQUEST_DISPLAY_REFRESH();
    if (menuMode == 0) {
        if (menuItem >= 1 && menuItem <= MENU_ITEM_COUNT) {
            const MenuItem &mi = MENU_ITEMS[menuItem - 1];
            if (mi.type == MENU_ACTION || mi.type == MENU_TOGGLE) {
                if (mi.action)
                    mi.action();
                LiveViewClear();
            } else { // MENU_EDIT
                menuMode = menuItem;
                LiveViewClear(); // the first detent is what opens the loom
            }
        }
    } else {
        menuMode = 0;
        LiveViewClear();
    }
}

Engine *createEngine() {
    Engine *e = new Engine();
    std::lock_guard<std::mutex> lock(g_globalsMutex);

    // The firmware's power-on defaults live in the globals' static initializers
    // (e.g. `menuItem = 1`), not in EngineState's members. On the first instance
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

void process(Engine *e, float dt, const float cvVolts[2], bool clockGateHigh,
             float outVolts[4]) {
    EngineScope scope(e);

    g_engineMicros += (unsigned long)(dt * 1.0e6f + 0.5f);

    // Feed the modulation CVs (volts -> 12-bit ADC) and the IN 1 level.
    e->host.adc[CV_1_IN_PIN] = voltsToAdc(cvVolts[0]);
    e->host.adc[CV_2_IN_PIN] = voltsToAdc(cvVolts[1]);
    e->host.gpio[CLK_IN_PIN] = clockGateHigh ? 1 : 0;
    if (clockGateHigh && !e->lastClock && _trigIsr)
        _trigIsr();
    e->lastClock = clockGateHigh;

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
            LiveViewClear();
        } else if (menuMode >= 1 && menuMode <= MENU_ITEM_COUNT) {
            MenuApplyEdit(menuMode, dir);
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
    // still holds whatever was last saved — on a fresh instance, nothing at all.
    // On this module that would lose the pattern itself, not just the settings:
    // the register contents are part of LoadSaveParams precisely so a patch
    // reload brings the phrase back.
    Save(CollectParams(), 0);
    return std::string((const char *)EEPROM.data.data(), EEPROM.data.size());
}

void deserialize(Engine *e, const std::string &blob) {
    EngineScope scope(e);
    EEPROM.data.assign(blob.begin(), blob.end());
    cal = LoadCalibration();
    UpdateParameters(Load(0));
    ApplyParams(registers, regParams, globalParams, modBus, liveParams);
    REQUEST_DISPLAY_REFRESH();
}

// ── Initialize / Randomize ───────────────────────────────────────────────────
// Back Rack's module actions. Both run entirely against the live firmware state
// under one EngineScope, so they must NOT call the public bridge helpers below
// (those take the same non-recursive lock).

void reset(Engine *e) {
    EngineScope scope(e);
    // Factory defaults, exactly what a fresh instance boots into. The stored
    // preset slots and the calibration block are deliberately left alone:
    // Initialize resets the patch you are playing, not your saved presets.
    UpdateParameters(LoadDefaultParams());
    ApplyParams(registers, regParams, globalParams, modBus, liveParams);
    menuItem = 1; // back to the top of the menu, like a power cycle
    menuMode = 0;
    MarkUnsaved(); // live state now differs from whatever slot 0 holds
    REQUEST_DISPLAY_REFRESH();
}

void randomize(Engine *e) {
    EngineScope scope(e);
    // Same implementation the hardware's PRESETS ▸ RANDOM action runs, so the
    // panel and the host roll the same kind of patch. lib/randomize.hpp
    // documents what is deliberately left alone (the output matrix, the CV
    // routing, the scale, the clock). Engine time is the entropy source — it is
    // per-instance and always moving, so two modules randomized in the same
    // frame do not agree.
    RandomizeParams((uint32_t)micros());
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

// ── Curated parameter bridge ─────────────────────────────────────────────────
// Absolute get/set helpers backing the Rack context menu. Each mutates the live
// firmware state under an EngineScope (globals lock + swap-in), then requests a
// display refresh so the emulated OLED tracks changes made from the menu.
//
// These write the *base* parameter block (regParams/globalParams/the slots), not
// the derived LiveParams — exactly as the on-panel menu does. Writing the
// derived block directly would be overwritten by the next ApplyParams().

// ── The registers ──
int lengthMin() { return WEA_MIN_LENGTH; }
int lengthMax() { return WEA_MAX_LENGTH; }
int lengthGet(Engine *e, int reg) {
    EngineScope scope(e);
    return (int)regParams[clampR(reg)].length;
}
void lengthSet(Engine *e, int reg, int n) {
    EngineScope scope(e);
    const int r = clampR(reg);
    regParams[r].length = (uint8_t)constrain(n, WEA_MIN_LENGTH, WEA_MAX_LENGTH);
    registers.Reg((uint8_t)r).SetLength(regParams[r].length);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

int chanceGet(Engine *e, int reg) {
    EngineScope scope(e);
    return (int)regParams[clampR(reg)].chance;
}
void chanceSet(Engine *e, int reg, int pct) {
    EngineScope scope(e);
    regParams[clampR(reg)].chance = (uint8_t)constrain(pct, 0, 100);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

void regRandomize(Engine *e, int reg) {
    EngineScope scope(e);
    registers.Reg((uint8_t)clampR(reg)).Randomize(_rndGen);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}
void regInvert(Engine *e, int reg) {
    EngineScope scope(e);
    registers.Reg((uint8_t)clampR(reg)).Invert();
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}
void regClear(Engine *e, int reg) {
    EngineScope scope(e);
    registers.Reg((uint8_t)clampR(reg)).Clear();
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}
void regFill(Engine *e, int reg) {
    EngineScope scope(e);
    registers.Reg((uint8_t)clampR(reg)).Fill();
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}
uint32_t regValue(Engine *e, int reg) {
    EngineScope scope(e);
    return (uint32_t)registers.Reg((uint8_t)clampR(reg)).Value();
}

// ── WEAVE ──
int weaveGet(Engine *e) {
    EngineScope scope(e);
    return (int)globalParams.weave;
}
void weaveSet(Engine *e, int pct) {
    EngineScope scope(e);
    globalParams.weave = (uint8_t)constrain(pct, 0, 100);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}
int weaveDirCount() { return (int)WeaveDirLength; }
std::string weaveDirName(int index) {
    if (index < 0 || index >= (int)WeaveDirLength)
        return "";
    return WeaveDirNames[index];
}
int weaveDirGet(Engine *e) {
    EngineScope scope(e);
    return (int)globalParams.dir;
}
void weaveDirSet(Engine *e, int index) {
    EngineScope scope(e);
    globalParams.dir = (uint8_t)constrain(index, 0, (int)WeaveDirLength - 1);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

// ── The output matrix ──
std::string jackName(int jack) { return OutJackNames[clampJ(jack)]; }

int routingCount() { return (int)RoutingLength; }
std::string routingName(int index) {
    if (index < 0 || index >= (int)RoutingLength)
        return "Custom";
    return RoutingNames[index];
}
int routingGet(Engine *e) {
    EngineScope scope(e);
    const uint8_t r = RoutingOf(outputs.Slots());
    return r == WEA_ROUTING_CUSTOM ? -1 : (int)r;
}
void routingSet(Engine *e, int index) {
    EngineScope scope(e);
    ApplyRouting(outputs.Slots(), (uint8_t)constrain(index, 0, (int)RoutingLength - 1));
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

int outSourceCount() { return (int)RegSourceLength; }
std::string outSourceName(int index) {
    if (index < 0 || index >= (int)RegSourceLength)
        return "";
    return RegSourceNames[index];
}
int outSourceGet(Engine *e, int jack) {
    EngineScope scope(e);
    return (int)outputs.Slot(clampJ(jack)).source;
}
void outSourceSet(Engine *e, int jack, int index) {
    EngineScope scope(e);
    OutSlot &s = outputs.Slot(clampJ(jack));
    s.source = (uint8_t)constrain(index, 0, (int)RegSourceLength - 1);
    // A rotate legal on the 32-position AB ring is out of range on a 16-position
    // one, so fold it rather than leaving the jack pointing off the end.
    if (s.source != SrcAB)
        s.rotate = (uint8_t)(s.rotate & 15);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

int outTypeCount() { return (int)OutTypeLength; }
std::string outTypeName(int index) {
    if (index < 0 || index >= (int)OutTypeLength)
        return "";
    return OutTypeNames[index];
}
int outTypeGet(Engine *e, int jack) {
    EngineScope scope(e);
    return (int)outputs.Slot(clampJ(jack)).type;
}
void outTypeSet(Engine *e, int jack, int index) {
    EngineScope scope(e);
    // Through the panel's own setter, so a type change reloads the same sensible
    // defaults for DEPTH and the two contextual fields on both hosts. It takes a
    // relative delta, so drive it to the target rather than assigning.
    const int j = clampJ(jack);
    const int target = constrain(index, 0, (int)OutTypeLength - 1);
    int guard = 0;
    while ((int)outputs.Slot(j).type != target && guard++ < (int)OutTypeLength) {
        const int d = (target > (int)outputs.Slot(j).type) ? 1 : -1;
        switch (j) {
        case 0: setOutType<0>(d); break;
        case 1: setOutType<1>(d); break;
        case 2: setOutType<2>(d); break;
        default: setOutType<3>(d); break;
        }
    }
    REQUEST_DISPLAY_REFRESH();
}

int outDepthGet(Engine *e, int jack) {
    EngineScope scope(e);
    return (int)outputs.Slot(clampJ(jack)).depth;
}
void outDepthSet(Engine *e, int jack, int bits) {
    EngineScope scope(e);
    outputs.Slot(clampJ(jack)).depth = (uint8_t)constrain(bits, 1, WEA_MAX_DEPTH);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

int outRotateGet(Engine *e, int jack) {
    EngineScope scope(e);
    return (int)outputs.Slot(clampJ(jack)).rotate;
}
void outRotateSet(Engine *e, int jack, int pos) {
    EngineScope scope(e);
    OutSlot &s = outputs.Slot(clampJ(jack));
    const int span = (s.source == SrcAB) ? 32 : 16;
    s.rotate = (uint8_t)(((pos % span) + span) % span);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}
int outRotateSpan(Engine *e, int jack) {
    EngineScope scope(e);
    return outputs.Slot(clampJ(jack)).source == SrcAB ? 32 : 16;
}

std::string outParamLabel(Engine *e, int jack) {
    EngineScope scope(e);
    switch (outputs.Slot(clampJ(jack)).type) {
    case OutNote:
        return "Range";
    case OutMod:
        return "Level";
    default:
        return "Threshold";
    }
}
int outParamMin(Engine *e, int jack) {
    EngineScope scope(e);
    return outputs.Slot(clampJ(jack)).type == OutNote ? 1 : 0;
}
int outParamMax(Engine *e, int jack) {
    EngineScope scope(e);
    return outputs.Slot(clampJ(jack)).type == OutNote ? QUANT_OCTAVES : 100;
}
int outParamGet(Engine *e, int jack) {
    EngineScope scope(e);
    return (int)outputs.Slot(clampJ(jack)).param;
}
void outParamSet(Engine *e, int jack, int v) {
    EngineScope scope(e);
    OutSlot &s = outputs.Slot(clampJ(jack));
    const int lo = (s.type == OutNote) ? 1 : 0;
    const int hi = (s.type == OutNote) ? QUANT_OCTAVES : 100;
    s.param = (uint8_t)constrain(v, lo, hi);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

std::string outParam2Label(Engine *e, int jack) {
    EngineScope scope(e);
    switch (outputs.Slot(clampJ(jack)).type) {
    case OutTrig:
        return "Width";
    case OutGate:
        return ""; // a gate has no second field
    default:
        return "Slew";
    }
}
int outParam2Min(Engine *e, int jack) {
    EngineScope scope(e);
    return outputs.Slot(clampJ(jack)).type == OutTrig ? 1 : 0;
}
int outParam2Max(Engine *e, int) { (void)e; return 100; }
int outParam2Get(Engine *e, int jack) {
    EngineScope scope(e);
    const OutSlot &s = outputs.Slot(clampJ(jack));
    // WIDTH is stored in half-milliseconds so a byte reaches 510 ms; the bridge
    // speaks milliseconds, which is what the menu shows.
    return s.type == OutTrig ? (int)s.param2 * 2 : (int)s.param2;
}
void outParam2Set(Engine *e, int jack, int v) {
    EngineScope scope(e);
    OutSlot &s = outputs.Slot(clampJ(jack));
    if (s.type == OutGate)
        return;
    if (s.type == OutTrig)
        s.param2 = (uint8_t)constrain(v / 2, 1, 100);
    else
        s.param2 = (uint8_t)constrain(v, 0, 100);
    MarkUnsaved();
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
bool clockIsExternal(Engine *e) {
    EngineScope scope(e);
    return clockEngine.IsExternalLive();
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
int rateCount() { return WEA_CLOCK_RATE_COUNT; }
std::string rateName(int index) {
    if (index < 0 || index >= WEA_CLOCK_RATE_COUNT)
        return "";
    return ClockRateNames[index];
}
int rateGet(Engine *e) {
    EngineScope scope(e);
    return clockEngine.GetRate();
}
void rateSet(Engine *e, int index) {
    EngineScope scope(e);
    clockEngine.SetRate(index);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

// ── Pitch ──
std::string noteName(int index) {
    if (index < 0 || index > 11)
        return "";
    return noteNames[index];
}
int rootGet(Engine *e) {
    EngineScope scope(e);
    return (int)rootIndex;
}
void rootSet(Engine *e, int index) {
    EngineScope scope(e);
    rootIndex = (uint8_t)constrain(index, 0, 11);
    bool notes[12];
    BuildScale(scaleIndex, rootIndex, notes);
    noteMask = 0;
    for (int i = 0; i < 12; i++)
        if (notes[i])
            noteMask |= (uint16_t)(1u << i);
    RebuildQuantizer();
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}
int scaleCount() { return numScales; }
std::string scaleName(int index) {
    if (index < 0 || index >= numScales)
        return "";
    return scaleNames[index];
}
int scaleGet(Engine *e) {
    EngineScope scope(e);
    return (int)scaleIndex;
}
void scaleSet(Engine *e, int index) {
    EngineScope scope(e);
    scaleIndex = (uint8_t)constrain(index, 0, numScales - 1);
    bool notes[12];
    BuildScale(scaleIndex, rootIndex, notes);
    noteMask = 0;
    for (int i = 0; i < 12; i++)
        if (notes[i])
            noteMask |= (uint16_t)(1u << i);
    RebuildQuantizer();
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}
int transposeGet(Engine *e) {
    EngineScope scope(e);
    return (int)globalParams.transpose;
}
void transposeSet(Engine *e, int semitones) {
    EngineScope scope(e);
    globalParams.transpose = (int8_t)constrain(semitones, -24, 24);
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
    return (int)cvTarget[clampIn(input)];
}
void cvTargetSet(Engine *e, int input, int index) {
    EngineScope scope(e);
    cvTarget[clampIn(input)] = (uint8_t)constrain(index, 0, (int)CVTargetLength - 1);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}
int cvDepthGet(Engine *e, int input) {
    EngineScope scope(e);
    return (int)cvDepth[clampIn(input)];
}
void cvDepthSet(Engine *e, int input, int pct) {
    EngineScope scope(e);
    cvDepth[clampIn(input)] = (uint8_t)constrain(pct, 0, 100);
    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

// ── forgevcv adapter ─────────────────────────────────────────────────────────
VcvEngine::VcvEngine() : e_(createEngine()) {}
VcvEngine::~VcvEngine() { destroyEngine(e_); }

void VcvEngine::process(float dt, const float *cv, int nCv, bool clockHigh,
                        float *out, int nOut) {
    float in[2] = {0.0f, 0.0f};
    for (int i = 0; i < nCv && i < 2; i++)
        in[i] = cv[i];
    float o[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    wvengine::process(e_, dt, in, clockHigh, o);
    for (int i = 0; i < nOut && i < 4; i++)
        out[i] = o[i];
}

void VcvEngine::encoderTurn(int detents) { wvengine::encoderTurn(e_, detents); }
void VcvEngine::encoderButton(bool pressed) { wvengine::encoderButton(e_, pressed); }
void VcvEngine::getFramebuffer(uint8_t out[1024]) { wvengine::getFramebuffer(e_, out); }
std::string VcvEngine::serialize() { return wvengine::serialize(e_); }
void VcvEngine::deserialize(const std::string &s) { wvengine::deserialize(e_, s); }
void VcvEngine::reset() { wvengine::reset(e_); }
void VcvEngine::randomize() { wvengine::randomize(e_); }

} // namespace wvengine
