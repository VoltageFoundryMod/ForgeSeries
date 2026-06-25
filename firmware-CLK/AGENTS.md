# ClockForge Firmware - AI Coding Agent Instructions

## Project Overview

ClockForge is a **Eurorack modular synthesizer clock/CV module** firmware for the **Seeed XIAO RP2040** microcontroller. It generates clock signals, gates, and CV waveforms with features like Euclidean rhythms, swing, quantization, and envelopes. See [Readme.md](Readme.md) for full user-facing feature documentation.

**Part of the Forge Series**: This is one module in a hardware platform family. The hardware is shared; firmware defines module behavior.

**Hardware Platform**: RP2040 (dual-core ARM Cortex-M0+), Arduino framework (`earlephilhower` core), PlatformIO build system.

## Architecture & Key Files

### Module Map

| File | Purpose |
|------|---------|
| [src/main.cpp](src/main.cpp) | Entry point, `setup()`, `loop()`, `setup1()`, `loop1()` (Core 1), global variables |
| [lib/clockEngine.hpp](lib/clockEngine.hpp) | BPM, PPQN, `tickCounter`, external clock sync, `ClockPulse()` ISR |
| [lib/outputs.hpp](lib/outputs.hpp) | `Output` class — all waveform/rhythm/envelope logic |
| [lib/cvInputs.hpp](lib/cvInputs.hpp) | `CVTarget` enum, ADC reads, calibration lookup, `HandleCVInputs()` |
| [lib/menuDefinitions.hpp](lib/menuDefinitions.hpp) | `MenuItem` struct, `RowStyle`/`MenuItemType` enums |
| [lib/menuHandlers.hpp](lib/menuHandlers.hpp) | `MENU_ITEMS[]` array, all setter/getter lambdas — **read the developer guide at the top of this file before editing** |
| [lib/menuDisplay.hpp](lib/menuDisplay.hpp) | Low-level draw primitives (`MD_Row`, `MD_TwoColRow`, …) |
| [lib/menuRender.hpp](lib/menuRender.hpp) | `HandleDisplay()`, per-group renderers |
| [lib/displayManager.hpp](lib/displayManager.hpp) | `DisplayManager` class — rate-limited non-blocking display |
| [lib/boardIO.hpp](lib/boardIO.hpp) | `InitIO()`, `InitWire()`, `InitDAC()`, `DACWriteAll()`, `ReadADC()` |
| [lib/pinouts.hpp](lib/pinouts.hpp) | XIAO RP2040 pin/GPIO assignments |
| [lib/presetManager.hpp](lib/presetManager.hpp) | `LoadSaveParams`, `CalibrationData`, `LoadDefaultParams()`, `CollectParams()`, `UpdateParameters()` |
| [lib/storage.hpp](lib/storage.hpp) | Platform storage backend (RP2040 EEPROM emulation) |
| [lib/calibration.hpp](lib/calibration.hpp) | Hardware calibration wizard (hold encoder on boot) |
| [lib/calibrationData.hpp](lib/calibrationData.hpp) | `CalibrationData` struct, LUT constants |
| [lib/euclidean.hpp](lib/euclidean.hpp) | Bjorklund's Euclidean rhythm algorithm |
| [lib/quantizer.cpp](lib/quantizer.cpp) | Pitch quantization logic |
| [lib/scales.cpp](lib/scales.cpp) | Musical scale definitions |
| [lib/metrics.hpp](lib/metrics.hpp) | `PerformanceMetrics` — ISR/loop/display timing stats |
| [lib/encoder.hpp](lib/encoder.hpp) | Rotary encoder wrapper |
| [lib/utils.hpp](lib/utils.hpp) | Shared math/utility helpers |
| [platformio.ini](platformio.ini) | Build config, dependencies, test environments |

### Dual-Core Architecture

- **Core 0** (`loop()`): main logic — encoder, CV inputs, clock engine, DAC writes via Wire1
- **Core 1** (`loop1()`): display only — `display.display()` via Wire (SSD1306)
- Coordination: `_displayFrameReady` (Core 0 sets, Core 1 clears) and `_displayLocked` (Core 0 pauses Core 1 GFX)
- Wire (GPIO 6/7, I2C1) → SSD1306 display **only** (Core 1 exclusive at runtime)
- Wire1 (GPIO 0/1, I2C0) → MCP4728 DAC **only** (Core 0 exclusive at runtime)
- No mutex needed — separate hardware I2C blocks on separate cores

### Key Subsystems

**Timing Core** (PPQN = 960):
- RP2040 `repeating_timer_t` fires `ClockPulse()` → `HandleOutputs()` → `outputs[i].Pulse()`
- `tickCounter` incremented every tick; ISR must stay fast
- External clock sync via `HandleExternalClock()` interrupt (rising edge on `CLK_IN_PIN`)

**Output System** — all 4 outputs via MCP4728 quad 12-bit I2C DAC:
- **No inverted gate logic** (old SAMD21 behavior is gone)
- Physical channel swap in hardware: DACB/DACC are swapped — see `_chanMap[]` in [lib/boardIO.hpp](lib/boardIO.hpp)
- 21 waveform types: `Square`, `Triangle`, `Sine`, `Parabolic`, `Sawtooth`, `ExpEnvelope`, `LogEnvelope`, `InvExpEnvelope`, `InvLogEnvelope`, `Hatchet2`, `Hatchet4`, `Noise`, `SmoothNoise`, `SampleHold`, `ResetTrig`, `Play`, `ADEnvelope`, `AREnvelope`, `ADSREnvelope`, `CVInput1`, `CVInput2` (mirror CV in 1/2 to the output; quantise via the Quantize toggle)

