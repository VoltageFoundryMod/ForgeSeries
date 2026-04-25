#include <Arduino.h>
#if !defined(ARDUINO_ARCH_RP2040)
#include <TimerTCC0.h>
#else
#include <hardware/timer.h>
static repeating_timer_t _clockTimer;
// Core 0 sets this flag after preparing a display buffer; Core 1 clears it after display.display().
// Wire  (GPIO6/7) = display only, used exclusively by Core 1 at runtime.
// Wire1 (GPIO0/1) = DAC only,     used exclusively by Core 0 at runtime.
// Separate hardware I2C blocks + separate cores = zero conflict, no mutex needed.
static volatile bool _displayFrameReady = false;
static volatile bool _displayLocked = false;  // Core 0 sets to pause Core 1 GFX rendering
#endif
// Rotary encoder setting
// ENCODER_OPTIMIZE_INTERRUPTS disabled - interrupts interfere with waveform timing
// Using polling mode instead (called in main loop at ~745Hz, fast enough)
// #define ENCODER_OPTIMIZE_INTERRUPTS
#if defined(ARDUINO_ARCH_RP2040)
// RP2040: PJRC Encoder uses architecture-specific macros not available on RP2040.
// Use a minimal polling-based shim with the same read() API.
// IMPORTANT: constructor must NOT call pinMode/digitalRead — pins are not ready
// at global-object-construction time. Call begin() from setup() before first read().
class Encoder {
    int _pin1, _pin2;
    long _pos = 0;
    bool _last1 = false, _last2 = false;
public:
    Encoder(int p1, int p2) : _pin1(p1), _pin2(p2) {}
    void begin() {
        pinMode(_pin1, INPUT_PULLUP);
        pinMode(_pin2, INPUT_PULLUP);
        _last1 = digitalRead(_pin1);
        _last2 = digitalRead(_pin2);
    }
    long read() {
        bool cur1 = digitalRead(_pin1);
        bool cur2 = digitalRead(_pin2);
        if (cur1 != _last1) {
            // Multiply by 2 so each detent (2 edges on pin1) gives 4 counts total,
            // matching PJRC Encoder convention expected by HandleEncoderPosition().
            // Negate to fix direction (hardware wiring puts CW as negative).
            _pos += (cur1 != cur2) ? -2 : 2;
        }
        _last1 = cur1;
        _last2 = cur2;
        return _pos;
    }
};
#else
#include <Encoder.h>
#endif

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

// Load local libraries
#include "boardIO.hpp"
#include "displayManager.hpp"
#include "loadsave.hpp"
#include "metrics.hpp"
#include "outputs.hpp"
#include "pinouts.hpp"
#include "splash.hpp"
#include "utils.hpp"
#include "version.hpp"

// ADC Calibration settings
const int ADC_THRESHOLD = 5;        // Threshold for ADC stability
float ADCCal[2] = {1.0180, 1.0180}; // ADC readings for the channels
int ADCOffset[2] = {23, 23};        // ADC offset for the channels

// Configuration
// RP2040 @ 133MHz: 960 PPQN → 208µs/tick at 300 BPM — plenty of ISR headroom.
// SAMD21 @ 48MHz:  192 PPQN → 1042µs/tick at 300 BPM — unchanged.
// 5× higher resolution = 5× smoother waveform steps at all BPMs.
#if defined(ARDUINO_ARCH_RP2040)
#define PPQN 960
#else
#define PPQN 192
#endif
#define MAXDAC 4095

#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// OLED display object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Display manager (non-blocking display updates)
DisplayManager displayMgr(display);

// Rotary encoder object
Encoder encoder(ENC_PIN_1, ENC_PIN_2); // rotary encoder library setting
float oldPosition = -999;              // rotary encoder library setting
float newPosition = -999;              // rotary encoder library setting

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

// CV modulation targets
enum CVTarget {
    None = 0,
    StartStop,
    Reset,
    SetBPM,
    Div1,
    Div2,
    Div3,
    Div4,
    Output1Prob,
    Output2Prob,
    Output3Prob,
    Output4Prob,
    Swing1Amount,
    Swing1Every,
    Swing2Amount,
    Swing2Every,
    Swing3Amount,
    Swing3Every,
    Swing4Amount,
    Swing4Every,
    Output3Level,
    Output4Level,
    Output3Offset,
    Output4Offset,
    Output3Waveform,
    Output4Waveform,
    Output1Duty,
    Output2Duty,
    Output3Duty,
    Output4Duty,
    Envelope1,
    Envelope2,
    Output3,
    Output4,
};

String CVTargetDescription[] = {
    "None",
    "Start/Stop",
    "Reset",
    "Set BPM",
    "Output 1 Div",
    "Output 2 Div",
    "Output 3 Div",
    "Output 4 Div",
    "Output 1 Prob",
    "Output 2 Prob",
    "Output 3 Prob",
    "Output 4 Prob",
    "Swing 1 Amt",
    "Swing 1 Every",
    "Swing 2 Amt",
    "Swing 2 Every",
    "Swing 3 Amt",
    "Swing 3 Every",
    "Swing 4 Amt",
    "Swing 4 Every",
    "Output 3 Lvl",
    "Output 4 Lvl",
    "Output 3 Off",
    "Output 4 Off",
    "Output 3 Wav",
    "Output 4 Wav",
    "Output 1 Duty",
    "Output 2 Duty",
    "Output 3 Duty",
    "Output 4 Duty",
    "Output 3 Env",
    "Output 4 Env",
    "Output 3",
    "Output 4",
};
int CVTargetLength = sizeof(CVTargetDescription) / sizeof(CVTargetDescription[0]);
CVTarget pendingCVInputTarget[NUM_CV_INS] = {CVTarget::None, CVTarget::None};

// ADC input offset and scale from calibration
float offsetScale[NUM_CV_INS][2]; // [channel][0: offset, 1: scale]
// CV target settings
CVTarget CVInputTarget[NUM_CV_INS] = {CVTarget::None, CVTarget::None};
int CVInputAttenuation[NUM_CV_INS] = {0, 0};
int CVInputOffset[NUM_CV_INS] = {0, 0};

// ADC input variables
float channelADC[NUM_CV_INS], oldChannelADC[NUM_CV_INS];

// BPM and clock settings
unsigned int BPM = 120;
unsigned int lastInternalBPM = 120;
unsigned int const minBPM = 10;
unsigned int const maxBPM = 300;

// Play/Stop state
bool masterState = true; // Track global play/stop state (true = playing, false = stopped)

// Global tick counter
volatile unsigned long tickCounter = 0;

// External clock variables
volatile unsigned long clockInterval = 0;
volatile unsigned long lastClockInterruptTime = 0;
volatile bool usingExternalClock = false;

static int const dividerAmount = 7;
int externalClockDividers[dividerAmount] = {1, 2, 4, 8, 16, 24, 48};
String externalDividerDescription[dividerAmount] = {"x1", "/2 ", "/4", "/8", "/16", "24PPQN", "48PPQN"};
int externalDividerIndex = 0;
volatile unsigned long externalTickCounter = 0;

unsigned long lastDisplayUpdateTime = 0;
const unsigned long DISPLAY_UPDATE_INTERVAL = 50; // Minimum 50ms between display updates
uint8_t frameSkip = 0;
const uint8_t FRAME_SKIP_COUNT = 3; // Only update every 4th request

// Menu variables
int menuItems = 66;
int menuItem = 2;
bool switchState = 1;
bool oldSwitchState = 1;
int menuMode = 0;                    // Menu mode for parameter editing
bool displayRefresh = 1;             // Display refresh flag
bool unsavedChanges = false;         // Unsaved changes flag
int euclideanOutputSelect = 0;       // Euclidean rhythm output index
int quantizerOutputSelect = 2;       // Quantizer output index
int envelopeOutputSelect = 2;        // Envelope output index
int saveSlot = 0;                    // Save slot index
unsigned long lastEncoderUpdate = 0; // Last encoder update time

// Macro to request display refresh (marks display dirty for update)
#define REQUEST_DISPLAY_REFRESH() do { displayRefresh = 1; displayMgr.MarkDirty(); } while(0)

// Function prototypes
void UpdateBPM(unsigned int);
void SetTapTempo();
void HandleIO();
void SetMasterState(bool);
void ToggleMasterState();
void HandleEncoderClick();
void HandleEncoderPosition();
void UpdateSpeedFactor();
void HandleDisplay();
void HandleExternalClock();
void HandleCVInputs();
void HandleCVTarget(int, float, CVTarget);
void HandleOutputs();
void ClockPulse();
void InitializeTimer();
void UpdateParameters(LoadSaveParams);
void ShowTemporaryMessage(const char *msg, uint32_t durationMs = 1000);

// ----------------------------------------------

