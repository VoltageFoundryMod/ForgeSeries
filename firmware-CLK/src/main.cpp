#include <Arduino.h>
#if !defined(ARDUINO_ARCH_RP2040)
#include <TimerTCC0.h>
#else
#include <hardware/timer.h>
// Core 0 sets this flag after preparing a display buffer; Core 1 clears it after display.display().
// Wire  (GPIO6/7) = display only, used exclusively by Core 1 at runtime.
// Wire1 (GPIO0/1) = DAC only,     used exclusively by Core 0 at runtime.
// Separate hardware I2C blocks + separate cores = zero conflict, no mutex needed.
static volatile bool _displayFrameReady = false;
static volatile bool _displayLocked = false;  // Core 0 sets to pause Core 1 GFX rendering
#endif

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

// Load local libraries
#include "boardIO.hpp"
#include "encoder.hpp"
#include "clockEngine.hpp"
#include "cvInputs.hpp"
#include "displayManager.hpp"
#include "storage.hpp"    // includes presetManager.hpp transitively
#include "metrics.hpp"
#include "outputs.hpp"
#include "pinouts.hpp"
#include "splash.hpp"
#include "utils.hpp"
#include "version.hpp"
#include "menuDefinitions.hpp" // MenuItem struct
#include "menuHandlers.hpp"    // MENU_ITEMS[] + setter/action implementations
#include "menuDisplay.hpp"     // MD_* display primitives (depends on menuHandlers.hpp)

// Configuration
#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// OLED display object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Display manager (non-blocking display updates)
DisplayManager displayMgr(display);

// Rotary encoder object
Encoder encoder(ENC_PIN_1, ENC_PIN_2); // rotary encoder library setting
float oldPosition = 0;  // last acted-on raw encoder count
float newPosition = 0;  // current raw encoder count

// Performance metrics
PerformanceMetrics metrics;

// Output objects
Output outputs[NUM_OUTPUTS] = {
#if defined(ARDUINO_ARCH_RP2040)
    Output(1, OutputType::DACOut),  // All outputs via MCP4728 on RP2040
    Output(2, OutputType::DACOut),
#else
    Output(1, OutputType::DigitalOut),
    Output(2, OutputType::DigitalOut),
#endif
    Output(3, OutputType::DACOut),
    Output(4, OutputType::DACOut)};

// ---- Global variables ----
// Play/Stop state
bool masterState = true; // Track global play/stop state (true = playing, false = stopped)

unsigned long lastDisplayUpdateTime = 0;
const unsigned long DISPLAY_UPDATE_INTERVAL = 50; // Minimum 50ms between display updates
uint8_t frameSkip = 0;
const uint8_t FRAME_SKIP_COUNT = 3; // Only update every 4th request

// Menu variables
int menuItem = 2;
bool switchState = 1;
bool oldSwitchState = 1;
int menuMode = 0;                    // Menu mode for parameter editing
bool displayRefresh = 1;             // Display refresh flag
bool unsavedChanges = false;         // Unsaved changes flag
int euclideanOutputSelect = 0;       // Euclidean rhythm output index
int quantizerOutputSelect = 2;       // Quantizer output index
int envelopeOutputSelect = 2;        // Envelope output index
unsigned long lastEncoderUpdate = 0; // Last encoder update time

// Macro to request display refresh (marks display dirty for update)
#define REQUEST_DISPLAY_REFRESH() do { displayRefresh = 1; displayMgr.MarkDirty(); } while(0)

#include "menuRender.hpp"      // MenuIndicator, MenuHeader, HandleDisplay
#include "calibration.hpp"     // RunCalibration() — output trim + CV LUT capture

// Calibration data (loaded from EEPROM at boot; updated by RunCalibration())
CalibrationData cal;

// Function prototypes
void HandleIO();
void SetMasterState(bool);
void ToggleMasterState();
void HandleEncoderClick();
void HandleEncoderPosition();
void UpdateSpeedFactor();
void HandleCVInputs();
void HandleOutputs();
void ShowTemporaryMessage(const char *msg, uint32_t durationMs);

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
                    if (mi.action) mi.action();
                } else { // MENU_EDIT
                    menuMode = menuItem;
                    // Fix: initialize pending state for CV target editing
                    // (original code had this as dead code inside menuMode==0 branch).
                    if (menuItem == 51) { pendingCVInputTarget[0] = CVInputTarget[0]; }
                    else if (menuItem == 52) { pendingCVInputTarget[1] = CVInputTarget[1]; }
                }
            }
        } else {
            // Commit changes and exit edit mode.
            // CV target items copy the pending selection back to the live value.
            if (menuMode == 51) { CVInputTarget[0] = pendingCVInputTarget[0]; }
            else if (menuMode == 52) { CVInputTarget[1] = pendingCVInputTarget[1]; }
            menuMode = 0;
        }
    }
}

