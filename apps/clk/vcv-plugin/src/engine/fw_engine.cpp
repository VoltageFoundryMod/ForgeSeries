// fw_engine.cpp — ClockForge firmware compiled inside VCV Rack via the shim layer.
//
// This is a VCV-adapted port of src/main.cpp: it pulls in the unchanged firmware
// lib/ (the valuable DSP + menu + render code) through the Arduino shim, defines
// the same set of globals, and replaces the hardware/dual-core integration glue
// with host-driven entry points. It exposes ONLY a POD/opaque API (fw_engine.hpp)
// so it never shares Arduino/rack types with the rest of the plugin.

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
#include <random>
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
// COMDAT copy of a same-named class. The public cfengine:: API below stays
// outside it, and is the only thing this TU exports.
namespace {

#include "../shim/Arduino.h"
#include "../shim/Wire.h"
#include "../shim/EEPROM.h"

// ── Shim symbol definitions ──────────────────────────────────────────────────
HostBridge *g_host = nullptr;
SerialShim Serial;
TwoWire Wire;
TwoWire Wire1;
EEPROMClass EEPROM;

// Engine time advances by the host's sample time (deterministic; correct under
// faster-than-realtime rendering), not wall-clock. process() advances it.
unsigned long g_engineMicros = 0;
unsigned long micros() { return g_engineMicros; }
unsigned long millis() { return g_engineMicros / 1000UL; }
void delay(unsigned long) {}             // never block inside Rack
void delayMicroseconds(unsigned long) {} // never block inside Rack

// External-clock ISR pointer (firmware calls attachInterrupt for CLK_IN rising).
static void (*_clkIsr)() = nullptr;
void attachInterrupt(int, void (*isr)(), int) { _clkIsr = isr; }

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
#include "../shim/Adafruit_GFX.h"
#include "../shim/Adafruit_SSD1306.h"

// shellObjects.hpp declares the board-owned display/displayMgr/encoder/cal
// that the shared headers reference. Must precede all of them.
#include "shellObjects.hpp"

#include "boardIO.hpp"
#include "clockEngine.hpp"
#include "cvInputs.hpp"
#include "displayManager.hpp"
#include "menuDefinitions.hpp"
#include "menuDisplay.hpp"
#include "menuHandlers.hpp"
#include "menuRender.hpp"
#include "metrics.hpp"
#include "outputs.hpp"
#include "jacks.hpp"
#include "presetManager.hpp"
#include "splash.hpp"
#include "storage.hpp"
#include "utils.hpp"

// ── Global objects (mirror main.cpp, minus hardware/dual-core bits) ───────────
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DisplayManager displayMgr(display);

// metrics lives in core/metrics.hpp now, one shared instance.

Output outputs[NUM_OUTPUTS] = {
    Output(1, OutputType::DACOut),
    Output(2, OutputType::DACOut),
    Output(3, OutputType::DACOut),
    Output(4, OutputType::DACOut)};

bool masterState = true;

int menuItem = 2;
bool switchState = 1;
bool oldSwitchState = 1;
int menuMode = 0;
bool displayRefresh = 1;
bool unsavedChanges = false;
int euclideanOutputSelect = 0;
int quantizerOutputSelect = 0;
int envelopeOutputSelect = 0;
int loopOutputSelect = 0;
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
    display.display(); // pack into HostBridge framebuffer
}



// Non-blocking: stash the message + expiry; the renderer draws it as an overlay.
void ShowTemporaryMessage(const char *msg, uint32_t durationMs) {
    strncpy(_tempMsg, msg, sizeof(_tempMsg) - 1);
    _tempMsg[sizeof(_tempMsg) - 1] = 0;
    _tempMsgUntil = millis() + durationMs;
    REQUEST_DISPLAY_REFRESH();
}

// HandleOutputs() and the transport — shared with the firmware rather than
// written out again here. Included after the globals they reference.
#include "engine.hpp"

// Hardware calibration wizard is meaningless in Rack — outputs/inputs are ideal.
void RunCalibration() {
    ShowTemporaryMessage("N/A", 1200);
}

} // namespace — end of the private firmware translation unit

// ── Touch points used by the engine entry layer (below) ───────────────────────
#include "fw_engine.hpp"

namespace cfengine {

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
    // Mirror one field per registered global.
#define CF_SCALAR(T, n) T n;
#define CF_ARRAY(T, n, N) T n[N];
#define CF_OBJECT(T, n) T n;
#include "engine_state.def"
#undef CF_SCALAR
#undef CF_ARRAY
#undef CF_OBJECT

