# ClockForge RP2040 Rewrite — Implementation Reference

This is the document which summarizes the new ClockForge V2.

In this new version, all 4 outputs are handled by the MCP4728 DAC on a shared I2C bus, and the SSD1306 display also shares that bus. The RP2040's dual cores allow us to cleanly separate the real-time clock engine (Core 1) from the UI and display updates (Core 0), with a mutex to manage access to the shared I2C bus.

The inputs are read from RP2040 GPIO pins

There are also many improvements to the code structure, modularity, and timing to ensure smooth waveform generation without glitches caused by encoder handling or display updates. The new architecture allows for better performance and more simultaneous outputs, as well as a more responsive UI with better feedback.

## Hardware Platform

- **MCU**: Seeed Studio XIAO RP2040
  - Dual-core ARM Cortex-M0+ up to 133MHz
  - 264KB SRAM, 2MB Flash
  - **1x I2C bus** (only one exposed on the XIAO pinout, despite RP2040 having two internally)
  - 1x SPI, 1x UART, 11 GPIO, 4 analog, 11 PWM
- **DAC**: MCP4728 — quad channel, 12-bit, I2C
- **Display**: SSD1306 128×64 OLED, I2C
- **Framework**: Arduino (Earle Philhower's `arduino-pico` core)

## Pinouts

The new RP2040-based ClockForge PCB has the following pinout:

- **I2C**: SDA → GPIO4, SCL → GPIO5 (shared by both DAC and display)
- **Input CVs**: IN1 (CLOCK TRIGGER IN) → A0 (ADC), IN2 (CV IN) → A1 (ADC), IN3 (CV IN) → A2 (ADC)
- **Encoder**: Pin A → D9, Pin B → D8, Switch → D10
- **Outputs**: OUT1 → D2, OUT2 → D3, OUT3 → D4, OUT4 → D5 (all via MCP4728 DAC channels)

CV ins: 3 (IN1 for clock trigger, IN2/IN3 for modulation)
Outputs: 4 (all via MCP4728 DAC)
Display: SSD1306 on shared I2C

## Main Issues and Improvements

- Fix CV calibration issues to improve accuracy of output voltages specially when using the quantizer for both quantizing generated waveforms and reading CV inputs. Createe a calibrarion screen on boot by holding the encoder and have a procedure to calibrate both CV inputs by applying known voltages and adjusting the readings accordingly. This will improve the accuracy of the quantizer and any CV modulation that relies on precise voltage readings.
- Handling the encoder, affects timing and apparently the screen updates affect the waveform generation.
- Improve code structure and modularity.
- Improve timing and scheduling of tasks to ensure smooth waveform generation.
- Have some analitics and metrics to understand CPU load, memory usage, and timing issues.
- Any possible improvements and optimizations in general
- Better and more automated menu system generation from a higher level description.
- Add internal modulators like LFOs to modulate internal parameters without external CVs.
- Implement the beat loop like Pams
- With new core for UI not affecting timing, add better indication in the display like clock symbols, better feedback of the encoder, and maybe a visual representation of the generated waveforms and blinking dots for the outputs on main screen.
- Use structs and enums to better organize code and improve readability and also avoid using global variables and instead pass state through function parameters or use a more structured approach.
- In the future, migrate to a dual core architecture and have a quad DAC or two dual DACs for better performance and more simultaneous outputs. This way we could have all four outputs generating waveforms.
- Maybe refactor to another language or framework that better handles timing and concurrency. Maybe Rust or Zephyr RTOS.

## Architecture Overview

The fundamental problem with the SAMD21 version was that encoder handling and screen updates caused glitches in waveform generation on the single core. The RP2040 solves this with a clean two-core split:

```
Core 0 — UI & housekeeping       Core 1 — real-time engine
─────────────────────────────     ────────────────────────────
Encoder polling & debounce        Clock pulse timing
Menu state machine                Euclidean pattern stepping
SSD1306 display updates           Waveform generation
Preset save/load                  MCP4728 DAC writes
CV input reading                  Swing & phase calculations
Parameter mapping                 FIFO/mutex parameter ingestion
Inter-core communication →        ← Parameter application
```

Core 1 **never blocks**. It only does `try_enter` on shared resources and skips/retries if busy.

---

## Shared I2C Bus — Critical Design Decision

Both the MCP4728 (DAC) and SSD1306 (display) share the **same physical I2C bus** on the PCB. Their addresses do not conflict:

| Device  | Default I2C Address |
| ------- | ------------------- |
| SSD1306 | `0x3C` (or `0x3D`)  |
| MCP4728 | `0x60`              |

Since two cores access the same bus, a **mutex is required** to prevent collisions.

### Access Patterns (Why Contention is Minimal)

- **MCP4728**: Frequent but very short — a 4-channel fast write is ~5–6 bytes, completed in microseconds at 400kHz. Core 1 owns the bus almost all the time.
- **SSD1306**: Longer transactions (~2ms for full frame at 400kHz) but only triggered by encoder interaction — rare at human timescales.

In practice Core 1 holds the bus continuously and Core 0 dips in occasionally. The mutex overhead is negligible.

---

## I2C Bus Implementation

### Enable Fast Mode (400kHz)

```cpp
Wire.begin();
Wire.setClock(400000);  // Fast Mode — required for acceptable DAC update rates
```

At 400kHz, a full 4-channel MCP4728 multi-write completes in ~125µs, giving a usable waveform sample rate well above LFO needs.

### Mutex Setup

```cpp
#include <hardware/sync.h>

mutex_t i2cMutex;

void setup() {
    mutex_init(&i2cMutex);
}
```

### Safe Display Update (Core 0 — blocking)

Core 0 can afford to wait since it only updates on encoder changes:

```cpp
void safeDisplayUpdate() {
    mutex_enter_blocking(&i2cMutex);
    display.display();       // or u8g2.sendBuffer()
    mutex_exit(&i2cMutex);
}
```

### Safe DAC Write (Core 1 — non-blocking)

Core 1 must **never block** on the display refresh. Missing one DAC update per display cycle is completely inaudible:

```cpp
void safeDACWrite(uint16_t v1, uint16_t v2, uint16_t v3, uint16_t v4) {
    if (mutex_try_enter(&i2cMutex, nullptr)) {
        dac.fastWrite(v1, v2, v3, v4);
        mutex_exit(&i2cMutex);
    }
    // If mutex is held by Core 0, just retry next waveform cycle
}
```

### IMPORTANT: Wrap at the Library Call Level

The Adafruit/U8g2 display libraries manage their own `Wire` calls internally and are not mutex-aware. Always wrap at the **library call level**, never inside the library:

```cpp
// CORRECT — wrap the library call
safeDisplayUpdate();

// WRONG — the library will call Wire internally without the mutex
display.display();  // called directly from either core
```

---

## Inter-Core Parameter Sharing

### Approach: Mutex-Protected Parameter Struct

For ClockForge's many parameters, a mutex-protected shared struct is cleaner than the raw FIFO. The FIFO is better for simple event notifications (e.g. "preset load requested").

```cpp
#include <hardware/sync.h>

// Full parameter state — add all ClockForge parameters here
struct ClockParams {
    uint16_t bpm;
    bool masterStop;
    struct OutputParams {
        uint16_t division;      // multiplier/divider ratio
        bool enabled;
        uint8_t probability;    // pulse probability 0-100%
        bool euclideanEnabled;
        uint8_t euclidSteps;
        uint8_t euclidTriggers;
        uint8_t euclidRotation;
        uint8_t euclidPad;
        uint8_t swingAmount;
        uint8_t swingEvery;
        uint8_t phaseShift;     // 0-100%
        uint8_t dutyCycle;      // 1-99%
        uint8_t waveform;       // enum: SQ, TRI, SAW, SIN, etc.
        uint8_t level;          // 0-100% → 0-5V (Out 3/4 only)
        int8_t offset;          // signed offset (Out 3/4 only)
        bool quantizeEnabled;
        uint8_t rootNote;
        uint8_t scale;
        int8_t octave;          // -3 to +3
    } output[4];
    struct CVInput {
        uint8_t target;         // which parameter this CV modulates
        uint8_t attenuation;
        int8_t offset;
    } cvInput[2];
    uint8_t extClockDivider;    // 1-16
};

mutex_t paramMutex;
ClockParams params;             // shared struct, access always via mutex
```

### Core 0 Writes (encoder changes a parameter)

```cpp
mutex_enter_blocking(&paramMutex);
params.output[n].division = newValue;
mutex_exit(&paramMutex);
```

### Core 1 Reads (snapshot approach — never blocks)

Core 1 works on a local copy so it is never stalled waiting for the UI:

```cpp
ClockParams localParams;  // Core 1's working copy

void loop1() {
    // Non-blocking snapshot of shared params
    if (mutex_try_enter(&paramMutex, nullptr)) {
        localParams = params;
        mutex_exit(&paramMutex);
    }
    // Always use localParams for engine work — never params directly
    runClockEngine(localParams);
}
```

If the mutex is held (Core 0 is writing), Core 1 simply uses its previous snapshot. At audio/CV rates this is imperceptible.

---

## Arduino Dual-Core Entry Points

The `arduino-pico` core provides separate setup/loop for each core — no SDK boilerplate needed:

```cpp
// Core 0 — UI
void setup()  { /* Wire, display, encoder, mutex init */ }
void loop()   { /* encoder poll, menu, safeDisplayUpdate() */ }

// Core 1 — real-time engine
void setup1() { /* DAC init, timer setup */ }
void loop1()  { /* waveform engine, safeDACWrite(), param snapshot */ }
```

### FIFO for Event Notifications (optional)

Use the FIFO for simple one-shot events from Core 0 → Core 1, rather than for full parameter state:

```cpp
// Core 0 signals Core 1 to reload preset
rp2040.fifo.push(EVENT_PRESET_LOAD);

// Core 1 checks non-blocking
if (rp2040.fifo.rcount() > 0) {
    uint32_t event = rp2040.fifo.pop();
    handleEvent(event);
}
```

---

## MCP4728 DAC Notes

- **12-bit resolution**: 4096 steps across 5V = ~1.2mV per step — sufficient for smooth waveforms and precise CV
- **Multi-write command**: Update all 4 channels in a single I2C transaction — keeps updates atomic and glitch-free. Important when Out 1/2 are clocks and Out 3/4 are waveforms that must stay phase-coherent
- **Internal voltage reference**: Use for stable CV output independent of power rail noise
- **EEPROM**: Stores startup values — useful but write endurance is ~1M cycles. Keep EEPROM writes to explicit user preset saves only, never auto-save on every parameter change
- **Library**: Adafruit MCP4728 library supports fast multi-channel write

```cpp
#include <Adafruit_MCP4728.h>
Adafruit_MCP4728 dac;

void setup1() {
    dac.begin();  // uses Wire (shared bus)
    dac.setChannelValue(MCP4728_CHANNEL_A, 0);
    // ...
}

// Atomic 4-channel update
void updateDAC(uint16_t a, uint16_t b, uint16_t c, uint16_t d) {
    if (mutex_try_enter(&i2cMutex, nullptr)) {
        dac.fastWrite(a, b, c, d);  // single I2C transaction
        mutex_exit(&i2cMutex);
    }
}
```

---

## SSD1306 Display Notes

- At 400kHz Fast Mode, full 128×64 frame refresh ≈ 2ms
- **Use partial/dirty region updates** — only redraw regions that changed. Especially important for the Euclidean pattern display which animates in real-time
- **U8g2 library** is recommended over Adafruit SSD1306 for better partial update support (`u8g2.updateDisplayArea()`)
- Display only redraws on encoder interaction — full redraws at human timescales, bus contention with Core 1 is negligible

---

## Clock Output Timing

For outputs 1 and 2 (square/gate only), use a `repeating_timer` on Core 1 for tight GPIO toggling. Jitter will be low microsecond range — perfectly acceptable for Eurorack:

```cpp
#include <hardware/timer.h>

repeating_timer_t clockTimer;

bool clockCallback(repeating_timer_t *rt) {
    // Step all outputs, apply Euclidean, swing, phase
    stepClockEngine();
    return true;  // keep repeating
}

void setup1() {
    // Fire at sub-millisecond intervals for clock resolution
    add_repeating_timer_us(-100, clockCallback, nullptr, &clockTimer);
}
```

For outputs 3 and 4 (waveforms), update the MCP4728 channels from within `loop1()` at the highest rate the I2C bus allows.

---

## Waveform Generation Notes

Outputs 3 and 4 support: Square, Triangle, Sawtooth, Sine, Parabolic, Log/Exp/Inv envelopes, Noise, Smooth Random, Sample & Hold, AD/AR/ADSR envelopes, Play, Reset, Quantize.

- **Sine/Parabolic**: Pre-compute a lookup table in `setup1()` to avoid `sin()` calls in the hot loop
- **S&H and Smooth Random**: Keep per-output state (last value, interpolation target)
- **AD/AR/ADSR**: Triggered by CV inputs. Can have up to 2 running simultaneously (one per CV input → one per output 3/4). Support configurable log/lin/exp curves and retriggering
- **Quantizer**: Applied post-waveform. Root note + scale + octave transpose. Can also quantize an external CV input routed through ClockForge

---

## Euclidean Rhythm Engine

Per-output state needed:

```cpp
struct EuclideanState {
    uint8_t steps;      // total steps (up to 64)
    uint8_t triggers;   // active hits
    uint8_t rotation;   // pattern rotation
    uint8_t pad;        // empty steps appended
    uint8_t position;   // current step (0 to steps+pad-1)
    uint64_t pattern;   // precomputed bitmask
};
```

Recompute `pattern` bitmask whenever steps/triggers/rotation/pad change — don't compute per-step. The Bjorklund algorithm generates the pattern; store as a bitmask for O(1) step lookup.

Euclidean pulse is also gated by the per-output **pulse probability** setting.

---

## Preset System

- 5 memory slots (0–4)
- Slot 0 auto-loads on boot
- Save/load via `EEPROM` or flash (RP2040 has no true EEPROM — use `arduino-pico`'s EEPROM emulation which writes to flash)
- Keep saves as explicit user actions — never auto-save. Flash endurance (~100K cycles) is lower than traditional EEPROM

---

## External Clock Sync

- Input on TRIG pin (0–5V)
- Module measures pulse interval → derives BPM → displays "E" on screen
- Input clock divider: /1 to /16 (for high-PPQN sources)
- Works 30–300 BPM
- On disconnect: reverts to last internal BPM
- High multipliers on slow external clocks → jitter. Warn user in UI if ratio is extreme

---

## CV Input Modulation

- 2 CV inputs (IN1, IN2), 0–5V
- Each assignable to: BPM, division/multiplication, pulse probability, swing amount, swing every, phase shift, duty cycle, waveform, level, offset, envelope trigger
- Per-input attenuation and offset configurable
- CV target only applies after user exits edit mode (prevents CV from changing parameters while scrolling menu)
- AD/AR/ADSR envelopes triggered by assigning CV input to Output 3/4 Env target

---

## Key Libraries

| Library                          | Purpose                                     |
| -------------------------------- | ------------------------------------------- |
| `arduino-pico` (Earle Philhower) | Dual-core Arduino framework for RP2040      |
| `Adafruit_MCP4728`               | DAC driver with fast multi-channel write    |
| `U8g2`                           | SSD1306 display with partial update support |
| `hardware/sync.h` (Pico SDK)     | `mutex_t` — accessible within arduino-pico  |
| `hardware/timer.h` (Pico SDK)    | `repeating_timer` for clock engine          |

---

## Common Pitfalls to Avoid

- **Never call `Wire` from both cores without the mutex** — even if addresses differ, the bus is shared silicon
- **Never block Core 1** — always `try_enter`, never `enter_blocking` on the DAC side
- **Don't auto-save to flash** — write explicitly on user preset save only
- **Wrap display library calls** — U8g2/Adafruit don't know about your mutex; wrap at the call site
- **Pre-compute Euclidean patterns** — recalculate bitmask on parameter change, not every step
- **Use sine lookup tables** — avoid `sin()`/`cos()` in the Core 1 hot loop
- **Parameter updates apply between waveform steps** — Core 1 ingests new params at a safe point in its cycle, never mid-calculation

---

*Generated from design discussion. Module: ClockForge by Voltage Foundry Modular. Part of the Forge series.*
