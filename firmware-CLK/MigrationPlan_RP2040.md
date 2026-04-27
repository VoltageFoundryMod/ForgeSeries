# ClockForge V2 — RP2040 Migration Plan

## Confirmed Design Decisions

All hardware details confirmed. Decisions below are final and reflected throughout the plan.

| #   | Topic                  | Decision                                                                                                                                                                                                                                                                         |
| --- | ---------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | DAC part               | **MCP4728** — schematic label "MCP4758" is a typo                                                                                                                                                                                                                                |
| 2   | Output routing         | **All 4 outputs via MCP4728 → MCP6004 op-amp only**. No RP2040 GPIO pins used for output signals. D2–D5 references in earlier ref doc are incorrect.                                                                                                                             |
| 3   | Clock trigger (IN1/A0) | **Interrupt-driven digital trigger** from other Eurorack modules. A0 is connected but only used via `digitalPinToInterrupt()` — not read as analog CV.                                                                                                                           |
| 4   | CV modulation inputs   | **IN2 (A1), IN3 (A2)** are the 2 modulation CVs. `NUM_CV_INS = 2`. Clock trigger handled completely separately.                                                                                                                                                                  |
| 5   | Output voltage adjust  | **Hardware trimpots (RV1–RV4)** on each output set the output voltage range. Firmware only needs a **calibration helper screen** that sets DAC to full scale (4095) so the user can trim each pot to exactly 5V with a multimeter. No digital offset storage needed for outputs. |

---

## Current vs Target Architecture Summary

| Aspect       | Current (SAMD21)                      | Target (RP2040)                                |
| ------------ | ------------------------------------- | ---------------------------------------------- |
| MCU          | SAMD21 @ 48MHz, 32KB RAM              | RP2040 @ 133MHz, 264KB RAM                     |
| Cores        | 1 — everything serialized             | 2 — UI on Core 0, engine on Core 1             |
| DAC          | Internal DAC (1ch) + MCP4725 (1ch)    | MCP4728 (4ch, 12-bit, I2C)                     |
| Gate outputs | 2× digital GPIO (inverted logic)      | 0 — all 4 outputs through MCP4728              |
| CV outputs   | 2 (Out3/4 only)                       | 4 (all outputs)                                |
| CV inputs    | 2 modulation inputs                   | 2 modulation (IN2/IN3) + 1 clock trigger (IN1) |
| Timer        | TimerTCC0 (SAMD21-specific)           | `repeating_timer` (Pico SDK)                   |
| Storage      | FlashStorage library                  | arduino-pico EEPROM emulation                  |
| Framework    | Arduino + atmelsam                    | Arduino + arduino-pico (Earle Philhower)       |
| Display      | Adafruit SSD1306                      | Adafruit SSD1306 (keep initially)              |
| I2C sharing  | No contention — separate bus accesses | Mutex required (DAC + display share bus)       |

---

## Files: What Changes vs What Can Be Kept

| File                      | Status              | Reason                                                                        |
| ------------------------- | ------------------- | ----------------------------------------------------------------------------- |
| `platformio.ini`          | Add new env         | RP2040 platform + new libs                                                    |
| `lib/pinouts.hpp`         | Rewrite             | Completely different pin assignments and I/O counts                           |
| `lib/boardIO.hpp`         | Rewrite             | No SAMD21 ADC regs, no internal DAC, no inverted gate logic, MCP4728          |
| `lib/loadsave.hpp`        | Rewrite             | Replace FlashStorage, configurable NUM_SLOTS, separate CalibrationData struct |
| `lib/displayManager.hpp`  | Update              | Add mutex parameter for safe Core 0 display calls                             |
| `lib/metrics.hpp`         | Update              | RP2040 RAM calculation, Core 1 timing, remove mallinfo                        |
| `lib/euclidean.hpp`       | Keep as-is          | No hardware dependency                                                        |
| `lib/quantizer.cpp`       | Keep as-is          | No hardware dependency                                                        |
| `lib/scales.cpp`          | Keep as-is          | No hardware dependency                                                        |
| `lib/utils.hpp`           | Keep as-is          | No hardware dependency                                                        |
| `lib/splash.hpp`          | Keep as-is          | Bitmap data only                                                              |
| `lib/outputs.hpp`         | Move + update       | Move from src/ to lib/; remove DigitalOut type; add sine LUT                  |
| `lib/clockEngine.hpp`     | **New** (Phase 12b) | Timer, BPM, external clock logic extracted from main.cpp                      |
| `lib/cvInputs.hpp`        | **New** (Phase 12c) | CVTarget enum, CV handlers, calibration, extracted from main.cpp              |
| `lib/presetManager.hpp`   | **New** (Phase 12d) | UpdateParameters, save/load helpers, extracted from main.cpp                  |
| `lib/menuDefinitions.hpp` | **New** (Phase 13)  | Data-driven MenuItem array — single source of truth for all menu items        |
| `lib/menuHandlers.hpp`    | **New** (Phase 13)  | GetterFn/SetterFn/ActionFn implementations, template helpers                  |
| `src/main.cpp`            | Major rewrite       | Dual-core split, then shrinks to ~150 lines after modularization              |
| `src/version.hpp`         | Update              | Bump to V2.0                                                                  |

---

## Migration Phases

Each phase is independently buildable and testable. Complete and verify each before starting the next.

### Progress Summary

- [x] Phase 1 — Build System & Platform Setup
- [x] Phase 2 — Pin Assignments (pinouts.hpp)
- [x] Phase 3 — Hardware I/O Abstraction (boardIO.hpp)
- [x] Phase 4 — Storage System (loadsave.hpp)
- [x] Phase 5 — Output Engine Updates (outputs.hpp)
- [x] Phase 6 — Dual-Core Architecture (main.cpp)
- [ ] Phase 7 — CV Input Updates
- [x] Phase 8 — Calibration System
- [x] Phase 9a — Unsaved changes indicator
- [ ] Phase 9b — Output activity indicators (reverted, needs rethink)
- [x] Phase 9c — External clock indicator
- [ ] Phase 9d — Waveform mini-preview
- [x] Phase 9e — Timeout to main screen
- [x] Phase 10 — Metrics System Update (ISR bug fix, DAC/Core1 timing, jitter reporting)
- [ ] Phase 11a — All 4 outputs full menu
- [x] Phase 11b — Higher PPQN resolution (RP2040=960)
- [ ] Phase 11c — Improved external clock PPQN detection
- [ ] Phase 11d — Audio-rate output capability
- [ ] Phase 11e–g — LFOs, quantizer, beat loop recorder
- [x] Phase 12a — Move outputs.hpp to lib/
- [x] Phase 12b — Extract clockEngine.hpp
- [x] Phase 12c — Extract cvInputs.hpp
- [x] Phase 12d — Extract presetManager.hpp
- [x] Phase 13 — Data-Driven Menu System (click + position handlers; HandleDisplay pending)

