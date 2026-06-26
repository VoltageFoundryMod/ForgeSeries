// fw_engine.cpp — ClockForge firmware compiled inside VCV Rack via the shim layer.
//
// This is a VCV-adapted port of src/main.cpp: it pulls in the unchanged firmware
// lib/ (the valuable DSP + menu + render code) through the Arduino shim, defines
// the same set of globals, and replaces the hardware/dual-core integration glue
// with host-driven entry points. It exposes ONLY a POD/opaque API (fw_engine.hpp)
// so it never shares Arduino/rack types with the rest of the plugin.

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
#include "pinouts.hpp"
#include "presetManager.hpp"
#include "splash.hpp"
#include "storage.hpp"
#include "utils.hpp"

// ── Global objects (mirror main.cpp, minus hardware/dual-core bits) ───────────
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DisplayManager displayMgr(display);

PerformanceMetrics metrics;

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

void SetMasterState(bool state) {
    if (!masterState && state) {
        tickCounter = 0;
        externalTickCounter = 0;
    }
    masterState = state;
    for (int i = 0; i < NUM_OUTPUTS; i++)
        outputs[i].SetMasterState(state);
}
void ToggleMasterState() { SetMasterState(!masterState); }

void HandleOutputs() {
    float raw[NUM_OUTPUTS];
    float norm[NUM_OUTPUTS];
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        raw[i] = outputs[i].ComputeRawOutput();
        norm[i] = raw[i] / (float)MAXDAC;
    }
    uint16_t v[NUM_OUTPUTS];
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        float r = raw[i];
        if (outputs[i].HasCrossOp()) {
            int src = outputs[i].GetCrossSourceIndex();
            float srcNorm;
            if (src < NUM_OUTPUTS)
                srcNorm = norm[src];
            else
                srcNorm = channelADC[src - NUM_OUTPUTS] / 4095.0f;
            r = outputs[i].ApplyCrossOp(raw[i], srcNorm);
        }
        v[i] = (uint16_t)outputs[i].FinalizeOutput(r);
    }
    DACWriteAll(v[0], v[1], v[2], v[3]);
    for (int i = 0; i < NUM_OUTPUTS; i++)
        outputs[i].GenEnvelope();
}

// Non-blocking: stash the message + expiry; the renderer draws it as an overlay.
void ShowTemporaryMessage(const char *msg, uint32_t durationMs) {
    strncpy(_tempMsg, msg, sizeof(_tempMsg) - 1);
    _tempMsg[sizeof(_tempMsg) - 1] = 0;
    _tempMsgUntil = millis() + durationMs;
    REQUEST_DISPLAY_REFRESH();
}

// Hardware calibration wizard is meaningless in Rack — outputs/inputs are ideal.
void RunCalibration() {
    ShowTemporaryMessage("N/A", 1200);
}

// ── Touch points used by the engine entry layer (below) ───────────────────────
#include "fw_engine.hpp"

namespace cfengine {

struct Engine {
    HostBridge host;
    double tickAccum = 0.0;
    bool lastClock = false;
    bool lastButton = false; // true = currently pressed
};

// NOTE: v1 backs the firmware's file-scope globals with a single shared set.
// One instance is fully correct; the per-instance EngineState context-swap is
// the next task. g_host (framebuffer/DAC/ADC) is already per-instance.
static bool _inited = false;

static uint16_t voltsToAdc(float v) {
    int a = (int)(v / 5.0f * 4095.0f + 0.5f);
    return (uint16_t)constrain(a, 0, 4095);
}

static void engineInitOnce() {
    if (_inited) return;
    _inited = true;
    EEPROMInit();
    InitIO();
    // Wire the external-clock interrupt exactly as main.cpp setup() does. Without
    // this the CLK IN rising-edge ISR (ClockReceived) is never registered, so the
    // engine's edge detector in process() has no callback to fire and external
    // clock sync never engages.
    attachInterrupt(digitalPinToInterrupt(CLK_IN_PIN), ClockReceived, RISING);
    display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
    InitDAC();
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
    g_host = &e->host;
    engineInitOnce();
    return e;
}

void destroyEngine(Engine *e) {
    if (g_host == &e->host) g_host = nullptr;
    delete e;
}

void process(Engine *e, float dt, const float cvVolts[2], bool clockGateHigh, float outVolts[4]) {
    g_host = &e->host;

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
    g_host = &e->host;
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
    g_host = &e->host;
    if (pressed && !e->lastButton) doEncoderClick(); // press edge
    e->lastButton = pressed;
    e->host.gpio[ENCODER_SW] = pressed ? 0 : 1;
}

void getFramebuffer(Engine *e, uint8_t out[1024]) {
    g_host = &e->host;
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
    g_host = &e->host;
    return std::string((const char *)EEPROM.data.data(), EEPROM.data.size());
}

void deserialize(Engine *e, const std::string &blob) {
    g_host = &e->host;
    EEPROM.data.assign(blob.begin(), blob.end());
    // Reload settings from slot 0 into the live state.
    cal = LoadCalibration();
    LoadSaveParams p = Load(0);
    UpdateParameters(p);
    UpdateBPM(BPM);
    REQUEST_DISPLAY_REFRESH();
}

int bpm(Engine *) { return (int)BPM; }

} // namespace cfengine
