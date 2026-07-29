// fw_engine.cpp — NoteForge firmware compiled inside VCV Rack via the shim layer.
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
// COMDAT copy of a same-named class. The public nfengine:: API below stays
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
unsigned long g_engineMicros = 0;
unsigned long micros() { return g_engineMicros; }
unsigned long millis() { return g_engineMicros / 1000UL; }
void delay(unsigned long) {}             // never block inside Rack
void delayMicroseconds(unsigned long) {} // never block inside Rack

// TRIG-input ISR pointer (the firmware calls attachInterrupt for CLK_IN rising).
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

// shellObjects.hpp declares the board-owned display/displayMgr/encoder/cal
// that the shared headers reference. Must precede all of them.
#include "shellObjects.hpp"

#include "boardIO.hpp"
#include "boardPinouts.hpp"
#include "channel.hpp"
#include "cvInputs.hpp"
#include "displayManager.hpp"
#include "presetManager.hpp"
#include "splash.hpp"
#include "storage.hpp"
#include "utils.hpp"

#include "menuDefinitions.hpp"
#include "menuDisplay.hpp"
#include "menuHandlers.hpp"
#include "menuRender.hpp"

// ── Global objects (mirror main.cpp, minus hardware/dual-core bits) ───────────
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DisplayManager displayMgr(display);

QuantizerChannel channels[NUM_CHANNELS];

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

// Non-blocking: stash the message + expiry; the renderer draws it as an overlay.
void ShowTemporaryMessage(const char *msg, uint32_t durationMs) {
    strncpy(_tempMsg, msg, sizeof(_tempMsg) - 1);
    _tempMsg[sizeof(_tempMsg) - 1] = 0;
    _tempMsgUntil = millis() + durationMs;
    REQUEST_DISPLAY_REFRESH();
}

// HandleOutputs() — shared with the firmware and the unified build. Included
// here, after the globals it references, rather than copied.
#include "engine.hpp"

} // namespace

// ── Touch points used by the engine entry layer (below) ───────────────────────
#include "fw_engine.hpp"

namespace nfengine {

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

static int clampChannel(int ch) { return ch < 0 ? 0 : (ch > NUM_CHANNELS - 1 ? NUM_CHANNELS - 1 : ch); }

// One-time setup of the *shared* shim objects — the TRIG interrupt vector, the
// SSD1306 and the MCP4728.  These are process-global (not part of any instance's
// swapped state), so they run exactly once regardless of how many modules are
// instantiated.  Wiring the TRIG rising-edge ISR here mirrors main.cpp setup();
// without it the gate/envelope never fires from the TRIG jack.
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

    // Advance engine time by the elapsed block.
    g_engineMicros += (unsigned long)(dt * 1.0e6f + 0.5f);

    // Feed the pitch CVs (volts -> 12-bit ADC) and the TRIG level.
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
    menuItem = 1; // back to the top of the menu, like a power cycle
    menuMode = 0;
    MarkUnsaved(); // live state now differs from whatever slot 0 holds
    REQUEST_DISPLAY_REFRESH();
}

void randomize(Engine *e) {
    EngineScope scope(e);
    static std::mt19937 rng(std::random_device{}());
    auto ri = [&](int lo, int hi) { // inclusive
        return (int)std::uniform_int_distribution<int>(lo, hi)(rng);
    };

    for (int i = 0; i < NUM_CHANNELS; i++) {
        // Skip scale 0 (Chromatic): quantizing to every semitone is the same as
        // not quantizing, so it is the one choice that makes randomize look
        // broken. SelectScale/SelectRoot rebuild the note mask; the plain Set*
        // accessors would leave the previous mask in place.
        channels[i].SelectScale(ri(1, numScales - 1));
        channels[i].SelectRoot(ri(0, 11));
        channels[i].SetOctave(ri(-2, 2));
        channels[i].SetGlide(ri(0, 1) ? ri(1, 40) : 0); // usually none, sometimes a slide
        channels[i].SetPitchMode(ri(0, (int)PitchModeLength - 1));

        // Gate. Decay is capped well below ENVELOPE_MAX_DECAY: a multi-second
        // envelope never returns to zero between notes and stops reading as a
        // gate at all.
        channels[i].envelope.SetMode(ri(0, (int)GateModeLength - 1));
        channels[i].envelope.SetAttack(ri(0, 80));
        channels[i].envelope.SetDecay(ri(60, 600));
        channels[i].envelope.SetLevel(ri(70, 100));
    }

    // The sync mode, the IN 2 role, the transposition range and the note settle
    // window are input-routing and timing decisions, not sound design — Rack
    // leaves ports alone on randomize and so do we.

    MarkUnsaved();
    REQUEST_DISPLAY_REFRESH();
}