**Bug fixes (this session):**
- [x] Wire1 (DAC bus) raised to 1MHz — DAC write time halved (~400µs → ~210µs)
- [x] Save/Load `display.display()` was called from Core 0 (Wire race) — caused garbled/flipped display; fixed with `ShowTemporaryMessage()` helper using `_displayFrameReady`
- [x] Save/Load `while(HandleIO())` loop was re-entrant inside `HandleEncoderClick()` — caused encoder inversion; replaced with `HandleOutputs()`-only wait loop
- [x] `_displayLocked` flag added to prevent Core 1 GFX rendering from overwriting temporary message buffer
- [x] Menu start index corrected (was starting on second page at boot)

---

### Phase 1 — Build System & Platform Setup ✅

**Goal:** Get the project compiling for RP2040 without any code changes.

**Files changed:** `platformio.ini`

**Tasks:**
1. Add `[env:xiao_rp2040]` environment:
   ```ini
   [env:xiao_rp2040]
   framework = arduino
   platform = https://github.com/maxgerhardt/platform-raspberrypi.git
   board = seeed_xiao_rp2040
   board_build.core = earlephilhower
   monitor_speed = 115200
   lib_deps =
     adafruit/Adafruit SSD1306@^2.5.13
     adafruit/Adafruit MCP4728@^1.0.5
     paulstoffregen/Encoder@^1.4.4
   build_flags = -std=gnu++17 -I lib
   ```
2. Remove `FlashStorage` from shared `[env]` libs (not supported on RP2040)
3. Remove `Adafruit MCP4725` from shared libs (replaced by MCP4728)
4. Keep `seeed_xiao` environment intact — no changes to SAMD21 build

**Verify:** `pio run --environment xiao_rp2040` — expect many errors (expected at this stage), but confirms toolchain installs.

---

### Phase 2 — Pin Assignments (pinouts.hpp) ✅

**Goal:** Replace SAMD21 pin constants with RP2040/XIAO RP2040 pin mapping.

**Files changed:** `lib/pinouts.hpp`

**New constants:**
```cpp
// I2C — shared bus (MCP4728 + SSD1306)
#define I2C_SDA_PIN  4   // GPIO4
#define I2C_SCL_PIN  5   // GPIO5

// External clock trigger input
#define CLK_IN_PIN   A0  // GPIO26 — interrupt-capable ADC pin (IN1)

// CV modulation inputs
#define CV_1_IN_PIN  A1  // GPIO27 (IN2)
#define CV_2_IN_PIN  A2  // GPIO28 (IN3)
#define NUM_CV_INS   2   // Modulation CVs (IN1 is clock-only)

// Encoder
#define ENC_PIN_1    9   // D9
#define ENC_PIN_2    8   // D8
#define ENCODER_SW   10  // D10

// Outputs (all via MCP4728 — no GPIO output pins)
#define NUM_OUTPUTS     4
#define NUM_DAC_OUTS    4  // All 4 outputs are DAC

// MCP4728 I2C address
#define MCP4728_ADDR  0x60
```

**Remove:** `OUT_PIN_1`, `OUT_PIN_2`, `DAC_INTERNAL_PIN`, `OUT_PINS[]`, `NUM_GATE_OUTS`

---

### Phase 3 — Hardware I/O Abstraction (boardIO.hpp) ✅

**Goal:** Replace all SAMD21-specific I/O with RP2040 equivalents.

**Files changed:** `lib/boardIO.hpp`

**Sub-tasks in order:**

#### 3a — I2C mutex
```cpp
#include <hardware/sync.h>
extern mutex_t i2cMutex;  // defined in main.cpp / setup()
```

#### 3b — MCP4728 object & safe write
```cpp
#include <Adafruit_MCP4728.h>
Adafruit_MCP4728 dac4;

// Non-blocking DAC write for Core 1 (skips if display holds mutex)
void DACWriteAll(uint16_t ch0, uint16_t ch1, uint16_t ch2, uint16_t ch3) {
    if (mutex_try_enter(&i2cMutex, nullptr)) {
        dac4.fastWrite(ch0, ch1, ch2, ch3);
        mutex_exit(&i2cMutex);
    }
    // If held by Core 0, skip — missed update is imperceptible
}

// Single-channel write (used during init)
void DACWrite(int channel, uint32_t value) {
    // channel 0–3 maps to MCP4728_CHANNEL_A–D
}
```

#### 3c — ADC / CV reads
```cpp
void InitIO() {
    analogReadResolution(12);  // RP2040: 12-bit ADC
    pinMode(CLK_IN_PIN, INPUT);
    pinMode(CV_1_IN_PIN, INPUT);
    pinMode(CV_2_IN_PIN, INPUT);
    pinMode(ENCODER_SW, INPUT_PULLUP);
    // No gate output pins (all through MCP4728)
}

void AdjustADCReadings(int pin, int ch) { ... }
```

#### 3d — Remove entirely
- `InternalDAC()` — no internal DAC on RP2040
- `MCP()` — replaced by `DACWriteAll()`
- `SetPin()` with inverted gate logic — no GPIO gates
- `PWMWrite()`, `PWM1()`, `PWM2()` — no PWM gates
- SAMD21 ADC registers (`REG_ADC_AVGCTRL`, `ADC_AVGCTRL_SAMPLENUM_128`, etc.)

**Note:** The RP2040 ADC is noisier than SAMD21. May need software averaging in `AdjustADCReadings()`.

---

### Phase 4 — Storage System (loadsave.hpp) ✅

**Goal:** Replace FlashStorage with arduino-pico's EEPROM emulation.

**Files changed:** `lib/loadsave.hpp`

**Tasks:**

#### 4a — Replace library
```cpp
// Old: #include <FlashStorage.h>
#include <EEPROM.h>  // arduino-pico provides this
```

#### 4b — Update LoadSaveParams
Changes needed:
- `NUM_CV_INS` references: 2 (modulation CVs only, clock trigger handled separately)
- Keep all output parameters — all 4 outputs are now DACOut
- Remove `outputType` if it was saved (no longer needed; all DAC)
- CV calibration data stored in a **separate** `CalibrationData` struct at a fixed EEPROM address beyond the preset slots, so it persists independently of preset loads

