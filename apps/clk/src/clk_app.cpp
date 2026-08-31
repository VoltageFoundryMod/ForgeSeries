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
#include "../lib/expander.hpp"
#include "../lib/euclidean.hpp"
#include "../lib/outputs.hpp"
#include "../lib/clockEngine.hpp"
#include "../lib/cvInputs.hpp"
#include "../lib/storage.hpp"
#include "metrics.hpp" // core: performance counters, shared by all modules

#include "../lib/menuDefinitions.hpp"
#include "menuDisplay.hpp" // core header, namespaced for its app hooks
#include "../lib/menuHandlers.hpp"
#include "../lib/menuRender.hpp"

// engine.hpp first: it defines HandleOutputs(), which appDisplay.hpp calls
// from ShowTemporaryMessage(). It externs the globals it needs, so it does
// not care that they are defined further down. Ordering them this way
// removes the forward declaration that used to bridge the gap - one that
// silently went missing and only showed up as an error inside appDisplay.
#include "../lib/engine.hpp" // the module engine step
#include "appDisplay.hpp"    // core: RedrawDisplay + SAVED/LOADED


#include "../lib/version.hpp"
// clang-format on

Output outputs[NUM_MAX_OUTPUTS] = {
    Output(1, OutputType::DACOut), // all outputs go through an MCP4728
    Output(2, OutputType::DACOut),
    Output(3, OutputType::DACOut),
    Output(4, OutputType::DACOut),
    // 5-8 exist whether or not an expander is fitted; ActiveOutputs()
    // decides how many are driven. See lib/expander.hpp.
    Output(5, OutputType::DACOut),
    Output(6, OutputType::DACOut),
    Output(7, OutputType::DACOut),
    Output(8, OutputType::DACOut)};

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
int expanderType = 0;
unsigned long lastEncoderUpdate = 0;

// ── core/encoderMenu.hpp hooks ───────────────────────────────────────────────
// ClockForge edits straight through the item's setter and has nothing to drop
// when the cursor moves.
static inline void MenuApplyEdit(int item, int delta) {
    if (MENU_ITEMS[item - 1].setter)
        MENU_ITEMS[item - 1].setter(delta);
}
static inline void OnItemActivated(const MenuItem &) {}
// The CV target rows edit through a pending copy, so it is seeded on the way in
// and committed on the way out. Which items those are comes from
// CVTargetItemChannel() in menuHandlers.hpp — see the note there.
static inline void OnEnterEdit(int item) {
    const int ch = CVTargetItemChannel(item);
    if (ch >= 0)
        pendingCVInputTarget[ch] = CVInputTarget[ch];
}
static inline void OnExitEdit(int item) {
    const int ch = CVTargetItemChannel(item);
    if (ch >= 0)
        CVInputTarget[ch] = pendingCVInputTarget[ch];
}
static inline void OnMenuNavigate() {}

#include "encoderMenu.hpp"

// ── 3. The shell contract ───────────────────────────────────────────────────
class ClockForgeApp final : public IApp {
  public:
    const char *Name() const override { return "ClockForge"; }
    const char *Version() const override { return VERSION; }

    void Begin() override {
        EEPROMInit();
        cal = LoadCalibration();
        UpdateParameters(Load(0));

        // UpdateParameters() has restored expanderType, so this is the first
        // point at which we know whether to look for the second DAC.
        if (ExpanderFitted())
            InitExpDAC();

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

    void EncoderButton(bool pressed) override { MenuEncoderButton(pressed); }

    // Navigate/edit lives in core/encoderMenu.hpp; MenuApplyEdit() and
    // OnMenuNavigate() above are this module's half of it. Direction is
    // unchanged from the standalone firmware — the shell sends -1/+1.
    void EncoderTurn(int detents) override { MenuEncoderTurn(detents); }
};

inline ClockForgeApp g_app;

} // namespace clk

IApp *ClkApp() { return &clk::g_app; }

} // namespace forge
