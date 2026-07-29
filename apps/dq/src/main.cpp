#include <Arduino.h>

// Core 0 sets this flag after preparing a display buffer; Core 1 clears it after
// display.display().
// Wire  (GPIO6/7) = display only, used exclusively by Core 1 at runtime.
// Wire1 (GPIO0/1) = DAC only,     used exclusively by Core 0 at runtime.
// Separate hardware I2C blocks + separate cores = zero conflict, no mutex needed.
static volatile bool _displayFrameReady = false;
static volatile bool _displayLocked = false; // Core 0 sets to pause Core 1 GFX

// The Arduino core launches Core 1 *before* it calls setup() on Core 0, so
// without a gate loop1() renders and flushes over Wire while Core 0 is still
// running InitWire() and display.begin(). That race corrupts the SSD1306 init
// command stream (a dropped COMSCANDEC/SEGREMAP leaves the panel mirrored) and
// wipes the splash/version screens the moment they are drawn. Core 1 idles
// until Core 0 raises this flag.
static volatile bool _core1Enabled = false;

// Configuration
#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Macro to request display refresh from user interactions
// (marks dirty + resets the screen-timeout timer)
#define REQUEST_DISPLAY_REFRESH()     \
    do {                              \
        displayRefresh = 1;           \
        displayMgr.MarkInteraction(); \
    } while (0)

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

// clang-format off
// INCLUDE ORDER IS LOAD-BEARING — do not sort. shellObjects.hpp declares the
// board-owned display/displayMgr/encoder/cal that several headers below use,
// and menuDisplay.hpp must precede menuRender.hpp, which needs its MD_*
// primitives. Alphabetical order breaks both.
// Load local libraries
#include "boardIO.hpp"
#include "channel.hpp"
#include "cvInputs.hpp"
#include "displayManager.hpp"
#include "shellObjects.hpp" // extern display/displayMgr/encoder/cal
#include "encoder.hpp"
#include "jacks.hpp"
#include "encoderAccel.hpp" // shared rotation acceleration // module jack semantics + core/boardPinouts.hpp
#include "storage.hpp" // includes presetManager.hpp transitively
#include "utils.hpp"

// appDisplay.hpp defines RedrawDisplay()/ShowTemporaryMessage() inline, and the
// menu headers below call both, so it must precede them. It needs these two
// declared first; the definitions come later in this file.
extern bool displayRefresh;
void HandleCVInputs();
void HandleOutputs();
#include "appDisplay.hpp" // core: RedrawDisplay + SAVED/LOADED overlay

#include "menuDefinitions.hpp" // MenuItem struct
#include "menuHandlers.hpp"    // MENU_ITEMS[] + setter/action implementations
#include "menuDisplay.hpp"     // MD_* display primitives
#include "menuRender.hpp"      // MenuHeader, HandleDisplay

#include "calibration.hpp" // RunCalibration() — output trim + CV capture
#include "splash.hpp"
#include "version.hpp"
// clang-format on

// OLED display object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Display manager (non-blocking display updates)
DisplayManager displayMgr(display);

// Rotary encoder object
Encoder encoder(ENC_PIN_1, ENC_PIN_2);
float oldPosition = 0; // last acted-on raw encoder count
float newPosition = 0; // current raw encoder count

// The two quantizer voices — the whole instrument lives in here.
QuantizerChannel channels[NUM_CHANNELS];

// Two independent voices. NoteForge calls them quantizer channels and
// GravityForge calls them containers, but the jack layout is the same, and
// deliberately so: patch cables carry over between the two firmwares.

// Output jack assignment (index into the DAC/output arrays).
// Jack 1 = CV 1, Jack 2 = CV 2, Jack 3 = GATE 1, Jack 4 = GATE 2.

// What this module calls its output jacks, used by the calibration wizard
// (core/calibration.hpp). Jack naming is module semantics, so it lives here
// with NUM_CHANNELS and OUT_CV/OUT_GATE rather than in the shared wizard.