// Handle encoder button click
void HandleEncoderClick() {
    oldSwitchState = switchState;
    switchState = digitalRead(ENCODER_SW);
    if (switchState == 1 && oldSwitchState == 0) {
        lastEncoderUpdate = millis();
        REQUEST_DISPLAY_REFRESH();
        if (menuMode == 0) {
            switch (menuItem) {
            case 1: // Set BPM
                menuMode = 1;
                break;
            case 2: // Toggle stopped state
                ToggleMasterState();
                break;
            case 3: // Set div1
                menuMode = 3;
                break;
            case 4: // Set div2
                menuMode = 4;
                break;
            case 5: // Set div3
                menuMode = 5;
                break;
            case 6: // Set div4
                menuMode = 6;
                break;
            case 7: // External clock divider
                menuMode = 7;
                break;
            case 8: // Toggle output 1
                outputs[0].ToggleOutputState();
                unsavedChanges = true;
                break;
            case 9: // Toggle output 2
                outputs[1].ToggleOutputState();
                unsavedChanges = true;
                break;
            case 10: // Toggle output 3
                outputs[2].ToggleOutputState();
                unsavedChanges = true;
                break;
            case 11: // Toggle output 4
                outputs[3].ToggleOutputState();
                unsavedChanges = true;
                break;
            case 12: // Set pulse probability for output 1
                menuMode = 12;
                break;
            case 13: // Set pulse probability for output 2
                menuMode = 13;
                break;
            case 14: // Set pulse probability for output 3
                menuMode = 14;
                break;
            case 15: // Set pulse probability for output 4
                menuMode = 15;
                break;
            case 16: // Select Euclidean rhythm output to edit
                menuMode = 16;
                break;
            case 17: // Toggle Euclidean rhythm for output
                outputs[euclideanOutputSelect].ToggleEuclidean();
                unsavedChanges = true;
                break;
            case 18: // Set Euclidean rhythm step length
                menuMode = 18;
                break;
            case 19: // Set Euclidean rhythm number of triggers
                menuMode = 19;
                break;
            case 20: // Set Euclidean rhythm rotation
                menuMode = 20;
                break;
            case 21: // Set euclidean padding
                menuMode = 21;
                break;
            case 22: // Set swing amount for output 1
                menuMode = 22;
                break;
            case 23: // Set swing every for output 1
                menuMode = 23;
                break;
            case 24: // Set swing amount for output 2
                menuMode = 24;
                break;
            case 25: // Set swing every for output 2
                menuMode = 25;
                break;
            case 26: // Set swing amount for output 3
                menuMode = 26;
                break;
            case 27: // Set swing every for output 3
                menuMode = 27;
                break;
            case 28: // Set swing amount for output 4
                menuMode = 28;
                break;
            case 29: // Set swing every for output 4
                menuMode = 29;
                break;
            case 30: // Set phase shift for output 1
                menuMode = 30;
                break;
            case 31: // Set phase shift for output 2
                menuMode = 31;
                break;
            case 32: // Set phase shift for output 3
                menuMode = 32;
                break;
            case 33: // Set phase shift for output 4
                menuMode = 33;
                break;
            case 34: // Duty Cycle for output 1
                menuMode = 34;
                break;
            case 35: // Duty Cycle for output 2
                menuMode = 35;
                break;
            case 36: // Duty Cycle for output 3
                menuMode = 36;
                break;
            case 37: // Duty Cycle for output 4
                menuMode = 37;
                break;
            case 38: // Level control for output 3
                menuMode = 38;
                break;
            case 39: // Level offset for output 3
                menuMode = 39;
                break;
            case 40: // Level control for output 4
                menuMode = 40;
                break;
            case 41: // Level offset for output 4
                menuMode = 41;
                break;
            case 42: // Set Waveform type for output 3
                menuMode = 42;
                break;
            case 43: // Set Waveform type for output 4
                menuMode = 43;
                break;
            case 44: // Envelope output selection
                menuMode = 44;
                break;
            case 45: // Envelope attack time
                menuMode = 45;
                break;
            case 46: // Envelope decay time
                menuMode = 46;
                break;
            case 47: // Envelope sustain level
                menuMode = 47;
                break;
            case 48: // Envelope release time
                menuMode = 48;
                break;
            case 49: // Envelope curve
                menuMode = 49;
                break;
            case 50: // Envelope retrigger toggle
                outputs[envelopeOutputSelect].ToggleRetrigger();
                unsavedChanges = true;
                break;
            case 51: // CV Input 1 target
                if (menuMode == 51) {
                    pendingCVInputTarget[0] = CVInputTarget[0];
                }
                menuMode = 51;
                break;
            case 52: // CV Input 2 target
                if (menuMode == 52) {
                    pendingCVInputTarget[1] = CVInputTarget[1];
                }
                menuMode = 52;
                break;
            case 53: // CV Input 1 attenuation
                menuMode = 53;
                break;
            case 54: // CV Input 1 offset
                menuMode = 54;
                break;
            case 55: // CV Input 2 attenuation
                menuMode = 55;
                break;
            case 56: // CV Input 2 offset
                menuMode = 56;
                break;
            case 57: // Quantizer Output
                menuMode = 57;
                break;
            case 58: // Quantizer Enable
                outputs[quantizerOutputSelect].ToggleQuantizer();
                unsavedChanges = true;
                break;
            case 59: // Quantizer root note
                menuMode = 59;
                break;
            case 60: // Quantizer scale
                menuMode = 60;
                break;
            case 61: // Quantizer octave shift
                menuMode = 61;
                break;
            case 62: // Tap tempo
                SetTapTempo();
                break;
            case 63: // Select save slot
                menuMode = 63;
                break;
            case 64: { // Save settings
                LoadSaveParams p;
                p.valid = true;
                p.BPM = BPM;
                p.externalClockDivIdx = externalDividerIndex;
                for (int i = 0; i < NUM_OUTPUTS; i++) {
                    p.divIdx[i] = outputs[i].GetDividerIndex();
                    p.dutyCycle[i] = outputs[i].GetDutyCycle();
                    p.outputState[i] = outputs[i].GetOutputState();
                    p.outputLevel[i] = outputs[i].GetLevel();
                    p.outputOffset[i] = outputs[i].GetOffset();
                    p.swingIdx[i] = outputs[i].GetSwingAmountIndex();
                    p.swingEvery[i] = outputs[i].GetSwingEvery();
                    p.pulseProbability[i] = outputs[i].GetPulseProbability();
                    p.euclideanParams[i] = outputs[i].GetEuclideanParams();
                    p.phaseShift[i] = outputs[i].GetPhase();
                    p.waveformType[i] = int(outputs[i].GetWaveformType());
                    p.envParams[i] = outputs[i].GetEnvelopeParams();
                    p.quantizerParams[i] = outputs[i].GetQuantizerParams();
                }
                for (int i = 0; i < NUM_CV_INS; i++) {
                    p.CVInputTarget[i] = CVInputTarget[i];
                    p.CVInputAttenuation[i] = CVInputAttenuation[i];
                    p.CVInputOffset[i] = CVInputOffset[i];
                }

                Save(p, saveSlot);
                unsavedChanges = false;
                ShowTemporaryMessage("SAVED");
                break;
            }
            case 65: { // Load from slot
                LoadSaveParams p = Load(saveSlot);
                UpdateParameters(p);
                unsavedChanges = false;
                ShowTemporaryMessage("LOADED");
                break;
            }
            case 66: { // Load default settings
                LoadSaveParams p = LoadDefaultParams();
                UpdateParameters(p);
                unsavedChanges = false;
                ShowTemporaryMessage("LOADED");
                break;
            }
            }
        } else {
            // Commit changes after exiting edit mode
            if (menuMode == 51) {
                CVInputTarget[0] = pendingCVInputTarget[0];
            } else if (menuMode == 52) {
                CVInputTarget[1] = pendingCVInputTarget[1];
            }
            // Exit edit mode
            menuMode = 0;
        }
    }
}

// Calculate the speed of the encoder rotation
float speedFactor;
unsigned long lastEncoderTime = 0;
void UpdateSpeedFactor() {
    unsigned long currentEncoderTime = millis();
    unsigned long timeDiff = currentEncoderTime - lastEncoderTime;
    lastEncoderTime = currentEncoderTime;

    if (timeDiff < 50) {
        speedFactor = 8.0; // Fast rotation
    } else if (timeDiff < 100) {
        speedFactor = 4.0; // Medium rotation
    } else if (timeDiff < 200) {
        speedFactor = 2.0; // Slow rotation
    } else {
        speedFactor = 1.0; // Normal rotation
    }
}

