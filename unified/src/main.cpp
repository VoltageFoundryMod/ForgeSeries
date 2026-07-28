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
#include "shellObjects.hpp"
#include "splash.hpp"

#include "apps/clk_app.hpp"
#include "apps/dq_app.hpp"
#include "apps/gen_app.hpp"
#include "apps/scp_app.hpp"

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

static void PollEncoder(forge::IApp *app) {
    app->EncoderButton(digitalRead(ENCODER_SW) == LOW); // active-low, pull-up

    newPosition = encoder.read();
    if ((newPosition - 3) / 4 > oldPosition / 4) { // counter-clockwise
        oldPosition = newPosition;
        app->EncoderTurn(-1);
    } else if ((newPosition + 3) / 4 < oldPosition / 4) { // clockwise
        oldPosition = newPosition;
        app->EncoderTurn(+1);
    }
}

// ── App selection ───────────────────────────────────────────────────────────
// TODO(step 4): persist the choice and show the boot menu when the encoder is
// held at power-on. Deliberately not done yet — the obvious place to store a
// slot byte is the emulated EEPROM, but that is exactly the sector ForgeView's
// settings.hpp already owns from offset 0, and the whole layout is due to move
// to LittleFS. Inventing a byte here would collide now and be thrown away then.
// Until that lands, the unified image always boots the first registered app.
static forge::IApp *SelectApp() {
    return kApps[0];
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

    // TODO(step 4): cal = LoadCalibration() once storage moves to LittleFS.
    // Until then `cal` stays value-initialised: valid=false makes the DAC
    // correction fall through to identity and CV reads use the nominal mapping.

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

    g_setupDone = true; // release Core 1
}

void loop() {
    PollEncoder(g_app);
    g_app->Tick0();
}

void setup1() {
    while (!g_setupDone)
        delay(1);
}

void loop1() {
    g_app->Tick1(display);
}
