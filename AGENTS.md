# ForgeSeries — AI Coding Agent Instructions

Eurorack module firmware for the **Seeed XIAO RP2040**, five modules in one
image, each also shipping as a VCV Rack plugin that runs the same code.

**[README.md](README.md) is the architecture document and it is current.** Its
"How it fits together" half covers `core/`, the unified firmware, the CV domain,
include order and image size in more depth than this file does. Read it before
making structural changes. This file is the short version plus the rules that
only matter when editing.

Per-module instructions live in `apps/<app>/AGENTS.md`.

## Layout

```text
platformio.ini  the firmware project — the repo root IS the PlatformIO project
src/main.cpp    the shell: owns the board, runs one app at a time
core/           the board, and everything every module shares
apps/clk/       ClockForge    — clock generator / modulation source
apps/dq/        NoteForge     — dual quantizer
apps/gen/       GravityForge  — physics-based generative sequencer
apps/scp/       ForgeView     — oscilloscope / spectrum analyser
apps/att/       ChaosForge    — dual chaotic-attractor modulation source
apps/wea/       WeaveForge    — dual shift-register sequencer
vcv/            the consolidated Rack plugin (all modules, one binary)
vcvlib/         shared Rack layer (Arduino shim, ForgeModule, IEngine, widgets)
panel-src/      the panel artwork you edit in Inkscape, one SVG per module
tools/env.ps1   optional PATH helper for Windows
tools/          prep_panel.py, panel_coords.py — the panel pipeline
```

Each module:

```text
apps/<app>/
  src/<app>_app.cpp   the module as the shell sees it (forge::IApp)
  src/<app>_app.hpp   its factory — the only thing the shell includes
  lib/                the module's own headers, including engine.hpp
  vcv-plugin/         the same module as a standalone Rack plugin
  test/test_native/   googletest + ArduinoFake suites
```

There is **no per-module `platformio.ini`** — the root project builds every
firmware and owns the native test environments (`test_dir = apps`). It has two
flavours of hardware image, from the same shell and the same module sources:

- `env:xiao_rp2040` — the unified image, every module, selector picks.
- `env:xiao_clk` / `xiao_dq` / `xiao_gen` / `xiao_scp` / `xiao_att` /
  `xiao_wea` — one module each.

The single-module envs differ in exactly two options: `-DFORGE_ONLY_APP=<Factory>`
and a `build_src_filter` naming one app TU. **`FORGE_ONLY_APP` is read in exactly
one place — the `kApps[]` initialiser in [src/main.cpp](src/main.cpp)** — and it
must stay that way. It names a factory rather than a module so that the one
`#ifdef` covers every module: adding a module is still one include and one array
entry, with no per-module conditional anywhere.

`kAppCount == 1` is what makes the selector show one module plus **CALIBRATE**;
everything else falls out of that. In particular `FORGE_UNIFIED` stays defined in
these builds — it means "there is a shell behind this app", not "every
module" — and the `<app>_app.hpp` includes stay unconditional, since a
declared-but-uncalled factory is not a link reference.

`lib/engine.hpp` holds the module's per-iteration engine step and **both hosts
include it**. Duplicating it instead is how GravityForge's Rack port silently
lost its LOOP▸NAP muting.

## Build & test

```sh
make                # the unified firmware — this is what you flash
make upload         # build + flash   (double-tap reset for the UF2 bootloader)
make modules        # the single-module images, one per module
make fw-clk         # one of them  (== pio run -e xiao_clk); fw-upload-clk flashes
make test           # native tests, every module
make test-dq        # one module  (== pio test -e native_dq)
make isolation      # VCV engine state-isolation tests
make screen-wea     # print a module's OLED to the terminal — see below
make vcv            # the consolidated Rack plugin
make plugins        # every standalone per-module plugin
make everything     # every firmware image + all standalone plugins
make panels         # convert the panel artwork for Rack — see below
```

## Panels

**Edit `panel-src/<Name>.svg` in Inkscape. Never edit
`apps/<app>/vcv-plugin/res/<Name>.svg` — it is generated, and your change is
gone the next time anyone touches the drawing.**