// ---- Global variables ----
int menuItem = 1; // 1-based; item 1 is the keyboard home screen
bool switchState = 1;
bool oldSwitchState = 1;
int menuMode = 0;                    // 0 = navigating, else the item being edited
bool displayRefresh = 1;             // Display refresh flag
bool unsavedChanges = false;         // Unsaved changes flag
int menuScreenTimeout = 2;           // Index into screenTimeoutOptions[]
unsigned long lastEncoderUpdate = 0; // Last encoder update time

// Calibration data (loaded from EEPROM at boot; updated by RunCalibration())
CalibrationData cal;

// Function prototypes
void HandleIO();
void HandleEncoderPosition();
void HandleOutputs(); // defined inline in lib/engine.hpp, included below

// ----------------------------------------------

// Handle encoder button click
void HandleEncoderClick() {
    oldSwitchState = switchState;
    switchState = digitalRead(ENCODER_SW);
    if (switchState == 1 && oldSwitchState == 0) {
        lastEncoderUpdate = millis();
        REQUEST_DISPLAY_REFRESH();
        if (menuMode == 0) {
            // Data-driven: look up the clicked item and execute or enter edit mode.
            if (menuItem >= 1 && menuItem <= MENU_ITEM_COUNT) {
                const MenuItem &mi = MENU_ITEMS[menuItem - 1];
                if (mi.type == MENU_ACTION || mi.type == MENU_TOGGLE) {
                    if (mi.action)
                        mi.action();
                } else { // MENU_EDIT
                    menuMode = menuItem;
                }
            }
        } else {
            menuMode = 0; // commit and leave edit mode
        }
    }
}

void HandleEncoderPosition() {
    newPosition = encoder.read();

    if ((newPosition - 3) / 4 > oldPosition / 4) { // turned counter-clockwise
        UpdateSpeedFactor(-1);
        oldPosition = newPosition;
        REQUEST_DISPLAY_REFRESH();
        lastEncoderUpdate = millis();
        if (menuMode == 0) {
            menuItem = (menuItem - 1 < 1) ? MENU_ITEM_COUNT : menuItem - 1;
        } else if (menuMode >= 1 && menuMode <= MENU_ITEM_COUNT) {
            if (MENU_ITEMS[menuMode - 1].setter)
                MENU_ITEMS[menuMode - 1].setter(-(int)speedFactor);
        }
    } else if ((newPosition + 3) / 4 < oldPosition / 4) { // turned clockwise
        UpdateSpeedFactor(+1);
        oldPosition = newPosition;
        REQUEST_DISPLAY_REFRESH();
        lastEncoderUpdate = millis();
        if (menuMode == 0) {
            menuItem = (menuItem + 1 > MENU_ITEM_COUNT) ? 1 : menuItem + 1;
        } else if (menuMode >= 1 && menuMode <= MENU_ITEM_COUNT) {
            if (MENU_ITEMS[menuMode - 1].setter)
                MENU_ITEMS[menuMode - 1].setter(+(int)speedFactor);
        }
    }
}


#include "engine.hpp" // HandleOutputs() — after the globals it references