```cpp
// Configurable slot count — increase this constant to add more slots in the future.
// EEPROM size is computed automatically from this.
// RP2040 arduino-pico EEPROM emulation supports up to 4096 bytes.
// Each LoadSaveParams is ~250 bytes, so 8 slots = ~2000 bytes comfortably.
#define NUM_SLOTS 5  // Slot 0 = auto-load on boot; slots 1–4 = user presets

struct LoadSaveParams {
    boolean valid;
    unsigned int BPM;
    unsigned int externalClockDivIdx;
    int divIdx[NUM_OUTPUTS];
    int dutyCycle[NUM_OUTPUTS];
    bool outputState[NUM_OUTPUTS];
    uint32_t outputLevel[NUM_OUTPUTS];
    int outputOffset[NUM_OUTPUTS];
    int swingIdx[NUM_OUTPUTS];
    int swingEvery[NUM_OUTPUTS];
    int pulseProbability[NUM_OUTPUTS];
    EuclideanParams euclideanParams[NUM_OUTPUTS];
    int phaseShift[NUM_OUTPUTS];
    int waveformType[NUM_OUTPUTS];
    byte CVInputTarget[NUM_CV_INS];
    int CVInputAttenuation[NUM_CV_INS];
    int CVInputOffset[NUM_CV_INS];
    EnvelopeParams envParams[NUM_OUTPUTS];
    QuantizerParams quantizerParams[NUM_OUTPUTS];
};

// CV input calibration — lives at a fixed address past the preset block.
// Never overwritten by preset save/load.
struct CalibrationData {
    boolean valid;
    float cvCalOffset[NUM_CV_INS];  // Raw ADC offset at 0V
    float cvCalScale[NUM_CV_INS];   // Scale factor: 4095 / (reading_5v - reading_0v)
};

// EEPROM layout:
//   0                          → LoadSaveParams[0]  (auto-load slot)
//   sizeof(LoadSaveParams)     → LoadSaveParams[1]
//   ...
//   NUM_SLOTS * sizeof(...)    → CalibrationData     (fixed, never moved by slot operations)
#define EEPROM_PRESET_BASE   0
#define EEPROM_CAL_BASE      (NUM_SLOTS * sizeof(LoadSaveParams))
#define EEPROM_TOTAL_SIZE    (EEPROM_CAL_BASE + sizeof(CalibrationData))
```

**To add more preset slots later:** change only `NUM_SLOTS`. `EEPROM_CAL_BASE` recalculates automatically, preserving calibration. **Warning:** increasing `NUM_SLOTS` shifts `EEPROM_CAL_BASE`, so existing calibration will need to be re-run once after the firmware update.

#### 4c — Load/Save functions using EEPROM
```cpp
#include <EEPROM.h>  // arduino-pico EEPROM emulation

void Save(const LoadSaveParams &p, int slot) {
    if (slot < 0 || slot >= NUM_SLOTS) return;
    EEPROM.put(EEPROM_PRESET_BASE + slot * sizeof(LoadSaveParams), p);
    EEPROM.commit();  // Required on RP2040 — flushes to flash
}

LoadSaveParams Load(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS) return LoadDefaultParams();
    LoadSaveParams p;
    EEPROM.get(EEPROM_PRESET_BASE + slot * sizeof(LoadSaveParams), p);
    return p.valid ? p : LoadDefaultParams();
}

void SaveCalibration(const CalibrationData &cal) {
    EEPROM.put(EEPROM_CAL_BASE, cal);
    EEPROM.commit();
}

CalibrationData LoadCalibration() {
    CalibrationData cal;
    EEPROM.get(EEPROM_CAL_BASE, cal);
    if (!cal.valid) {
        // Default: no correction
        for (int i = 0; i < NUM_CV_INS; i++) {
            cal.cvCalOffset[i] = 0.0f;
            cal.cvCalScale[i] = 1.0f;
        }
    }
    return cal;
}
```

**Important:** `EEPROM.begin(EEPROM_TOTAL_SIZE)` must be called in `setup()` before any EEPROM operations.

---

### Phase 5 — Output Engine Updates (outputs.hpp) ✅

**Goal:** Remove SAMD21-specific output types; make all 4 outputs waveform-capable.

**Files changed:** `src/outputs.hpp`

**Tasks:**

#### 5a — Remove OutputType::DigitalOut
All outputs are now `DACOut`. The `OutputType` enum can be removed or kept as single value.

Update Output constructor default: change all outputs in `main.cpp` from:
```cpp
Output(1, OutputType::DigitalOut),  // was digital
Output(2, OutputType::DigitalOut),
```
to:
```cpp
Output(1, OutputType::DACOut),  // now all DAC
Output(2, OutputType::DACOut),
```

#### 5b — Sine lookup table
Pre-compute in constructor or `setup1()` to avoid `sin()` in the hot loop:
```cpp
static float sineLUT[256];
static bool sineLUTReady = false;

static void BuildSineLUT() {
    if (sineLUTReady) return;
    for (int i = 0; i < 256; i++) {
        sineLUT[i] = (sin(2.0f * PI * i / 256.0f) + 1.0f) / 2.0f;  // 0.0 to 1.0
    }
    sineLUTReady = true;
}
```

Replace `sin()` call in `GenerateSineWave()` with LUT lookup.

#### 5c — Update GetOutputLevel()
Remove the `DigitalOut` path:
```cpp
uint32_t Output::GetOutputLevel() {
    // All outputs are DAC now — always return 0–4095
    ...
}
```

#### 5d — Remove `_dividerAmount - 1` skip for digital outputs
The `SetDivider()` function was skipping the "Env" divider for digital outputs. Remove that restriction.

---

### Phase 6 — Dual-Core Architecture (main.cpp) ✅

**Goal:** Split the monolithic loop into Core 0 (UI) and Core 1 (real-time engine).

**Files changed:** `src/main.cpp`

This is the largest phase. Break into sub-tasks:

#### 6a — Global shared state struct + mutexes
```cpp
#include <hardware/sync.h>

mutex_t i2cMutex;    // Protects Wire/I2C bus
mutex_t paramMutex;  // Protects shared ClockParams

// Shared parameter struct (Core 0 writes, Core 1 reads snapshot)
struct SharedParams {
    uint16_t bpm;
    bool masterStop;
    bool paramsUpdated;  // Dirty flag
    // Output params (replaces individual output object access from Core 1)
    // ... (expand as needed)
};

SharedParams sharedParams;
```

For the initial migration, the simplest approach is:
- Keep `outputs[]` array as the shared state
- Use `paramMutex` to protect it during Core 1 reads and Core 0 encoder writes
- Core 1 takes a snapshot each cycle (copy of output params to local struct)