Rack's SVG parser is nanosvg, which draws `path rect circle ellipse line
polyline polygon` and nothing else. `<text>` and `<use>` are simply not in that
list, so a label typed in Inkscape and a tiled clone both vanish silently — the
ClockForge panel shipped for months with five labels Rack never drew. `make
panels` is what closes that gap:

```sh
make panels          # every panel whose drawing changed
make panels-clk      # just one
make panels-force    # rebuild all regardless of timestamps
make panel-coords-clk  # print the components layer as mm, for the module's .cpp
```

It runs on timestamps, so a build whose artwork has not changed pays nothing,
and `make vcv`, `make plugins` and `make <app>` all depend on it. Both files are
committed: the source because it is the source, the output because a machine
without Inkscape still has to be able to build — there, the step warns and keeps
what is already there.

What the pipeline does, in order (`tools/prep_panel.py`, then Inkscape):

1. strips the `components` and `Drill` guide layers, which are construction
   geometry Rack would otherwise render on top of the panel
2. flattens every `<use>` into real geometry
3. drops groups the drawing has switched off (`display:none`)
4. hands the result to Inkscape for `object-to-path` on the text only — glyph
   outlines need font metrics, which is the one thing here Inkscape is for
5. checks the output for `<text>` that survived and would still draw

⚠ Widget positions in each module's `<Name>.cpp` are `mm2px(Vec(...))` and are
*not* generated — they are kept in agreement with the drawing by hand. After
moving anything on the `components` layer, run `make panel-coords-<app>` and
reconcile. It accumulates the ancestor transforms and the viewBox scale (1 uu =
0.01 mm on these panels), which reading `cx`/`cy` out of the XML does not.

## Looking at a module's screen

**`make screen-<app>` dumps the module's 128×64 OLED to the terminal as ASCII,
with an x ruler and numbered rows.** Host compiler only — no board, no Rack, no
Rack SDK. Use it whenever you touch rendering code.

```sh
make screen-wea
make screen-wea ARGS="--ms 2100 --turn 3 --click 1"   # drive it to a menu page
make screen-att ARGS="--ms 1500 --clock 4 --cv 2.5 0" # clock it, hold CV
```

`--ms` is engine time, and it is exact rather than approximate: engine time comes
from the passed-in `dt`, never a wall clock, so a given `--ms` always produces
the same frame. That matters for anything phase-dependent — a mark drawn for the
first third of a step is invisible at `--ms 2000` and obvious at `--ms 2100`.

**Do not check screen geometry against a photo of a panel.** Every constant in a
render file is a row or column number, and a photo cannot tell you whether a
label sits over the cell it names — that exact bug lived in WeaveForge's loom
until this tool was written. Read the pixels.

The tool is `vcvlib/test/screenshot.cpp`, generic over `forgevcv::IEngine`. It
also carries an adapter for ScopeForge, which predates that interface and exposes
free functions with a per-sample `feedSample()` instead; the build script picks
the shape by what the module's header declares and reads the engine namespace out
of it, so neither is a list that can go stale.

**Run `make everything` before calling a change done.** PlatformIO does not
compile `vcv-plugin/`, so a green firmware build says nothing about the Rack
ports.

On Windows the Makefile finds msys2 and PlatformIO itself, so plain `make` works
from PowerShell. `. .\tools\env.ps1` only matters for running the tools by hand.
Rack SDK is expected at `../Rack-SDK` (override with `RACK_DIR=`).

## The shell ↔ app contract

[core/IApp.hpp](core/IApp.hpp) is the interface and its header comment is the
authority. In short: the shell brings up the hardware, draws the module
selector, owns calibration, then drives one app through
`Begin()`/`Tick0()`/`Tick1(display)` plus encoder events.

- **Core 0** (`Tick0`): encoder, ADC, engine, DAC — owns **Wire1** (GPIO 0/1, MCP4728)
- **Core 1** (`Tick1`): GFX render + flush — owns **Wire** (GPIO 6/7, SSD1306)
- Separate I2C blocks on separate cores, so no mutex. **Never touch `Wire` from
  Core 0.**
- An app must not poll the encoder pins — the shell needs those events too (the
  hold-to-return-to-menu gesture) and two readers race the detent state.
- `forge::RequestAppMenu()` asks for the module selector. Declared always,
  **defined only by the shell**, so app menu rows guard it with
  `#ifdef FORGE_UNIFIED` — the Rack ports compile the same `lib/` with no shell.

## Rules for `core/`

- **Everything `core/` defines must be `inline`** (C++17 inline variables
  included). The image links the shell plus one TU per module; a file-scope
  `static` gives each TU a private copy of what should be shared hardware state.
- **`core/` must never include an app header.** Where a shared header needs
  something the caller owns, it *requires* the caller to have defined it — the
  way `calibration.hpp` takes `SaveCalibration()` from the shell.
- What stays in a module's `lib/` is genuinely its own: menu definitions, preset
  schema, `engine.hpp`, and the DSP that makes it that module.
- **[core/fonts/](core/fonts/) is the deliberate exception to the `inline` rule.**
  GFX font headers are `const` glyph tables, and a module includes them from
  *inside* its own namespace like any other app header — so each app that uses one
  carries its own copy, which is what the per-namespace TU layout requires and what
  keeps the unified firmware and the consolidated plugin from seeing several
  definitions of one symbol. They live in `core/` to be edited once, not to be
  stored once: ClockForge, ScopeForge and WeaveForge share `helvB12`/`helvB24`,
  which were three byte-identical copies before. Budget ~40 KB of flash per app
  that pulls both in.

## Rules for a module TU

- Each module TU wraps itself in `namespace forge::<app>`. That is what lets
  several firmwares sharing one binary all define `menuMode`, `switchState` and
  `param` at file scope.
- **Include order is load-bearing.** Standard library, third-party and `core/`
  headers at *global* scope first; only then the module's own headers, inside the
  namespace. Miss one — `<cstring>`, say — and libstdc++ lands inside
  `forge::<app>`, producing hundreds of errors in `stringfwd.h` that never name
  the file responsible. The blocks are bracketed `clang-format off/on` because an
  include-sorting editor breaks this silently. See the header comment in
  [apps/scp/src/scp_app.cpp](apps/scp/src/scp_app.cpp).