void HandleEncoderPosition() {
    metrics.BeginEncoderMeasurement();
    newPosition = encoder.read();

    if ((newPosition - 3) / 4 > oldPosition / 4) { // Decrease, turned counter-clockwise
        UpdateSpeedFactor();
        oldPosition = newPosition;
        REQUEST_DISPLAY_REFRESH();
        lastEncoderUpdate = millis();
        switch (menuMode) {
        case 0:
            menuItem = (menuItem - 1 < 1) ? menuItems : menuItem - 1;
            break;
        case 1: // Set BPM
            UpdateBPM(BPM - speedFactor);
            unsavedChanges = true;
            break;
        case 3:
        case 4:
        case 5:
        case 6: // Set div1, div2, div3, div4
            outputs[menuMode - 3].SetDivider(outputs[menuMode - 3].GetDividerIndex() - speedFactor);
            unsavedChanges = true;
            break;
        case 7: // External clock divider
            externalDividerIndex = constrain(externalDividerIndex - speedFactor, 0, dividerAmount - 1);
            unsavedChanges = true;
            break;
        case 12:
        case 13:
        case 14:
        case 15: // Set Pulse Probability for outputs
            outputs[menuMode - 12].SetPulseProbability(outputs[menuMode - 12].GetPulseProbability() - speedFactor);
            unsavedChanges = true;
            break;
        case 16: // Set euclidean output to edit
            euclideanOutputSelect = (euclideanOutputSelect - 1 < 0) ? NUM_OUTPUTS - 1 : euclideanOutputSelect - 1;
            unsavedChanges = true;
            break;
        case 18: // Set Euclidean rhythm step length
            outputs[euclideanOutputSelect].SetEuclideanSteps(outputs[euclideanOutputSelect].GetEuclideanSteps() - speedFactor);
            unsavedChanges = true;
            break;
        case 19: // Set Euclidean rhythm number of triggers
            outputs[euclideanOutputSelect].SetEuclideanTriggers(outputs[euclideanOutputSelect].GetEuclideanTriggers() - speedFactor);
            unsavedChanges = true;
            break;
        case 20: // Set Euclidean rhythm rotation
            outputs[euclideanOutputSelect].SetEuclideanRotation(outputs[euclideanOutputSelect].GetEuclideanRotation() - speedFactor);
            unsavedChanges = true;
            break;
        case 21: // Set Euclidean padding
            outputs[euclideanOutputSelect].SetEuclideanPadding(outputs[euclideanOutputSelect].GetEuclideanPadding() - speedFactor);
            unsavedChanges = true;
            break;
        case 22:
            outputs[0].SetSwingAmount(outputs[0].GetSwingAmountIndex() - speedFactor);
            unsavedChanges = true;
            break;
        case 23:
            outputs[0].SetSwingEvery(outputs[0].GetSwingEvery() - speedFactor);
            unsavedChanges = true;
            break;
        case 24:
            outputs[1].SetSwingAmount(outputs[1].GetSwingAmountIndex() - speedFactor);
            unsavedChanges = true;
            break;
        case 25:
            outputs[1].SetSwingEvery(outputs[1].GetSwingEvery() - speedFactor);
            unsavedChanges = true;
            break;
        case 26:
            outputs[2].SetSwingAmount(outputs[2].GetSwingAmountIndex() - speedFactor);
            unsavedChanges = true;
            break;
        case 27:
            outputs[2].SetSwingEvery(outputs[2].GetSwingEvery() - speedFactor);
            unsavedChanges = true;
            break;
        case 28:
            outputs[3].SetSwingAmount(outputs[3].GetSwingAmountIndex() - speedFactor);
            unsavedChanges = true;
            break;
        case 29:
            outputs[3].SetSwingEvery(outputs[3].GetSwingEvery() - speedFactor);
            unsavedChanges = true;
            break;
        case 30:
        case 31:
        case 32:
        case 33: // Set phase shift for outputs
            outputs[menuMode - 30].SetPhase(outputs[menuMode - 30].GetPhase() - speedFactor);
            unsavedChanges = true;
            break;
        case 34: // Duty Cycle for output 1
            outputs[0].SetDutyCycle(outputs[0].GetDutyCycle() - speedFactor);
            unsavedChanges = true;
            break;
        case 35: // Duty Cycle for output 2
            outputs[1].SetDutyCycle(outputs[1].GetDutyCycle() - speedFactor);
            unsavedChanges = true;
            break;
        case 36: // Duty Cycle for output 3
            outputs[2].SetDutyCycle(outputs[2].GetDutyCycle() - speedFactor);
            unsavedChanges = true;
            break;
        case 37: // Duty Cycle for output 4
            outputs[3].SetDutyCycle(outputs[3].GetDutyCycle() - speedFactor);
            unsavedChanges = true;
            break;
        case 38: // Set level for output 3
            outputs[2].SetLevel(outputs[2].GetLevel() - speedFactor);
            unsavedChanges = true;
            break;
        case 39: // Set offset for output 3
            outputs[2].SetOffset(outputs[2].GetOffset() - speedFactor);
            unsavedChanges = true;
            break;
        case 40: // Set level for output 4
            outputs[3].SetLevel(outputs[3].GetLevel() - speedFactor);
            unsavedChanges = true;
            break;
        case 41: // Set offset for output 4
            outputs[3].SetOffset(outputs[3].GetOffset() - speedFactor);
            unsavedChanges = true;
            break;
        case 42: // Set Output 3 waveform type
            outputs[2].SetWaveformType(static_cast<WaveformType>((outputs[2].GetWaveformType() - 1 + WaveformTypeLength) % WaveformTypeLength));
            unsavedChanges = true;
            break;
        case 43: // Set Output 4 waveform type
            outputs[3].SetWaveformType(static_cast<WaveformType>((outputs[3].GetWaveformType() - 1 + WaveformTypeLength) % WaveformTypeLength));
            unsavedChanges = true;
            break;
        case 44: // Envelope output selection
            envelopeOutputSelect = (envelopeOutputSelect - 1 < 2) ? 3 : envelopeOutputSelect - 1;
            unsavedChanges = true;
            break;
        case 45: // Envelope attack time
            outputs[envelopeOutputSelect].SetAttack(outputs[envelopeOutputSelect].GetAttack() - speedFactor * 2);
            unsavedChanges = true;
            break;
        case 46: // Envelope decay time
            outputs[envelopeOutputSelect].SetDecay(outputs[envelopeOutputSelect].GetDecay() - speedFactor * 2);
            unsavedChanges = true;
            break;
        case 47: // Envelope sustain level
            outputs[envelopeOutputSelect].SetSustain(outputs[envelopeOutputSelect].GetSustain() - speedFactor);
            unsavedChanges = true;
            break;
        case 48: // Envelope release time
            outputs[envelopeOutputSelect].SetRelease(outputs[envelopeOutputSelect].GetRelease() - speedFactor * 2);
            unsavedChanges = true;
            break;
        case 49: // Envelope curve
            outputs[envelopeOutputSelect].SetCurve(outputs[envelopeOutputSelect].GetCurve() - speedFactor * 0.01);
            unsavedChanges = true;
            break;
        case 51: { // CV Input 1 target
            CVTarget tmp = static_cast<CVTarget>((pendingCVInputTarget[0] - 1 + CVTargetLength) % CVTargetLength);
            if (tmp != pendingCVInputTarget[1] || tmp == CVTarget::None) {
                pendingCVInputTarget[0] = tmp;
            } else {
                pendingCVInputTarget[0] = static_cast<CVTarget>((pendingCVInputTarget[0] - 2 + CVTargetLength) % CVTargetLength);
            }
            break;
        }
        case 52: { // CV Input 2 target
            CVTarget tmp = static_cast<CVTarget>((pendingCVInputTarget[1] - 1 + CVTargetLength) % CVTargetLength);
            if (tmp != pendingCVInputTarget[0] || tmp == CVTarget::None) {
                pendingCVInputTarget[1] = tmp;
            } else {
                pendingCVInputTarget[1] = static_cast<CVTarget>((pendingCVInputTarget[1] - 2 + CVTargetLength) % CVTargetLength);
            }
            break;
        }
        case 53: // CV Input 1 attenuation
            CVInputAttenuation[0] = constrain(CVInputAttenuation[0] - speedFactor, 0, 100);
            break;
        case 54: // CV Input 1 offset
            CVInputOffset[0] = constrain(CVInputOffset[0] - speedFactor, 0, 100);
            break;
        case 55: // CV Input 2 attenuation
            CVInputAttenuation[1] = constrain(CVInputAttenuation[1] - speedFactor, 0, 100);
            break;
        case 56: // CV Input 2 offset
            CVInputOffset[1] = constrain(CVInputOffset[1] - speedFactor, 0, 100);
            break;
        case 57: // Quantizer output
            quantizerOutputSelect = (quantizerOutputSelect - 1 < 2) ? 3 : quantizerOutputSelect - 1;
            unsavedChanges = true;
            break;
        case 59: // Quantizer root note
            outputs[quantizerOutputSelect].SetQuantizerNoteIndex(outputs[quantizerOutputSelect].GetQuantizerNoteIndex() - speedFactor);
            unsavedChanges = true;
            break;
        case 60: // Quantizer scale
            outputs[quantizerOutputSelect].SetQuantizerScaleIndex(outputs[quantizerOutputSelect].GetQuantizerScaleIndex() - speedFactor);
            unsavedChanges = true;
            break;
        case 61: // Quantizer octave shift
            outputs[quantizerOutputSelect].SetQuantizerOctaveShift(outputs[quantizerOutputSelect].GetQuantizerOctaveShift() - speedFactor);
            unsavedChanges = true;
            break;
        case 63: // Select save slot
            saveSlot = (saveSlot - 1 < 0) ? NUM_SLOTS : saveSlot - 1;
            break;
        }
    } else if ((newPosition + 3) / 4 < oldPosition / 4) { // Increase, turned clockwise
        UpdateSpeedFactor();
        oldPosition = newPosition;
        REQUEST_DISPLAY_REFRESH();
        lastEncoderUpdate = millis();
        switch (menuMode) {
        case 0:
            menuItem = (menuItem + 1 > menuItems) ? 1 : menuItem + 1;
            break;
        case 1: // Set BPM
            UpdateBPM(BPM + speedFactor);
            unsavedChanges = true;
            break;
        case 3:
        case 4:
        case 5:
        case 6: // Set div1, div2, div3, div4
            outputs[menuMode - 3].SetDivider(outputs[menuMode - 3].GetDividerIndex() + speedFactor);
            unsavedChanges = true;
            break;
        case 7: // External clock divider
            externalDividerIndex = constrain(externalDividerIndex + speedFactor, 0, dividerAmount - 1);
            unsavedChanges = true;
            break;
        case 12:
        case 13:
        case 14:
        case 15: // Set Pulse Probability for outputs
            outputs[menuMode - 12].SetPulseProbability(outputs[menuMode - 12].GetPulseProbability() + speedFactor);
            unsavedChanges = true;
            break;
        case 16: // Set euclidean output to edit
            euclideanOutputSelect = (euclideanOutputSelect + 1 > NUM_OUTPUTS - 1) ? 0 : euclideanOutputSelect + 1;
            unsavedChanges = true;
            break;
        case 18: // Set Euclidean rhythm step length
            outputs[euclideanOutputSelect].SetEuclideanSteps(outputs[euclideanOutputSelect].GetEuclideanSteps() + speedFactor);
            unsavedChanges = true;
            break;
        case 19: // Set Euclidean rhythm number of triggers
            outputs[euclideanOutputSelect].SetEuclideanTriggers(outputs[euclideanOutputSelect].GetEuclideanTriggers() + speedFactor);
            unsavedChanges = true;
            break;
        case 20: // Set Euclidean rhythm rotation
            outputs[euclideanOutputSelect].SetEuclideanRotation(outputs[euclideanOutputSelect].GetEuclideanRotation() + speedFactor);
            unsavedChanges = true;
            break;
        case 21: // Set Euclidean padding
            outputs[euclideanOutputSelect].SetEuclideanPadding(outputs[euclideanOutputSelect].GetEuclideanPadding() + speedFactor);
            unsavedChanges = true;
            break;
        case 22:
            outputs[0].SetSwingAmount(outputs[0].GetSwingAmountIndex() + speedFactor);
            unsavedChanges = true;
            break;
        case 23:
            outputs[0].SetSwingEvery(outputs[0].GetSwingEvery() + speedFactor);
            unsavedChanges = true;
            break;
        case 24:
            outputs[1].SetSwingAmount(outputs[1].GetSwingAmountIndex() + speedFactor);
            unsavedChanges = true;
            break;
        case 25:
            outputs[1].SetSwingEvery(outputs[1].GetSwingEvery() + speedFactor);
            unsavedChanges = true;
            break;
        case 26:
            outputs[2].SetSwingAmount(outputs[2].GetSwingAmountIndex() + speedFactor);
            unsavedChanges = true;
            break;
        case 27:
            outputs[2].SetSwingEvery(outputs[2].GetSwingEvery() + speedFactor);
            unsavedChanges = true;
            break;
        case 28:
            outputs[3].SetSwingAmount(outputs[3].GetSwingAmountIndex() + speedFactor);
            unsavedChanges = true;
            break;
        case 29:
            outputs[3].SetSwingEvery(outputs[3].GetSwingEvery() + speedFactor);
            unsavedChanges = true;
            break;
        case 30:
        case 31:
        case 32:
        case 33: // Set phase shift for outputs
            outputs[menuMode - 30].SetPhase(outputs[menuMode - 30].GetPhase() + speedFactor);
            unsavedChanges = true;
            break;
        case 34: // Duty Cycle for output 1
            outputs[0].SetDutyCycle(outputs[0].GetDutyCycle() + speedFactor);
            unsavedChanges = true;
            break;
        case 35: // Duty Cycle for output 2
            outputs[1].SetDutyCycle(outputs[1].GetDutyCycle() + speedFactor);
            unsavedChanges = true;
            break;
        case 36: // Duty Cycle for output 3
            outputs[2].SetDutyCycle(outputs[2].GetDutyCycle() + speedFactor);
            unsavedChanges = true;
            break;
        case 37: // Duty Cycle for output 4
            outputs[3].SetDutyCycle(outputs[3].GetDutyCycle() + speedFactor);
            unsavedChanges = true;
            break;
        case 38: // Set level for output 3
            outputs[2].SetLevel(outputs[2].GetLevel() + speedFactor);
            unsavedChanges = true;
            break;
        case 39: // Set offset for output 3
            outputs[2].SetOffset(outputs[2].GetOffset() + speedFactor);
            unsavedChanges = true;
            break;
        case 40: // Set level for output 4
            outputs[3].SetLevel(outputs[3].GetLevel() + speedFactor);
            unsavedChanges = true;
            break;
        case 41: // Set offset for output 4
            outputs[3].SetOffset(outputs[3].GetOffset() + speedFactor);
            unsavedChanges = true;
            break;
        case 42: // Set Output 3 waveform type
            outputs[2].SetWaveformType(static_cast<WaveformType>((outputs[2].GetWaveformType() + 1) % WaveformTypeLength));
            unsavedChanges = true;
            break;
        case 43: // Set Output 4 waveform type
            outputs[3].SetWaveformType(static_cast<WaveformType>((outputs[3].GetWaveformType() + 1) % WaveformTypeLength));
            unsavedChanges = true;
            break;
        case 44: // Envelope output selection
            envelopeOutputSelect = (envelopeOutputSelect + 1 > 3) ? 2 : envelopeOutputSelect + 1;
            unsavedChanges = true;
            break;
        case 45: // Envelope attack time
            outputs[envelopeOutputSelect].SetAttack(outputs[envelopeOutputSelect].GetAttack() + speedFactor * 2);
            unsavedChanges = true;
            break;
        case 46: // Envelope decay time
            outputs[envelopeOutputSelect].SetDecay(outputs[envelopeOutputSelect].GetDecay() + speedFactor * 2);
            unsavedChanges = true;
            break;
        case 47: // Envelope sustain level
            outputs[envelopeOutputSelect].SetSustain(outputs[envelopeOutputSelect].GetSustain() + speedFactor);
            unsavedChanges = true;
            break;
        case 48: // Envelope release time
            outputs[envelopeOutputSelect].SetRelease(outputs[envelopeOutputSelect].GetRelease() + speedFactor * 2);
            unsavedChanges = true;
            break;
        case 49: // Envelope curve
            outputs[envelopeOutputSelect].SetCurve(outputs[envelopeOutputSelect].GetCurve() + speedFactor * 0.01);
            unsavedChanges = true;
            break;
        case 51: { // CV Input 1 target
            CVTarget tmp = static_cast<CVTarget>((pendingCVInputTarget[0] + 1) % CVTargetLength);
            if (tmp != pendingCVInputTarget[1] || tmp == CVTarget::None) {
                pendingCVInputTarget[0] = tmp;
            } else {
                pendingCVInputTarget[0] = static_cast<CVTarget>((pendingCVInputTarget[0] + 2) % CVTargetLength);
            }
            break;
        }
        case 52: { // CV Input 2 target
            CVTarget tmp = static_cast<CVTarget>((pendingCVInputTarget[1] + 1) % CVTargetLength);
            if (tmp != pendingCVInputTarget[0] || tmp == CVTarget::None) {
                pendingCVInputTarget[1] = tmp;
            } else {
                pendingCVInputTarget[1] = static_cast<CVTarget>((pendingCVInputTarget[1] + 2) % CVTargetLength);
            }
            break;
        }
        case 53: // CV Input 1 attenuation
            CVInputAttenuation[0] = constrain(CVInputAttenuation[0] + speedFactor, 0, 100);
            break;
        case 54: // CV Input 1 offset
            CVInputOffset[0] = constrain(CVInputOffset[0] + speedFactor, 0, 100);
            break;
        case 55: // CV Input 2 attenuation
            CVInputAttenuation[1] = constrain(CVInputAttenuation[1] + speedFactor, 0, 100);
            break;
        case 56: // CV Input 2 offset
            CVInputOffset[1] = constrain(CVInputOffset[1] + speedFactor, 0, 100);
            break;
        case 57: // Quantizer output
            quantizerOutputSelect = (quantizerOutputSelect + 1 > 3) ? 2 : quantizerOutputSelect + 1;
            unsavedChanges = true;
            break;
        case 59: // Quantizer root note
            outputs[quantizerOutputSelect].SetQuantizerNoteIndex(outputs[quantizerOutputSelect].GetQuantizerNoteIndex() + speedFactor);
            unsavedChanges = true;
            break;
        case 60: // Quantizer scale
            outputs[quantizerOutputSelect].SetQuantizerScaleIndex(outputs[quantizerOutputSelect].GetQuantizerScaleIndex() + speedFactor);
            unsavedChanges = true;
            break;
        case 61: // Quantizer octave shift
            outputs[quantizerOutputSelect].SetQuantizerOctaveShift(outputs[quantizerOutputSelect].GetQuantizerOctaveShift() + speedFactor);
            unsavedChanges = true;
            break;
        case 63: // Select save slot
            saveSlot = (saveSlot + 1 > NUM_SLOTS) ? 0 : saveSlot + 1;
            break;
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
    _displayLocked = true;
    delay(10);  // Let Core 1 finish any in-flight HandleDisplay() call (~1ms max)

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

    _displayLocked = false;     // Resume normal Core 1 rendering
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

// Draw a menu position indicator at the right side of the display
void MenuIndicator() {
    displayMgr.DrawMenuIndicator(menuItem, menuItems);
}

void MenuHeader(const char *header) {
    displayMgr.DrawMenuHeader(header);
}

// Handle display drawing
void HandleDisplay() {
    // Sync unsaved changes state to display manager
    displayMgr.SetUnsavedChanges(unsavedChanges);

    // Check for timeout to main screen
    if (displayMgr.ShouldTimeout(menuItem, menuMode)) {
        menuItem = 2;
        menuMode = 0;
        REQUEST_DISPLAY_REFRESH();
    }

    // Only refresh the display if needed and rate-limited
    if (displayRefresh && displayMgr.ShouldUpdate()) {
        // Begin frame prepares the buffer (fast, no I2C)
        displayMgr.BeginFrame();
        MenuIndicator();

        // Draw the menu
        int menuIdx = 1;
        int itemAmount = 2;
        if (menuItem >= menuIdx && menuItem < menuIdx + itemAmount) {
            String s = String(BPM) + "BPM";
            display.setTextSize(3);
            // Centralize the BPM display
            display.setCursor((SCREEN_WIDTH - (s.length() * 18)) / 2, 0);
            display.print(s);
            if (usingExternalClock) {
                display.setTextSize(1);
                display.setCursor(120, 24);
                display.print("E");
            }
            // Draw selection triangle
            if (menuMode == 0 && menuItem == 1) {
                display.drawTriangle(2, 6, 2, 14, 6, 10, 1);
            } else if (menuMode == menuItem) {
                display.fillTriangle(2, 6, 2, 14, 6, 10, 1);
            }

            if (menuMode >= 0 && menuMode <= 2) {
                display.setTextSize(2);
                display.setCursor(44, 27);
                if (menuItem == 2) {
                    display.drawLine(43, 42, 88, 42, 1);
                }
                if (!masterState) {
                    display.fillRoundRect(23, 26, 17, 17, 2, 1);
                    display.print("STOP");
                } else {
                    display.fillTriangle(23, 26, 23, 42, 39, 34, 1);
                    display.print("PLAY");
                }
            }

            // Show a box to each output showing if it's enabled
            display.setTextSize(1);
            for (int i = 0; i < NUM_OUTPUTS; i++) {
                if (outputs[i].GetOutputState()) {
                    display.fillRect((i * 30) + 14, 46, 9, 9, WHITE);
                    display.setCursor((i * 30) + 16, 47);
                    display.setTextColor(BLACK);
                    display.print(i + 1);
                } else {
                    display.drawRect((i * 30) + 14, 46, 9, 9, WHITE);
                    display.setCursor((i * 30) + 16, 47);
                    display.setTextColor(WHITE);
                    display.print(i + 1);
                }
                display.setTextColor(WHITE);
                String s = outputs[i].GetDividerDescription();
                display.setCursor((i * 30) + 13 + (6 - (s.length() * 3)), 56);
                display.print(s);
            }
            RedrawDisplay();
            return;
        }

        // Clock dividers menu
        menuIdx = menuIdx + itemAmount;
        itemAmount = 5;
        if (menuItem >= menuIdx && menuItem < menuIdx + itemAmount) {
            display.setTextSize(1);
            MenuHeader("CLOCK DIVIDERS");
            int yPosition = 20;
            for (int i = 0; i < NUM_OUTPUTS; i++) {
                display.setCursor(10, yPosition);
                display.print("OUTPUT " + String(i + 1) + ":");
                display.setCursor(84, yPosition);
                display.print(outputs[i].GetDividerDescription());
                if (menuItem == i + menuIdx) {
                    if (menuMode == 0) {
                        display.drawTriangle(1, 19 + (i * 9), 1, 27 + (i * 9), 5, 23 + (i * 9), 1);
                    } else if (menuMode == i + menuIdx) {
                        display.fillTriangle(1, 19 + (i * 9), 1, 27 + (i * 9), 5, 23 + (i * 9), 1);
                    }
                }
                yPosition += 9;
            }
            // For external clock divider
            display.setCursor(10, yPosition);
            display.print("EXT. DIV:");
            display.setCursor(84, yPosition);
            display.print(externalDividerDescription[externalDividerIndex]);
            if (menuItem == 7) {
                if (menuMode == 0) {
                    display.drawTriangle(1, 19 + (NUM_OUTPUTS * 9), 1, 27 + (NUM_OUTPUTS * 9), 5, 23 + (NUM_OUTPUTS * 9), 1);
                } else if (menuMode == 7) {
                    display.fillTriangle(1, 19 + (NUM_OUTPUTS * 9), 1, 27 + (NUM_OUTPUTS * 9), 5, 23 + (NUM_OUTPUTS * 9), 1);
                }
            }

            RedrawDisplay();
            return;
        }

        // Clock outputs state menu
        menuIdx = menuIdx + itemAmount;
        itemAmount = 4;
        if (menuItem >= menuIdx && menuItem < menuIdx + itemAmount) {
            display.setTextSize(1);
            MenuHeader("OUTPUT STATE");
            int yPosition = 20;
            for (int i = 0; i < NUM_OUTPUTS; i++) {
                display.setCursor(10, yPosition);
                display.print("OUTPUT " + String(i + 1) + ":");
                display.setCursor(70, yPosition);
                display.print(outputs[i].GetOutputState() ? "ON" : "OFF");
                if (menuItem == i + menuIdx) {
                    if (menuMode == 0) {
                        display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
                    } else if (menuMode == i + menuIdx) {
                        display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
                    }
                }
                yPosition += 9;
            }
            RedrawDisplay();
            return;
        }

        // Pulse Probability menu
        menuIdx = menuIdx + itemAmount;
        itemAmount = 4;
        if (menuItem >= menuIdx && menuItem < menuIdx + itemAmount) {
            display.setTextSize(1);
            MenuHeader("PROBABILITY");
            int yPosition = 20;
            for (int i = 0; i < NUM_OUTPUTS; i++) {
                display.setCursor(10, yPosition);
                display.print("OUTPUT " + String(i + 1) + ":");
                display.setCursor(70, yPosition);
                display.print(outputs[i].GetPulseProbabilityDescription());
                if (menuItem == menuIdx + i) {
                    if (menuMode == 0) {
                        display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
                    } else if (menuMode == menuIdx + i) {
                        display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
                    }
                }
                yPosition += 9;
            }
            RedrawDisplay();
            return;
        }

        // Euclidean rhythm menu
        menuIdx = menuIdx + itemAmount;
        itemAmount = 6;
        if (menuItem >= menuIdx && menuItem < menuIdx + itemAmount) {
            display.setTextSize(1);
            MenuHeader("EUCLIDEAN RHYTHM");
            int yPosition = 20;
            int xPosition = 64;
            display.setCursor(10, yPosition);
            display.print("OUTPUT: ");
            display.setCursor(xPosition, yPosition);
            display.print(String(euclideanOutputSelect + 1));
            if (menuItem == menuIdx && menuMode == 0) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            } else if (menuMode == menuIdx) {
                display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }
            yPosition += 9;
            display.setCursor(10, yPosition);
            display.print("ENABLED: ");
            display.setCursor(xPosition, yPosition);
            display.print(String(outputs[euclideanOutputSelect].GetEuclidean() ? "YES" : "NO"));
            if (menuItem == menuIdx + 1 && menuMode == 0) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            } else if (menuMode == menuIdx + 1) {
                display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }
            yPosition += 9;
            display.setCursor(10, yPosition);
            display.print("STEPS: ");
            display.setCursor(xPosition, yPosition);
            display.print(String(outputs[euclideanOutputSelect].GetEuclideanSteps()));
            if (menuItem == menuIdx + 2 && menuMode == 0) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            } else if (menuMode == menuIdx + 2) {
                display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }
            yPosition += 9;
            display.setCursor(10, yPosition);
            display.print("HITS: ");
            display.setCursor(xPosition, yPosition);
            display.print(String(outputs[euclideanOutputSelect].GetEuclideanTriggers()));
            if (menuItem == menuIdx + 3 && menuMode == 0) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            } else if (menuMode == menuIdx + 3) {
                display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }
            yPosition += 9;
            display.setCursor(10, yPosition);
            display.print("ROT:");
            display.print(String(outputs[euclideanOutputSelect].GetEuclideanRotation()));
            if (menuItem == menuIdx + 4 && menuMode == 0) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            } else if (menuMode == menuIdx + 4) {
                display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }

            display.setCursor(xPosition, yPosition);
            display.print("PAD:");
            display.print(String(outputs[euclideanOutputSelect].GetEuclideanPadding()));
            if (menuItem == menuIdx + 5 && menuMode == 0) {
                display.drawTriangle(xPosition - 8, yPosition - 1, xPosition - 8, yPosition + 7, xPosition - 4, yPosition + 3, 1);
            } else if (menuMode == menuIdx + 5) {
                display.fillTriangle(xPosition - 8, yPosition - 1, xPosition - 8, yPosition + 7, xPosition - 4, yPosition + 3, 1);
            }

            // Draw the Euclidean rhythm pattern for the selected output
            if (outputs[euclideanOutputSelect].GetEuclidean()) {
                display.fillTriangle(90, 10, 94, 10, 92, 14, WHITE);
                yPosition = 15;
                int euclideanSteps = outputs[euclideanOutputSelect].GetEuclideanSteps();
                int euclideanPadding = outputs[euclideanOutputSelect].GetEuclideanPadding();
                for (int i = 0; i < euclideanSteps + euclideanPadding && i < 47; i++) {
                    int column = i / 8;
                    int row = i % 8;
                    display.setCursor(90 + (column * 6), yPosition + (row * 6));
                    if (i < euclideanSteps && outputs[euclideanOutputSelect].GetRhythmStep(i)) {
                        display.fillRect(90 + (column * 6), yPosition + (row * 6), 5, 5, WHITE);
                    } else {
                        if (i < euclideanSteps) {
                            display.drawRect(90 + (column * 6), yPosition + (row * 6), 5, 5, WHITE);
                        } else {
                            display.drawRect(90 + (column * 6), yPosition + (row * 6), 5, 5, WHITE);
                            display.writePixel(90 + (column * 6) + 2, yPosition + (row * 6) + 2, WHITE);
                        }
                    }
                }
                if (euclideanSteps + euclideanPadding > 47) {
                    display.fillTriangle(120, 57, 124, 57, 122, 61, WHITE);
                }
            }
            RedrawDisplay();
            return;
        }

        // Swing amount menu
        menuIdx = menuIdx + itemAmount;
        itemAmount = 8;
        if (menuItem >= menuIdx && menuItem < menuIdx + itemAmount) {
            display.setTextSize(1);
            MenuHeader("OUTPUT SWING");
            int yPosition = 20;
            display.setCursor(64, yPosition);
            display.println("AMT");
            display.setCursor(94, yPosition);
            display.println("EVERY");
            yPosition += 9;
            for (int i = 0; i < NUM_OUTPUTS; i++) {
                display.setCursor(10, yPosition);
                display.print("OUTPUT " + String(i + 1) + ":");
                display.setCursor(70, yPosition);
                display.print(outputs[i].GetSwingAmountDescription());
                display.setCursor(100, yPosition);
                display.print(outputs[i].GetSwingEvery());

                if (menuItem % 2 == 0) {
                    display.fillTriangle(59, 20, 59, 26, 62, 23, 1);
                } else {
                    display.fillTriangle(89, 20, 89, 26, 92, 23, 1);
                }

                if (menuItem - menuIdx == i * 2 || menuItem - menuIdx == i * 2 + 1) {
                    if (menuMode == 0) {
                        display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
                    } else {
                        display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
                    }
                }
                yPosition += 9;
            }
            RedrawDisplay();
            return;
        }

        // Phase shift menu
        menuIdx = menuIdx + itemAmount;
        itemAmount = 4;
        if (menuItem >= menuIdx && menuItem < menuIdx + itemAmount) {
            display.setTextSize(1);
            MenuHeader("PHASE SHIFT");
            int yPosition = 0;
            yPosition = 20;
            for (int i = 0; i < NUM_OUTPUTS; i++) {
                display.setCursor(10, yPosition);
                display.print("OUTPUT " + String(i + 1) + ":");
                display.setCursor(70, yPosition);
                display.print(outputs[i].GetPhaseDescription());
                if (menuItem == menuIdx + i) {
                    if (menuMode == 0) {
                        display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
                    } else if (menuMode == menuIdx + i) {
                        display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
                    }
                }
                yPosition += 9;
            }
            RedrawDisplay();
            return;
        }

        // Duty cycle menu
        menuIdx = menuIdx + itemAmount;
        itemAmount = 4;
        if (menuItem >= menuIdx && menuItem < menuIdx + itemAmount) {
            display.setTextSize(1);
            MenuHeader("OUTPUT SETTINGS");
            int yPosition = 21;

            for (int i = 0; i < itemAmount; i++) {
                display.setCursor(10, yPosition);
                display.print("OUT " + String(i + 1) + " DUTY: ");
                display.print(outputs[i].GetDutyCycleDescription());
                if (menuItem == menuIdx + i && menuMode == 0) {
                    display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
                } else if (menuMode == menuIdx + i) {
                    display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
                }
                yPosition += 9;
            }

            RedrawDisplay();
            return;
        }

        // Level/Offset/Waveform control menu
        menuIdx = menuIdx + itemAmount;
        itemAmount = 6;
        if (menuItem >= menuIdx && menuItem < menuIdx + itemAmount) {
            display.setTextSize(1);
            MenuHeader("OUTPUT SETTINGS");
            int yPosition = 12;

            // Levels and offsets
            display.setCursor(70, yPosition);
            display.println("LVL");
            display.setCursor(100, yPosition);
            display.println("OFF");
            if (menuItem == menuIdx || menuItem == menuIdx + 2) {
                display.fillTriangle(65, yPosition, 65, yPosition + 6, 68, yPosition + 3, 1);
            }
            if (menuItem == menuIdx + 1 || menuItem == menuIdx + 3) {
                display.fillTriangle(95, yPosition, 95, yPosition + 6, 98, yPosition + 3, 1);
            }
            yPosition += 9;
            for (int i = 2; i < NUM_OUTPUTS; i++) {
                display.setCursor(10, yPosition);
                display.print("OUTPUT " + String(i + 1) + ":");
                display.setCursor(70, yPosition);
                display.print(outputs[i].GetLevelDescription());
                display.setCursor(100, yPosition);
                display.print(outputs[i].GetOffsetDescription());

                if (menuItem == menuIdx + (i - 2) * 2 || menuItem == menuIdx + 1 + (i - 2) * 2) {
                    if (menuMode == 0) {
                        display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
                    } else {
                        display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
                    }
                }
                yPosition += 9;
            }
            // Waveform type
            yPosition += 7;
            display.setCursor(10, yPosition);
            display.println("OUT 3 WAV:");
            display.setCursor(70, yPosition);
            display.print(outputs[2].GetWaveformTypeDescription());
            if (menuItem == menuIdx + 4 && menuMode == 0) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            } else if (menuMode == menuIdx + 4) {
                display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }
            yPosition += 9;
            display.setCursor(10, yPosition);
            display.println("OUT 4 WAV:");
            display.setCursor(70, yPosition);
            display.print(outputs[3].GetWaveformTypeDescription());
            if (menuItem == menuIdx + 5 && menuMode == 0) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            } else if (menuMode == menuIdx + 5) {
                display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }

            RedrawDisplay();
            return;
        }

        // Envelope settings
        menuIdx = menuIdx + itemAmount;
        itemAmount = 7;
        if (menuItem >= menuIdx && menuItem < menuIdx + itemAmount) {
            display.setTextSize(1);
            MenuHeader("ENVELOPE SETTINGS");
            int yPosition = 10;
            int xPosition = 64;
            display.setCursor(10, yPosition);
            display.print("OUTPUT: ");
            display.setCursor(xPosition, yPosition);
            display.print(String(envelopeOutputSelect + 1));
            if (menuItem == menuIdx && menuMode == 0) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            } else if (menuMode == menuIdx) {
                display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }
            yPosition += 9;
            display.setCursor(10, yPosition);
            display.print("Attack: ");
            display.setCursor(xPosition, yPosition);
            display.print(String(outputs[envelopeOutputSelect].GetAttackDescription()));
            if (menuItem == menuIdx + 1 && menuMode == 0) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            } else if (menuMode == menuIdx + 1) {
                display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }
            yPosition += 9;
            display.setCursor(10, yPosition);
            display.print("Decay: ");
            display.setCursor(xPosition, yPosition);
            display.print(String(outputs[envelopeOutputSelect].GetDecayDescription()));
            if (menuItem == menuIdx + 2 && menuMode == 0) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            } else if (menuMode == menuIdx + 2) {
                display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }
            yPosition += 9;
            display.setCursor(10, yPosition);
            display.print("Sustain: ");
            display.setCursor(xPosition, yPosition);
            display.print(String(outputs[envelopeOutputSelect].GetSustainDescription()));
            if (menuItem == menuIdx + 3 && menuMode == 0) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            } else if (menuMode == menuIdx + 3) {
                display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }
            yPosition += 9;
            display.setCursor(10, yPosition);
            display.print("Release: ");
            display.setCursor(xPosition, yPosition);
            display.print(String(outputs[envelopeOutputSelect].GetReleaseDescription()));
            if (menuItem == menuIdx + 4 && menuMode == 0) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            } else if (menuMode == menuIdx + 4) {
                display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }
            yPosition += 9;
            display.setCursor(10, yPosition);
            display.print("Curv:");
            display.print(String(outputs[envelopeOutputSelect].GetCurveDescription()));
            if (menuItem == menuIdx + 5 && menuMode == 0) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            } else if (menuMode == menuIdx + 5) {
                display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }

            display.setCursor(64, yPosition);
            display.print("Retr:");
            display.print(String(outputs[envelopeOutputSelect].GetRetriggerDescription()));
            if (menuItem == menuIdx + 6 && menuMode == 0) {
                display.drawTriangle(56, yPosition - 1, 56, yPosition + 7, 60, yPosition + 3, 1);
            } else if (menuMode == menuIdx + 6) {
                display.fillTriangle(56, yPosition - 1, 56, yPosition + 7, 60, yPosition + 3, 1);
            }

            RedrawDisplay();
            return;
        }

        // CV input target menu
        menuIdx = menuIdx + itemAmount;
        itemAmount = 6;
        if (menuItem >= menuIdx && menuItem < menuIdx + itemAmount) {
            display.setTextSize(1);
            MenuHeader("CV INPUT TARGETS");
            int yPosition = 20;
            display.setCursor(10, yPosition);
            display.print("CV 1: ");
            display.print(CVTargetDescription[menuMode == 51 ? pendingCVInputTarget[0] : CVInputTarget[0]]);

            if (menuItem == menuIdx && menuMode == 0) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            } else if (menuMode == menuIdx) {
                display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }

            yPosition += 9;
            display.setCursor(10, yPosition);
            display.print("CV 2: ");
            display.print(CVTargetDescription[menuMode == 52 ? pendingCVInputTarget[1] : CVInputTarget[1]]);
            if (menuItem == menuIdx + 1 && menuMode == 0) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            } else if (menuMode == menuIdx + 1) {
                display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }
            yPosition += 9;
            // Levels and offsets
            display.setCursor(60, yPosition);
            display.println("ATTN");
            display.setCursor(100, yPosition);
            display.println("OFF");
            if (menuItem == menuIdx + 2 || menuItem == menuIdx + 4) {
                display.fillTriangle(55, yPosition, 55, yPosition + 6, 58, yPosition + 3, 1);
            }
            if (menuItem == menuIdx + 3 || menuItem == menuIdx + 5) {
                display.fillTriangle(95, yPosition, 95, yPosition + 6, 98, yPosition + 3, 1);
            }
            yPosition += 9;
            for (int i = 0; i < NUM_CV_INS; i++) {
                display.setCursor(10, yPosition);
                display.print("CV " + String(i + 1) + ":");
                display.setCursor(60, yPosition);
                display.print(String(CVInputAttenuation[i]) + "%");
                display.setCursor(100, yPosition);
                display.print(String(CVInputOffset[i]) + "%");

                if (menuItem == menuIdx + 2 + (i) * 2 || menuItem == menuIdx + 3 + (i) * 2) {
                    if (menuMode == 0) {
                        display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
                    } else {
                        display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
                    }
                }
                yPosition += 9;
            }

            RedrawDisplay();
            return;
        }

        // Quantize menu
        menuIdx = menuIdx + itemAmount;
        itemAmount = 5;
        if (menuItem >= menuIdx && menuItem < menuIdx + itemAmount) {
            display.setTextSize(1);
            MenuHeader("QUANTIZE SETTINGS");
            int yPosition = 20;
            display.setCursor(10, yPosition);
            display.print("OUTPUT: ");
            display.print(String(quantizerOutputSelect + 1));
            if (menuItem == menuIdx && menuMode == 0) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            } else if (menuMode == menuIdx) {
                display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }
            yPosition += 9;
            display.setCursor(10, yPosition);
            display.print("ENABLED: ");
            display.print(String(outputs[quantizerOutputSelect].GetQuantizerEnable() ? "YES" : "NO"));
            if (menuItem == menuIdx + 1 && menuMode == 0) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            } else if (menuMode == menuIdx + 1) {
                display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }
            yPosition += 9;
            display.setCursor(10, yPosition);
            display.print("ROOT NOTE: ");
            display.print(String(outputs[quantizerOutputSelect].GetQuantizerNoteDescription()));
            if (menuItem == menuIdx + 2 && menuMode == 0) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            } else if (menuMode == menuIdx + 2) {
                display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }
            yPosition += 9;
            display.setCursor(10, yPosition);
            display.print("SCALE: ");
            display.print(String(outputs[quantizerOutputSelect].GetQuantizerScaleDescription()));
            if (menuItem == menuIdx + 3 && menuMode == 0) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            } else if (menuMode == menuIdx + 3) {
                display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }
            yPosition += 9;
            display.setCursor(10, yPosition);
            display.print("OCT TRANSPOSE:");
            display.print(String(outputs[quantizerOutputSelect].GetQuantizerOctaveShiftDescription()));
            if (menuItem == menuIdx + 4 && menuMode == 0) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            } else if (menuMode == menuIdx + 4) {
                display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }

            RedrawDisplay();
            return;
        }

        // Other settings
        menuIdx = menuIdx + itemAmount;
        itemAmount = 5;
        if (menuItem >= menuIdx && menuItem < menuIdx + itemAmount) {
            display.setTextSize(1);
            int yPosition = 9;
            // Tap tempo
            display.setCursor(10, yPosition);
            display.print("TAP TEMPO");
            display.print(" (" + String(BPM) + " BPM)");
            if (menuItem == menuIdx) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }
            yPosition += 9;
            yPosition += 9;
            // Save
            display.setCursor(10, yPosition);
            display.print("PRESET SLOT: ");
            display.print(saveSlot);
            if (menuItem == menuIdx + 1 && menuMode == 0) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            } else if (menuMode == menuIdx + 1) {
                display.fillTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }
            yPosition += 9;
            display.setCursor(10, yPosition);
            display.print("SAVE");
            if (menuItem == menuIdx + 2) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }
            yPosition += 9;
            display.setCursor(10, yPosition);
            display.print("LOAD");
            if (menuItem == menuIdx + 3) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }
            yPosition += 9;
            // Load default settings
            display.setCursor(10, yPosition);
            display.print("LOAD DEFAULTS");
            if (menuItem == menuIdx + 4) {
                display.drawTriangle(1, yPosition - 1, 1, yPosition + 7, 5, yPosition + 3, 1);
            }

            RedrawDisplay();
            return;
        }
    }

    // EndDisplayMeasurement removed - now measured in RedrawDisplay()
}

