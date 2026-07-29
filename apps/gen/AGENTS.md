# GravityForge Firmware - AI Coding Agent Instructions

## Project Overview

GravityForge is a **Eurorack dual physics-based generative sequencer** for the
**Seeed XIAO RP2040**. Two rotating containers hold bouncing balls; every ball
that strikes a peg on a container wall emits a note. A single PROXIMITY control
slides the two containers together, taking them from independent sequencers
through overlapping-and-coupled to a single merged space.

Inspired by Teenage Engineering's Tombola. See [docs/Design.md](docs/Design.md)
for the full concept and the reasoning behind every architectural decision — read
it before making structural changes.

**Part of the Forge Series**: one module in a hardware platform family. The
hardware is shared with ClockForge and NoteForge; firmware defines behavior.

**Hardware Platform**: RP2040 (dual-core ARM Cortex-M0+), Arduino framework
(`earlephilhower` core), PlatformIO.

**Two targets from one codebase**: the same `lib/` builds for the RP2040 *and*
for the VCV Rack plugin in [vcv-plugin/](vcv-plugin/), which compiles the
unmodified firmware against the `ForgeSeries-VCVLib` Arduino shim. Anything
added to `lib/` has to keep working in both — no hardware-only calls, no
wall-clock reads.

## Architecture & Key Files

### Module Map

| File | Purpose |
|------|---------|
| [src/main.cpp](src/main.cpp) | `setup()`/`loop()` (Core 0), `setup1()`/`loop1()` (Core 1), globals |
| [lib/physics.hpp](lib/physics.hpp) | `Ball`, `Container`, `PhysicsWorld` — the simulation and proximity coupling |
| [lib/sequencer.hpp](lib/sequencer.hpp) | `GravityChannel` — peg→note mapping, quantize deferral, envelope |
| [lib/clock.hpp](lib/clock.hpp) | Internal/external tempo, rotation rate, the quantize grid |
| [lib/params.hpp](lib/params.hpp) | `ContainerParams`/`WorldParams`/`ModBus` + `ApplyParams()` |
| [lib/cvInputs.hpp](lib/cvInputs.hpp) | ADC reads, `CvNorm()`/`CvBipolar()`, modulation matrix, IN 1 role |
| [lib/quantizer.hpp](lib/quantizer.hpp) | Semitone table, nearest-note search, `SemitoneAt()` |
| [lib/scales.hpp](lib/scales.hpp) | Scale tables + `BuildScale()` |
| [lib/envelope.hpp](lib/envelope.hpp) | AD envelope / trigger / gate for the GATE jacks |
| [lib/menuDefinitions.hpp](lib/menuDefinitions.hpp) | `MenuItem` struct, `RowStyle`/`MenuItemType` |
| [lib/menuHandlers.hpp](lib/menuHandlers.hpp) | `MENU_ITEMS[]` + setters — **read the guide at the top before editing** |
| [lib/menuDisplay.hpp](lib/menuDisplay.hpp) | Draw primitives (`MD_Row`, `MD_RenderGroup`, …) |
| [lib/menuRender.hpp](lib/menuRender.hpp) | `HandleDisplay()` + the physics home screen |
| [lib/presetManager.hpp](lib/presetManager.hpp) | `LoadSaveParams`, `CollectParams()`, `UpdateParameters()` |
| [lib/randomize.hpp](lib/randomize.hpp) | `RandomizeParams()` — backs both PRESETS ▸ RANDOM and the plugin's Randomize |
| [lib/storage.hpp](lib/storage.hpp) | RP2040 EEPROM emulation backend |
| [../../core/calibration.hpp](../../core/calibration.hpp) | Calibration wizard — board-level, run from the shell's module selector |
| [lib/boardIO.hpp](lib/boardIO.hpp) | `InitIO()`, `InitWire()`, `InitDAC()`, `DACWriteAll()` |
| [lib/pinouts.hpp](lib/pinouts.hpp) | Pin/GPIO assignments, jack map |

### Signal Flow