    // outputs[] is handled outside the registry: Output has no default ctor, so
    // the array member needs an initializer (mirroring the global definition).
    Output outputs[NUM_OUTPUTS] = {
        Output(1, OutputType::DACOut),
        Output(2, OutputType::DACOut),
        Output(3, OutputType::DACOut),
        Output(4, OutputType::DACOut)};

    // Exchange this snapshot with the live firmware globals (symmetric).
    void swapWithGlobals() {
        using std::swap;
#define CF_SCALAR(T, n)          \
    {                            \
        T _t = (T)::n;           \
        ::n = this->n;           \
        this->n = _t;            \
    }
#define CF_ARRAY(T, n, N)             \
    for (int _i = 0; _i < (N); ++_i) { \
        T _t = (T)::n[_i];             \
        ::n[_i] = this->n[_i];         \
        this->n[_i] = _t;              \
    }
#define CF_OBJECT(T, n) swap(::n, this->n);
#include "engine_state.def"
#undef CF_SCALAR
#undef CF_ARRAY
#undef CF_OBJECT
        for (int i = 0; i < NUM_OUTPUTS; ++i)
            swap(::outputs[i], this->outputs[i]);
    }

    // Deep-copy the live globals into this snapshot (globals unchanged).  Used
    // once to capture the firmware's initialised power-on defaults — the scalar
    // globals get their defaults from their *initializers* (e.g. `menuItem = 2`),
    // which a default-constructed EngineState would not have.
    void copyFromGlobals() {
#define CF_SCALAR(T, n) this->n = (T)::n;
#define CF_ARRAY(T, n, N) \
    for (int _i = 0; _i < (N); ++_i) this->n[_i] = (T)::n[_i];
#define CF_OBJECT(T, n) this->n = ::n;
#include "engine_state.def"
#undef CF_SCALAR
#undef CF_ARRAY
#undef CF_OBJECT
        for (int i = 0; i < NUM_OUTPUTS; ++i)
            this->outputs[i] = ::outputs[i];
    }
};

struct Engine {
    HostBridge host;
    EngineState state;
    double tickAccum = 0.0;
    bool lastClock = false;
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

// One-time setup of the *shared* shim objects — the external-clock interrupt
// vector, the SSD1306, and the MCP4728.  These are process-global (not part of
// any instance's swapped state), so they run exactly once regardless of how many
// modules are instantiated.  Wiring the CLK IN rising-edge ISR (ClockReceived)
// here mirrors main.cpp setup(); without it external-clock sync never engages.
static void globalOneTimeInit() {
    static bool done = false;
    if (done) return;
    done = true;
    attachInterrupt(digitalPinToInterrupt(CLK_IN_PIN), ClockReceived, RISING);
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
    UpdateBPM(BPM);
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
                if (mi.action) mi.action();
            } else { // MENU_EDIT
                menuMode = menuItem;
                if (menuItem == 61)
                    pendingCVInputTarget[0] = CVInputTarget[0];
                else if (menuItem == 62)
                    pendingCVInputTarget[1] = CVInputTarget[1];
            }
        }
    } else {
        if (menuMode == 61)
            CVInputTarget[0] = pendingCVInputTarget[0];
        else if (menuMode == 62)
            CVInputTarget[1] = pendingCVInputTarget[1];
        menuMode = 0;
    }
}

Engine *createEngine() {
    Engine *e = new Engine();
    std::lock_guard<std::mutex> lock(g_globalsMutex);

    // The firmware's power-on defaults live in the globals' static initializers
    // (e.g. `menuItem = 2`, `BPM = 120`), not in EngineState's members.  On the
    // first instance the globals are still at those defaults, so run the one-time
    // shared setup + a full instance init against them and capture the result as
    // the template every instance is seeded from.
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
    if (g_host == &e->host) g_host = nullptr;
    delete e;
}