// Calculate the speed of the encoder rotation.
// Resets to 1.0 when direction reverses so the first detent after a turn-around
// is always a single step — avoids the "skips 2" artifact on BPM decrease.
float speedFactor = 1.0;
unsigned long lastEncoderTime = 0;
int lastEncoderDir = 0; // +1 or -1
void UpdateSpeedFactor(int dir) {
    unsigned long currentEncoderTime = millis();
    unsigned long timeDiff = currentEncoderTime - lastEncoderTime;
    lastEncoderTime = currentEncoderTime;

    if (lastEncoderDir != 0 && dir != lastEncoderDir) {
        // Direction changed — clamp to 1 for first step of new direction
        speedFactor = 1.0;
        lastEncoderDir = dir;
        return;
    }
    lastEncoderDir = dir;

    if (timeDiff < 30) {
        speedFactor = 8.0; // Very fast spin
    } else if (timeDiff < 60) {
        speedFactor = 4.0; // Fast spin
    } else if (timeDiff < 120) {
        speedFactor = 2.0; // Moderate spin
    } else {
        speedFactor = 1.0; // Normal
    }
}

void HandleEncoderPosition() {
    metrics.BeginEncoderMeasurement();
    newPosition = encoder.read();

    if ((newPosition - 3) / 4 > oldPosition / 4) { // Decrease, turned counter-clockwise
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
    } else if ((newPosition + 3) / 4 < oldPosition / 4) { // Increase, turned clockwise
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
    metrics.EndEncoderMeasurement();
}


// Show a brief full-screen message (e.g. "SAVED", "LOADED") then return.
// On RP2040: signals Core 1 to flush via Wire — Core 0 never touches the Wire bus.
// On SAMD21: flushes inline as usual.
// Uses delay() instead of a HandleIO() loop to avoid re-entrant HandleEncoderClick().
void ShowTemporaryMessage(const char *msg, uint32_t durationMs) {
    // Pause Core 1's GFX rendering so it can't overwrite the buffer.
    // Use _displayLocked rather than touching Wire from Core 0.
#if defined(ARDUINO_ARCH_RP2040)
    _displayLocked = true;
    delay(10);  // Let Core 1 finish any in-flight HandleDisplay() call (~1ms max)
#endif

    display.clearDisplay();
    display.setTextSize(2);
    int x = (SCREEN_WIDTH - (int)(strlen(msg)) * 12) / 2;
    display.setCursor(x, SCREEN_HEIGHT / 2 - 8);
    display.print(msg);
#if defined(ARDUINO_ARCH_RP2040)
    _displayFrameReady = true;  // Core 1 flushes over Wire — no Wire call from Core 0
#else
    display.display();
#endif
    // Keep DAC outputs running during the message — only skip encoder/display work.
    uint32_t _msgStart = millis();
    while (millis() - _msgStart < durationMs) {
        HandleOutputs();
    }

#if defined(ARDUINO_ARCH_RP2040)
    _displayLocked = false;     // Resume normal Core 1 rendering
#endif
    REQUEST_DISPLAY_REFRESH();  // Force a clean redraw after returning
}

// Redraw the display.
// RP2040: Core 0 prepares the buffer (no I2C) and signals Core 1 to flush via Wire.
// Core 0 then returns immediately and keeps calling HandleOutputs() via Wire1 unblocked.
// SAMD21: single-core, flush inline.
void RedrawDisplay() {
    metrics.BeginDisplayMeasurement();
    displayMgr.PrepareFrame();   // DrawOverlays + CommitFrame, no display.display()
    metrics.EndDisplayMeasurement();
    displayRefresh = 0;
#if defined(ARDUINO_ARCH_RP2040)
    _displayFrameReady = true;  // Signal Core 1 to call display.display() over Wire
#else
    display.display();           // SAMD21: single-core, flush inline
#endif
}

// Toggle the master state and update all outputs
void ToggleMasterState() {
    SetMasterState(!masterState);
}

// Set the master state and update all outputs
void SetMasterState(bool state) {
    // If toggling from off to on, reset the tick counters
    if (!masterState && state) {
        tickCounter = 0;
        externalTickCounter = 0;
    }
    masterState = state;
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        outputs[i].SetMasterState(state);
    }
}

