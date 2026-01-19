# ClockForge Firmware - AI Coding Agent Instructions

## Project Overview

ClockForge is a **Eurorack modular synthesizer clock/CV module** firmware for the **Seeed XIAO SAMD21** microcontroller. It generates clock signals, gates, and CV waveforms with advanced features like Euclidean rhythms, swing, quantization, and envelopes. The module draws inspiration from ALM Pamela's Pro Workout and similar clock modules.

**Part of the Forge Series**: This is one module in a hardware platform family. The hardware is shared; firmware defines module behavior.

**Hardware Platform**: SAMD21-based (ARM Cortex-M0+), Arduino framework, PlatformIO build system.

## Architecture & Key Components

### Core Structure

- **[src/main.cpp](src/main.cpp)** (2000+ lines): Main event loop, menu system, encoder handling, display updates, timer initialization
- **[src/outputs.hpp](src/outputs.hpp)** (1200+ lines): `Output` class implementing all waveform generation, Euclidean rhythms, swing, envelopes, quantization
- **[lib/](lib/)**: Header-only libraries for hardware abstraction and algorithms

### Major Subsystems

1. **Timing Core** (PPQN = 192):

   - Hardware timer (TCC0) generates interrupts at 192 pulses per quarter note
   - Global `tickCounter` incremented in timer ISR
   - External clock sync via interrupt-driven PPQN detection

2. **Output System** (4 outputs):

   - Outputs 1-2: Digital gates (PWM-capable for duty cycle)
   - Outputs 3-4: DAC outputs (internal DAC0 + MCP4725 I2C DAC) for CV/waveforms
   - Each output is an `Output` object with independent divider, swing, probability, Euclidean rhythm, phase shift, duty cycle