void process(Engine *e, float dt, const float cvVolts[2], bool clockGateHigh, float outVolts[4]) {
    EngineScope scope(e);

    // Advance engine time by the elapsed block.
    g_engineMicros += (unsigned long)(dt * 1.0e6f + 0.5f);

    // Feed CV inputs (volts -> 12-bit ADC) and the external-clock gate.
    e->host.adc[CV_1_IN_PIN] = voltsToAdc(cvVolts[0]);
    e->host.adc[CV_2_IN_PIN] = voltsToAdc(cvVolts[1]);
    e->host.gpio[CLK_IN_PIN] = clockGateHigh ? 1 : 0;
    if (clockGateHigh && !e->lastClock && _clkIsr) _clkIsr();
    e->lastClock = clockGateHigh;

    // Apply any deferred BPM change.
    if (bpmNeedsUpdate) { bpmNeedsUpdate = false; UpdateBPM(BPM); }

    // Advance the PPQN clock for the elapsed time (batch ticks).
    e->tickAccum += (double)dt * (double)BPM / 60.0 * (double)PPQN;
    int guard = 0;
    while (e->tickAccum >= 1.0 && guard++ < 200000) {
        ClockPulse();
        e->tickAccum -= 1.0;
    }

    HandleCVInputs();
    HandleOutputs();
    HandleExternalClock();

    if (displayRefresh) displayMgr.MarkDirty();

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
            if (menuItem < 1) menuItem = MENU_ITEM_COUNT;
            else if (menuItem > MENU_ITEM_COUNT) menuItem = 1;
        } else if (menuMode >= 1 && menuMode <= MENU_ITEM_COUNT) {
            if (MENU_ITEMS[menuMode - 1].setter)
                MENU_ITEMS[menuMode - 1].setter(dir);
        }
    }
}

void encoderButton(Engine *e, bool pressed) {
    EngineScope scope(e);
    if (pressed && !e->lastButton) doEncoderClick(); // press edge
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
        if (_tempMsg[0]) { _tempMsg[0] = 0; REQUEST_DISPLAY_REFRESH(); }
        if (displayRefresh) displayMgr.MarkDirty();
        HandleDisplay();
    }
    for (int i = 0; i < 1024; i++) out[i] = e->host.fb[i];
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
    lastInternalBPM = BPM;
    UpdateBPM(BPM);
    SetMasterState(true); // a fresh module is running
    menuItem = 2;         // back to the top of the menu, like a power cycle
    menuMode = 0;
    unsavedChanges = true; // live state now differs from whatever slot 0 holds
    REQUEST_DISPLAY_REFRESH();
}

void randomize(Engine *e) {
    EngineScope scope(e);
    static std::mt19937 rng(std::random_device{}());
    auto ri = [&](int lo, int hi) { // inclusive
        return (int)std::uniform_int_distribution<int>(lo, hi)(rng);
    };

    for (int i = 0; i < NUM_OUTPUTS; i++) {
        // Waveform first: SetWaveformType moves an envelope output onto the
        // locked "Env" divider slot, and SetDivider is a no-op once it has. Only
        // the plain pulse/LFO shapes are offered — rolling an envelope type would
        // silently take the output's divider away with it.
        outputs[i].SetWaveformType(static_cast<WaveformType>(
            ri(0, (int)ExpEnvelope - 1)));

        // Dividers cluster around x1 (index 9). The extremes of the table are
        // /128 and x64, and a random pick across the whole range mostly produces
        // outputs that fire once a minute or once a millisecond.
        outputs[i].SetDivider(ri(5, 14));
        outputs[i].SetDutyCycle(ri(10, 75));
        outputs[i].SetOutputState(true); // never randomize an output into silence
        outputs[i].SetInvert(false);
        outputs[i].SetLevel(100);
        outputs[i].SetOffset(0);
        outputs[i].SetPhase(ri(0, 1) ? ri(1, 75) : 0);

        // Swing and probability: leave them off most of the time, so when they
        // do land the result still reads as a clock rather than as a stutter.
        outputs[i].SetSwingAmount(ri(0, 2) == 0 ? ri(1, 6) : 0);
        outputs[i].SetSwingEvery(ri(1, 4));
        outputs[i].SetPulseProbability(ri(0, 2) == 0 ? ri(60, 99) : 100);

        // Euclidean patterns are the interesting half of this module, so give
        // them a real chance — but always with at least one trigger in the
        // pattern, and never more triggers than steps.
        if (ri(0, 1)) {
            EuclideanParams eu = outputs[i].GetEuclideanParams();
            eu.enabled = true;
            eu.steps = ri(4, 16);
            eu.triggers = ri(1, eu.steps);
            eu.rotation = ri(0, eu.steps - 1);
            outputs[i].SetEuclideanParams(eu);
        } else {
            EuclideanParams eu = outputs[i].GetEuclideanParams();
            eu.enabled = false;
            outputs[i].SetEuclideanParams(eu);
        }
    }

    // Tempo, the external clock divider, the CV matrix and the cross/loop
    // routing are patch-level sync decisions, not sound design — Rack leaves
    // ports alone on randomize and so do we.

    UpdateBPM(BPM);
    unsavedChanges = true;
    REQUEST_DISPLAY_REFRESH();
}

void deserialize(Engine *e, const std::string &blob) {
    EngineScope scope(e);
    EEPROM.data.assign(blob.begin(), blob.end());
    // Reload settings from slot 0 into the live state.
    cal = LoadCalibration();
    LoadSaveParams p = Load(0);
    UpdateParameters(p);
    UpdateBPM(BPM);
    REQUEST_DISPLAY_REFRESH();
}