**CV Input Modulation**:
- 2 CV inputs (0–5V) mapped to `CVTarget` targets via `HandleCVTarget()` in [lib/cvInputs.hpp](lib/cvInputs.hpp)
- Calibration uses a 2-point linear LUT per channel (1V + 3V reference); stored in EEPROM via `CalibrationData`

**Persistence**:
- 10 preset slots; slot 0 auto-loaded at boot
- Schema: `LoadSaveParams` in [lib/presetManager.hpp](lib/presetManager.hpp); platform storage in [lib/storage.hpp](lib/storage.hpp)
- EEPROM layout: `[0 .. NUM_SLOTS×sizeof(LoadSaveParams))` for presets, then `CalibrationData`
- **Changing `LoadSaveParams` invalidates all saved slots** — increment `VALID_MAGIC` or add migration

## Build, Test & Upload

```bash
pio run                    # build (env: xiao_rp2040)
pio test -e native         # run GoogleTest native tests
pio device monitor         # serial debug at 115200 baud
```

VS Code tasks: **Build PlatformIO Project** → **Convert ELF to UF2** → **Upload Firmware to Seeeduino XIAO**.

**Flashing**: Double-tap reset to enter UF2 bootloader; device mounts as mass storage.

## Naming Conventions

- **CapitalCase**: Free functions (`UpdateBPM()`, `HandleEncoderPosition()`)
- **_underscoreCamelCase**: Private class members (`_isPulseOn`, `_dutyCycle`)
- **ALL_CAPS**: Constants and macros (`PPQN`, `MAXDAC`, `CLK_IN_PIN`, `REQUEST_DISPLAY_REFRESH()`)
- **Enums**: PascalCase names and values (`OutputType::DACOut`, `WaveformType::ExpEnvelope`)

## Critical Patterns

**Display refresh** (always use the macro, never set `displayRefresh` directly):
```cpp
REQUEST_DISPLAY_REFRESH(); // marks dirty + resets screen-timeout timer
```

**Unsaved changes indicator** (use the manager, not the raw flag):
```cpp
displayMgr.SetUnsavedChanges(true);
unsavedChanges = true; // also keep the raw flag in sync for save/load checks
```

**Menu state**: `menuItem` is 1-based item number; `menuMode` is 0 = navigating, or equals the item number being edited. See the developer guide in [lib/menuHandlers.hpp](lib/menuHandlers.hpp).

**Adding a menu item**: Add an entry to `MENU_ITEMS[]` in [lib/menuHandlers.hpp](lib/menuHandlers.hpp). `MENU_ITEM_COUNT` is computed automatically. Items with the same `group` render on the same page.

## Common Tasks

### Adding a New Waveform

1. Add enum value to `WaveformType` in [lib/outputs.hpp](lib/outputs.hpp)
2. Add description string to `WaveformTypeDescriptions[]`
3. Implement generation in `Output::GeneratePulse()` switch statement
4. Update user-facing docs in [Readme.md](Readme.md)

### Adding a CV Modulation Target

1. Add enum to `CVTarget` in [lib/cvInputs.hpp](lib/cvInputs.hpp)
2. Add description to `CVTargetDescription[]`
3. Implement in `HandleCVTarget()` in the same file

### Adding a Menu Parameter

1. Write `valueFn` getter and `setter`/`action` in [lib/menuHandlers.hpp](lib/menuHandlers.hpp)
2. Add a `MenuItem` entry to `MENU_ITEMS[]` — group, rowStyle, and type determine rendering
3. Add the parameter to `LoadSaveParams`, `CollectParams()`, and `UpdateParameters()` in [lib/presetManager.hpp](lib/presetManager.hpp)
4. Set `displayMgr.SetUnsavedChanges(true)` and `unsavedChanges = true` inside the setter

## Gotchas & Constraints

- **No inverted gate logic** — this is NOT the SAMD21 version; all outputs are DAC via MCP4728
- **DAC channel swap**: hardware swaps DACB↔DACC; compensated by `_chanMap[]` in [lib/boardIO.hpp](lib/boardIO.hpp) — do not change without verifying with hardware
- **Core 1 owns Wire**: never call `Wire` (display bus) from Core 0 — use `_displayFrameReady`/`_displayLocked` flags
- **Keep ISR fast**: `ClockPulse()` runs at 960 PPQN; avoid heap allocation or blocking calls inside it
- **`quantizer.cpp` and `scales.cpp`** are `#include`d directly in [lib/outputs.hpp](lib/outputs.hpp) — they are not compiled separately
- **Calibration** wizard: hold encoder button during boot → runs [lib/calibration.hpp](lib/calibration.hpp); `CalibrationData` survives firmware flashes in EEPROM

---

**For user-facing feature documentation, see [Readme.md](Readme.md). For hardware specifics, consult the Seeed XIAO RP2040 datasheet.**