- A few `core/` headers are included **late**, after the module's state exists,
  because they close over it: `engine.hpp`, `appDisplay.hpp` (`HandleOutputs`,
  the display flags) and `encoderMenu.hpp` (the five hooks).
- **Tables in headers are `static`** so several test translation units can
  include them. Keep new ones `static` too.

## Storage

LittleFS via [core/fsStore.hpp](core/fsStore.hpp) → [core/appStorage.hpp](core/appStorage.hpp);
a module's `lib/storage.hpp` is a four-line shim that defines `FORGE_APP_SLUG`.

```text
/cal.bin        CalibrationData, shared by every module
/boot           which module to start, + a return-to-menu flag
/<app><n>.pre   one preset slot
```

**Not the emulated EEPROM** — that is a single 4096-byte sector whose `begin()`
clamps silently, which is how ClockForge shipped with permanently broken
calibration. The EEPROM backend survives only for the Rack port, where the shim
is a byte buffer Rack persists into the patch.

- 10 preset slots per module; slot 0 auto-loaded at boot.
- **Changing `LoadSaveParams` invalidates saved slots** — bump `VALID_MAGIC`.
- A parameter reachable from the menu but missing from `CollectParams()` is
  silently not persisted, and in Rack silently lost on patch reload.

## CV

Readings are **normalised floats**: `1.0` is +5 V at the jack on every hardware
revision. Current hardware is 0–5 V; a later revision moves to ±5 V, built with
`-DFORGE_CV_BIPOLAR`. Read through the adapters in
[core/cvInput.hpp](core/cvInput.hpp) — `CvRead`, `CvUni` (0..1), `CvBi` (-1..1)
— never open-code a mapping, because which adapter is the identity swaps when
the flag flips.

Pitch is **not** normalised: `CvSemitones()` returns 1 V/oct semitones, and DAC
output stays in counts, scaled at the write. Three domains, three units.

## VCV Rack

Two flavours, both built from the same module code:

- `apps/<app>/vcv-plugin/` — one standalone plugin per module (slugs
  `ClockForge`, `NoteForge`, `GravityForge`, `ForgeView`)
- `vcv/` — the consolidated plugin that ships (slug `VoltageFoundryMod`)

Do not leave both installed in Rack; every module then appears twice in the
browser. `make -C vcv print-plugins-dir` prints Rack's plugin directory.

`FORGEVCV` defaults to the in-repo `vcvlib/`; it no longer needs a sibling
checkout.

**Any new mutable file-scope global in `lib/` must be registered in
`apps/<app>/vcv-plugin/src/engine/engine_state.def`,** or two Rack instances
share it. `make isolation` is the guard, and only the Rack build catches a stale
entry.

Panels: `vcv-plugin/res/<Module>.svg` is the single source of jack labels — do
not draw labels in the widget. Rack renders through nanosvg, which **ignores SVG
`<text>`**, so every glyph must be converted to a path in Inkscape. That is why
the files are ~18 MB and thousands of paths; this is normal for the series, not
something to fix. Keep an editable `-src.svg` with real text and re-run the
conversion rather than hand-editing the converted file.

## Naming conventions

- **CapitalCase** — free functions (`HandleCVInputs()`, `ApplyParams()`)
- **_underscoreCamelCase** — private class members (`_isPulseOn`, `_pegMask`)
- **ALL_CAPS** — constants and macros (`PPQN`, `MAXDAC`, `REQUEST_DISPLAY_REFRESH()`)
- **Enums** — PascalCase names and values (`GateMode::GateEnvelope`)

## Shared UI patterns

**Display refresh** — always the macro, never set `displayRefresh` directly:

```cpp
REQUEST_DISPLAY_REFRESH(); // marks dirty + resets screen-timeout timer
```

**Menu state**: `menuItem` is a 1-based item number; `menuMode` is 0 =
navigating, or equals the item number being edited. Each module's
`lib/menuHandlers.hpp` opens with a developer guide — read it before editing.

**Adding a menu item**: add a `MenuItem` to `MENU_ITEMS[]`; `MENU_ITEM_COUNT` is
computed. Items sharing a `group` render on one page, titled from `groupTitles[]`
in `lib/menuRender.hpp`.

**Six rows per menu page.** `MD_START_Y=12` + `MD_ROW_H=9` puts row 6 at y=57,
ending on row 63. A seventh row is clipped with no error.

## Board gotchas

- **All outputs are DAC** (MCP4728 quad 12-bit). This is not the old SAMD21
  hardware: no PWM gate pins, no inverted gate logic.
- **DAC channel swap**: hardware swaps DACB↔DACC, compensated by `_chanMap[]` in
  [core/boardIO.hpp](core/boardIO.hpp) — do not change without hardware in hand.
- **Calibration** is board-level, not per module: CALIBRATE on the shell's module
  selector runs [core/calibration.hpp](core/calibration.hpp), writes `/cal.bin`,
  and every module picks it up.