```
                    ┌──────────── IN 1 (role: CLOCK/RESET/KICK/SPAWN)
                    ▼
IN2/IN3 ──► CvNorm ──► BuildModBus ──► ModBus ──┐
                                                 ▼
                        ContainerParams ──► ApplyParams() ──► PhysicsWorld
                                                                    │
                                                       peg hit ─────┤
                                                                    ▼
                                                       GravityChannel::Process()
                                                          │              │
                                          Quantizer ──────┤              ├────── Envelope
                                                          ▼              ▼
                                                   CV A / CV B    GATE A / GATE B
```

### Jack Map

All four outputs go through the MCP4728. Deliberately identical to NoteForge:

| DAC index | Jack | Signal |
|-----------|------|--------|
| 0 | 1 | CV A — pitch of the last peg hit, container A |
| 1 | 2 | CV B |
| 2 | 3 | GATE A |
| 3 | 4 | GATE B |

`OUT_CV(ch)` / `OUT_GATE(ch)` in [lib/pinouts.hpp](lib/pinouts.hpp) name these.

### Dual-Core Architecture

- **Core 0** (`loop()`): CV reads, physics, sequencing, DAC writes via Wire1
- **Core 1** (`loop1()`): display only — GFX rendering + `display.display()` via Wire
- Coordination: `_displayFrameReady` (Core 0 sets, Core 1 clears) and
  `_displayLocked` (Core 0 pauses Core 1 GFX)
- Boot gate: the Arduino core launches Core 1 **before** Core 0's `setup()` runs,
  so `setup1()` spins on `_core1Enabled` until `setup()` has done `InitWire()`,
  `display.begin()` and the splash. Without it Core 1 flushes over Wire mid-init
  (mirrored panel) and wipes the splash/version screens
- No mutex needed — separate hardware I2C blocks on separate cores

## Key Subsystems

**Pegs live on the container wall.** Free-floating pegs would be O(balls × pegs)
per step and would not fit the CPU budget. On the wall it is O(1): the wall
collision already computes the contact angle, and the peg index is one division.
This is the decision the whole module's performance rests on.

**Float, not fixed-point.** ~14 soft-float ops per ball per step; at 8 balls × 2
containers × 1 kHz that is ~8 % of one core. `sqrtf`/`atan2f` run only on actual
collisions. See [docs/Design.md](docs/Design.md) §3 for the worked budget.

**Determinism is a requirement, not a nicety.** Fixed 1 ms timestep, a
self-contained `PhysRandom`, and time passed in as a parameter — never read from
a wall clock. That is what makes the sim unit-testable and what will make the
VCV port match the hardware under faster-than-realtime rendering. Do not
introduce `micros()` calls inside `lib/` DSP code.

**Base params vs live sim.** `ContainerParams`/`WorldParams` are what the *user*
set; `ModBus` is this loop's CV modulation; `ApplyParams()` combines them into
the live `Container`. Never write CV modulation straight into a `Container` — the
menu would show the modulated value and presets would save it.

**Proximity coupling.** `PROXIMITY` sets the centre separation; `Overlap()` is the
coupling strength. A wall strike whose contact point falls inside the *other*
container transmits an impulse into it, **and rings the peg nearest that point
on the receiving rim** (`Container::RingPegNear`) so the transfer is heard as
well as seen. The note is always the receiving container's own peg, so a
transfer can never be out of key. This is energy transfer, not ball transfer —
ball counts stay stable. `PhysicsWorld::OverlapArc()` exists for the deferred
"portal" feature and is kept correct and tested.

Coupling fires only a few times a second amid much busier bouncing, so it is
invisible without a cue: a transmitted strike raises a **spark** (an expanding
ring drawn at the contact point). Do not remove it — without it the whole
control reads as if it does nothing, which is exactly how it was first reported.
Assert coupling by *magnitude*, never by mere non-zero divergence; a chaotic
system diverges from any perturbation, so a "> 0.01 px" test passes even when the
effect is imperceptible.

**Loop mode is snapshot + step count.** `PhysicsWorld` captures both containers
(balls, rotation, `PhysRandom`) and rewinds every N beats, so a deterministic sim
becomes a repeating phrase. Two things it rests on, and neither is optional:

- The rewind happens on an exact **step** boundary inside `Advance()`, never on
  elapsed wall time. One step of drift is a different phrase within a few
  repeats.
- The sim runs on `_simUs` — its own clock, exactly 1 ms per step — because the
  peg refractory windows are measured against it. On wall time the same ball
  state can clear a 12 ms window on one pass and miss it on the next, which is
  invisible free-running and fatal to a loop.

Hit timestamps travel through the snapshot as **ages**, not absolute times, or
each repeat would clear a refractory window it did not clear the first time.
Parameters are deliberately *not* snapshotted: the point is to keep playing the
controls over a locked phrase. `Reset()` re-arms, which is what gives Randomize a
fresh phrase for free.

**Quantize is last-wins.** With the grid on, a newer peg hit replaces a pending
one rather than stacking. Stacking would release a burst of retriggers at the
boundary instead of something that sounds like the physics that made it.

**CV range.** Current hardware is **0–5 V**; a later revision moves to ±5 V.
Every modulation target reads through `CvNorm()` (0..1) or `CvBipolar()` (-1..1)
in [lib/cvInputs.hpp](lib/cvInputs.hpp). Keep it that way — the hardware change
must stay a one-function edit.

**Persistence**: 10 preset slots, slot 0 auto-loaded at boot. EEPROM layout is
`[0 .. NUM_SLOTS×sizeof(LoadSaveParams))` then `CalibrationData`.
**Changing `LoadSaveParams` invalidates all saved slots** — bump `VALID_MAGIC`.

In VCV Rack the EEPROM is a byte buffer stored in the patch, and `serialize()`
**commits the live state to slot 0 before dumping it**. Without that the blob
holds only what an explicit SAVE last wrote — nothing, on a fresh instance — and
the patch reloads at factory defaults. Anything that must survive a patch
save/load therefore has to be in `LoadSaveParams`; a parameter reachable from the
context menu but missing from `CollectParams()` is silently not persisted.

**Randomize** lives in [lib/randomize.hpp](lib/randomize.hpp) so the hardware's
PRESETS ▸ RANDOM action and Rack's Randomize (Ctrl+R) roll the same patch. It
deliberately leaves tempo, the IN 1 role and the CV matrix alone — those are
patch wiring, not sound design.

## Build, Test & Upload

```bash
pio run                    # build (env: xiao_rp2040)
pio test -e native         # run GoogleTest native tests
pio device monitor         # serial debug at 115200 baud
```

The native tests need a host compiler on PATH. On this machine:
`export PATH="/c/msys64/mingw64/bin:/c/msys64/usr/bin:$HOME/.platformio/penv/Scripts:$PATH"`

**Flashing**: Double-tap reset to enter UF2 bootloader; device mounts as mass storage.

### VCV Rack plugin

```bash
cd vcv-plugin
make                          # build plugin.dll / .so / .dylib
test/build_isolation_test.sh  # multi-instance state isolation test (no Rack needed)
```

`make install` is unreliable on Windows here: Rack's `plugin.mk` derives
`RACK_USER_DIR` from `$(LOCALAPPDATA)`, which does not survive into make under
MSYS, so it silently installs to `/Rack2` (i.e. `C:\msys64\Rack2`). Copy the
unpacked plugin directory instead:

```bash
cp -r dist/GravityForge "/c/Users/<user>/AppData/Local/Rack2/plugins-win-x64/"
```

Run `test/build_isolation_test.sh` after touching anything in `lib/` that adds a
file-scope global — it is the guard for `engine_state.def`.

### Panel artwork

`vcv-plugin/res/GravityForge.svg` is **the single source of jack labels.** Do not
add label-drawing code to the widget — an earlier stopgap did that with nanovg
and it now double-draws over the real artwork.

Rack renders panels through nanosvg, which **ignores SVG `<text>` elements**, so
every glyph has to be converted to a path in Inkscape before the panel ships.
That is why the file is ~18 MB and ~3000 paths; NoteForge's panel is the same
size, so this is normal for the series and not something to "fix". Keep an
editable `-src.svg` with real text and re-run the conversion rather than
hand-editing the path-converted file.