#### 6b — Replace TimerTCC0 with repeating_timer
```cpp
#include <hardware/timer.h>

repeating_timer_t clockTimer;

bool ClockPulseCallback(repeating_timer_t *rt) {
    ClockPulse();  // existing logic
    return true;   // keep repeating
}

void InitializeTimer() {
    // Fire at PPQN rate
    // Interval = 60,000,000 µs/min / BPM / PPQN
    // At 120 BPM, PPQN=192: 2604 µs per tick
    int64_t intervalUs = 60000000LL / BPM / PPQN;
    add_repeating_timer_us(-intervalUs, ClockPulseCallback, nullptr, &clockTimer);
}

void UpdateBPM(unsigned int newBPM) {
    BPM = constrain(newBPM, minBPM, maxBPM);
    cancel_repeating_timer(&clockTimer);
    int64_t intervalUs = 60000000LL / BPM / PPQN;
    add_repeating_timer_us(-intervalUs, ClockPulseCallback, nullptr, &clockTimer);
}
```

**Note:** The negative sign on `add_repeating_timer_us` means fire relative to trigger time (not completion), giving drift-free timing.

#### 6c — Core 0 entry points (UI)
```cpp
// Core 0 — UI, menu, display, encoder, presets
void setup() {
    mutex_init(&i2cMutex);
    mutex_init(&paramMutex);
    
    Serial.begin(115200);
    Wire.setSDA(I2C_SDA_PIN);
    Wire.setSCL(I2C_SCL_PIN);
    Wire.begin();
    Wire.setClock(400000);  // Fast Mode required
    
    InitIO();  // ADC, encoder pins (no gate pins!)
    
    // Init display (Core 0 owns display)
    display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
    // ... splash screen ...
    
    // Init EEPROM and load preset
    EEPROM.begin(NUM_SLOTS * sizeof(LoadSaveParams));
    LoadSaveParams p = Load(0);
    UpdateParameters(p);
}

void loop() {
    // Core 0: UI only — no DAC writes, no clock pulse
    HandleEncoderClick();
    HandleEncoderPosition();
    HandleCVInputs();
    HandleExternalClock();
    HandleDisplay();  // safeDisplayUpdate inside
    metrics.PrintStats();
}
```

#### 6d — Core 1 entry points (real-time engine)
```cpp
// Core 1 — clock engine, DAC writes
void setup1() {
    // DAC init (uses Wire which is already initialized by Core 0)
    // Wait for Core 0 to finish Wire.begin() init
    while (!mutex_is_initialized(&i2cMutex)) { /* wait */ }
    
    if (mutex_try_enter(&i2cMutex, nullptr)) {
        dac4.begin(MCP4728_ADDR);
        DACWriteAll(0, 0, 0, 0);
        mutex_exit(&i2cMutex);
    }
    
    // External clock interrupt
    attachInterrupt(digitalPinToInterrupt(CLK_IN_PIN), ClockReceived, RISING);
    
    InitializeTimer();
}

void loop1() {
    // Take parameter snapshot (non-blocking)
    // Run waveform engine on local copy
    // Write DAC (non-blocking with mutex)
    
    // Envelope generation (for all 4 outputs now)
    if (mutex_try_enter(&paramMutex, nullptr)) {
        // Read current output states
        mutex_exit(&paramMutex);
    }
    
    for (int i = 0; i < NUM_OUTPUTS; i++) {
        outputs[i].GenEnvelope();
    }
    
    // DAC write (atomic 4-channel)
    DACWriteAll(
        outputs[0].GetOutputLevel(),
        outputs[1].GetOutputLevel(),
        outputs[2].GetOutputLevel(),
        outputs[3].GetOutputLevel()
    );
}
```

#### 6e — Safe display update (Core 0)
```cpp
void safeDisplayUpdate() {
    mutex_enter_blocking(&i2cMutex);  // Core 0 can block; it only updates on interaction
    display.display();
    mutex_exit(&i2cMutex);
}
```

Replace `display.display()` call in `DisplayManager::EndFrame()` with mutex-wrapped version.

#### 6f — Update ClockPulse() ISR
- Remove `SetPin(0, ...)` and `SetPin(1, ...)` — no GPIO gates
- Core 1 handles all DAC writes in `loop1()`
- ISR only calls `outputs[i].Pulse()` and increments `tickCounter`

#### 6g — Update HandleOutputs()
- Remove GPIO-only gate writes (`SetPin(2, ...)`, `SetPin(3, ...)`)
- DAC writes now happen in Core 1 `loop1()` via `DACWriteAll()`
- `HandleOutputs()` is removed from Core 0 entirely

#### 6h — Update external clock interrupt
- `ClockReceived()` stays as ISR
- Runs on Core 1 (attachInterrupt in setup1)

**Critical note on noInterrupts():** On RP2040 with arduino-pico, `noInterrupts()` only disables interrupts on the *current core*. Cross-core data sharing requires the mutex, not `noInterrupts()`. Audit all `noInterrupts()`/`interrupts()` pairs and replace with appropriate mutex usage.

---

### Phase 7 — CV Input Updates (main.cpp) ⬜

**Goal:** Update for 3 CV inputs (IN1=clock trigger, IN2/IN3=modulation).

**Files changed:** `src/main.cpp`, `lib/pinouts.hpp`

**Tasks:**
- `NUM_CV_INS` stays 2 (IN2, IN3 are the modulation CVs)
- `CLK_IN_PIN` = IN1/A0 handled separately by clock interrupt
- `CV_IN_PINS[]` = `{CV_1_IN_PIN, CV_2_IN_PIN}` (A1, A2)
- No changes to CVTarget enum required
- Update `AdjustADCReadings()` for RP2040 ADC (no hardware averaging — do software moving average)

---

### Phase 8 — Calibration System ⬜

**Goal:** Two calibration procedures: (A) output voltage trim helper, (B) CV input 2-point calibration.

**Files changed:** `src/main.cpp`, `lib/loadsave.hpp`

Both are triggered at boot when the encoder button is held. A second boot without holding the button loads normally.

---

#### 8A — Output Voltage Calibration Helper (hardware trimpot assist)

Each output has a hardware trimpot (RV1–RV4 on the PCB) that sets the actual output voltage. The firmware calibration helper just sets all 4 DAC channels to full scale (4095) so the user can trim each pot to exactly 5.00V with a multimeter. **No digital correction is stored.**

