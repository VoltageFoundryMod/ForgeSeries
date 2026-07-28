# VCV Rack Plugin — Developer Reference

This document describes how the VCV Rack port of NoteForge is built. For the
user-facing feature list and shortcuts, see [VCVRack_Plugin.md](VCVRack_Plugin.md).

## Strategy: run the firmware, don't rewrite it

The plugin is **not** a reimplementation of the module. It compiles the
**unmodified hardware firmware** (`lib/`) inside VCV Rack by providing a thin
compatibility ("shim") layer that emulates the Arduino/RP2040 environment the
firmware expects. The OLED is emulated as a framebuffer, so the on-screen UI is
pixel-identical to the hardware and the entire menu + quantizer + envelope
system comes across for free. Future firmware changes to `lib/` re-port with
little or no effort.

```txt
VCV Rack  ──►  NoteForge.cpp (Module + widgets)
                     │  clean POD API (nfengine)
                     ▼
              fw_engine.cpp  ──includes──►  ../lib/*.hpp   (unchanged firmware)
                     │                          │
                     ▼                          ▼
              ForgeSeries-VCVLib/shim/ (Arduino, Wire, GFX/SSD1306, MCP4728, EEPROM)
```

The shim, the reusable `ForgeModule`/`IEngine`/widget layer and the build
fragment all live in the shared [ForgeSeries-VCVLib](https://github.com/VoltageFoundryMod/ForgeSeries-VCVLib)
repository, which ClockForge uses too. This plugin adds only the parts that are
actually specific to NoteForge.

## Repository layout (`vcv-plugin/`)

| Path                            | Purpose                                                                                                                                                                             |
| ------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/plugin.cpp` / `plugin.hpp` | Plugin entry point; registers `modelNoteForge`.                                                                                                                                     |
| `src/NoteForge.cpp`             | The Rack `Module` + widgets (display, encoder, ports) and the context menu. Includes **only** `fw_engine.hpp` — never the shim or firmware headers.                                 |
| `src/engine/fw_engine.hpp`      | Clean opaque/POD API (`namespace nfengine`). The only bridge header; exposes no Arduino/firmware types so it coexists with `rack.hpp`.                                              |
| `src/engine/fw_engine.cpp`      | VCV-adapted port of the firmware's `main.cpp`: defines the firmware globals, non-blocking versions of the integration functions, and the `nfengine` entry points.                    |
| `src/engine/engine_state.def`   | The registry of per-instance firmware state (see below).                                                                                                                            |
| `res/NoteForge-src.svg`         | Panel authoring source (real `<text>` elements).                                                                                                                                     |
| `res/NoteForge.svg`             | The shipped panel — generated from the source with text converted to paths.                                                                                                          |
| `test/isolation_test.cpp`       | Two-instance state isolation harness; no Rack or hardware required.                                                                                                                  |
| `../lib/`                       | The hardware firmware — **reused unchanged** via the `-I../lib` include path on the engine translation unit only.                                                                    |

## The engine (`fw_engine`)

`fw_engine.cpp` mirrors the firmware's `main.cpp` globals and provides
non-blocking replacements for the hardware/dual-core integration functions
(`HandleOutputs`, `RedrawDisplay`, `ShowTemporaryMessage`). The `nfengine` API
it exposes:

- `createEngine` / `destroyEngine`
- `process(dt, cvVolts[2], trigGateHigh, outVolts[4])` — reads the pitch CVs,
  quantizes, renders the envelopes.
- `encoderTurn(detents)` / `encoderButton(pressed)` — UI.
- `getFramebuffer(out[1024])` — current screen.
- `serialize` / `deserialize` — the EEPROM blob, for patch persistence.
- A curated parameter bridge (scales, roots, note masks, octave, glide, gate
  mode, sync, attack/decay) backing the Rack context menu.

## Module & UI (`NoteForge.cpp`)

- Runs the engine at **control rate** (`engineDecim = 8` samples ≈ 5.5 kHz at
  44.1 kHz). Outputs are held between updates.
- `forgevcv::FramebufferDisplay` blits the firmware framebuffer as a cached
  128×64 texture sampled `NVG_IMAGE_NEAREST` for crisp pixels at any zoom.
- `forgevcv::EncoderKnob` — drag to rotate (relative detents via `cursorLock`),
  click to push. UI events reach `process()` through `std::atomic` counters.
- Persistence: the EEPROM blob is stored as a JSON int array via
  `ForgeModule::baseToJson` / `baseFromJson`.

### Input CV handling: shift, never scale

`forgevcv::ForgeModule` offers a `cvRange` remap that linearly rescales a
bipolar input onto the firmware's 0–5V domain. **NoteForge deliberately does not
use it.** Pitch CV is 1V/oct: rescaling −5…+5V onto 0…5V would halve
volts-per-octave and an octave would stop being an octave.

Instead the module exposes an **Input CV Shift** (0V / +1V / +2V / +3V) that
*offsets* the incoming voltage into the hardware's window, leaving
volts-per-octave untouched. A sequencer putting out −2…+3V works correctly with
a +2V shift. `cvRange` is still serialized by the base class; it is simply
unused here.

## Time model

Engine time (`g_engineMicros`, backing `millis()`/`micros()`) advances by
`process()`'s `dt` — **not** wall-clock. This is deterministic and stays correct
when Rack renders faster than real time. The firmware's 20 Hz display
rate-limiter, envelope timing and glide all depend on it.

> If you write a headless harness and the screen never changes, this is why: the
> display rate-limiter needs engine time to advance, so call `process()` between
> `getFramebuffer()` calls.

## I/O mapping

- **CV in**: input volts (after the shift) → 0–4095 ADC (0–5 V range, matching hardware).
- **Outputs**: 0–4095 DAC → 0–5 V.
- **DAC channel swap**: hardware swaps DAC B/C; the shim undoes it
  (`hwToOut = {0,2,1,3}`) so outputs map 1:1 to jacks 1–4.
- Jack order is CV 1, CV 2, GATE 1, GATE 2.

## Per-instance state (multi-instance context-swap)

The firmware keeps all DSP/menu state in **file-scope globals** so the same
`lib/` builds unchanged for the RP2040. That makes the state process-global,
which VCV — where a patch can hold many module instances — cannot allow. IO
(framebuffer/DAC/ADC) is already per-instance via the `g_host` pointer; the
DSP/menu state is made per-instance by a **context-swap** instead of a rewrite:

- Each `Engine` owns an `EngineState` snapshot mirroring every mutable global.
  The globals it covers are registered **once** in
  [`engine_state.def`](../vcv-plugin/src/engine/engine_state.def) (an X-macro
  list expanded three ways — struct fields, swap, and deep copy). Adding a
  firmware global is a one-line edit there.
- Every `nfengine` entry point wraps its body in an `EngineScope` RAII guard
  that locks a mutex, points `g_host` at the instance, **swaps** its
  `EngineState` into the live globals, runs the firmware, then swaps it back
  out. Swapping (never copying) keeps every heap-backed `String`/`std::vector`
  living in exactly one place at a time, so it is allocation-free and safe; the
  swap is symmetric and self-restoring, so the resting global values are
  irrelevant.
- One `std::mutex` serializes all entry points across all instances, so
  `process()` (audio thread) and `getFramebuffer()` (draw thread) never
  interleave on the shared globals.
- New instances are seeded from a **pristine snapshot** captured on the first
  `createEngine()` (the firmware's power-on defaults live in the globals'
  static initializers, e.g. `menuItem = 1`, not in `EngineState`).

The registry macros are `CF_SCALAR`, `CF_ARRAY`, `CF_OBJECT` and `CF_OBJARRAY`.
The two `QuantizerChannel` voices — which hold essentially the whole DSP state —
go through `CF_OBJARRAY`, so they are swapped element-wise with `std::swap`.

Verified by [`test/isolation_test.cpp`](../vcv-plugin/test/isolation_test.cpp),
a host harness (no Rack needed) that drives two engines independently; build and
run with `test/build_isolation_test.sh`. It is also the guard against a future
global being added to the firmware but not to the registry.

Deliberately **shared** (safe under the entry-point lock): `display` (redrawn
each frame), `displayMgr` (rate-limiter state), and the one-time shim setup
(interrupt vector, `display.begin`, `InitDAC`). Promote any of these into
`engine_state.def` if a visible cross-instance artifact appears.

## The panel

`res/NoteForge.svg` is **generated**. Rack renders panels through nanosvg, which
ignores `<text>` elements, so every glyph has to be a path. Edit
`res/NoteForge-src.svg` (which keeps real text) and regenerate:

```bash
cd vcv-plugin/res
inkscape NoteForge-src.svg --export-type=svg --export-plain-svg \
         --export-text-to-path --export-filename=NoteForge.svg
```

Never hand-edit `NoteForge.svg`. The jack/screen/encoder coordinates in the
source are the same ones used in `src/NoteForge.cpp`, so the two must be changed
together.

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

- The firmware/shim require **C++17** (Rack defaults to C++11). `forgevcv.mk`
  adds `EXTRA_CXXFLAGS += -std=c++17`, which lands after Rack's `-std` and wins.
- The shim include paths are scoped to **only** the engine TU
  (`build/src/engine/fw_engine.cpp.o: EXTRA_CXXFLAGS += -I$(FORGEVCV_SHIM) -I../lib`)
  so they never leak into the rack-facing sources, which use `rack.hpp` types of
  the same names.
- `RACK_USER_DIR` must be passed explicitly: in the MSYS2 shell `LOCALAPPDATA`
  isn't exported, so `make install` would otherwise target the wrong folder.
- `FORGEVCV` defaults to `../../ForgeSeries-VCVLib`. Override it if the shared
  layer lives elsewhere.

## Implementation notes & gotchas

- **NanoVG batches draw calls** and flushes at end-of-frame. Calling
  `nvgDeleteImage` inside `draw()` frees the texture before the flush → blank
  screen. (Handled inside `forgevcv::FramebufferDisplay`.)
- The engine module must stay isolated from `rack.hpp`: `String`, `map`, etc.
  exist in both worlds. Keep the boundary at `fw_engine.hpp`.
- Header-scope tables in `lib/` (`scaleNames`, `GateModeNames`, …) are `static`
  so they can be included from several translation units. Keep new ones static.
- The calibration wizard is not ported: Rack's inputs and outputs are ideal, so
  there is nothing to trim. `LoadCalibration()` returns the nominal mapping.