// Tap tempo function
static unsigned long lastTapTime = 0;
static unsigned long tapTimes[3] = {0, 0, 0};
static int tapIndex = 0;
void SetTapTempo() {
    if (usingExternalClock) {
        return;
    }
    unsigned long currentMillis = millis();
    if (currentMillis - lastTapTime > 2000) {
        tapIndex = 0;
    }
    if (tapIndex < 3) {
        tapTimes[tapIndex] = currentMillis;
        tapIndex++;
        lastTapTime = currentMillis;
    }
    if (tapIndex == 3) {
        unsigned long averageTime = (tapTimes[2] - tapTimes[0]) / 2;
        unsigned int newBPM = 60000 / averageTime;
        tapIndex++;
        UpdateBPM(newBPM);
        unsavedChanges = true;
    }
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

// Adjust the ADC readings
void AdjustADCReadings(int CV_IN_PIN, int ch) {
    // Apply calibration
    float calibratedReading = max((analogRead(CV_IN_PIN) - ADCOffset[ch]) * ADCCal[ch], 0.0f);
    channelADC[ch] = calibratedReading;
}

void HandleCVInputs() {
    for (int i = 0; i < NUM_CV_INS; i++) {
        oldChannelADC[i] = channelADC[i];
        AdjustADCReadings(CV_IN_PINS[i], i);
        ONE_POLE(channelADC[i], oldChannelADC[i], 0.5f);
        if (abs(channelADC[i] - oldChannelADC[i]) > 10) {
            HandleCVTarget(i, channelADC[i], CVInputTarget[i]);
        }
    }
}

volatile bool lastResetState = false;
// Handle the CV target based on the CV value
void HandleCVTarget(int ch, float CVValue, CVTarget cvTarget) {
    // Attenuate and offset the CVValue
    float attenuatedValue = CVValue * ((100 - CVInputAttenuation[ch]) / 100.0f);
    float offsetValue = attenuatedValue + (CVInputOffset[ch] / 100.0f * MAXDAC);
    CVValue = constrain(offsetValue, 0, MAXDAC);

    // CRITICAL SECTION: Protect parameter updates that affect timing
    // Prevents race conditions with ISR reading output parameters
    noInterrupts();

    // DEBUG_PRINT("Ch: " + String(ch) + " CV Target: " + String(cvTarget) + " CV Value: " + String(CVValue) + "\n");
    switch (cvTarget) {
    case CVTarget::None:
        break;
    case CVTarget::StartStop:
        if (CVValue > MAXDAC / 2) {
            SetMasterState(true);
        } else {
            SetMasterState(false);
        }
        break;
    case CVTarget::Reset:
        if (CVValue > MAXDAC / 2 && !lastResetState) {
            tickCounter = 0;
            externalTickCounter = 0;
            lastResetState = true;
        } else if (CVValue < MAXDAC / 2) {
            lastResetState = false;
        }
        break;
    case CVTarget::SetBPM:
        // Convert float value to BPM range
        UpdateBPM(map(CVValue, 0, MAXDAC, minBPM, maxBPM));
        break;
    case CVTarget::Div1:
        outputs[0].SetDivider(map(CVValue, 0, MAXDAC, 0, outputs[0].GetDividerAmounts()));
        break;
    case CVTarget::Div2:
        outputs[1].SetDivider(map(CVValue, 0, MAXDAC, 0, outputs[1].GetDividerAmounts()));
        break;
    case CVTarget::Div3:
        outputs[2].SetDivider(map(CVValue, 0, MAXDAC, 0, outputs[2].GetDividerAmounts()));
        break;
    case CVTarget::Div4:
        outputs[3].SetDivider(map(CVValue, 0, MAXDAC, 0, outputs[3].GetDividerAmounts()));
        break;
    case CVTarget::Output1Prob:
        outputs[0].SetPulseProbability(map(CVValue, 0, MAXDAC, 1, 100));
        break;
    case CVTarget::Output2Prob:
        outputs[1].SetPulseProbability(map(CVValue, 0, MAXDAC, 1, 100));
        break;
    case CVTarget::Output3Prob:
        outputs[2].SetPulseProbability(map(CVValue, 0, MAXDAC, 1, 100));
        break;
    case CVTarget::Output4Prob:
        outputs[3].SetPulseProbability(map(CVValue, 0, MAXDAC, 1, 100));
        break;
    case CVTarget::Swing1Amount:
        outputs[0].SetSwingAmount(map(CVValue, 0, MAXDAC, 0, outputs[0].GetSwingAmounts()));
        break;
    case CVTarget::Swing1Every:
        outputs[0].SetSwingEvery(map(CVValue, 0, MAXDAC, 1, outputs[0].GetSwingEveryAmounts()));
        break;
    case CVTarget::Swing2Amount:
        outputs[1].SetSwingAmount(map(CVValue, 0, MAXDAC, 0, outputs[1].GetSwingAmounts()));
        break;
    case CVTarget::Swing2Every:
        outputs[1].SetSwingEvery(map(CVValue, 0, MAXDAC, 1, outputs[1].GetSwingEveryAmounts()));
        break;
    case CVTarget::Swing3Amount:
        outputs[2].SetSwingAmount(map(CVValue, 0, MAXDAC, 0, outputs[2].GetSwingAmounts()));
        break;
    case CVTarget::Swing3Every:
        outputs[2].SetSwingEvery(map(CVValue, 0, MAXDAC, 1, outputs[2].GetSwingEveryAmounts()));
        break;
    case CVTarget::Swing4Amount:
        outputs[3].SetSwingAmount(map(CVValue, 0, MAXDAC, 0, outputs[3].GetSwingAmounts()));
        break;
    case CVTarget::Swing4Every:
        outputs[3].SetSwingEvery(map(CVValue, 0, MAXDAC, 1, outputs[3].GetSwingEveryAmounts()));
        break;
    case CVTarget::Output3Offset:
        outputs[2].SetOffset(map(CVValue, 0, MAXDAC, 0, 100));
        break;
    case CVTarget::Output4Offset:
        outputs[3].SetOffset(map(CVValue, 0, MAXDAC, 0, 100));
        break;
    case CVTarget::Output3Level:
        outputs[2].SetLevel(map(CVValue, 0, MAXDAC, 0, 100));
        break;
    case CVTarget::Output4Level:
        outputs[3].SetLevel(map(CVValue, 0, MAXDAC, 0, 100));
        break;
    case CVTarget::Output3Waveform:
        outputs[2].SetWaveformType(static_cast<WaveformType>(map(CVValue, 0, MAXDAC, 0, WaveformTypeLength)));
        break;
    case CVTarget::Output4Waveform:
        outputs[3].SetWaveformType(static_cast<WaveformType>(map(CVValue, 0, MAXDAC, 0, WaveformTypeLength)));
        break;
    case CVTarget::Output1Duty:
        outputs[0].SetDutyCycle(map(CVValue, 0, MAXDAC, 0, 100));
        break;
    case CVTarget::Output2Duty:
        outputs[1].SetDutyCycle(map(CVValue, 0, MAXDAC, 0, 100));
        break;
    case CVTarget::Output3Duty:
        outputs[2].SetDutyCycle(map(CVValue, 0, MAXDAC, 0, 100));
        break;
    case CVTarget::Output4Duty:
        outputs[3].SetDutyCycle(map(CVValue, 0, MAXDAC, 0, 100));
        break;
    case CVTarget::Envelope1:
        outputs[2].SetExternalTrigger(CVValue > MAXDAC / 2);
        break;
    case CVTarget::Envelope2:
        outputs[3].SetExternalTrigger(CVValue > MAXDAC / 2);
        break;
    case CVTarget::Output3:
        outputs[2].SetCVValue(CVValue);
        break;
    case CVTarget::Output4:
        outputs[3].SetCVValue(CVValue);
        break;
    }

    interrupts(); // End critical section

    // Update the display if the CV target is not None
    // if (cvTarget != CVTarget::None && menuMode == 0 && millis() - lastDisplayUpdateTime > 1000) {
    //     displayRefresh = 1;
    //     lastDisplayUpdateTime = millis();
    // }
}

// External clock interrupt service routine
void ClockReceived() {
    unsigned long currentTime = millis();
    // Debounce: ignore interrupts that occur too close together (less than 1ms)
    if (currentTime - lastClockInterruptTime < 1) {
        return;
    }

    unsigned long interval = currentTime - lastClockInterruptTime;
    lastClockInterruptTime = currentTime;

    // Ignore obviously wrong intervals (too short or too long)
    if (interval < 10 || interval > 2000) {
        return;
    }

    static unsigned long intervals[3] = {0, 0, 0};
    static int intervalIndex = 0;

    intervals[intervalIndex] = interval;
    intervalIndex = (intervalIndex + 1) % 3;

    // Calculate average interval, excluding outliers
    unsigned long averageInterval = 0;
    int validIntervals = 0;
    for (int i = 0; i < 3; i++) {
        if (intervals[i] > 0 && abs((long)intervals[i] - (long)interval) < interval / 2) {
            averageInterval += intervals[i];
            validIntervals++;
        }
    }

    if (validIntervals > 0) {
        averageInterval /= validIntervals;
    } else {
        return;
    }

    // Divide the external clock signal by the selected divider
    if (externalTickCounter % externalClockDividers[externalDividerIndex] == 0) {
        if (averageInterval > 0) {
            clockInterval = averageInterval;
            unsigned int newBPM = 60000 / (averageInterval * externalClockDividers[externalDividerIndex]);
            // Add hysteresis to BPM changes
            if (abs((int)newBPM - (int)BPM) > 3) {
                UpdateBPM(newBPM);
                displayRefresh = 1;
                DEBUG_PRINT("External clock connected");
            }
        }

        // Update outputs
        noInterrupts(); // Critical section
        for (int i = 0; i < NUM_OUTPUTS; i++) {
            outputs[i].SetExternalClock(true);
            outputs[i].IncrementInternalCounter();
        }
        usingExternalClock = true;
        tickCounter = 0;
        interrupts();
    }
    externalTickCounter++;
}

// Called on loop to check if the external clock is still connected and revert to internal clock if not
void HandleExternalClock() {
    unsigned long currentTime = millis();
    if (usingExternalClock && (currentTime - lastClockInterruptTime) > 2000) {
        usingExternalClock = false;
        BPM = lastInternalBPM;
        UpdateBPM(BPM);
        for (int i = 0; i < NUM_OUTPUTS; i++) {
            outputs[i].SetExternalClock(false);
        }
        displayRefresh = 1;
        DEBUG_PRINT("External clock disconnected");
    }
}

// Set the hardware timer based on the BPM
void UpdateBPM(unsigned int newBPM) {
    BPM = constrain(newBPM, minBPM, maxBPM);
#if defined(ARDUINO_ARCH_RP2040)
    cancel_repeating_timer(&_clockTimer);
    int64_t intervalUs = 60000000LL / BPM / PPQN;
    add_repeating_timer_us(-intervalUs, [](repeating_timer_t *) -> bool { ClockPulse(); return true; }, nullptr, &_clockTimer);
#else
    TimerTcc0.setPeriod(60L * 1000 * 1000 / BPM / PPQN / 4);
#endif
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

void ClockPulse() { // Inside the interrupt
    // CRITICAL: This ISR must not be delayed by encoder interrupts
    // The encoder library's ENCODER_OPTIMIZE_INTERRUPTS uses pin change interrupts
    // which can interfere with precise waveform timing
    metrics.BeginISRMeasurement();

    // Calculate pulse state for all outputs
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        outputs[i].Pulse(PPQN, tickCounter);
    }
    tickCounter++;

#if !defined(ARDUINO_ARCH_RP2040)
    // SAMD21: outputs 0-1 are DigitalOut (fast digitalWrite — safe in ISR)
    // DAC outputs 2-3 are updated in main loop HandleOutputs()
    SetPin(0, outputs[0].GetOutputLevel());
    SetPin(1, outputs[1].GetOutputLevel());
#endif
    // RP2040: ALL outputs are MCP4728 I2C DAC — I2C is interrupt-driven and
    // MUST NOT be called from an interrupt context (deadlock). All 4 outputs
    // are flushed in HandleOutputs() on the main loop instead.

    metrics.EndISRMeasurement();
}

void UpdateParameters(LoadSaveParams p) {
    BPM = constrain(p.BPM, minBPM, maxBPM);
    externalDividerIndex = p.externalClockDivIdx;
    // Serial.println(p.divIdx[0]);
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        outputs[i].SetDivider(p.divIdx[i]);
        outputs[i].SetDutyCycle(p.dutyCycle[i]);
        outputs[i].SetOutputState(p.outputState[i]);
        outputs[i].SetLevel(p.outputLevel[i]);
        outputs[i].SetOffset(p.outputOffset[i]);
        outputs[i].SetSwingAmount(p.swingIdx[i]);
        outputs[i].SetSwingEvery(p.swingEvery[i]);
        outputs[i].SetPulseProbability(p.pulseProbability[i]);
        outputs[i].SetEuclideanParams(p.euclideanParams[i]);
        outputs[i].SetPhase(p.phaseShift[i]);
        outputs[i].SetWaveformType(static_cast<WaveformType>(p.waveformType[i]));
        outputs[i].SetEnvelopeParams(p.envParams[i]);
        outputs[i].SetQuantizerParams(p.quantizerParams[i]);
    }
    for (int i = 0; i < NUM_CV_INS; i++) {
        CVInputTarget[i] = static_cast<CVTarget>(p.CVInputTarget[i]);
        CVInputAttenuation[i] = p.CVInputAttenuation[i];
        CVInputOffset[i] = p.CVInputOffset[i];
    }
}