```
Boot → hold encoder → Screen shows: "OUTPUT CALIBRATION"

Display:
  "All outputs → 5V"
  "Trim RV1–RV4 to 5.00V"
  "Press to exit"

Firmware:  DACWriteAll(4095, 4095, 4095, 4095)  // hold until encoder pressed
```

This is a minimal helper screen — it just drives full scale and waits. No EEPROM writes.

#### 8B — CV Input Calibration (2-point, software correction)

CV inputs IN2 and IN3 have analog non-linearities and offset from the RP2040 ADC. A 2-point calibration corrects these so CV modulation and the quantizer track correctly.

**Reference voltages: 1V and 3V.** These are within the practical Eurorack CV range and are easy to source — from another calibrated module, a precision voltage reference, or from ClockForge's own outputs after output trim (8A) is complete. Avoiding 0V (ground offsets are unreliable at ADC level) and 5V (near-rail non-linearity) makes the calibration more accurate in the range that actually matters for 1V/oct tracking.

In a nominal 12-bit ADC with a 0–5V input range:
- 1V → expected ADC = `4095 × 1/5` = **819**
- 3V → expected ADC = `4095 × 3/5` = **2457**

**Boot trigger:** hold encoder at power-on → show calibration mode selection:
```
"CALIBRATION MODE"
  > Output Trim (8A)
    CV Input Cal (8B)
    Exit
```

**8B procedure:**
```
Step 1 of 2:
  Screen: "APPLY 1V TO IN2+IN3"
          "(use OUT1 or ref module)"
  "Press encoder to record"
  → records raw ADC reading_1v for both channels

Step 2 of 2:
  Screen: "APPLY 3V TO IN2+IN3"
          "(use OUT3 or ref module)"
  "Press encoder to record"
  → records raw ADC reading_3v for both channels

Compute per channel:
  // Range between the two reference points in ideal ADC counts
  const float EXPECTED_1V = 819.0f;   // 4095 * 1/5
  const float EXPECTED_3V = 2457.0f;  // 4095 * 3/5

  cvCalScale[ch]  = (EXPECTED_3V - EXPECTED_1V) / (reading_3v - reading_1v)
                  = 1638.0f / (reading_3v - reading_1v)

  cvCalOffset[ch] = reading_1v - (EXPECTED_1V / cvCalScale[ch])

Display: "SAVED" → boot normally
```

The `CalibrationData` struct (defined in Phase 4) stores `cvCalOffset[ch]` and `cvCalScale[ch]` — no change to the struct signature needed.

**8c — Apply calibration in AdjustADCReadings():**
```cpp
CalibrationData cal;  // loaded at boot from EEPROM_CAL_BASE

void AdjustADCReadings(int pin, int ch) {
    // Software moving average (8 samples) — RP2040 ADC is noisier than SAMD21
    static int32_t accum[NUM_CV_INS] = {0};
    static uint8_t count[NUM_CV_INS] = {0};

    accum[ch] += analogRead(pin);
    count[ch]++;
    if (count[ch] >= 8) {
        float raw = accum[ch] / 8.0f;
        accum[ch] = 0;
        count[ch] = 0;
        // Apply 2-point calibration: maps measured raw values to ideal 0–4095 range
        float calibrated = (raw - cal.cvCalOffset[ch]) * cal.cvCalScale[ch];
        channelADC[ch] = constrain(calibrated, 0.0f, 4095.0f);
    }
}
```

---

### Phase 9 — Display & UI Improvements 🔄

> **Partial:** 9a (unsaved indicator), 9c (ext clock indicator), 9e (timeout) done. 9b reverted. 9d/9f deferred.

**Goal:** Better visual feedback on OLED; keep existing menu structure intact initially.

**Files changed:** `src/main.cpp`, `lib/displayManager.hpp`

**Tasks:**

#### 9a — Unsaved changes indicator (move from DisplayManager to main screen)
Already done in `displayManager.hpp` (`fillCircle` in `EndFrame()`). Keep as-is.

#### 9b — Output activity indicators on main screen
Main screen currently shows 4 squares for output state. Enhance to:
- Filled square = output active/high (pulse on)
- Blinking behavior: flash when pulse fires
```cpp
// Store last pulse state change timestamps
unsigned long lastPulseTime[NUM_OUTPUTS] = {0};
bool lastPulseState[NUM_OUTPUTS] = {false};

// In main screen draw loop:
for (int i = 0; i < NUM_OUTPUTS; i++) {
    bool pulsed = outputs[i].GetPulseState();
    if (pulsed != lastPulseState[i]) {
        lastPulseTime[i] = millis();
        lastPulseState[i] = pulsed;
    }
    bool showFilled = (millis() - lastPulseTime[i] < 100) || pulsed;
    if (showFilled) display.fillRect(...);
    else display.drawRect(...);
}
```

#### 9c — External clock indicator
Show "E" next to BPM when `usingExternalClock` (already exists).

#### 9d — Waveform mini-preview
Optional: small SVG-like icon per waveform type in the wave selection menu. Use bitmap icons (8×8 or 16×8) stored in PROGMEM.

#### 9e — Timeout to main screen
Already implemented in `displayManager.hpp` (7 second timeout). Keep as-is.

#### 9f — (Future: U8g2 migration)
Switching from Adafruit SSD1306 to U8g2 enables partial buffer updates. Defer to after core migration is stable.

---

### Phase 10 — Metrics System Update (metrics.hpp) ✅

**Goal:** Fix RP2040 RAM reporting and add Core 1 timing.

**Files changed:** `lib/metrics.hpp`

**Tasks:**
- Remove `#include <malloc.h>` and `mallinfo()` call (not reliable on RP2040)
- RP2040 free RAM: use `rp2040.getFreeHeap()` (arduino-pico provides this)
- Add Core 1 loop timing category
- Report which core printed stats (add core ID to output)

```cpp
uint32_t GetFreeRAM() {
    return rp2040.getFreeHeap();
}
```

---

### Phase 11 — New Features & Improvements (Post-Migration) 🔄

> **Partial:** 11b (PPQN=960 on RP2040) done. 11a, 11c–g pending.

These are enhancements after the core migration is stable. Tackle in order of value.

#### 11a — All 4 outputs waveform/level/offset menu
Currently only outputs 3 & 4 have waveform, level, offset, duty cycle and phase controls in the menu. With all 4 being DAC on RP2040, expose the full set for Out 1 & 2 as well:
- Add waveform, level, offset, duty cycle, phase menu items for Out 1 & 2
- Remove or guard the old SAMD21 "Output type" (DigitalOut vs DACOut) selector for those outputs
- Update `menuItems` count
- Update `HandleDisplay()` and `HandleEncoderClick()` switch statements
- Update `LoadSaveParams` / `LoadDefaultParams` if new defaults are needed