void setup() {
    Serial.begin(115200);
    // USB-CDC on RP2040: wait up to 3 s for a host to open the port so early
    // Serial.println() messages are not silently dropped. Times out
    // unconditionally so the module boots normally without a connected PC.
    {
        uint32_t _t = millis();
        while (!Serial && (millis() - _t) < 3000) { /* wait */
        }
        if (Serial)
            delay(100); // Let TX path stabilize after CDC connects
    }
    Serial.println("\n\n--- Starting NoteForge ---");
    Serial.println("Initializing core 0...");

    Serial.println("Initializing encoder and I2C...");
    delay(500);      // Give USB-CDC time to enumerate
    encoder.begin(); // Deferred pin init for RP2040 (safe here, after runtime ready)
    InitWire();      // Configure SDA/SCL for both buses

    Serial.println("Initializing storage...");
    EEPROMInit();

    Serial.println("Initializing I/O...");
    InitIO();

    Serial.println("Initializing display...");
    // Initialize OLED display — comes BEFORE the DAC so errors can be shown on screen
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        // Can't show on display — blink LED instead as distress signal
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
    display.cp437(true); // Use full 256 char 'Code Page 437' font

    Serial.println("Initializing DAC...");
    if (!InitDAC()) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(WHITE);
        display.setCursor(0, 10);
        display.println("MCP4728 DAC");
        display.println("not found!");
        display.println("");
        display.println("Check I2C wiring");
        display.printf("Addr: 0x%02X", MCP4728_ADDR);
        display.display();
        Serial.println("I2C bus scan:");
        for (uint8_t addr = 1; addr < 127; addr++) {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) {
                Serial.printf("  Found device at 0x%02X\n", addr);
            }
        }
        while (1)
            ; // Halt — the DAC is required for all outputs
    }

    Serial.println("Initialization complete. Showing splash screen...");
    display.clearDisplay();
    display.drawBitmap(30, 0, VFM_Splash, 68, 64, 1);
    display.display();
    delay(2000);
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(4, 20);
    display.print("NoteForge");
    display.setTextSize(1);
    display.setCursor(80, 54);
    display.print("V" VERSION);
    display.display();
    delay(1500);

    Serial.println("Loading settings from flash memory slot 0...");
    // Calibration first so the CV inputs quantize accurately from the start.
    cal = LoadCalibration();
    if (!cal.valid) {
        Serial.println("No calibration data found. Using ideal defaults. Run calibration for best pitch accuracy.");
    } else {
        Serial.println("Calibration data loaded.");
    }
    LoadSaveParams p = Load(0);
    UpdateParameters(p);

    // ── Calibration mode: hold the encoder button during boot ──────────────
    // Checked AFTER the display is up (so instructions can be shown) but BEFORE
    // the trigger interrupt is attached (so RunCalibration() can block freely).
    if (digitalRead(ENCODER_SW) == LOW) {
        Serial.println("Encoder held at boot — entering calibration mode.");
        // RunCalibration() draws its own screens and relies on Core 1 to flush
        // them (_CalFlush() only raises _displayFrameReady). Release Core 1 as a
        // pure flush engine: locked so it never renders the menu over the wizard.
        _displayLocked = true;
        _core1Enabled = true;
        RunCalibration(); // blocks; reboots at the end
        // never returns
    }

    attachInterrupt(digitalPinToInterrupt(CLK_IN_PIN), TriggerReceived, RISING);

    // Force an immediate display refresh — clears the version screen.
    REQUEST_DISPLAY_REFRESH();

    _core1Enabled = true; // release Core 1's render/flush loop
}

// Handle IO without the display
void HandleIO() {
    HandleEncoderClick();
    HandleEncoderPosition();
}

void loop() {
    HandleCVInputs();
    HandleOutputs();
    HandleEncoderClick();
    HandleEncoderPosition();

    // HandleOutputs() sets displayRefresh on a note change but cannot call
    // displayMgr.MarkDirty(). Propagate here so HandleDisplay() actually fires
    // (it requires both flags).
    if (displayRefresh)
        displayMgr.MarkDirty();

    // RP2040: HandleDisplay() (GFX rendering) runs on Core 1 to keep this tight.
}

// Core 1: owns Wire (GPIO6/7, I2C1) — handles ALL display work.
// GFX buffer rendering (HandleDisplay) + display.display() both live here so
// Core 0's loop is never stalled by display work. Core 0 only does:
//   ADC reads + quantization + DACWriteAll (Wire1) + encoder.
void setup1() {
    // Wait for Core 0's setup() to finish InitWire() + display.begin() + splash
    // before touching the display object or the Wire bus (see _core1Enabled).
    while (!_core1Enabled)
        delay(1);
    Serial.println("Initialized core 1: display GFX + flush engine (Wire) running.");
}

void loop1() {
    // Render the GFX buffer (CPU-only, no I2C) into display RAM.
    // Skip while Core 0 holds the buffer (e.g. showing a temporary message).
    if (!_displayLocked) {
        HandleDisplay();
    }
    if (_displayFrameReady) {
        _displayFrameReady = false;
        display.display(); // Wire (GPIO6/7, I2C1), Core 1 only
    }
}
