# NoteForge Firmware - AI Coding Agent Instructions

## Project Overview

NoteForge is a **Eurorack modular synthesizer dual CV quantizer** firmware for the **Seeed XIAO RP2040**. It snaps two independent pitch CV inputs to user-editable scales and emits a quantized CV plus a gate/envelope per channel. See [Readme.md](Readme.md) for full user-facing feature documentation.

**Part of the Forge Series**: one module in a hardware platform family. The hardware is shared with ClockForge and the rest of the series; firmware defines module behavior.

**Hardware Platform**: RP2040 (dual-core ARM Cortex-M0+), Arduino framework (`earlephilhower` core), PlatformIO build system.

**Two targets from one codebase**: the same `lib/` builds for the RP2040 *and* for the VCV Rack plugin in [vcv-plugin/](vcv-plugin/), which compiles the unmodified firmware against an Arduino shim. Anything added to `lib/` must keep working in both — see [docs/VCVRack_Plugin_Development.md](docs/VCVRack_Plugin_Development.md).

## Architecture & Key Files

### Module Map

| File | Purpose |
|------|---------|
| [src/main.cpp](src/main.cpp) | Entry point, `setup()`, `loop()`, `setup1()`, `loop1()` (Core 1), global variables |
| [lib/channel.hpp](lib/channel.hpp) | `QuantizerChannel` — one complete voice: note mask, pitch mode, offsets, glide, transposition, sync policy, envelope |
| [lib/quantizer.hpp](lib/quantizer.hpp) | `Quantizer` class — semitone table, nearest-note search, boundary hysteresis, degree transposition |
| [lib/envelope.hpp](lib/envelope.hpp) | `Envelope` class — AD envelope / trigger / gate for the GATE jacks |
| [lib/scales.hpp](lib/scales.hpp) | Scale tables + `BuildScale()` (populates a channel's 12-note mask) |
| [lib/cvInputs.hpp](lib/cvInputs.hpp) | ADC reads, calibration lookup, filtering, TRIG input ISR + edge queue, IN 2 routing + transpose CV |
| [lib/boardIO.hpp](lib/boardIO.hpp) | `InitIO()`, `InitWire()`, `InitDAC()`, `DACWriteAll()` |
| [lib/pinouts.hpp](lib/pinouts.hpp) | XIAO RP2040 pin/GPIO assignments, jack map |
| [lib/menuDefinitions.hpp](lib/menuDefinitions.hpp) | `MenuItem` struct, `RowStyle`/`MenuItemType` enums |
| [lib/menuHandlers.hpp](lib/menuHandlers.hpp) | `MENU_ITEMS[]` array, all setters/actions — **read the developer guide at the top of this file before editing** |
| [lib/menuDisplay.hpp](lib/menuDisplay.hpp) | Low-level draw primitives (`MD_Row`, `MD_RenderGroup`, …) |
| [lib/menuRender.hpp](lib/menuRender.hpp) | `HandleDisplay()`, the keyboard home screen |
| [lib/displayManager.hpp](lib/displayManager.hpp) | `DisplayManager` class — rate-limited non-blocking display |
| [lib/presetManager.hpp](lib/presetManager.hpp) | `LoadSaveParams`, `LoadDefaultParams()`, `CollectParams()`, `UpdateParameters()` |
| [lib/storage.hpp](lib/storage.hpp) | Platform storage backend (RP2040 EEPROM emulation) |
| [lib/calibration.hpp](lib/calibration.hpp) | Hardware calibration wizard (hold encoder on boot) |
| [lib/calibrationData.hpp](lib/calibrationData.hpp) | `CalibrationData` struct, reference-voltage constants |
| [platformio.ini](platformio.ini) | Build config, dependencies, test environments |

### Signal Flow

```
IN2/IN3 (pitch CV) ──► AdjustADCReadings ──► one-pole filter ──► channelADC[]
                                                                      │
TRIG (IN1) ──► TriggerReceived ISR ──► edge queue ────┐               ▼
                                                      └──► QuantizerChannel::Process()
                                                                      │
                                             ┌────────────────────────┴─────────┐
                                             ▼                                  ▼
                                     Quantizer (+octave, glide)          Envelope (ENV/TRIG/GATE)
                                             │                                  │
                                             ▼                                  ▼
                                     CV 1 / CV 2 jacks                 GATE 1 / GATE 2 jacks
```

### Jack Map

All four outputs go through the MCP4728 quad 12-bit DAC:

| DAC index | Jack | Signal |
|-----------|------|--------|
| 0 | 1 | CV 1 — quantized pitch, channel 1 |
| 1 | 2 | CV 2 — quantized pitch, channel 2 |
| 2 | 3 | GATE 1 — gate/envelope, channel 1 |
| 3 | 4 | GATE 2 — gate/envelope, channel 2 |

`OUT_CV(ch)` / `OUT_GATE(ch)` in [lib/pinouts.hpp](lib/pinouts.hpp) name these indices.

### Dual-Core Architecture

- **Core 0** (`loop()`): main logic — ADC reads, quantization, envelopes, encoder, DAC writes via Wire1
- **Core 1** (`loop1()`): display only — GFX rendering + `display.display()` via Wire (SSD1306)
- Coordination: `_displayFrameReady` (Core 0 sets, Core 1 clears) and `_displayLocked` (Core 0 pauses Core 1 GFX)
- Wire (GPIO 6/7, I2C1) → SSD1306 display **only** (Core 1 exclusive at runtime)
- Wire1 (GPIO 0/1, I2C0) → MCP4728 DAC **only** (Core 0 exclusive at runtime)
- No mutex needed — separate hardware I2C blocks on separate cores

### Key Subsystems

**Pitch domain**: everything works in *semitones*, not raw counts. 0–5V at 1V/oct = 60 semitones, so one semitone is `QUANT_COUNTS_PER_SEMITONE` = 4095/60 = 68.25 counts. `CountsToSemitones()` / `SemitonesToCounts()` are the only places that conversion lives.

**Quantization** ([lib/quantizer.hpp](lib/quantizer.hpp)):
- `Build()` expands a 12-note mask into a sorted table of enabled semitones over 0..60
- `Quantize()` snaps to the nearest entry, with a hysteresis band around the midpoint between the held note and its neighbour so a CV sitting on a boundary does not chatter
- Hysteresis is skipped when the held note is no longer emittable, so scale changes take effect immediately
- An all-off mask falls back to chromatic rather than freezing the output

**Gate/envelope** ([lib/envelope.hpp](lib/envelope.hpp)): three modes — `ENV` (AD, retriggerable), `TRIG` (fixed 10 ms pulse), `GATE` (follows the TRIG input level). The 200-point curve table is carried over verbatim from the SAMD21 firmware so the envelope keeps its original snap.

**Scale selection** ([lib/channel.hpp](lib/channel.hpp)): `SelectScale()`/`SelectRoot()` record the choice *and* rebuild the note mask; `SetScaleIndex()`/`SetRootIndex()` only record it. The split is load-bearing — a preset stores the mask alongside the scale/root, and the mask is the source of truth because it may have been hand-edited. `UpdateParameters()` must use the plain setters, or restoring a preset would overwrite the saved mask with a freshly generated scale. Anything user-driven uses the `Select*` pair.

**Settle window** ([lib/channel.hpp](lib/channel.hpp)): a new note must hold for `_settleMs` before it is committed. The input smoother takes a few loop iterations to converge after a step, and a follow-the-input quantizer plays every note it crosses on the way — heard as a sweep. The settle window drops those transients while still letting a genuinely slow input play every note. Two cases bypass it deliberately: the first note after power-on, and a held note that has left the scale (so scale edits are never delayed). It replaces the old SENS gain trim, which detuned the module now that calibration fits the input scale.

**Pitch mode** ([lib/channel.hpp](lib/channel.hpp)): `TRACK` follows the input (subject to the settle window); `S&H` latches the quantized note on a TRIG edge and holds it, so nothing the input does between triggers reaches the jack. In S&H the settle window doubles as the *sample delay* — a sequencer emits pitch and gate together, so at the edge the pitch CV may still be in transit. A scale change re-snaps the *held* note rather than re-reading the input, preserving the S&H contract.

**Transposition** ([lib/quantizer.hpp](lib/quantizer.hpp), [lib/cvInputs.hpp](lib/cvInputs.hpp)): `IN 2` can be switched from channel 2's pitch input to a transposition CV, in which case channel 2 quantizes IN 1 alongside channel 1. Transposition steps along the enabled-note table (`Quantizer::TransposeDegrees`), so it is always in-scale and the interval follows the scale. Clamps at the table ends rather than wrapping. The CV→degrees mapping has its own deadband for the same reason the note quantizer does.

**Sync policy** ([lib/channel.hpp](lib/channel.hpp)): `TRIG` (TRIG input edge), `NOTE` (quantized note changed), or `BOTH`.

**Octave shift** folds by whole octaves rather than clamping at the ends of the 0–60 range, so a shift can never change the pitch class the quantizer chose.

**Persistence**:
- 10 preset slots; slot 0 auto-loaded at boot
- Schema: `LoadSaveParams` in [lib/presetManager.hpp](lib/presetManager.hpp); platform storage in [lib/storage.hpp](lib/storage.hpp)
- EEPROM layout: `[0 .. NUM_SLOTS×sizeof(LoadSaveParams))` for presets, then `CalibrationData`
- **Changing `LoadSaveParams` invalidates all saved slots** — increment `VALID_MAGIC` or add migration

**Calibration**: two-point linear fit per CV input (1V + 3V references) plus a per-output DAC command remap. This matters more here than on any other Forge module — an uncalibrated ADC gain error walks the output off by a semitone or more toward 5V.

## Build, Test & Upload

```bash
pio run                    # build (env: xiao_rp2040)
pio test -e native         # run GoogleTest native tests
pio device monitor         # serial debug at 115200 baud
```

The native tests need a host compiler on PATH. On Windows this repo uses MSYS2:
`export PATH="/c/msys64/mingw64/bin:$PATH"`.

VCV plugin:

```bash
cd vcv-plugin
make                          # build plugin.dll / .so / .dylib
make install                  # install into RACK_USER_DIR
test/build_isolation_test.sh  # multi-instance state isolation test (no Rack needed)
```

**Flashing**: Double-tap reset to enter UF2 bootloader; device mounts as mass storage.

## Naming Conventions

- **CapitalCase**: Free functions (`HandleCVInputs()`, `HandleEncoderPosition()`)
- **_underscoreCamelCase**: Private class members (`_quantizedSemitone`, `_gateCounts`)
- **ALL_CAPS**: Constants and macros (`MAXDAC`, `CLK_IN_PIN`, `REQUEST_DISPLAY_REFRESH()`)
- **Enums**: PascalCase names and values (`GateMode::GateEnvelope`, `SyncMode::SyncNote`)

## Critical Patterns

**Display refresh** (always use the macro, never set `displayRefresh` directly):
```cpp
REQUEST_DISPLAY_REFRESH(); // marks dirty + resets screen-timeout timer
```

**Unsaved changes indicator** — call `MarkUnsaved()` from [lib/menuHandlers.hpp](lib/menuHandlers.hpp); it keeps the raw flag and the display manager in sync.

**Menu state**: `menuItem` is a 1-based item number; `menuMode` is 0 = navigating, or equals the item number being edited. See the developer guide at the top of [lib/menuHandlers.hpp](lib/menuHandlers.hpp).

**Per-channel handlers are templates**: `setScale<0>` / `setScale<1>` etc. share one implementation. Add a new per-channel parameter once, instantiate it twice in `MENU_ITEMS[]`.

## Common Tasks

### Adding a Scale

1. Append the name to `scaleNames[]` and a ≤4-char form to `scaleShortNames[]` in [lib/scales.hpp](lib/scales.hpp)
2. Append its semitone offsets to `scaleNotes[]` **and** its length to `scaleNoteCount[]`
3. Appending never moves existing indices, so stored presets keep resolving correctly
4. `test_scales.cpp` cross-checks the length table against the generated mask

### Adding a Menu Parameter

1. Write the getter/setter (template on the channel index if it is per-channel) in [lib/menuHandlers.hpp](lib/menuHandlers.hpp)
2. Add a `MenuItem` entry to `MENU_ITEMS[]` with the right `group`; `MENU_ITEM_COUNT` updates itself
3. Add the field to `LoadSaveParams`, `CollectParams()` and `UpdateParameters()` in [lib/presetManager.hpp](lib/presetManager.hpp), and bump `VALID_MAGIC`
4. Call `MarkUnsaved()` inside the setter
5. If the parameter should also appear in the Rack context menu, add a bridge pair in `vcv-plugin/src/engine/fw_engine.{hpp,cpp}` and a menu entry in `vcv-plugin/src/NoteForge.cpp`

### Adding a Menu Page

1. Pick the next free `group` number
2. Append the items with that group at the end of `MENU_ITEMS[]`
3. Add the page title at that index in `groupTitles[]` in [lib/menuRender.hpp](lib/menuRender.hpp)
4. The generic renderer handles plain lists — no rendering code needed unless the page needs a custom layout

### Adding a Firmware Global (VCV port)

Any new mutable file-scope global **must** be registered in [vcv-plugin/src/engine/engine_state.def](vcv-plugin/src/engine/engine_state.def), or Rack instances will share it. `test/build_isolation_test.sh` is the guard.

## Gotchas & Constraints

- **All outputs are DAC** — this is NOT the SAMD21 version; there are no PWM gate pins and no inverted gate logic
- **DAC channel swap**: hardware swaps DACB↔DACC; compensated by `_chanMap[]` in [lib/boardIO.hpp](lib/boardIO.hpp) — do not change without verifying with hardware
- **Core 1 owns Wire**: never call `Wire` (display bus) from Core 0 — use `_displayFrameReady`/`_displayLocked`
- **Tables in headers are `static`**: `scaleNames`, `GateModeNames`, `SyncModeNames` etc. have internal linkage so several test translation units can include them. Keep new header tables `static` too.
- **`micros()` is the only clock**: envelopes and glide are driven entirely by the caller-supplied timestamp, never by wall-clock. That is what makes them testable and what makes the VCV port deterministic.
- **Calibration** wizard: hold the encoder button during boot → runs [lib/calibration.hpp](lib/calibration.hpp); `CalibrationData` survives firmware flashes in EEPROM

---

**For user-facing feature documentation, see [Readme.md](Readme.md). For the VCV Rack port, see [docs/VCVRack_Plugin.md](docs/VCVRack_Plugin.md).**
