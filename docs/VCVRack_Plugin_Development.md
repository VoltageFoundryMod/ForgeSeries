# VCV Rack Plugin — Developer Reference

This document describes how the VCV Rack port of ClockForge is built. For the
user-facing feature list and shortcuts, see [VCVRack_Plugin.md](VCVRack_Plugin.md).

## Strategy: run the firmware, don't rewrite it

The plugin is **not** a reimplementation of the module. It compiles the
**unmodified hardware firmware** (`lib/`) inside VCV Rack by providing a thin
compatibility ("shim") layer that emulates the Arduino/RP2040 environment the
firmware expects. The OLED is emulated as a framebuffer, so the on-screen UI is
pixel-identical to the hardware and the entire menu/DSP system comes across for
free. Future firmware changes to `lib/` re-port with little or no effort.

```txt
VCV Rack  ──►  ClockForge.cpp (Module + widgets)
                     │  clean POD API (cfengine)
                     ▼
              fw_engine.cpp  ──includes──►  ../lib/*.hpp   (unchanged firmware)
                     │                          │
                     ▼                          ▼
              shim/ (Arduino, Wire, GFX/SSD1306, MCP4728, EEPROM, timer)
```

## Repository layout (`vcv-plugin/`)

| Path                            | Purpose                                                                                                                                                                                          |
| ------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `src/plugin.cpp` / `plugin.hpp` | Plugin entry point; registers `modelClockForge`.                                                                                                                                                 |
| `src/ClockForge.cpp`            | The Rack `Module` + widgets (display, encoder, ports). Includes **only** `fw_engine.hpp` — never the shim or firmware headers.                                                                   |
| `src/engine/fw_engine.hpp`      | Clean opaque/POD API (`namespace cfengine`). The only bridge header; exposes no Arduino/firmware types so it coexists with `rack.hpp`.                                                           |
| `src/engine/fw_engine.cpp`      | VCV-adapted port of the firmware's `main.cpp`: defines the firmware globals, non-blocking versions of the integration functions, and the `cfengine` entry points. Includes the shim + `../lib/`. |
| `src/shim/`                     | The Arduino/RP2040 compatibility layer (see below).                                                                                                                                              |
| `../lib/`                       | The hardware firmware — **reused unchanged** via the `-I../lib` include path on the engine translation unit only.                                                                                |

### The shim layer (`src/shim/`)

| File                 | Emulates                                                                                                                                         |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| `Arduino.h`          | Core Arduino API (millis/micros, digital/analog IO, math, random, Serial→no-op) + the `HostBridge` that connects firmware IO to the host module. |
| `WString.h`          | Arduino `String` over `std::string`.                                                                                                             |
| `Wire.h`             | I2C. Parses the MCP4728 multi-write frames from `DACWriteAll()` back into per-output values.                                                     |
| `Adafruit_GFX.h`     | Self-contained GFX with Adafruit's exact primitive algorithms + the real 5×7 `glcdfont.h` → pixel-identical text.                                |
| `Adafruit_SSD1306.h` | `Adafruit_GFX` subclass whose `drawPixel` targets a buffer; `display()` packs it into the host framebuffer (1bpp, row-major, MSB-first).         |
| `Adafruit_MCP4728.h` | DAC `setChannelValue` path (the `InitDAC`/`DACWrite` route).                                                                                     |
| `EEPROM.h`           | EEPROM emulation over a byte buffer (persisted via patch save/load).                                                                             |
| `hardware/timer.h`   | RP2040 repeating timer → inert stubs (the module drives the clock instead).                                                                      |
| `glcdfont.h`         | The genuine Adafruit 5×7 font (copied verbatim).                                                                                                 |

`HostBridge` is the per-instance IO struct: ADC inputs and GPIO in, DAC values
and the 1024-byte framebuffer out. The active bridge is `g_host`.

### The engine (`fw_engine`)

`fw_engine.cpp` mirrors the firmware's `main.cpp` globals and provides
non-blocking replacements for the hardware/dual-core integration functions
(`HandleOutputs`, `SetMasterState`, `RedrawDisplay`, `ShowTemporaryMessage`,
`RunCalibration` stub). The `cfengine` API it exposes:

- `createEngine` / `destroyEngine`
- `process(dt, cvVolts[2], clockGateHigh, outVolts[4])` — advances the clock,
  reads CV, renders outputs.
- `encoderTurn(detents)` / `encoderButton(pressed)` — UI.
- `getFramebuffer(out[1024])` — current screen.
- `serialize` / `deserialize` — the EEPROM blob, for patch persistence.

### Module & UI (`ClockForge.cpp`)

- Runs the engine at **control rate** (`ENGINE_DECIM = 8` samples ≈ 5.5 kHz at
  44.1 kHz; the 960-PPQN clock peaks near 4.8 kHz). Outputs are held between
  updates.
- `FramebufferDisplay` blits the firmware framebuffer as a **cached 128×64
  texture sampled `NVG_IMAGE_NEAREST`** for crisp pixels at any zoom.
- `EncoderKnob` — a custom control: drag to rotate (relative detents via
  `cursorLock`), click to push. UI events reach `process()` through
  `std::atomic` counters.
- Persistence: the EEPROM blob is stored as a JSON int array in
  `dataToJson`/`dataFromJson`.

### Time model

Engine time (`g_engineMicros`, backing `millis()`/`micros()`) advances by
`process()`'s `dt` — **not** wall-clock. This is deterministic and stays correct
when Rack renders faster than real time. The firmware's 20 Hz display
rate-limiter and external-clock timing depend on it.

### I/O mapping

- **CV in**: input volts → 0–4095 ADC (0–5 V range, matching hardware).
- **Outputs**: 0–4095 DAC → 0–5 V.
- **DAC channel swap**: hardware swaps DAC B/C; the shim undoes it
  (`hwToOut = {0,2,1,3}`) so outputs map 1:1 to jacks 1–4.

## Building & installing

Requires the [VCV Rack SDK](https://vcvrack.com/manual/Building) and a GCC
toolchain. On Windows this repo is built with **MSYS2 MinGW64** (`g++`, GNU
`make`), which are not on the default PATH.

```bash
# Windows / MSYS2 example
export PATH="/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH"
cd vcv-plugin
make RACK_DIR="<path-to>/Rack-SDK" \
     RACK_USER_DIR="<path-to>/Rack2" install
```

Notes:

- The firmware/shim require **C++17** (Rack defaults to C++11). The Makefile adds
  `EXTRA_CXXFLAGS += -std=c++17`, which lands after Rack's `-std` and wins.
- The shim include paths are scoped to **only** the engine TU
  (`build/src/engine/fw_engine.cpp.o: EXTRA_CXXFLAGS += -Isrc/shim -I../lib`) so
  they never leak into the rack-facing sources, which use `rack.hpp` types of the
  same names.
- `RACK_USER_DIR` must be passed explicitly: in the MSYS2 shell `LOCALAPPDATA`
  isn't exported, so `make install` would otherwise target the wrong folder.

## Known limitations / TODO

- **Per-instance state**: the firmware uses file-scope globals, and the same
  `lib/` must keep building for the RP2040 hardware, so it can't be rewritten
  into members. The framebuffer/DAC/ADC are already per-instance (`g_host`), but
  the DSP/menu state is shared — a second module instance in the same patch will
  share it. The planned fix is a per-instance "context-swap" of an `EngineState`
  snapshot around each engine entry point.
- Encoder feel (sensitivity, direction) and CV input ranges (0–5 V vs Rack's
  ±5/±10 V norms) are candidates for tuning / context-menu options.

## Implementation notes & gotchas

- **NanoVG batches draw calls** and flushes at end-of-frame. Calling
  `nvgDeleteImage` inside `draw()` frees the texture before the flush → blank
  screen. Create the image once, `nvgUpdateImage` each frame, never delete it in
  `draw()`. (Per-pixel rect fills also look blurry from antialiasing of
  fractional/overlapping rects — the nearest-sampled texture is the crisp way.)
- The engine module must stay isolated from `rack.hpp`: `String`, `Output`,
  `map`, etc. exist in both worlds. Keep the boundary at `fw_engine.hpp`.
- `quantizer.cpp` and `scales.cpp` are `#include`d by `outputs.hpp`, so they
  compile as part of the engine TU (no separate build entries needed).
