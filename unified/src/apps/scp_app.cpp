// scp_app.cpp — ForgeView (oscilloscope) hosted in the unified firmware.
//
// The whole module is wrapped in namespace forge::scp. That is what lets several
// firmwares share one binary: every ForgeSeries app defines globals with the
// same names (menuMode, param, switchState, display helpers, ...) and, once
// linked together, they would otherwise be duplicate symbols.
//
// The pattern, which every app TU follows:
//
//   1. Include third-party and core headers at GLOBAL scope first. Their include
//      guards then make the copies pulled in from inside the namespace no-ops,
//      so <Arduino.h> and friends are never namespaced. Getting this order wrong
//      produces spectacular, unreadable errors.
//   2. Include the app's own headers inside the namespace.
//   3. Implement forge::IApp over them.
//
// apps/scp/ is untouched and still builds standalone.

// ── 1. Global scope: standard library, third-party, core ────────────────────
// EVERY header the app's own headers reach for must be listed here, including
// standard ones. Miss one and it is included from inside the namespace, which
// puts std:: inside forge::scp and produces hundreds of errors deep in libstdc++
// ("'allocator' does not name a type" in stringfwd.h is the classic symptom) —
// never anything pointing at the file that actually caused it.
//
// scope.hpp -> <math.h>, settings.hpp -> <cstring>.
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
// Global scope: settings.hpp reaches this from inside the namespace, and it
// pulls in <LittleFS.h> and the standard library.
#include "fsStore.hpp"

#include "scp_app.hpp"

// ForgeView's screen geometry, expected by its own headers.
#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// ── 2. The module itself ────────────────────────────────────────────────────
namespace forge {
namespace scp {

#include "../../../apps/scp/lib/definitions.hpp"
#include "../../../apps/scp/lib/scope.hpp"    // #includes fixfft.cpp itself — no separate TU
#include "../../../apps/scp/lib/settings.hpp" // after scope.hpp: persists its globals
#include "../../../apps/scp/src/version.hpp"
// clang-format on

// ── 3. The shell contract ───────────────────────────────────────────────────
class ScopeApp final : public IApp {
  public:
    const char *Name() const override { return "ForgeView"; }
    const char *Version() const override { return VERSION; }

    void Begin() override {
        // The shell has already done InitWire/InitIO/display.begin/InitDAC, so
        // only ForgeView's own state is left.
        ScopeInit();
        ScopeSettingsInit(); // restore saved mode/params over the defaults
    }

    void Tick0() override {
        const int ch1 = analogRead(CV_1_IN_PIN);
        const int ch2 = analogRead(CV_2_IN_PIN);
        const bool clk = digitalRead(CLK_IN_PIN);
        ScopeFeedSample(ch1, ch2, clk);

        // Buffered oscilloscope through: CV1 -> Out1/Out3, CV2 -> Out2/Out4.
        DACWriteAll((uint16_t)ch1, (uint16_t)ch2, (uint16_t)ch1, (uint16_t)ch2);

        ScopeSettingsPoll(); // debounced save, 5 s after the last change
    }

    void Tick1(Adafruit_SSD1306 &display) override {
        // The scope always wants a fresh frame, so this never rate-limits.
        ScopeRender(display);
        display.display(); // Wire (I2C1) — Core 1 only
    }

    void EncoderTurn(int detents) override { ScopeEncoderTurn(detents); }
    void EncoderButton(bool pressed) override { ScopeEncoderButton(pressed); }

    void End() override {
        // Force out anything the debounced save is still holding.
        ScopeSettingsPoll();
    }
};

inline ScopeApp g_app;

} // namespace scp

IApp *ScpApp() { return &scp::g_app; }

} // namespace forge