// ── Curated parameter bridge ──────────────────────────────────────────────────
// Absolute get/set helpers backing the Rack context menu.  Each mutates the live
// firmware state under an EngineScope (globals lock + swap-in), then requests a
// display refresh so the emulated OLED tracks changes made from the menu.

int scaleCount() { return numScales; }
std::string scaleName(int index) {
    if (index < 0 || index >= numScales)
        return "";
    return scaleNames[index];
}
int channelScale(Engine *e, int ch) {
    EngineScope scope(e);
    return channels[clampChannel(ch)].GetScaleIndex();
}
void setChannelScale(Engine *e, int ch, int scale) {
    EngineScope scope(e);
    channels[clampChannel(ch)].SelectScale(scale);
    unsavedChanges = true;
    REQUEST_DISPLAY_REFRESH();
}

std::string noteName(int note) {
    if (note < 0 || note >= 12)
        return "";
    return noteNames[note];
}
int channelRoot(Engine *e, int ch) {
    EngineScope scope(e);
    return channels[clampChannel(ch)].GetRootIndex();
}
void setChannelRoot(Engine *e, int ch, int root) {
    EngineScope scope(e);
    channels[clampChannel(ch)].SelectRoot(root);
    unsavedChanges = true;
    REQUEST_DISPLAY_REFRESH();
}

bool noteEnabled(Engine *e, int ch, int note) {
    EngineScope scope(e);
    return channels[clampChannel(ch)].GetActiveNote(note);
}
void setNoteEnabled(Engine *e, int ch, int note, bool on) {
    EngineScope scope(e);
    channels[clampChannel(ch)].SetActiveNote(note, on);
    unsavedChanges = true;
    REQUEST_DISPLAY_REFRESH();
}

int octaveMin() { return CHANNEL_OCTAVE_MIN; }
int octaveMax() { return CHANNEL_OCTAVE_MAX; }
int channelOctave(Engine *e, int ch) {
    EngineScope scope(e);
    return channels[clampChannel(ch)].GetOctave();
}
void setChannelOctave(Engine *e, int ch, int octave) {
    EngineScope scope(e);
    channels[clampChannel(ch)].SetOctave(octave);
    unsavedChanges = true;
    REQUEST_DISPLAY_REFRESH();
}

int channelGlide(Engine *e, int ch) {
    EngineScope scope(e);
    return channels[clampChannel(ch)].GetGlide();
}
void setChannelGlide(Engine *e, int ch, int glide) {
    EngineScope scope(e);
    channels[clampChannel(ch)].SetGlide(glide);
    unsavedChanges = true;
    REQUEST_DISPLAY_REFRESH();
}

int pitchModeCount() { return (int)PitchModeLength; }
std::string pitchModeName(int index) {
    if (index < 0 || index >= (int)PitchModeLength)
        return "";
    return PitchModeNames[index];
}
int channelPitchMode(Engine *e, int ch) {
    EngineScope scope(e);
    return channels[clampChannel(ch)].GetPitchMode();
}
void setChannelPitchMode(Engine *e, int ch, int mode) {
    EngineScope scope(e);
    channels[clampChannel(ch)].SetPitchMode(mode);
    unsavedChanges = true;
    REQUEST_DISPLAY_REFRESH();
}

int in2RoleCount() { return (int)In2RoleLength; }
std::string in2RoleName(int index) {
    if (index < 0 || index >= (int)In2RoleLength)
        return "";
    return In2RoleNames[index];
}
int in2RoleGet(Engine *e) {
    EngineScope scope(e);
    return (int)in2Role;
}
void in2RoleSet(Engine *e, int role) {
    EngineScope scope(e);
    in2Role = (unsigned char)constrain(role, 0, (int)In2RoleLength - 1);
    unsavedChanges = true;
    REQUEST_DISPLAY_REFRESH();
}

int transposeRangeCount() { return (int)TransposeRangeLength; }
std::string transposeRangeName(int index) {
    if (index < 0 || index >= (int)TransposeRangeLength)
        return "";
    return TransposeRangeNames[index];
}
int transposeRangeGet(Engine *e) {
    EngineScope scope(e);
    return (int)transposeRange;
}
void transposeRangeSet(Engine *e, int range) {
    EngineScope scope(e);
    transposeRange = (unsigned char)constrain(range, 0, (int)TransposeRangeLength - 1);
    unsavedChanges = true;
    REQUEST_DISPLAY_REFRESH();
}

