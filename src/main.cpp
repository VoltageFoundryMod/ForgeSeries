// main.cpp — the ForgeSeries shell.
//
// Owns the board; runs exactly one app at a time.
//
//   Core 0 (setup/loop)   hardware bring-up, encoder, the selected app's Tick0
//   Core 1 (setup1/loop1) the selected app's Tick1 (render + flush)
//
// The core split and bus ownership are the same ones every ForgeSeries firmware
// already used: Wire1/ADC on Core 0, Wire/display on Core 1, which is why
// neither needs a mutex.

#include <Arduino.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <Wire.h>

#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#include "IApp.hpp"
#include "boardIO.hpp"
#include "boardPinouts.hpp"
#include "calibrationData.hpp"
#include "encoder.hpp"
#include "fsStore.hpp"
#include "shellObjects.hpp"
#include "splash.hpp"
// calibration.hpp is included further down: it needs `display` and
// SaveCalibration(), both defined below.

#include "clk_app.hpp"
#include "dq_app.hpp"
#include "gen_app.hpp"
#include "scp_app.hpp"

// ── Board-owned singletons ──────────────────────────────────────────────────
// One display, one encoder, one calibration for the whole module, rather than a
// set per firmware. Apps reach the display only through IApp::Tick1's argument
// and the encoder only through IApp's event calls, so they cannot fight over
// either.
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DisplayManager displayMgr(display);
Encoder encoder(ENC_PIN_1, ENC_PIN_2);

// Referenced by core/boardIO.hpp (DAC correction) and core/cvInput.hpp.
// Shared by every app: calibration describes the board, not the module.
CalibrationData cal = {};

// ── The app registry ────────────────────────────────────────────────────────
// Adding an app is one include, one entry, and one -I line in platformio.ini.
static forge::IApp *const kApps[] = {
    forge::ClkApp(),
    forge::DqApp(),
    forge::GenApp(),
    forge::ScpApp(),
};
static constexpr int kAppCount = (int)(sizeof(kApps) / sizeof(kApps[0]));

static forge::IApp *g_app = nullptr;

// ── Calibration ─────────────────────────────────────────────────────────────
// The wizard is board-level: it measures the analog front and back ends, which
// are the same hardware whichever module you go on to boot, so one run serves
// every app and it lives here rather than in any of them.
//
// It draws straight to `display` and blocks, which is only safe because it runs
// from the boot menu — Core 0, before Core 1 is released, with no app started.
void SaveCalibration(const CalibrationData &c) { forge::fs::SaveCalibrationFs(c); }
#include "calibration.hpp"

// Core 1 is started by the Arduino core *before* setup() runs on Core 0, so
// without this gate it would render over Wire while display.begin() is still
// initialising the panel — corrupting the init command stream and wiping the
// splash. Core 1 idles until Core 0 raises it.
static volatile bool g_setupDone = false;

// ── Encoder ─────────────────────────────────────────────────────────────────
// The shell polls the encoder and forwards events, rather than letting apps read
// the pins: it needs the same events to catch the "back to menu" gesture, and
// two readers would race the quarter-step detent state.
static long oldPosition = 0;
static long newPosition = 0;

// Hold the encoder this long while an app is running to return to the selector.
// Long enough not to fire on an ordinary click, short enough to find by
// accident-then-intent.
#define APP_SWITCH_HOLD_MS 2000

static bool g_switchRequested = false;
static unsigned long g_pressStartMs = 0;
static bool g_wasPressed = false;

namespace forge {
void RequestAppMenu() { g_switchRequested = true; }
} // namespace forge

static void PollEncoder(forge::IApp *app) {
    const bool pressed = (digitalRead(ENCODER_SW) == LOW); // active-low, pull-up

    // Hold-to-switch. Every app acts on the button's *release* edge, so a hold
    // that ends in a reboot is never seen as a click: the app only ever receives
    // "pressed", and the release it would act on never arrives.
    if (pressed && !g_wasPressed) {
        g_pressStartMs = millis();
    } else if (pressed && (millis() - g_pressStartMs) >= APP_SWITCH_HOLD_MS) {
        g_switchRequested = true;
    }
    g_wasPressed = pressed;

    app->EncoderButton(pressed);

    // ── Detent detection ────────────────────────────────────────────────────
    // The quadrature decoder counts all four transitions, so one detent is
    // COUNTS_PER_DETENT counts. Consume whole detents out of the accumulated
    // delta rather than snapping oldPosition to wherever the encoder happens to
    // be.
    //
    // The previous form — a +/-3 deadband against (newPosition +/- 3) / 4, then
    // oldPosition = newPosition — was asymmetric. From an aligned position it
    // took one detent to register in the decreasing direction and two in the
    // increasing one, because integer division truncates and the snap threw
    // away the remainder. That is the "reversing needs two clicks" symptom.
    // Truncation toward zero also flipped which direction was penalised once
    // the count went negative.
    //
    // Subtracting a whole detent keeps the remainder, so direction changes cost
    // exactly one detent either way, and the loop catches up if several arrive
    // between polls.
    static constexpr long COUNTS_PER_DETENT = 4;

    newPosition = encoder.read();
    long delta = newPosition - oldPosition;

    while (delta >= COUNTS_PER_DETENT) {
        delta -= COUNTS_PER_DETENT;
        oldPosition += COUNTS_PER_DETENT;
        app->EncoderTurn(-1); // increasing counts = counter-clockwise
    }
    while (delta <= -COUNTS_PER_DETENT) {
        delta += COUNTS_PER_DETENT;
        oldPosition -= COUNTS_PER_DETENT;
        app->EncoderTurn(+1); // decreasing counts = clockwise
    }
}

