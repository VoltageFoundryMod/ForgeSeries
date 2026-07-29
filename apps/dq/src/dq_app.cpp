// dq_app.cpp — NoteForge (dual quantizer) hosted in the unified firmware.
//
// See scp_app.cpp for the pattern and the include-order rule. NoteForge is the
// first app with a menu system, so it also exercises the part ForgeView did not:
// core/menuDisplay.hpp is included INSIDE the namespace, so its RedrawDisplay()
// and MenuHeader() hooks bind to this app's implementations, while `display`,
// `displayMgr` and `encoder` stay global via core/shellObjects.hpp.

// ── 1. Global scope: standard library, third-party, core ────────────────────
// Every header reached from inside the namespace must already be included here.
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
#include "shellObjects.hpp" // display / displayMgr / encoder / cal — the globals
#include "splash.hpp"
#include "utils.hpp"

#include "dq_app.hpp"

#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

namespace forge {
namespace dq {

// Core 0 prepares a frame; Core 1 flushes it. Per-app, because each app runs its
// own render policy — the shell only decides which app owns the cores.
static volatile bool _displayFrameReady = false;
static volatile bool _displayLocked = false;

// Marks the display dirty and restarts the screen-timeout clock. Must be defined
// before menuHandlers.hpp, which uses it.
#define REQUEST_DISPLAY_REFRESH()     \
    do {                              \
        displayRefresh = 1;           \
        displayMgr.MarkInteraction(); \
    } while (0)

// ── 2. NoteForge's own headers ──────────────────────────────────────────────
#include "../lib/channel.hpp"
#include "../lib/cvInputs.hpp"
#include "../lib/storage.hpp" // pulls in presetManager.hpp

#include "../lib/menuDefinitions.hpp"
#include "../lib/menuHandlers.hpp"
#include "menuDisplay.hpp" // core header, namespaced for its app hooks
#include "../lib/menuRender.hpp"

// engine.hpp first: it defines HandleOutputs(), which appDisplay.hpp calls
// from ShowTemporaryMessage(). It externs the globals it needs, so it does
// not care that they are defined further down. Ordering them this way
// removes the forward declaration that used to bridge the gap - one that
// silently went missing and only showed up as an error inside appDisplay.
#include "../lib/engine.hpp" // the module engine step
#include "appDisplay.hpp"    // core: RedrawDisplay + SAVED/LOADED

#include "calibration.hpp" // core: the shared wizard
#include "../lib/version.hpp"
// clang-format on

// ── App state (was main.cpp's file-scope globals) ───────────────────────────
QuantizerChannel channels[NUM_CHANNELS];

int menuItem = 1; // 1-based; item 1 is the keyboard home screen
bool switchState = 1;
bool oldSwitchState = 1;
int menuMode = 0; // 0 = navigating, else the item being edited
bool displayRefresh = 1;
bool unsavedChanges = false;
int menuScreenTimeout = 2; // index into screenTimeoutOptions[]
unsigned long lastEncoderUpdate = 0;









// ── core/encoderMenu.hpp hooks ───────────────────────────────────────────────
// NoteForge edits straight through the item's setter and has nothing to drop
// when the cursor moves.
static inline void MenuApplyEdit(int item, int delta) {
    if (MENU_ITEMS[item - 1].setter)
        MENU_ITEMS[item - 1].setter(delta);
}
static inline void OnItemActivated(const MenuItem &) {}
static inline void OnEnterEdit(int) {}
static inline void OnExitEdit(int) {}
static inline void OnMenuNavigate() {}

#include "encoderMenu.hpp"

// ── 3. The shell contract ───────────────────────────────────────────────────
class NoteForgeApp final : public IApp {
  public:
    const char *Name() const override { return "NoteForge"; }
    const char *Version() const override { return VERSION; }

    void Begin() override {
        // The shell has done InitWire/InitIO/display.begin/InitDAC already.
        EEPROMInit();
        cal = LoadCalibration();
        UpdateParameters(Load(0));

        attachInterrupt(digitalPinToInterrupt(CLK_IN_PIN), TriggerReceived, RISING);
        REQUEST_DISPLAY_REFRESH();
    }

    void Tick0() override {
        HandleCVInputs();
        HandleOutputs();
        // HandleOutputs() can set displayRefresh on a note change but cannot
        // reach displayMgr; propagate here so HandleDisplay() actually fires
        // (it needs both flags).
        if (displayRefresh)
            displayMgr.MarkDirty();
    }

    void Tick1(Adafruit_SSD1306 &disp) override {
        // Skip rendering while Core 0 holds the buffer (temporary message).
        if (!_displayLocked)
            HandleDisplay();
        if (_displayFrameReady) {
            _displayFrameReady = false;
            disp.display(); // Wire (I2C1) — Core 1 only
        }
    }

    void EncoderButton(bool pressed) override { MenuEncoderButton(pressed); }

    // Direction is deliberately identical to the standalone firmware — only the
    // control flow changed (the shell polls and calls us, rather than us polling
    // the pins). The chain, unchanged end to end:
    //
    //   (new-3)/4 > old/4  ->  shell sends -1  ->  menuItem-1, setter(-speed)
    //   (new+3)/4 < old/4  ->  shell sends +1  ->  menuItem+1, setter(+speed)
    //
    // If a turn ever feels backwards, the quarter-step test in the shell's
    // PollEncoder() is the single place to flip it, for every app at once.
    // Navigate/edit lives in core/encoderMenu.hpp; MenuApplyEdit() and
    // OnMenuNavigate() above are this module's half of it. Direction is
    // unchanged from the standalone firmware — the shell sends -1/+1.
    void EncoderTurn(int detents) override { MenuEncoderTurn(detents); }
};

inline NoteForgeApp g_app;

} // namespace dq

IApp *DqApp() { return &dq::g_app; }

} // namespace forge