int bpm(Engine *e) {
    EngineScope scope(e);
    return (int)BPM;
}

// ── Curated parameter bridge ──────────────────────────────────────────────────
// Absolute get/set helpers backing the Rack context menu.  Each mutates the live
// firmware state under an EngineScope (globals lock + swap-in), then requests a
// display refresh so the emulated OLED tracks changes made from the menu.

static int clampOut(int out) { return out < 0 ? 0 : (out > NUM_OUTPUTS - 1 ? NUM_OUTPUTS - 1 : out); }

bool isRunning(Engine *e) {
    EngineScope scope(e);
    return masterState;
}
void setRunning(Engine *e, bool running) {
    EngineScope scope(e);
    SetMasterState(running);
    REQUEST_DISPLAY_REFRESH();
}

int bpmMin() { return (int)minBPM; }
int bpmMax() { return (int)maxBPM; }
void setBpm(Engine *e, int newBpm) {
    EngineScope scope(e);
    BPM = (unsigned int)constrain(newBpm, (int)minBPM, (int)maxBPM);
    lastInternalBPM = BPM;
    bpmNeedsUpdate = true; // process() calls UpdateBPM on the next block
    unsavedChanges = true;
    REQUEST_DISPLAY_REFRESH();
}

int waveformCount() { return WaveformTypeLength; }
std::string waveformName(int index) {
    if (index < 0 || index >= WaveformTypeLength)
        return "";
    return WaveformTypeDescriptions[index].c_str();
}
int outputWaveform(Engine *e, int out) {
    EngineScope scope(e);
    return outputs[clampOut(out)].GetWaveformTypeIndex();
}
void setOutputWaveform(Engine *e, int out, int waveform) {
    EngineScope scope(e);
    if (waveform < 0 || waveform >= WaveformTypeLength)
        return;
    outputs[clampOut(out)].SetWaveformType(static_cast<WaveformType>(waveform));
    unsavedChanges = true;
    REQUEST_DISPLAY_REFRESH();
}

int dividerCount(Engine *e) {
    EngineScope scope(e);
    return outputs[0].GetDividerAmounts(); // user-selectable slots (excludes "Env")
}
std::string dividerName(Engine *e, int index) {
    EngineScope scope(e);
    return outputs[0].GetDividerDescriptionAt(index).c_str();
}
int outputDivider(Engine *e, int out) {
    EngineScope scope(e);
    return outputs[clampOut(out)].GetDividerIndex();
}
void setOutputDivider(Engine *e, int out, int index) {
    EngineScope scope(e);
    outputs[clampOut(out)].SetDivider(index); // no-op for envelope outputs (locked to "Env")
    unsavedChanges = true;
    REQUEST_DISPLAY_REFRESH();
}
bool outputIsEnvelope(Engine *e, int out) {
    EngineScope scope(e);
    return outputs[clampOut(out)].IsEnvelopeType();
}

bool outputEnabled(Engine *e, int out) {
    EngineScope scope(e);
    return outputs[clampOut(out)].GetOutputState();
}
void setOutputEnabled(Engine *e, int out, bool on) {
    EngineScope scope(e);
    outputs[clampOut(out)].SetOutputState(on);
    unsavedChanges = true;
    REQUEST_DISPLAY_REFRESH();
}

// ── forgevcv::IEngine adapter ─────────────────────────────────────────────────
// Thin forwards to the free-function bridge above. The firmware is fixed at
// 2 CV inputs / 4 outputs, so the nCv/nOut counts are informational here.
VcvEngine::VcvEngine() : e_(createEngine()) {}
VcvEngine::~VcvEngine() { destroyEngine(e_); }

void VcvEngine::process(float dt, const float *cv, int /*nCv*/,
                        bool clockHigh, float *out, int /*nOut*/) {
    cfengine::process(e_, dt, cv, clockHigh, out);
}
void VcvEngine::encoderTurn(int detents) { cfengine::encoderTurn(e_, detents); }
void VcvEngine::encoderButton(bool pressed) { cfengine::encoderButton(e_, pressed); }
void VcvEngine::getFramebuffer(uint8_t out[1024]) { cfengine::getFramebuffer(e_, out); }
std::string VcvEngine::serialize() { return cfengine::serialize(e_); }
void VcvEngine::deserialize(const std::string &blob) { cfengine::deserialize(e_, blob); }
void VcvEngine::reset() { cfengine::reset(e_); }
void VcvEngine::randomize() { cfengine::randomize(e_); }

} // namespace cfengine
