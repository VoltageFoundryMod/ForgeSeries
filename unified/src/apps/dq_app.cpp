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
#include "../../../apps/dq/lib/channel.hpp"
#include "../../../apps/dq/lib/cvInputs.hpp"
#include "../../../apps/dq/lib/storage.hpp" // pulls in presetManager.hpp

#include "../../../apps/dq/lib/menuDefinitions.hpp"
#include "../../../apps/dq/lib/menuHandlers.hpp"
#include "menuDisplay.hpp" // core header, namespaced for its app hooks
#include "../../../apps/dq/lib/menuRender.hpp"

#include "calibration.hpp" // core: the shared wizard
#include "../../../apps/dq/src/version.hpp"
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


void HandleOutputs(); // defined inline in lib/engine.hpp, included below


// Brief full-screen message ("SAVED", "LOADED"). Core 0 never touches Wire, so
// it prepares the buffer and lets Core 1 flush. Keeps the quantizers running
// throughout so held notes do not drop out.
void ShowTemporaryMessage(const char *msg, uint32_t durationMs) {
    _displayLocked = true;
    delay(10); // let Core 1 finish any in-flight HandleDisplay()

    display.clearDisplay();
    display.setTextSize(2);
    const int x = (SCREEN_WIDTH - (int)strlen(msg) * 12) / 2;
    display.setCursor(x < 0 ? 0 : x, SCREEN_HEIGHT / 2 - 8);
    display.print(msg);
    _displayFrameReady = true;

    const uint32_t start = millis();
    while (millis() - start < durationMs) {
        HandleCVInputs();
        HandleOutputs();
    }

    _displayLocked = false;
    REQUEST_DISPLAY_REFRESH();
}

// Core 0 prepares the buffer (no I2C) and signals Core 1 to flush it.
void RedrawDisplay() {
    displayMgr.PrepareFrame();
    displayRefresh = 0;
    _displayFrameReady = true;
}


#include "../../../apps/dq/lib/engine.hpp" // HandleOutputs()

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

    void EncoderButton(bool pressed) override {
        oldSwitchState = switchState;
        switchState = pressed ? 0 : 1; // active-low, matching the raw pin
        if (switchState != 1 || oldSwitchState != 0)
            return; // act on release only

        lastEncoderUpdate = millis();
        REQUEST_DISPLAY_REFRESH();
        if (menuMode != 0) {
            menuMode = 0; // commit and leave edit mode
            return;
        }
        if (menuItem >= 1 && menuItem <= MENU_ITEM_COUNT) {
            const MenuItem &mi = MENU_ITEMS[menuItem - 1];
            if (mi.type == MENU_ACTION || mi.type == MENU_TOGGLE) {
                if (mi.action)
                    mi.action();
            } else { // MENU_EDIT
                menuMode = menuItem;
            }
        }
    }

    // Direction is deliberately identical to the standalone firmware — only the
    // control flow changed (the shell polls and calls us, rather than us polling
    // the pins). The chain, unchanged end to end:
    //
    //   (new-3)/4 > old/4  ->  shell sends -1  ->  menuItem-1, setter(-speed)
    //   (new+3)/4 < old/4  ->  shell sends +1  ->  menuItem+1, setter(+speed)
    //
    // If a turn ever feels backwards, the quarter-step test in the shell's
    // PollEncoder() is the single place to flip it, for every app at once.
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

inline NoteForgeApp g_app;

} // namespace dq

IApp *DqApp() { return &dq::g_app; }

} // namespace forge
