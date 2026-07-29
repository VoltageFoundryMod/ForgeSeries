// clk_app.cpp — ClockForge (clock generator) hosted in the unified firmware.
//
// See scp_app.cpp for the pattern and the include-order rule.

// ── 1. Global scope: standard library, third-party, core ────────────────────
// clang-format off
// INCLUDE ORDER IS LOAD-BEARING HERE — do not sort.
//
// Two separate reasons, both of which produce errors that point at the wrong
// file:
//   * everything third-party and core must be included at GLOBAL scope before
//     the namespace opens, or the standard library ends up inside forge::<app>;
//   * inside the namespace, an app's headers depend on each other (pinouts
//     before anything using NUM_OUTPUTS, menuDisplay before menuRender, which
//     needs its MD_* primitives).
//
// An editor that sorts includes alphabetically breaks the second one silently:
// the app headers carry a ../../../ prefix, so they all sort ahead of any
// bare-name core header and menuDisplay.hpp lands last.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <math.h>
#include <string>

#include <Arduino.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <LittleFS.h>
#include <Wire.h>

#include "IApp.hpp"
#include "boardIO.hpp"
#include "boardPinouts.hpp"
#include "calibrationData.hpp"
#include "cvInput.hpp"
#include "displayManager.hpp"
// fsStore.hpp here at global scope: storage.hpp includes it from inside the
// namespace, and it reaches <LittleFS.h> and the standard library.
#include "fsStore.hpp"
#include "encoder.hpp"
#include "encoderAccel.hpp" // shared rotation acceleration
#include "envelope.hpp"
#include "scales.hpp"
#include "shellObjects.hpp"
#include "splash.hpp"
#include "utils.hpp"

#include "clk_app.hpp"

#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

namespace forge {
namespace clk {

static volatile bool _displayFrameReady = false;
static volatile bool _displayLocked = false;

#define REQUEST_DISPLAY_REFRESH()     \
    do {                              \
        displayRefresh = 1;           \
        displayMgr.MarkInteraction(); \
    } while (0)

// ── 2. ClockForge's own headers ─────────────────────────────────────────────
#include "../../../apps/clk/lib/euclidean.hpp"
#include "../../../apps/clk/lib/outputs.hpp"
#include "../../../apps/clk/lib/clockEngine.hpp"
#include "../../../apps/clk/lib/cvInputs.hpp"
#include "../../../apps/clk/lib/storage.hpp"
#include "metrics.hpp" // core: performance counters, shared by all modules

#include "../../../apps/clk/lib/menuDefinitions.hpp"
#include "menuDisplay.hpp" // core header, namespaced for its app hooks
#include "../../../apps/clk/lib/menuHandlers.hpp"
#include "../../../apps/clk/lib/menuRender.hpp"

#include "calibration.hpp" // core: the shared wizard
#include "../../../apps/clk/src/version.hpp"
// clang-format on

// ── App state (was main.cpp's file-scope globals) ───────────────────────────
PerformanceMetrics metrics;

Output outputs[NUM_OUTPUTS] = {
    Output(1, OutputType::DACOut), // all outputs go through the MCP4728
    Output(2, OutputType::DACOut),
    Output(3, OutputType::DACOut),
    Output(4, OutputType::DACOut)};

bool masterState = true; // global play/stop

int menuItem = 2; // 1-based; items 1-2 are the transport home screen
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
unsigned long lastEncoderUpdate = 0;


// Global play/stop. Declared by cvInputs.hpp (a CV target can drive it) and
// referenced from MENU_ITEMS, so both live here rather than in a lib header.
void SetMasterState(bool state) {
    // Coming back from stopped restarts the count rather than resuming mid-bar.
    if (!masterState && state) {
        tickCounter = 0;
        externalTickCounter = 0;
    }
    masterState = state;
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        outputs[i].SetMasterState(state);
    }
}

void ToggleMasterState() { SetMasterState(!masterState); }

// Drive all four outputs for one iteration.
void HandleOutputs() {
    // Pass 1: each output's raw (pre-quantisation) value plus a normalised
    // snapshot. Cross operations read the frozen snapshot so results are
    // order-independent — no feedback when two outputs cross-modulate.
    float raw[NUM_OUTPUTS];
    float norm[NUM_OUTPUTS];
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        raw[i] = outputs[i].ComputeRawOutput();
        norm[i] = raw[i] / (float)MAXDAC;
    }

    // Pass 2: apply cross operations against the snapshot, then quantise/clamp.
    uint16_t v[NUM_OUTPUTS];
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        float r = raw[i];
        if (outputs[i].HasCrossOp()) {
            const int src = outputs[i].GetCrossSourceIndex();
            float srcNorm;
            if (src < NUM_OUTPUTS) {
                srcNorm = norm[src]; // another output's pre-cross value
            } else {
                // IN1 / IN2 sampled CV, normalised 0..1 by the core adapter
                srcNorm = CvUni(channelCv[src - NUM_OUTPUTS]);
            }
            r = outputs[i].ApplyCrossOp(raw[i], srcNorm);
        }
        v[i] = (uint16_t)outputs[i].FinalizeOutput(r);
    }

    metrics.BeginDACMeasurement();
    DACWriteAll(v[0], v[1], v[2], v[3]);
    metrics.EndDACMeasurement();

    for (int i = 0; i < NUM_OUTPUTS; i++) {
        outputs[i].GenEnvelope();
    }
}

