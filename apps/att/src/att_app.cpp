// att_app.cpp — ChaosForge (chaotic attractors) hosted in the unified firmware.
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
//   * inside the namespace, an app's headers depend on each other (attractors
//     before generator, cvInputs before storage's presetManager, menuDisplay
//     before menuRender, which needs its MD_* primitives).
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
#include "shellObjects.hpp"
#include "splash.hpp"
#include "utils.hpp"

#include "att_app.hpp"

#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

namespace forge {
namespace att {

static volatile bool _displayFrameReady = false;
static volatile bool _displayLocked = false;

#define REQUEST_DISPLAY_REFRESH()     \
    do {                              \
        displayRefresh = 1;           \
        displayMgr.MarkInteraction(); \
    } while (0)

// ── 2. ChaosForge's own headers ─────────────────────────────────────────────
#include "../lib/attractors.hpp"
#include "../lib/generator.hpp"
#include "../lib/params.hpp"
#include "../lib/cvInputs.hpp"
#include "../lib/randomize.hpp"
#include "../lib/storage.hpp"

#include "../lib/menuDefinitions.hpp"
#include "../lib/menuHandlers.hpp"
#include "menuDisplay.hpp"
#include "../lib/menuRender.hpp"

// engine.hpp first: it defines HandleOutputs(), which appDisplay.hpp calls from
// ShowTemporaryMessage(). It externs the globals it needs, so it does not care
// that they are defined further down.
#include "../lib/engine.hpp" // the module engine step
#include "appDisplay.hpp"    // core: RedrawDisplay + SAVED/LOADED

#include "../lib/version.hpp"
// clang-format on

// ── The instrument ──────────────────────────────────────────────────────────
// `world` is the simulation — two orbits and the coupling between them.
// genParams/worldParams are what the *user* set, kept separate so CV modulation
// can sit on top without overwriting it.
ChaosWorld world;
GenParams genParams[2];
WorldParams worldParams;
ModBus modBus;

int menuItem = 1; // 1-based; item 1 is the plot home screen
bool switchState = 1;
bool oldSwitchState = 1;
int menuMode = 0;
bool displayRefresh = 1;
bool unsavedChanges = false;
int menuScreenTimeout = 2;
uint8_t homeView = 0; // 0 = both plots, 1 = A only, 2 = B only
unsigned long lastEncoderUpdate = 0;

// ── core/encoderMenu.hpp hooks ───────────────────────────────────────────────
// ChaosForge already has MenuApplyEdit in menuHandlers.hpp. Navigating away
// drops a toggle-armed plot preview.
//
// A flagged toggle (RANGE) has no edit mode to turn in, so the click itself has
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
class ChaosForgeApp final : public IApp {
  public:
    const char *Name() const override { return "ChaosForge"; }
    const char *Version() const override { return VERSION; }

    void Begin() override {
        EEPROMInit();
        cal = LoadCalibration();
        UpdateParameters(Load(0));

        // The home screen is an animation, so it wants a faster redraw than the
        // menu-shaped default — see ATT_DISPLAY_INTERVAL_MS.
        displayMgr.SetUpdateInterval(ATT_DISPLAY_INTERVAL_MS);

        // Push the loaded patch into the simulation before the first step, so
        // the systems and their parameters are right on the very first frame
        // rather than one loop late.
        ApplyParams(world, genParams, worldParams, modBus);
        world.Reseed();

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
    // OnMenuNavigate() above are this module's half of it.
    void EncoderTurn(int detents) override { MenuEncoderTurn(detents); }
};

inline ChaosForgeApp g_app;

} // namespace att

IApp *AttApp() { return &att::g_app; }

} // namespace forge