// ── Boot menu ───────────────────────────────────────────────────────────────
// Hold the encoder at power-on to choose which module the hardware is. The
// choice persists, so the module boots straight into it from then on.
//
// This runs entirely on Core 0, before Core 1 is released, so it can drive the
// display directly — no frame-ready handshake, and no app is running yet.
//
// The list is the apps plus one trailing CALIBRATE row. That row is why the
// selector is reachable from an app's SETTINGS page at all: the wizard is
// board-level and can only run with no app started, so this screen is the only
// place it can live, and without a way back here there would be no way in.
static constexpr int kCalibrateIndex = kAppCount;
static constexpr int kMenuEntries = kAppCount + 1;

static void DrawMenu(int sel, int top) {
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setCursor(2, 2);
    display.print("SELECT MODULE");
    display.drawFastHLine(0, 11, SCREEN_WIDTH, WHITE);

    // Four visible rows at 13px; the list scrolls when there are more entries.
    const int rows = 4;
    for (int r = 0; r < rows && (top + r) < kMenuEntries; r++) {
        const int i = top + r;
        const int y = 15 + r * 12;
        if (i == sel) {
            display.fillRect(0, y - 2, SCREEN_WIDTH, 11, WHITE);
            display.setTextColor(BLACK);
        } else {
            display.setTextColor(WHITE);
        }
        display.setCursor(4, y);
        if (i == kCalibrateIndex) {
            display.print("CALIBRATE"); // no version — it is not a module
        } else {
            display.print(kApps[i]->Name());
            display.setCursor(84, y);
            display.print("v");
            display.print(kApps[i]->Version());
        }
    }
    display.setTextColor(WHITE);
    display.display();
}

// Blocks until the user clicks. Returns the chosen index — an app index, or
// kCalibrateIndex.
static int RunBootMenu(int initial) {
    int sel = (initial >= 0 && initial < kMenuEntries) ? initial : 0;
    int top = 0;

    // The gesture that got us here is the button being held. Wait for release
    // first, or that same press is read as the selection click immediately.
    while (digitalRead(ENCODER_SW) == LOW)
        delay(10);
    delay(50); // contact settle

    long oldPos = encoder.read();
    DrawMenu(sel, top);

    for (;;) {
        // Whole-detent consumption, same as PollEncoder — see the note there
        // for why snapping to newPos made direction changes cost two detents.
        const long newPos = encoder.read();
        int dir = 0;
        if (newPos - oldPos >= 4) {
            oldPos += 4;
            dir = -1;
        } else if (newPos - oldPos <= -4) {
            oldPos -= 4;
            dir = +1;
        }
        if (dir != 0) {
            sel += dir;
            if (sel < 0)
                sel = kMenuEntries - 1;
            else if (sel >= kMenuEntries)
                sel = 0;
            if (sel < top)
                top = sel;
            else if (sel > top + 3)
                top = sel - 3;
            DrawMenu(sel, top);
        }

        if (digitalRead(ENCODER_SW) == LOW) { // clicked — commit
            while (digitalRead(ENCODER_SW) == LOW)
                delay(10);
            return sel;
        }
        delay(2);
    }
}