#include "tempMessage.hpp" // core: shared SAVED/LOADED overlay

void RedrawDisplay() {
    displayMgr.PrepareFrame();
    displayRefresh = 0;
    _displayFrameReady = true;
}

// ── 3. The shell contract ───────────────────────────────────────────────────
class ClockForgeApp final : public IApp {
  public:
    const char *Name() const override { return "ClockForge"; }
    const char *Version() const override { return VERSION; }

    void Begin() override {
        EEPROMInit();
        cal = LoadCalibration();
        UpdateParameters(Load(0));

        // Timer BEFORE the external-clock interrupt: ClockReceived() calls
        // UpdateBPM(), which cancels the repeating timer, so the timer struct
        // must be valid before any interrupt can fire.
        InitializeTimer();
        UpdateBPM(BPM);
        attachInterrupt(digitalPinToInterrupt(CLK_IN_PIN), ClockReceived, RISING);

        REQUEST_DISPLAY_REFRESH();
    }

    void Tick0() override {
        metrics.BeginLoopMeasurement();

        // Apply a deferred BPM change. UpdateBPM() cancels and restarts the
        // hardware repeating timer, leaving a gap of up to one PPQN tick
        // (520 µs at 120 BPM, 6.25 ms at 10 BPM) where no ISR fires and every
        // output freezes. Rate-limited so a fast encoder spin cannot stack up
        // consecutive gaps, which is audible.
        if (bpmNeedsUpdate) {
            static unsigned long lastBPMApply = 0;
            const unsigned long now = millis();
            if (now - lastBPMApply >= 30) {
                lastBPMApply = now;
                bpmNeedsUpdate = false;
                UpdateBPM(BPM);
            }
        }

        HandleOutputs();
        HandleCVInputs();
        HandleExternalClock();
        if (displayRefresh)
            displayMgr.MarkDirty();

        metrics.EndLoopMeasurement();
    }

    void Tick1(Adafruit_SSD1306 &disp) override {
        if (!_displayLocked)
            HandleDisplay();
        if (_displayFrameReady) {
            _displayFrameReady = false;
            metrics.BeginCore1FlushMeasurement();
            disp.display();
            metrics.EndCore1FlushMeasurement();
        }
    }

    void EncoderButton(bool pressed) override {
        oldSwitchState = switchState;
        switchState = pressed ? 0 : 1; // active-low, matching the raw pin
        if (switchState != 1 || oldSwitchState != 0)
            return; // act on release only

        lastEncoderUpdate = millis();
        REQUEST_DISPLAY_REFRESH();

        if (menuMode != 0) {
            // Commit and leave edit mode. CV target items copy the pending
            // selection back to the live value.
            if (menuMode == 61) {
                CVInputTarget[0] = pendingCVInputTarget[0];
            } else if (menuMode == 62) {
                CVInputTarget[1] = pendingCVInputTarget[1];
            }
            menuMode = 0;
            return;
        }

        if (menuItem >= 1 && menuItem <= MENU_ITEM_COUNT) {
            const MenuItem &mi = MENU_ITEMS[menuItem - 1];
            if (mi.type == MENU_ACTION || mi.type == MENU_TOGGLE) {
                if (mi.action)
                    mi.action();
            } else { // MENU_EDIT
                menuMode = menuItem;
                // Seed the pending state for CV target editing.
                if (menuItem == 61) {
                    pendingCVInputTarget[0] = CVInputTarget[0];
                } else if (menuItem == 62) {
                    pendingCVInputTarget[1] = CVInputTarget[1];
                }
            }
        }
    }

    // Direction matches the standalone firmware exactly; only the control flow
    // changed. See the note in dq_app.cpp.
    void EncoderTurn(int detents) override {
        if (detents == 0)
            return;
        const int dir = (detents > 0) ? 1 : -1;
        UpdateSpeedFactor(dir);
        REQUEST_DISPLAY_REFRESH();
        lastEncoderUpdate = millis();

        if (menuMode == 0) {
            menuItem += dir;
            if (menuItem < 1)
                menuItem = MENU_ITEM_COUNT;
            else if (menuItem > MENU_ITEM_COUNT)
                menuItem = 1;
        } else if (menuMode >= 1 && menuMode <= MENU_ITEM_COUNT) {
            if (MENU_ITEMS[menuMode - 1].setter)
                MENU_ITEMS[menuMode - 1].setter(dir * (int)speedFactor);
        }
    }
};

inline ClockForgeApp g_app;

} // namespace clk

IApp *ClkApp() { return &clk::g_app; }

} // namespace forge
