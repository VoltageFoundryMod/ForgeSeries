// ForgeView — Eurorack oscilloscope / spectrum module (Seeed XIAO RP2040).
//
// Thin hardware entry point. All scope logic lives in lib/scope.hpp (shared with
// the VCV Rack plugin). Dual-core split mirrors ClockForge:
//   Core 0 (loop)  : encoder, ADC sampling, DAC pass-through  (Wire1 / ADC)
//   Core 1 (loop1) : GFX render + display flush               (Wire)
// Separate hardware I2C blocks on separate cores => no mutex needed.

#include <Arduino.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h> // here (not just settings.hpp) so PlatformIO's LDF finds it
#include <Wire.h>

#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#include "boardIO.hpp"
#include "encoder.hpp"
#include "pinouts.hpp"
#include "scope.hpp"
#include "settings.hpp" // after scope.hpp: persists its globals to EEPROM
#include "splash.hpp"
#include "version.hpp"

// OLED display (Wire / I2C1, owned by Core 1 at runtime)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Rotary encoder (polled on Core 0)
Encoder encoder(ENC_PIN_1, ENC_PIN_2);
long oldPosition = 0;
long newPosition = 0;

// Core 1 renders when this is set by Core 0's interaction, but the scope always
// wants fresh frames, so Core 1 just renders continuously (see loop1).

// Core 1 starts running in parallel with Core 0's setup() on this Arduino core,
// but display.begin() (framebuffer allocation) and the splash/version screens
// happen inside setup() — rendering before it finishes crashes the boot or
// wipes the splash. Core 1 idles until Core 0 raises this flag.
static volatile bool g_setupDone = false;

void HandleEncoder() {
    // Button (active-low with pull-up)
    bool pressed = (digitalRead(ENCODER_SW) == LOW);
    ScopeEncoderButton(pressed);

    // Rotation — quarter-step detent detection with hysteresis (matches CLK).
    newPosition = encoder.read();
    if ((newPosition - 3) / 4 > oldPosition / 4) { // counter-clockwise
        oldPosition = newPosition;
        ScopeEncoderTurn(-1);
    } else if ((newPosition + 3) / 4 < oldPosition / 4) { // clockwise
        oldPosition = newPosition;
        ScopeEncoderTurn(+1);
    }
}

void setup() {
    Serial.begin(115200);
    {
        uint32_t t = millis();
        while (!Serial && (millis() - t) < 2000) { /* wait for USB-CDC */
        }
    }
    Serial.println("\n\n--- Starting ForgeView ---");

    encoder.begin(); // deferred pin init (safe after runtime is up)
    InitWire();
    InitIO();
    Serial.println("Initializing ForgeView version " VERSION "...");
    ScopeInit();
    ScopeSettingsInit(); // restore saved mode/params over the defaults

    // Display first, so hardware errors can be shown on screen.
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        pinMode(LED_BUILTIN, OUTPUT);
        while (1) {
            digitalWrite(LED_BUILTIN, HIGH);
            delay(200);
            digitalWrite(LED_BUILTIN, LOW);
            delay(200);
        }
    }
    display.clearDisplay();
    display.setTextWrap(false);
    display.cp437(true);
    Serial.printf("SSD1306 init: %dx%d @ 0x%02X\n", SCREEN_WIDTH, SCREEN_HEIGHT, OLED_ADDRESS);

    // DAC is optional for the scope (used for input pass-through); warn only.
    if (!InitDAC())
        Serial.println("MCP4728 not found — output pass-through disabled.");
    Serial.println("DAC init complete");
    // Splash + version screen.
    display.clearDisplay();
    display.drawBitmap(30, 0, VFM_Splash, 68, 64, 1);
    display.display();
    delay(1500);
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(4, 20);
    display.print("ForgeView");
    display.setTextSize(1);
    display.setCursor(80, 54);
    display.print("V" VERSION);
    display.display();
    delay(1200);

    Serial.println("ForgeView initialization complete. Core 0: loop(), Core 1: loop1().");
    g_setupDone = true; // release Core 1's render loop
}

// Core 0: encoder + high-rate ADC sampling + DAC pass-through.
void loop() {
    HandleEncoder();

    int ch1 = analogRead(CV_1_IN_PIN);
    int ch2 = analogRead(CV_2_IN_PIN);
    bool clk = digitalRead(CLK_IN_PIN);
    ScopeFeedSample(ch1, ch2, clk);

    // Buffered oscilloscope through: CV1 -> Out1/Out3, CV2 -> Out2/Out4.
    DACWriteAll((uint16_t)ch1, (uint16_t)ch2, (uint16_t)ch1, (uint16_t)ch2);

    ScopeSettingsPoll(); // debounced EEPROM save (5 s after the last change)
}

// Core 1: render the current mode + flush over the display bus.
void setup1() {
    while (!g_setupDone)
        delay(1);
}

void loop1() {
    ScopeRender(display);
    display.display(); // Wire (I2C1) — Core 1 only
}
