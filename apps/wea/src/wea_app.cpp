// wea_app.cpp — WeaveForge (dual shift-register sequencer) hosted in the
// unified firmware.
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
//   * inside the namespace, an app's headers depend on each other (shiftreg
//     before params, params before cvInputs, outputs before presetManager,
//     menuDisplay before menuRender, which needs its MD_* primitives).
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
#include "clockSource.hpp"
#include "cvInput.hpp"
#include "displayManager.hpp"
// fsStore.hpp here at global scope: storage.hpp includes it from inside the
// namespace, and it reaches <LittleFS.h> and the standard library.
#include "fsStore.hpp"
#include "encoder.hpp"
#include "encoderAccel.hpp" // shared rotation acceleration
#include "quantizer.hpp"
#include "scales.hpp"
#include "shellObjects.hpp"
#include "splash.hpp"
#include "utils.hpp"

#include "wea_app.hpp"

#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

namespace forge {
namespace wea {

static volatile bool _displayFrameReady = false;
static volatile bool _displayLocked = false;

#define REQUEST_DISPLAY_REFRESH()     \
    do {                              \
        displayRefresh = 1;           \
        displayMgr.MarkInteraction(); \
    } while (0)

// ── 2. WeaveForge's own headers ─────────────────────────────────────────────
#include "../lib/shiftreg.hpp"
#include "../lib/params.hpp"
#include "../lib/clock.hpp"
#include "../lib/outputs.hpp"
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
// `registers` is the machine — two shift registers and the weave between them.
// regParams/globalParams are what the *user* set, kept separate so CV
// modulation can sit on top without overwriting it (see lib/params.hpp).
WeavePair registers;
OutputBank outputs;
StepClock clockEngine;
Quantizer quantizer;
RegParams regParams[WEA_NUM_REGS];
GlobalParams globalParams;
ModBus modBus;
LiveParams liveParams;
bool resetArmed = false;

uint16_t noteMask = 0x0FFF;
uint8_t scaleIndex = 0;
uint8_t rootIndex = 0;

int menuItem = 1; // 1-based; item 1 is the loom home screen
bool switchState = 1;
bool oldSwitchState = 1;
int menuMode = 0;
bool displayRefresh = 1;
bool unsavedChanges = false;
int menuScreenTimeout = 2;
unsigned long lastEncoderUpdate = 0;

// ── core/encoderMenu.hpp hooks ───────────────────────────────────────────────
// MenuApplyEdit is in menuHandlers.hpp with the setters it dispatches to; it is
// what arms the live loom view. Everything here drops it again: navigating away,
// entering or leaving edit mode all mean you are looking at the page, not the
// picture.
static inline void OnItemActivated(const MenuItem &) { LiveViewClear(); }
static inline void OnEnterEdit(int) { LiveViewClear(); } // first detent opens it
static inline void OnExitEdit(int) { LiveViewClear(); }
static inline void OnMenuNavigate() { LiveViewClear(); }

#include "encoderMenu.hpp"

// ── 3. The shell contract ───────────────────────────────────────────────────
class WeaveForgeApp final : public IApp {
  public:
    const char *Name() const override { return "WeaveForge"; }
    const char *Version() const override { return VERSION; }

    void Begin() override {
        EEPROMInit();
        cal = LoadCalibration();
        UpdateParameters(Load(0));

        // The loom moves on every clock step, so it wants a faster redraw than
        // the menu-shaped default — see WEA_DISPLAY_INTERVAL_MS.
        displayMgr.SetUpdateInterval(WEA_DISPLAY_INTERVAL_MS);

        // Seed the pattern PRNG from the boot-time clock, so two modules coming
        // up together do not drift identically. The registers themselves come
        // from the preset, which is what makes a saved pattern reproducible.
        registers.Seed((uint32_t)micros());

        // Push the loaded patch through before the first step, so the outputs
        // are right on the very first frame rather than one loop late.
        ApplyParams(registers, regParams, globalParams, modBus, liveParams);

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

    // Navigate/edit lives in core/encoderMenu.hpp; MenuApplyEdit() and the four
    // hooks above are this module's half of it.
    void EncoderTurn(int detents) override { MenuEncoderTurn(detents); }
};

inline WeaveForgeApp g_app;

} // namespace wea

IApp *WeaApp() { return &wea::g_app; }

} // namespace forge