// Which app to run: the stored choice, unless the encoder is held at boot.
// Returns only when a module was picked; CALIBRATE runs the wizard, which
// reboots.
//
// The held-encoder check is deliberately unconditional and happens before any
// app code runs, so a firmware that hangs in its own Begin() can still be
// escaped — hold the encoder and power-cycle.
static forge::IApp *SelectApp() {
    uint8_t stored = forge::fs::LoadBootApp(0);
    if (stored >= kAppCount)
        stored = 0; // stale index (app removed from this build)

    // Either gesture opens the menu: the encoder held at power-on, or an app
    // having asked for it before rebooting.
    const bool requested = forge::fs::BootMenuRequested();
    if (requested || digitalRead(ENCODER_SW) == LOW) {
        const int chosen = RunBootMenu(stored);

        if (chosen == kCalibrateIndex) {
            // Come back to this screen afterwards rather than dropping into a
            // module: calibrating is rarely the last thing you meant to do, and
            // RunCalibration() reboots as soon as it has written /cal.bin.
            // Keep `stored` as the pre-selection so the list opens where it was.
            forge::fs::SaveBootApp(stored, /*showMenu=*/true);
            RunCalibration(); // saves and reboots
            for (;;) { /* not reached */
            }
        }

        // Always rewrite, even when the choice is unchanged: it is what clears
        // the showMenu flag, and leaving it set would trap the module in the
        // menu on every boot.
        forge::fs::SaveBootApp((uint8_t)chosen, false);
        stored = (uint8_t)chosen;
    }
    return kApps[stored];
}

// ────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    {
        const uint32_t t = millis();
        while (!Serial && (millis() - t) < 2000) { /* wait for USB-CDC */
        }
    }
    Serial.println("\n\n--- ForgeSeries unified firmware ---");

    encoder.begin(); // deferred pin init — safe only once the runtime is up
    InitWire();
    InitIO();

    // Display first, so any later hardware failure can be shown on screen.
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        pinMode(LED_BUILTIN, OUTPUT);
        while (1) { // nothing useful left to do — blink for a service tech
            digitalWrite(LED_BUILTIN, HIGH);
            delay(200);
            digitalWrite(LED_BUILTIN, LOW);
            delay(200);
        }
    }
    display.clearDisplay();
    display.setTextWrap(false);
    display.cp437(true);

    // The DAC is optional for some apps (the scope only uses it for
    // pass-through), so a failure here warns rather than halts.
    if (!InitDAC())
        Serial.println("MCP4728 not found — outputs disabled.");

    // Storage, then calibration. One calibration serves every app because it
    // describes the board, not the module — which is the whole reason it lives
    // in the shell rather than being re-run per firmware.
    if (!forge::fs::Begin())
        Serial.println("No persistent storage — presets and calibration disabled.");
    cal = forge::fs::LoadCalibrationFs();
    Serial.println(cal.valid ? "Calibration: loaded"
                             : "Calibration: not set (using nominal)");

    display.clearDisplay();
    display.drawBitmap(30, 0, VFM_Splash, 68, 64, 1);
    display.display();
    delay(1200);

    g_app = SelectApp();

    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(4, 20);
    display.print(g_app->Name());
    display.setTextSize(1);
    display.setCursor(80, 54);
    display.print("V");
    display.print(g_app->Version());
    display.display();
    delay(1000);

    Serial.printf("Starting app: %s v%s\n", g_app->Name(), g_app->Version());
    g_app->Begin();

    // Re-baseline the encoder before the app sees it.
    //
    // Anything turned during boot is on the counter by now — most obviously the
    // scrolling done in the module selector, which tracks its own position and
    // leaves this one at zero. Without this, PollEncoder's first call reads a
    // delta covering the whole menu rotation and fires that many detents
    // straight into the app: the module came up with its cursor moved by
    // exactly as far as you had scrolled to select it, and only after selecting
    // it from the menu — a plain power-cycle looked fine.
    oldPosition = encoder.read();
    newPosition = oldPosition;

    g_setupDone = true; // release Core 1
}

// Return to the selector. Reboots rather than unwinding in place — the running
// app owns interrupts, hardware timers and Core 1 work, and restarting is both
// simpler and more reliable than tearing that down live.
static void SwitchToMenu() {
    g_app->End(); // let the app flush anything it owes to storage

    // Keep the current app as the pre-selection so the menu opens on it.
    uint8_t current = 0;
    for (int i = 0; i < kAppCount; i++) {
        if (kApps[i] == g_app) {
            current = (uint8_t)i;
            break;
        }
    }
    forge::fs::SaveBootApp(current, /*showMenu=*/true);

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(18, 28);
    display.print("SELECT MODULE...");
    display.display();
    delay(400); // let the message register before the screen blanks

    rp2040.reboot();
    for (;;) { /* not reached */
    }
}

void loop() {
    PollEncoder(g_app);
    if (g_switchRequested)
        SwitchToMenu();
    g_app->Tick0();
}

void setup1() {
    while (!g_setupDone)
        delay(1);
}

void loop1() {
    g_app->Tick1(display);
}