#### 11b — Higher internal PPQN resolution for cleaner waveforms
The current `PPQN = 192` limits waveform step resolution at low BPM. RP2040 timer has sub-100µs precision:
- Evaluate raising PPQN to 960 or 1920 (5× or 10× current)
- Audit all `periodTicks`, swing, and phase calculations for overflow with `unsigned long` at higher tick rates
- Measure ISR load at max BPM × new PPQN to confirm CPU headroom
- Update `UpdateBPM()` interval calculation

#### 11c — Improved external clock PPQN detection
The current detection fires on every rising edge and assumes 24PPQN input. Improve robustness:
- Measure inter-pulse interval over a rolling window (e.g. 4 pulses) to detect input PPQN
- Auto-detect common standards: 1PPQN (quarter note), 2, 4, 8, 24, 48, 96PPQN
- Show detected input PPQN on display
- Reduce false-trigger sensitivity (debounce + hysteresis on interval)

#### 11d — Audio-rate output capability (future)
RP2040 timer can sustain ~40kHz+ tick rates. This is a longer-term research item:
- Profile `setChannelValue` I2C latency — MCP4728 at 400kHz I2C has a minimum ~28µs per multi-write, limiting update rate to ~35kHz theoretical maximum
- If latency is acceptable, add an "Audio" mode that bypasses the PPQN divider and drives outputs at a fixed sample rate
- May require switching to SPI DAC for true audio rate (design decision)
- Keep as documented future work until Phase 11a–11c are stable

#### 11e — Internal LFOs (modulators without external CV)
Add `LFOSource` as a CV modulation source type.
- Parameters: Rate (Hz), Shape (sine, triangle, square, ramp), Depth, Phase
- At least 2 independent LFOs assignable to any CVTarget
- Store LFO params in `LoadSaveParams`
- Displayed in a new "LFO" menu section

#### 11f — Quantizer improvements
- After Phase 8 calibration, verify quantization pitch tracking.
- Consider adding semitone-level calibration per-channel.

#### 11g — Beat loop recorder (Pam's-style)
Complex looping/recording of beat patterns. Design separately when all other features are stable.

---

## Implementation Order

```
Phase 1  → Phase 2  → Phase 3        [Build system, pins, boardIO — get it compiling]
    ↓
Phase 4  → Phase 5                   [Storage with configurable slots + Output engine]
    ↓
Phase 6a → 6b → 6c+6d                [Dual-core split — verify clock engine alone]
    ↓
Phase 6e → 6f → 6g → 6h              [Display mutex + remove digital GPIO outputs]
    ↓
Phase 7  → Phase 8A → Phase 8B       [CV inputs, output trim helper, CV cal]
    ↓
Phase 9  → Phase 10                  [Display improvements + metrics update]
    ↓
Phase 12 (a → b → c → d → e)        [Modularize: one file at a time, test after each]
    ↓
Phase 13 (group by group)            [Menu rework: one menu group at a time]
    ↓
Phase 11a                            [All 4 outputs full menu — quick win, high value]
    ↓
Phase 11b → 11c                      [Waveform resolution + clock detection improvements]
    ↓
Phase 11d                            [Audio rate — research/design gate]
    ↓
Phase 11e → 11f → 11g                [LFOs, quantizer polish, beat loop recorder]
```

**Key: complete and compile-verify each arrow before proceeding.**

---

### Phase 12 — Code Modularization 🔄

> **Partial:** 12a (outputs.hpp → lib/) done. 12b–d pending.

**Goal:** Break `src/main.cpp` (2065 lines, monolithic) into clean, single-responsibility modules in `lib/`. Each module is a header-only file following the existing project convention. `main.cpp` becomes a thin orchestrator containing only `setup()`, `loop()`, `setup1()`, `loop1()`, and global object instantiations.

**Principle:** Extract one module at a time, compiling and testing after each. Do not batch extractions.

---

#### Target file layout after modularization

```
lib/
  pinouts.hpp          — pin constants (rewritten in Phase 2)
  boardIO.hpp          — hardware I/O, ADC, DACWriteAll (rewritten in Phase 3)
  loadsave.hpp         — EEPROM load/save, LoadSaveParams, CalibrationData (Phase 4)
  outputs.hpp          — OUTPUT MOVED: Output class → lib/ (was src/)
  euclidean.hpp        — unchanged
  quantizer.cpp        — unchanged
  scales.cpp           — unchanged
  utils.hpp            — unchanged
  splash.hpp           — unchanged
  metrics.hpp          — updated in Phase 10
  displayManager.hpp   — updated in Phase 6e
  clockEngine.hpp      — NEW: timer, BPM, external clock logic
  cvInputs.hpp         — NEW: CVTarget enum, HandleCVInputs, HandleCVTarget
  presetManager.hpp    — NEW: UpdateParameters, save/load UI helpers
  menuDefinitions.hpp  — NEW: menu item array (Phase 13 — menu rework)
  menuHandlers.hpp     — NEW: GetMenuValue, SetMenuValue, MenuAction (Phase 13)

src/
  main.cpp             — setup/loop/setup1/loop1 only (~150 lines)
  version.hpp          — unchanged
```

---

#### 12a — Move `src/outputs.hpp` → `lib/outputs.hpp`

The Output class has no dependency on main.cpp globals. It's already self-contained. Moving it to lib/ makes it importable by tests and other future modules without including the full main translation unit.

- Update `#include "outputs.hpp"` paths in `main.cpp` and `loadsave.hpp`
- Update `test_native/test_outputs.cpp` include path

#### 12b — Extract `lib/clockEngine.hpp`

Extract from `main.cpp`:
- `ClockPulse()` function
- `ClockPulseCallback()` (repeating_timer wrapper)
- `InitializeTimer()` and `UpdateBPM()`
- `HandleExternalClock()` and `ClockReceived()` ISR
- Variables: `tickCounter`, `clockTimer`, `clockInterval`, `lastClockInterruptTime`, `usingExternalClock`, `externalTickCounter`, `BPM`, `lastInternalBPM`

```cpp
// lib/clockEngine.hpp
#pragma once
#include <Arduino.h>
#include <hardware/timer.h>
#include "pinouts.hpp"
#include "outputs.hpp"   // needs Output array reference

class ClockEngine {
public:
    void Init(Output outputs[], int numOutputs, int ppqn);
    void SetBPM(unsigned int bpm);
    unsigned int GetBPM() const;
    bool IsExternalClock() const;
    void HandleExternalClockTimeout();
    // tickCounter exposed for outputs
    volatile unsigned long tickCounter = 0;
    ...
private:
    repeating_timer_t _timer;
    ...
};
```