## Naming Conventions

- **CapitalCase**: Free functions (`HandleCVInputs()`, `ApplyParams()`)
- **_underscoreCamelCase**: Private class members (`_pegMask`, `_hitPending`)
- **ALL_CAPS**: Constants and macros (`PHYS_R`, `MAXDAC`, `REQUEST_DISPLAY_REFRESH()`)
- **Enums**: PascalCase names and values (`GateMode::GateEnvelope`, `SpinRate::Spin4`)

## Critical Patterns

**Display refresh** (always use the macro, never set `displayRefresh` directly):
```cpp
REQUEST_DISPLAY_REFRESH(); // marks dirty + resets screen-timeout timer
```

**Unsaved changes** — call `MarkUnsaved()` from
[lib/menuHandlers.hpp](lib/menuHandlers.hpp); it keeps the raw flag and the
display manager in sync.

**Menu state**: `menuItem` is a 1-based item number; `menuMode` is 0 = navigating,
or equals the item number being edited.

**Per-container handlers are templates**: `setGravity<0>` / `setGravity<1>` share
one implementation. Add a per-container parameter once, instantiate it twice.

## Common Tasks

### Adding a Menu Parameter

1. Write the getter/setter (template on the container index if per-container) in
   [lib/menuHandlers.hpp](lib/menuHandlers.hpp)
2. Add a `MenuItem` to `MENU_ITEMS[]` with the right `group` — **max six rows per
   page**, a seventh is silently clipped
3. Add the field to `LoadSaveParams`, `CollectParams()` and `UpdateParameters()`
   in [lib/presetManager.hpp](lib/presetManager.hpp), and bump `VALID_MAGIC`
   — a parameter missing from `CollectParams()` will not survive a patch reload
4. Call `MarkUnsaved()` inside the setter
5. Decide whether `RandomizeParams()` in [lib/randomize.hpp](lib/randomize.hpp)
   should roll it (sound-shaping: yes; routing or sync: no)

### Adding a Menu Page

1. Pick the next free `group` number
2. Append the items with that group at the end of `MENU_ITEMS[]`
3. Add the page title at that index in `groupTitles[]` in
   [lib/menuRender.hpp](lib/menuRender.hpp)

### Adding a CV Modulation Target

1. Add the enum to `CVTarget` in [lib/cvInputs.hpp](lib/cvInputs.hpp)
2. Add its name to `CVTargetNames[]` (keep it ≤ 7 chars — it has to fit the row)
3. Add a field to `ModBus` if it is a new destination, and handle it in
   `BuildModBus()` and `ApplyParams()`

### Touching the Physics

Run `pio test -e native` — the physics tests cover containment (balls never
escape, including at max gravity and spin), determinism, peg-index range, the
energy floor that stops the sequencer dying, coupling symmetry, and the
catch-up guard. They are cheap and they catch real regressions.

## Gotchas & Constraints

- **All outputs are DAC** — no PWM gate pins, no inverted gate logic
- **DAC channel swap**: hardware swaps DACB↔DACC; compensated by `_chanMap[]` in
  [lib/boardIO.hpp](lib/boardIO.hpp) — do not change without hardware in hand
- **Core 1 owns Wire**: never call `Wire` from Core 0 — use the flags
- **Six rows per menu page.** `MD_START_Y=12` + `MD_ROW_H=9` puts row 6 at y=57,
  ending on row 63. A seventh row is clipped with no error.
- **The home screen must self-mark dirty** — it is an animation, so
  `HandleDisplay()` marks it dirty every pass rather than waiting for an event
- **`physics.hpp` works in screen pixels** — the renderer transforms nothing.
  Changing `PHYS_R` means re-deriving the home screen's vertical budget.
- **Header include order matters**: `presetManager.hpp` uses types from
  `cvInputs.hpp` without including it, so `cvInputs.hpp` must come first in
  `main.cpp` (same pattern as NoteForge)
- **Tables in headers are `static`** so several test translation units can
  include them. Keep new header tables `static` too.

---

**For the concept, the decisions and their reasoning, see
[docs/Design.md](docs/Design.md).**