void HandleOutputs() {
    // Update DAC outputs and envelopes
#if defined(ARDUINO_ARCH_RP2040)
    // All 4 outputs are MCP4728 I2C DAC. DACWriteAll is called directly here.
    // display.display() is gated to encoder-idle periods in RedrawDisplay() so
    // it never overlaps with DACWriteAll (same I2C bus).
    uint16_t v0 = (uint16_t)outputs[0].GetOutputLevel();
    uint16_t v1 = (uint16_t)outputs[1].GetOutputLevel();
    uint16_t v2 = (uint16_t)outputs[2].GetOutputLevel();
    uint16_t v3 = (uint16_t)outputs[3].GetOutputLevel();
    metrics.BeginDACMeasurement();
    DACWriteAll(v0, v1, v2, v3);
    metrics.EndDACMeasurement();
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        outputs[i].GenEnvelope();
    }
#endif
#if !defined(ARDUINO_ARCH_RP2040)
    // SAMD21: gates 0-1 driven in ISR; update DAC outputs 2-3 here
    // CRITICAL SECTION: Prevent encoder/display from interrupting DAC writes
    // DAC writes take ~100-200µs, must complete atomically
    noInterrupts();
    SetPin(2, outputs[2].GetOutputLevel());
    SetPin(3, outputs[3].GetOutputLevel());
    interrupts();

    // Envelope generation can happen outside critical section
    outputs[2].GenEnvelope();
    outputs[3].GenEnvelope();
#endif
}

void setup() {
    Serial.begin(115200);
#if defined(ARDUINO_ARCH_RP2040)
    // USB-CDC on RP2040: wait up to 3 s for a host to open the port so that
    // early Serial.println() messages are not silently dropped.  We time-out
    // unconditionally so the module boots normally even without a connected PC.
    {
        uint32_t _t = millis();
        while (!Serial && (millis() - _t) < 3000) { /* wait */ }
        if (Serial) delay(100); // Let TX path stabilize after CDC connects
    }
#endif
    Serial.println("\n\n--- Starting ClockForge ---");
    Serial.println("Initializing core 0...");

    Serial.println("Initializing encoder and I2C...");
#if defined(ARDUINO_ARCH_RP2040)
    delay(500);               // Give USB-CDC time to enumerate
    encoder.begin();          // Deferred pin init for RP2040 (safe here, after runtime ready)
    InitWire();               // Configure SDA/SCL and Wire.begin() — shared by display + DAC
#endif

    Serial.println("Initializing storage...");
    EEPROMInit();

    // Initialize I/O pins (ADC resolution, input pins, encoder button)
    Serial.println("Initializing I/O...");
    InitIO();

    Serial.println("Initializing display...");
    // Initialize OLED display — comes BEFORE DAC so errors can be shown on screen
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        // Can't show on display — blink LED instead as distress signal
        pinMode(LED_BUILTIN, OUTPUT);
        while (1) {
            digitalWrite(LED_BUILTIN, HIGH); delay(200);
            digitalWrite(LED_BUILTIN, LOW);  delay(200);
        }
    }
#if !defined(ARDUINO_ARCH_RP2040)
    Wire.setClock(1000000);  // SAMD21 only — 1MHz I2C (RP2040 already set to 400KHz in InitWire)
#endif
    display.clearDisplay();
    display.setTextWrap(false);
    display.cp437(true); // Use full 256 char 'Code Page 437' font

    Serial.println("Initializing DAC...");
#if defined(ARDUINO_ARCH_RP2040)
    // Initialize the quad DAC — show error on display if not found
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
        // Scan I2C bus and print found addresses to serial
        Serial.println("I2C bus scan:");
        for (uint8_t addr = 1; addr < 127; addr++) {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) {
                Serial.printf("  Found device at 0x%02X\n", addr);
            }
        }
        while (1);  // Halt — DAC is required for all outputs
    }