**Note:** `ClockPulse()` calls `outputs[i].Pulse()` — the ClockEngine needs a reference to the outputs array. Pass it in `Init()`.

#### 12c — Extract `lib/cvInputs.hpp`

Extract from `main.cpp`:
- `CVTarget` enum and `CVTargetDescription[]`
- `CVInputTarget[]`, `CVInputAttenuation[]`, `CVInputOffset[]`
- `HandleCVInputs()`, `HandleCVTarget()`, `AdjustADCReadings()`
- `channelADC[]`, `oldChannelADC[]`, `offsetScale[]`

```cpp
// lib/cvInputs.hpp
#pragma once
#include "pinouts.hpp"
#include "outputs.hpp"
#include "loadsave.hpp"  // for CalibrationData

class CVInputManager {
public:
    void Init(const CalibrationData &cal);
    void Process(Output outputs[], unsigned int bpm, bool &masterState, unsigned long &tickCounter);
    CVTarget GetTarget(int ch) const;
    void SetTarget(int ch, CVTarget t);
    int GetAttenuation(int ch) const;
    void SetAttenuation(int ch, int val);
    int GetOffset(int ch) const;
    void SetOffset(int ch, int val);
    float GetRawValue(int ch) const;
    ...
};
```

#### 12d — Extract `lib/presetManager.hpp`

Extract from `main.cpp`:
- `UpdateParameters()` function
- Save/load menu action code (currently inline in `HandleEncoderClick()` case 64/65/66)

```cpp
// lib/presetManager.hpp
#pragma once
#include "loadsave.hpp"
#include "outputs.hpp"

class PresetManager {
public:
    void ApplyParams(const LoadSaveParams &p, Output outputs[], CVInputManager &cv, unsigned int &bpm, int &extDivIdx);
    LoadSaveParams CaptureParams(const Output outputs[], const CVInputManager &cv, unsigned int bpm, int extDivIdx);
    void SaveSlot(int slot, const LoadSaveParams &p);
    LoadSaveParams LoadSlot(int slot);
    LoadSaveParams LoadDefaults();
    int GetCurrentSlot() const { return _slot; }
    void SetCurrentSlot(int s) { _slot = constrain(s, 0, NUM_SLOTS - 1); }
private:
    int _slot = 0;
};
```

#### 12e — Final main.cpp structure

After modularization, `main.cpp` becomes:
```cpp
// Core setup, loop, and object instantiation only
#include "clockEngine.hpp"
#include "cvInputs.hpp"
#include "presetManager.hpp"
#include "displayManager.hpp"
#include "menuDefinitions.hpp"
#include "menuHandlers.hpp"
#include "metrics.hpp"
#include "boardIO.hpp"

// --- Global objects ---
Output outputs[NUM_OUTPUTS] = { ... };
ClockEngine clockEngine;
CVInputManager cvManager;
PresetManager presetManager;
DisplayManager displayMgr(display);
PerformanceMetrics metrics;
mutex_t i2cMutex;
mutex_t paramMutex;

// --- Core 0 ---
void setup() { ... }   // ~40 lines
void loop() { ... }    // ~20 lines

// --- Core 1 ---
void setup1() { ... }  // ~20 lines
void loop1() { ... }   // ~15 lines
```

---

### Phase 13 — Data-Driven Menu System ⬜

**Goal:** Replace the current hardcoded 66-item switch-case menu (spread across `HandleEncoderClick()` ~400 lines, `HandleEncoderPosition()` ~350 lines, `HandleDisplay()` ~600 lines) with a declarative, data-driven approach. Adding or moving a menu item becomes a single array entry change, not surgery across three 300-line functions.

**Files changed:** New `lib/menuDefinitions.hpp`, new `lib/menuHandlers.hpp`, `src/main.cpp`

---

#### The Core Problem

The current menu cannot be maintained:
- Adding one parameter requires edits in 3 places (click handler, position handler, display handler)
- Moving a menu page requires manually renumbering all case labels downstream
- The 66 `menuItem` integers are implicit — no label, no structure
- Display sections are hardcoded pixel positions with no connection to the logical item they represent

#### Design: Descriptor Array + Generic Render Engine

Each menu item is described by a `MenuItem` struct. The render loop, encoder handler, and display code all work from this single array.

```cpp
// lib/menuDefinitions.hpp
#pragma once

enum MenuItemType : uint8_t {
    MENU_SECTION,   // Non-selectable section header
    MENU_ACTION,    // Click fires a callback — no edit mode
    MENU_TOGGLE,    // Click toggles a bool — immediate, no edit mode
    MENU_INT,       // Click enters edit; encoder changes integer value
    MENU_ENUM,      // Click enters edit; encoder cycles through string list
};

// Function pointer types for handlers
typedef String (*GetterFn)();
typedef void   (*SetterFn)(int delta);   // delta = signed encoder movement × speed
typedef void   (*ActionFn)();

struct MenuItem {
    const char*  label;       // Display text, e.g. "OUTPUT 1 DIVIDER"
    MenuItemType type;
    GetterFn     getter;      // Returns current value as display string (nullptr for SECTION/ACTION)
    SetterFn     setter;      // Called with delta when in edit mode (nullptr for TOGGLE/ACTION/SECTION)
    ActionFn     action;      // Called on click for TOGGLE and ACTION types (nullptr for INT/ENUM)
    int8_t       group;       // Page group: items with same group render together (0 = global/always visible)
};
```

**The item array is declared once:**
```cpp
// In menuDefinitions.hpp — extern declaration
extern const MenuItem MENU_ITEMS[];
extern const int MENU_ITEM_COUNT;
```

```cpp
// In menuHandlers.cpp / menuHandlers.hpp — the actual data
#include "menuDefinitions.hpp"

// Example entries (abbreviated):
const MenuItem MENU_ITEMS[] = {
    // group 0 — always visible (main screen navigated to directly)

    // group 1 — BPM / transport
    { "BPM",         MENU_INT,    getBPM,         setBPM,         nullptr,             1 },
    { "PLAY/STOP",   MENU_ACTION, getMasterState,  nullptr,        toggleMasterState,   1 },
    { "EXT CLK DIV", MENU_ENUM,   getExtClkDiv,   setExtClkDiv,   nullptr,             1 },

    // group 2 — Output divisions
    { "OUTPUT DIVIDERS", MENU_SECTION, nullptr, nullptr, nullptr,                       2 },
    { "OUT 1 DIV",   MENU_ENUM,   getDiv<0>,      setDiv<0>,      nullptr,             2 },
    { "OUT 2 DIV",   MENU_ENUM,   getDiv<1>,      setDiv<1>,      nullptr,             2 },
    { "OUT 3 DIV",   MENU_ENUM,   getDiv<2>,      setDiv<2>,      nullptr,             2 },
    { "OUT 4 DIV",   MENU_ENUM,   getDiv<3>,      setDiv<3>,      nullptr,             2 },

    // group 3 — Output enable
    { "OUT 1",       MENU_TOGGLE, getOutState<0>, nullptr,        toggleOut<0>,        3 },
    ...
};
const int MENU_ITEM_COUNT = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);
```