3. **Waveform Generation**:

   - 18 waveform types including Square, Sine, Triangle, Sawtooth, Noise, S&H, envelopes (AD/AR/ADSR)
   - Special modes: quantized CV input passthrough, reset triggers, play/stop gate
   - See `WaveformType` enum in [src/outputs.hpp](src/outputs.hpp#L17)

4. **CV Input Modulation**:

   - 2 CV inputs (0-5V) with configurable targets via `CVTarget` enum
   - Each CV can modulate BPM, dividers, swing, duty cycle, output levels, waveforms, etc.
   - CV calibration: `ADCCal[]` and `ADCOffset[]` arrays in [src/main.cpp](src/main.cpp#L21-L22)

5. **Persistence**:
   - Uses `FlashStorage` library (SAMD21 flash emulation)
   - 4 save slots, see [lib/loadsave.hpp](lib/loadsave.hpp)
   - `LoadSaveParams` struct holds all configuration state

### Hardware Abstraction

**[lib/pinouts.hpp](lib/pinouts.hpp)**: Pin assignments for XIAO SAMD21

- **Important**: Pins 4/5 reserved for I2C (OLED + DAC)
- Gates: Pins 1-2 (inverted logic: `LOW = HIGH output`)
- CV inputs: Pins 8-9 (12-bit ADC)
- Clock input: Pin 7 (external sync)

**[lib/boardIO.hpp](lib/boardIO.hpp)**: Hardware I/O abstractions

- `SetPin(pin, value)`: Gate outputs (inverted logic!)
- `DACWrite(pin, value)`: 0=internal DAC, 1=MCP4725
- ADC configuration: 128x averaging for stability (~0.7ms per read)

## Critical Developer Workflows

### Build & Upload

```bash
# Build firmware (PlatformIO task)
pio run
# Or use VS Code task: "Build PlatformIO Project"

# Convert to UF2 format (SAMD21 bootloader)
./scripts/convertFirmwareUF2.sh .pio/build/seeed_xiao/firmware.bin
# Or use task: "Convert ELF to UF2 Firmware for SAMD21"

# Upload (copies to mounted XIAO bootloader volume)
./scripts/uploadFirmware.sh .pio/build/seeed_xiao/firmware.uf2
# Or use task: "Upload Firmware to Seeeduino XIAO"
```

**UF2 Bootloader**: Double-tap reset button on XIAO to enter bootloader mode. Device appears as `/Volumes/Seeed XIAO` (macOS) or similar mass storage device.

### Testing

- **Native tests**: `test_native/` uses GoogleTest with ArduinoFake mocks
- Run with: `pio test -e native`
- Currently tests output waveform generation logic without hardware

### Debugging

- Serial monitor: `pio device monitor` (115200 baud)
- **Performance caveat**: Encoder reads and display updates cause timing glitches. This is expected and mentioned in README.

## Project-Specific Patterns & Conventions

### Code Organization

- **Header-only libraries**: All `lib/*.hpp` files are included directly, no separate `.cpp` compilation
- **Single translation unit**: `src/main.cpp` includes everything; `outputs.hpp` includes `quantizer.cpp` and `scales.cpp` directly
- **Build flags**: `-std=gnu++17 -I lib` in [platformio.ini](platformio.ini#L18)

### Naming Conventions

- **CapitalCase**: Functions (`UpdateBPM()`, `HandleEncoderClick()`)
- **\_underscoreCamelCase**: Private class members (`_isPulseOn`, `_dutyCycle`)
- **ALL_CAPS**: Constants and pin definitions (`PPQN`, `MAXDAC`, `CLK_IN_PIN`)
- **Enums**: PascalCase names, PascalCase values (`OutputType::DigitalOut`, `WaveformType::ExpEnvelope`)

### State Management

- **Menu system**: `menuItem` (0-66) indexes current parameter, `menuMode` (0=navigate, >0=editing)
- **Unsaved changes**: `unsavedChanges` flag shows small circle in top-left of OLED
- **Master vs. individual state**: Global `masterState` controls all outputs; each output has independent `_state` that persists through global stop/start

### Critical Patterns

**Output Pulse Generation**:

```cpp
// From Output::Pulse() in outputs.hpp
// Called every timer tick with PPQN and global tickCounter
// Handles: divider/multiplier, swing, probability, Euclidean, phase shift, duty cycle
void Pulse(int PPQN, unsigned long tickCounter);
```

**Interrupt Safety**:

- Timer ISR updates `tickCounter` and calls `ClockPulse()` → `HandleOutputs()` → `outputs[i].Pulse()`
- Use `noInterrupts()`/`interrupts()` when accessing shared state from non-ISR code (see flash save/load)

**Divider/Multiplier Array**:

- Defined in [src/outputs.hpp](src/outputs.hpp) as `dividerMultiplier[]` array
- Index-based selection from `/128` to `x32` including triplets (1.5, 3x) and dotted notes
- CV modulation maps 0-5V input to array index

**Euclidean Rhythm**:

- Bjorklund's algorithm in [lib/euclidean.hpp](lib/euclidean.hpp)
- Per-output params: steps, triggers, rotation, padding
- Pattern regenerated whenever params change; stored in `_euclideanRhythm[]` array

**Quantizer**:

- Lives in [lib/quantizer.cpp](lib/quantizer.cpp) and [lib/scales.cpp](lib/scales.cpp)
- Can quantize: output waveform, CV input passthrough
- Configurable: scale, root note, octave shift, channel sensitivity (pitch bend range)

## External Dependencies

**PlatformIO Libraries** (from [platformio.ini](platformio.ini#L15-L18)):

- `FlashStorage`: SAMD21 flash-based EEPROM emulation
- `Adafruit SSD1306` + `Adafruit GFX`: OLED display (I2C, 128x64)
- `Encoder` (PJRC): Rotary encoder with interrupt optimization
- `Adafruit MCP4725`: 12-bit I2C DAC for Output 4

**Framework**: Arduino core for SAMD21 (`platform = atmelsam`)

**Custom Timer Library**: `TimerTCC0.h` (not in lib/, likely installed via PlatformIO)

## Common Tasks & Gotchas

### Adding a New Waveform

1. Add enum value to `WaveformType` in [src/outputs.hpp](src/outputs.hpp#L17)
2. Add description to `WaveformTypeDescriptions[]` array
3. Implement generation logic in `Output::GeneratePulse()` switch statement
4. Update `WaveformTypeLength` calculation

### Adding a CV Modulation Target

1. Add enum to `CVTarget` in [src/main.cpp](src/main.cpp#L47)
2. Add description to `CVTargetDescription[]` string array
3. Implement handling in `HandleCVTarget()` function
4. Update `CVTargetLength` calculation

### Modifying Menu System

- Menu items are hardcoded in `HandleEncoderClick()` and `HandleDisplay()` switch statements
- Menu navigation bar: right side of display shows position in menu tree
- Update `menuItems` count when adding/removing parameters

### Gate Output Polarity

**CRITICAL**: Gate outputs 1-2 use **inverted logic** in hardware!

- `digitalWrite(pin, LOW)` = gate HIGH (5V)
- `digitalWrite(pin, HIGH)` = gate LOW (0V)
- See `SetPin()` in [lib/boardIO.hpp](lib/boardIO.hpp#L94)

### Performance Considerations

- **Display updates throttled**: 50ms minimum interval + frame skipping (see `DISPLAY_UPDATE_INTERVAL`, `FRAME_SKIP_COUNT`)
- **ADC reads slow**: 128x averaging takes ~0.7ms per channel
- **Interrupt budget**: Keep timer ISR fast; complex waveform math done outside ISR when possible

### Calibration

- ADC calibration values hardcoded: `ADCCal[2]` and `ADCOffset[2]`
- CV input scaling: `offsetScale[NUM_CV_INS][2]` stores per-channel offset/scale
- Modify these if CV input tracking is inaccurate

## When Modifying This Codebase

1. **Respect timing constraints**: Changes affecting timer ISR or output pulse generation may introduce jitter
2. **Test with hardware**: Timing behavior differs significantly from native tests
3. **Check flash usage**: SAMD21 has limited flash; avoid bloat from templates or excessive inlining
4. **Preserve save/load compatibility**: Changes to `LoadSaveParams` struct invalidate saved slots
5. **Update `unsavedChanges` flag**: Any parameter modification should set this to `true`
6. **Document waveforms**: User-facing features should update [Readme.md](Readme.md)

## Key Files Reference

| File                                   | Purpose                                           |
| -------------------------------------- | ------------------------------------------------- |
| [src/main.cpp](src/main.cpp)           | Main loop, menu, encoder, display, initialization |
| [src/outputs.hpp](src/outputs.hpp)     | Output class, all waveform/rhythm logic           |
| [lib/boardIO.hpp](lib/boardIO.hpp)     | Hardware I/O abstraction (DAC, gates, ADC)        |
| [lib/pinouts.hpp](lib/pinouts.hpp)     | XIAO SAMD21 pin assignments                       |
| [lib/loadsave.hpp](lib/loadsave.hpp)   | Flash storage, save slots                         |
| [lib/euclidean.hpp](lib/euclidean.hpp) | Bjorklund's Euclidean rhythm algorithm            |
| [lib/quantizer.cpp](lib/quantizer.cpp) | Pitch quantization logic                          |
| [lib/scales.cpp](lib/scales.cpp)       | Musical scale definitions                         |
| [platformio.ini](platformio.ini)       | Build config, dependencies, test environments     |

---

**For questions on hardware specifics, consult the Seeed XIAO SAMD21 datasheet and Forge Series hardware documentation.**
