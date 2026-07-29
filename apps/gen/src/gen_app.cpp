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

// engine.hpp first: it defines HandleOutputs(), which appDisplay.hpp calls
// from ShowTemporaryMessage(). It externs the globals it needs, so it does
// not care that they are defined further down. Ordering them this way
// removes the forward declaration that used to bridge the gap - one that
// silently went missing and only showed up as an error inside appDisplay.
#include "../lib/engine.hpp" // the module engine step
#include "appDisplay.hpp"    // core: RedrawDisplay + SAVED/LOADED

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









// ── core/encoderMenu.hpp hooks ───────────────────────────────────────────────
// GravityForge already has MenuApplyEdit in menuHandlers.hpp. Navigating away
// drops a toggle-armed physics preview.
// A flagged toggle (DIR) has no edit mode to turn in, so the click itself has
// to be what shows the result. Everything else lands back on the page rather
// than the animation.
static inline void OnItemActivated(const MenuItem &mi) {
    if (mi.livePreview)
        LiveViewArm();
    else
        LiveViewClear();
}
static inline void OnEnterEdit(int) { LiveViewClear(); } // first detent opens it
static inline void OnExitEdit(int) { LiveViewClear(); }
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

    void EncoderButton(bool pressed) override { MenuEncoderButton(pressed); }

    // Navigate/edit lives in core/encoderMenu.hpp; MenuApplyEdit() and
    // OnMenuNavigate() above are this module's half of it. Direction is
    // unchanged from the standalone firmware — the shell sends -1/+1.
    void EncoderTurn(int detents) override { MenuEncoderTurn(detents); }
};

inline GravityForgeApp g_app;

} // namespace gen

IApp *GenApp() { return &gen::g_app; }

} // namespace forge