#endif

    Serial.println("Initialization complete. Showing splash screen...");
    display.clearDisplay();
    display.drawBitmap(30, 0, VFM_Splash, 68, 64, 1);
    display.display();
    delay(2000);
    // Print module name in the middle of the screen
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(4, 20);
    display.print("ClockForge");
    display.setTextSize(1);
    display.setCursor(80, 54);
    display.print("V" VERSION);
    display.display();
    delay(1500);

    Serial.println("Loading settings from flash memory slot 0...");
    // Load calibration data first so CV inputs work correctly from the start.
    cal = LoadCalibration();
    if (!cal.valid) {
        Serial.println("No calibration data found. Using ideal defaults. Run calibration for best accuracy.");
    } else {
        Serial.println("Calibration data loaded.");
    }
    // Load settings from flash memory (slot 0) or set defaults
    LoadSaveParams p = Load(0);
    UpdateParameters(p);

    // ── Calibration mode: hold encoder button during boot ──────────────────
    // Check AFTER display is up (so we can show instructions) but BEFORE
    // the timer starts (so RunCalibration() can block freely).
    if (digitalRead(ENCODER_SW) == LOW) {
        Serial.println("Encoder held at boot — entering calibration mode.");
        RunCalibration();  // blocks; reboots at end
        // never returns
    }

    // Initialize timer BEFORE attaching external clock interrupt.
    // ClockReceived() calls UpdateBPM() which calls cancel_repeating_timer();
    // the timer struct must be valid before any interrupt can fire.
    Serial.println("Initializing timer...");
    InitializeTimer();
    UpdateBPM(BPM);
    Serial.printf("Initial BPM: %d\n", BPM);

    // Attach external clock interrupt last — timer must be initialized first.
    attachInterrupt(digitalPinToInterrupt(CLK_IN_PIN), ClockReceived, RISING);

    // Force an immediate display refresh — clears the version screen.
    REQUEST_DISPLAY_REFRESH();
}

// Handle IO without the display (used during save/load message display)
void HandleIO() {
    HandleEncoderClick();
    HandleEncoderPosition();
    HandleExternalClock();
}

void loop() {
    metrics.BeginLoopMeasurement();

    // Apply deferred BPM change — UpdateBPM() calls RP2040 SDK alarm-pool functions
    // (cancel_repeating_timer + add_repeating_timer_us) which block for ~50-100µs.
    // Keeping that out of HandleEncoderPosition() prevents encoder edges being missed.
    if (bpmNeedsUpdate) {
        bpmNeedsUpdate = false;
        UpdateBPM(BPM);
    }

    HandleOutputs();
    HandleEncoderClick();
    HandleEncoderPosition();
    HandleCVInputs();
    HandleExternalClock();
    // Clock and CV handlers set displayRefresh=1 but cannot call displayMgr.MarkDirty().
    // Propagate here so HandleDisplay() actually fires (it requires both flags).
    if (displayRefresh) displayMgr.MarkDirty();

#if !defined(ARDUINO_ARCH_RP2040)
    // SAMD21: single-core, GFX + display flush in main loop with frame-skip.
    if (frameSkip == 0) {
        HandleDisplay();
    }
    frameSkip = (frameSkip + 1) % FRAME_SKIP_COUNT;
#endif
    // RP2040: HandleDisplay() (GFX rendering) runs on Core 1 to keep this loop tight.

    static unsigned long lastStatsTime = 0;
    if (millis() - lastStatsTime >= 5000) {
        metrics.PrintStats();
        lastStatsTime = millis();
        metrics.Reset();
    }

    metrics.EndLoopMeasurement();
}

#if defined(ARDUINO_ARCH_RP2040)
// Core 1: owns Wire (GPIO6/7, I2C1) — handles ALL display work.
// GFX buffer rendering (HandleDisplay) + display.display() both live here so
// Core 0's loop is never stalled by display work.  Core 0 only does:
//   timer ISR (ClockPulse) + DACWriteAll (Wire1) + encoder + CV reads.
void setup1() {
    Serial.println("Initialized core 1 started: display GFX + flush engine (Wire) running.");
}

void loop1() {
    // Render the GFX buffer (CPU-only, no I2C) into display RAM.
    // Skip rendering when Core 0 holds the buffer (e.g. showing a temporary message).
    if (!_displayLocked) {
        HandleDisplay();
    }
    // Flush the prepared frame over Wire when Core 0 (or Display timeout on
    // Core 1) has signalled a new frame is ready.
    if (_displayFrameReady) {
        _displayFrameReady = false;
        metrics.BeginCore1FlushMeasurement();
        display.display();   // Wire (GPIO6/7, I2C1), Core 1 only
        metrics.EndCore1FlushMeasurement();
    }
}
#endif