bool channelTranspose(Engine *e, int ch) {
    EngineScope scope(e);
    return channels[clampChannel(ch)].GetTransposeEnabled();
}
void setChannelTranspose(Engine *e, int ch, bool on) {
    EngineScope scope(e);
    channels[clampChannel(ch)].SetTransposeEnabled(on);
    unsavedChanges = true;
    REQUEST_DISPLAY_REFRESH();
}
int channelTransposeDegrees(Engine *e, int ch) {
    EngineScope scope(e);
    return channels[clampChannel(ch)].GetTransposeDegrees();
}

int settleMax() { return CHANNEL_SETTLE_MAX_MS; }
int channelSettle(Engine *e, int ch) {
    EngineScope scope(e);
    return channels[clampChannel(ch)].GetSettle();
}
void setChannelSettle(Engine *e, int ch, int ms) {
    EngineScope scope(e);
    channels[clampChannel(ch)].SetSettle(ms);
    unsavedChanges = true;
    REQUEST_DISPLAY_REFRESH();
}

int gateModeCount() { return (int)GateModeLength; }
std::string gateModeName(int index) {
    if (index < 0 || index >= (int)GateModeLength)
        return "";
    return GateModeNames[index];
}
int channelGateMode(Engine *e, int ch) {
    EngineScope scope(e);
    return channels[clampChannel(ch)].envelope.GetMode();
}
void setChannelGateMode(Engine *e, int ch, int mode) {
    EngineScope scope(e);
    channels[clampChannel(ch)].envelope.SetMode(mode);
    unsavedChanges = true;
    REQUEST_DISPLAY_REFRESH();
}

int syncModeCount() { return (int)SyncModeLength; }
std::string syncModeName(int index) {
    if (index < 0 || index >= (int)SyncModeLength)
        return "";
    return SyncModeNames[index];
}
int channelSyncMode(Engine *e, int ch) {
    EngineScope scope(e);
    return channels[clampChannel(ch)].GetSyncMode();
}
void setChannelSyncMode(Engine *e, int ch, int mode) {
    EngineScope scope(e);
    channels[clampChannel(ch)].SetSyncMode(mode);
    unsavedChanges = true;
    REQUEST_DISPLAY_REFRESH();
}

int attackMax() { return ENVELOPE_MAX_ATTACK; }
int channelAttack(Engine *e, int ch) {
    EngineScope scope(e);
    return channels[clampChannel(ch)].envelope.GetAttack();
}
void setChannelAttack(Engine *e, int ch, int ms) {
    EngineScope scope(e);
    channels[clampChannel(ch)].envelope.SetAttack(ms);
    unsavedChanges = true;
    REQUEST_DISPLAY_REFRESH();
}

int decayMax() { return ENVELOPE_MAX_DECAY; }
int channelDecay(Engine *e, int ch) {
    EngineScope scope(e);
    return channels[clampChannel(ch)].envelope.GetDecay();
}
void setChannelDecay(Engine *e, int ch, int ms) {
    EngineScope scope(e);
    channels[clampChannel(ch)].envelope.SetDecay(ms);
    unsavedChanges = true;
    REQUEST_DISPLAY_REFRESH();
}

std::string currentNote(Engine *e, int ch) {
    EngineScope scope(e);
    int c = clampChannel(ch);
    return std::string(noteNames[channels[c].GetNoteIndex()]) +
           std::to_string(channels[c].GetOctaveOut());
}

// ── forgevcv::IEngine adapter ─────────────────────────────────────────────────
// Thin forwards to the free-function bridge above. The firmware is fixed at
// 2 CV inputs / 4 outputs, so the nCv/nOut counts are informational here.
VcvEngine::VcvEngine() : e_(createEngine()) {}
VcvEngine::~VcvEngine() { destroyEngine(e_); }

void VcvEngine::process(float dt, const float *cv, int /*nCv*/,
                        bool clockHigh, float *out, int /*nOut*/) {
    nfengine::process(e_, dt, cv, clockHigh, out);
}
void VcvEngine::encoderTurn(int detents) { nfengine::encoderTurn(e_, detents); }
void VcvEngine::encoderButton(bool pressed) { nfengine::encoderButton(e_, pressed); }
void VcvEngine::getFramebuffer(uint8_t out[1024]) { nfengine::getFramebuffer(e_, out); }
std::string VcvEngine::serialize() { return nfengine::serialize(e_); }
void VcvEngine::deserialize(const std::string &blob) { nfengine::deserialize(e_, blob); }
void VcvEngine::reset() { nfengine::reset(e_); }
void VcvEngine::randomize() { nfengine::randomize(e_); }

} // namespace nfengine