**Template helpers** avoid duplicating 4 identical functions per output:
```cpp
// In menuHandlers.hpp
template<int N> String getDiv()   { return outputs[N].GetDividerDescription(); }
template<int N> void   setDiv(int d) { outputs[N].SetDivider(outputs[N].GetDividerIndex() + d); unsavedChanges = true; }
template<int N> String getOutState() { return outputs[N].GetOutputState() ? "ON" : "OFF"; }
template<int N> void   toggleOut()   { outputs[N].ToggleOutputState(); unsavedChanges = true; }
```

#### Generic encoder handler (~25 lines total)

```cpp
int currentMenuItem = 0;
bool inEditMode = false;

void HandleEncoderClick() {
    if (!encoderPressed()) return;

    const MenuItem& item = MENU_ITEMS[currentMenuItem];
    if (!inEditMode) {
        if (item.type == MENU_ACTION || item.type == MENU_TOGGLE) {
            if (item.action) item.action();
        } else if (item.type == MENU_INT || item.type == MENU_ENUM) {
            inEditMode = true;
        }
    } else {
        inEditMode = false;  // Commit and exit
    }
    displayMgr.MarkDirty();
}

void HandleEncoderPosition() {
    int delta = readEncoderDelta() * speedFactor;
    if (delta == 0) return;

    if (!inEditMode) {
        // Navigate — skip SECTION items
        do {
            currentMenuItem = (currentMenuItem + (delta > 0 ? 1 : -1) + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
        } while (MENU_ITEMS[currentMenuItem].type == MENU_SECTION);
    } else {
        if (MENU_ITEMS[currentMenuItem].setter)
            MENU_ITEMS[currentMenuItem].setter(delta);
    }
    displayMgr.MarkDirty();
}
```

#### Generic display renderer (~40 lines for full menu)

```cpp
void HandleDisplay() {
    // Find surrounding items in the same group to fill visible rows
    int8_t currentGroup = MENU_ITEMS[currentMenuItem].group;

    // Collect items for current page
    int pageItems[MAX_VISIBLE_ROWS];
    int pageCount = 0;
    for (int i = 0; i < MENU_ITEM_COUNT && pageCount < MAX_VISIBLE_ROWS; i++) {
        if (MENU_ITEMS[i].group == currentGroup) {
            pageItems[pageCount++] = i;
        }
    }

    // Section header from first SECTION item in group (or group name)
    // ...

    // Draw rows
    int yPos = 20;
    for (int i = 0; i < pageCount; i++) {
        const MenuItem& item = MENU_ITEMS[pageItems[i]];
        bool selected = (pageItems[i] == currentMenuItem);
        bool editing  = selected && inEditMode;

        // Draw cursor triangle
        if (selected && !editing) display.drawTriangle(...);
        if (editing)              display.fillTriangle(...);

        // Draw label and value
        display.setCursor(10, yPos);
        display.print(item.label);
        if (item.getter) {
            display.setCursor(70, yPos);
            display.print(item.getter());
        }
        yPos += 9;
    }

    // Navigation indicator (right edge bar)
    displayMgr.DrawMenuIndicator(currentMenuItem, MENU_ITEM_COUNT);
    displayMgr.EndFrame();
}
```

#### Benefits of this approach

| Before                                                   | After                              |
| -------------------------------------------------------- | ---------------------------------- |
| Add 1 param: edit 3 functions (~30 lines)                | Add 1 `MenuItem` struct literal    |
| Move a whole menu section: renumber all downstream cases | Rearrange array entries            |
| Menu item count tracked manually (`menuItems = 66`)      | `sizeof(MENU_ITEMS) / sizeof(...)` |
| Display rendering: ~600 lines of per-item pixel math     | ~40 lines generic draw loop        |
| Items have no name — just integer IDs                    | String label + typed getter/setter |
| Skip-section logic: magic number ranges                  | `type == MENU_SECTION` check       |

#### Migration path within Phase 13

Do not attempt the full conversion at once. Migrate one *group* at a time:
1. Implement the struct, handlers, and generic engine working for **one group** (e.g. BPM/transport)
2. Verify encoding, display, and edit mode work correctly
3. Port next group
4. Remove old switch-case code for ported groups
5. Repeat until `HandleEncoderClick()`, `HandleEncoderPosition()`, `HandleDisplay()` switch-cases are gone

---

## Risk Register

| Risk                                                        | Likelihood | Impact              | Mitigation                                             |
| ----------------------------------------------------------- | ---------- | ------------------- | ------------------------------------------------------ |
| MCP4728 `fastWrite()` timing different from expected        | Medium     | DAC channels desync | Test waveform quality at 400kHz early                  |
| RP2040 ADC noise higher than SAMD21                         | High       | CV reads unstable   | Software 8-sample moving average in AdjustADCReadings  |
| Mutex deadlock (Core 1 holding I2C during display)          | Low        | Freeze              | Core 0 uses blocking enter; Core 1 uses try_enter only |
| EEPROM.commit() corrupts data mid-write on power loss       | Low        | Lost preset         | Can't avoid; acceptable for Eurorack use               |
| FlashStorage data format incompatible                       | N/A        | Old presets wiped   | Expected/acceptable — document as V2 breaking change   |
| repeating_timer drift vs TimerTCC0                          | Very Low   | Clock jitter        | Negative-delay form prevents accumulation              |
| Wire.begin() sequencing (Core 0 inits Wire, Core 1 uses it) | Medium     | Crash               | Spin-wait in setup1() until i2cMutex initialized       |

---

## Build Command Reference

```bash
# Build for SAMD21 (current hardware)
pio run --environment seeed_xiao

# Build for RP2040 (new hardware)
pio run --environment xiao_rp2040

# Run native tests (should still pass throughout migration)
pio test -e native
```

---

*Plan based on: ClockForge_RP2040_Rewrite_Reference.md, schematic, and analysis of src/main.cpp + src/outputs.hpp + lib/* as of April 2026.*
