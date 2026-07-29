// gen_app.cpp — GravityForge (physics sequencer) hosted in the unified firmware.
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

#include "gen_app.hpp"

#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

namespace forge {
namespace gen {

static volatile bool _displayFrameReady = false;
static volatile bool _displayLocked = false;

#define REQUEST_DISPLAY_REFRESH()     \
    do {                              \
        displayRefresh = 1;           \
        displayMgr.MarkInteraction(); \
    } while (0)

// ── 2. GravityForge's own headers ───────────────────────────────────────────
#include "../lib/params.hpp"
#include "../lib/physics.hpp"
#include "../lib/clock.hpp"
#include "../lib/sequencer.hpp"
#include "../lib/cvInputs.hpp"
#include "../lib/randomize.hpp"
#include "../lib/storage.hpp"

#include "../lib/menuDefinitions.hpp"
#include "../lib/menuHandlers.hpp"
#include "menuDisplay.hpp"
#include "../lib/menuRender.hpp"

#include "calibration.hpp" // core: the shared wizard
#include "../lib/version.hpp"
// clang-format on

// ── The instrument ──────────────────────────────────────────────────────────
// physicsWorld is the simulation; channels[] turn its peg hits into notes;
// containerParams/worldParams are what the *user* set, kept separate so CV
// modulation can sit on top without overwriting it.
PhysicsWorld physicsWorld;
GravityChannel channels[NUM_CHANNELS];
ContainerParams containerParams[2];
WorldParams worldParams;
Clock clockEngine;
ModBus modBus;

int menuItem = 1; // 1-based; item 1 is the physics home screen
bool switchState = 1;
bool oldSwitchState = 1;
int menuMode = 0;
bool displayRefresh = 1;
bool unsavedChanges = false;
int menuScreenTimeout = 2;
unsigned long lastEncoderUpdate = 0;



void HandleOutputs(); // defined inline in ../lib/engine.hpp, included below
#include "appDisplay.hpp" // core: RedrawDisplay + SAVED/LOADED overlay




#include "../lib/engine.hpp"


// ── core/encoderMenu.hpp hooks ───────────────────────────────────────────────
// GravityForge already has MenuApplyEdit in menuHandlers.hpp. Navigating away
// drops a toggle-armed physics preview.
static inline void OnMenuNavigate() { LiveViewClear(); }

#include "encoderMenu.hpp"

// ── 3. The shell contract ───────────────────────────────────────────────────
class GravityForgeApp final : public IApp {
  public:
    const char *Name() const override { return "GravityForge"; }
    const char *Version() const override { return VERSION; }

    void Begin() override {
        EEPROMInit();
        cal = LoadCalibration();
        UpdateParameters(Load(0));

        attachInterrupt(digitalPinToInterrupt(CLK_IN_PIN), TriggerReceived, RISING);
        REQUEST_DISPLAY_REFRESH();
    }

    void Tick0() override {
        HandleCVInputs();
        HandleOutputs();
        if (displayRefresh)
            displayMgr.MarkDirty();
    }

    void Tick1(Adafruit_SSD1306 &disp) override {
        if (!_displayLocked)
            HandleDisplay();
        if (_displayFrameReady) {
            _displayFrameReady = false;
            disp.display();
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
            menuMode = 0;    // commit and leave edit mode
            LiveViewClear(); // …and land back on the page, not the animation
            return;
        }
        if (menuItem >= 1 && menuItem <= MENU_ITEM_COUNT) {
            const MenuItem &mi = MENU_ITEMS[menuItem - 1];
            if (mi.type == MENU_ACTION || mi.type == MENU_TOGGLE) {
                if (mi.action)
                    mi.action();
                // A flagged toggle (DIR) has no edit mode to turn in, so the
                // click itself has to be what shows the result.
                if (mi.livePreview)
                    LiveViewArm();
                else
                    LiveViewClear();
            } else { // MENU_EDIT
                menuMode = menuItem;
                LiveViewClear(); // the first detent is what opens the physics view
            }
        }
    }

    // Navigate/edit lives in core/encoderMenu.hpp; MenuApplyEdit() and
    // OnMenuNavigate() above are this module's half of it. Direction is
    // unchanged from the standalone firmware — the shell sends -1/+1.
    void EncoderTurn(int detents) override { MenuEncoderTurn(detents); }
};

inline GravityForgeApp g_app;

} // namespace gen

IApp *GenApp() { return &gen::g_app; }

} // namespace forge