// Initialize the hardware timer
void InitializeTimer() {
#if defined(ARDUINO_ARCH_RP2040)
    int64_t intervalUs = 60000000LL / BPM / PPQN;
    if (intervalUs <= 0) intervalUs = 60000000LL / 120 / PPQN;  // fallback to 120 BPM
    add_repeating_timer_us(-intervalUs, [](repeating_timer_t *) -> bool {
        ClockPulse();
        return true;
    }, nullptr, &_clockTimer);
#else
    TimerTcc0.initialize();
    TimerTcc0.attachInterrupt(ClockPulse);
    // Set high priority for the timer interrupt if your platform supports it
    NVIC_SetPriority(TCC0_IRQn, 0); // Highest priority (0)
#endif
}

void setup() {
    Serial.begin(115200);

#if defined(ARDUINO_ARCH_RP2040)
    delay(500);  // Give USB-CDC time to enumerate
    encoder.begin();          // Deferred pin init for RP2040 (safe here, after runtime ready)
    InitWire();               // Configure SDA/SCL and Wire.begin() — shared by display + DAC
#endif

    EEPROMInit();

    // Initialize I/O pins (ADC resolution, input pins, encoder button)
    InitIO();

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

    // Load settings from flash memory (slot 0) or set defaults
    LoadSaveParams p = Load(0);
    UpdateParameters(p);

    // Initialize timer BEFORE attaching external clock interrupt.
    // ClockReceived() calls UpdateBPM() which calls cancel_repeating_timer();
    // the timer struct must be valid before any interrupt can fire.
    InitializeTimer();
    UpdateBPM(BPM);

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

    HandleOutputs();
    HandleEncoderClick();
    HandleEncoderPosition();
    HandleCVInputs();
    HandleExternalClock();

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
    Serial.println("Core 1 started: display GFX + flush engine (Wire) running.");
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
