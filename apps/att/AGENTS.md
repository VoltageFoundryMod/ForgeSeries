# ChaosForge — AI Coding Agent Instructions

**Read [../../AGENTS.md](../../AGENTS.md) first** — the board, the shell↔app
contract, the core split, storage, CV, naming, menu patterns and the VCV rules
are all shared and documented there, not repeated here. This file covers only
what is ChaosForge's own.

ChaosForge is the **dual chaotic-attractor modulation source**. Two independent
generators each integrate a three-variable chaotic system; each sends two of its
three state variables to a pair of output jacks. A COUPLE control lets the two
orbits pull on each other, from unrelated through entrained to locked. Inspired
by Hemisphere Suite's Low-rents.

**[docs/Design.md](docs/Design.md) holds the concept and the reasoning behind
every architectural decision — read it before making structural changes.**

## Module map

| File | Purpose |
| ---- | ------- |
| [src/att_app.cpp](src/att_app.cpp) | the `forge::IApp` — `Begin`/`Tick0`/`Tick1`, encoder events, all file-scope state |
| [src/att_app.hpp](src/att_app.hpp) | `forge::AttApp()` factory, the only thing the shell includes |
| [lib/engine.hpp](lib/engine.hpp) | the per-iteration step + `DACWriteAll()`, shared with the Rack port |
| [lib/attractors.hpp](lib/attractors.hpp) | the twelve systems, their metadata, the RK4 integrator — **pure maths, no state** |
| [lib/generator.hpp](lib/generator.hpp) | `Generator` / `ChaosWorld` — orbit state, the clock, output scaling, coupling, the trail |
| [lib/params.hpp](lib/params.hpp) | `GenParams` / `WorldParams` / `ModBus` + `ApplyParams()` |
| [lib/cvInputs.hpp](lib/cvInputs.hpp) | `CvNorm()` / `CvBipolar()`, `CVTarget`, `BuildModBus()`, the IN 1 role |
| [lib/randomize.hpp](lib/randomize.hpp) | `RandomizeParams()` — backs both PRESETS ▸ RANDOM and Rack's Randomize |
| [lib/menuDefinitions.hpp](lib/menuDefinitions.hpp) | `MenuItem` struct, `RowStyle` / `MenuItemType` enums |
| [lib/menuHandlers.hpp](lib/menuHandlers.hpp) | `MENU_ITEMS[]`, `MarkUnsaved()`, every setter — **read the guide at the top before editing** |
| [lib/menuRender.hpp](lib/menuRender.hpp) | `HandleDisplay()`, `groupTitles[]`, the Lissajous home screen |
| [lib/presetManager.hpp](lib/presetManager.hpp) | `LoadSaveParams`, `CollectParams()`, `UpdateParameters()` |
| [lib/storage.hpp](lib/storage.hpp) | four-line shim over `core/appStorage.hpp` |
| [lib/version.hpp](lib/version.hpp) | module version string shown in the selector |

`boardIO`, `boardPinouts`, `displayManager`, `menuDisplay`, `encoder`,
`encoderMenu` and `calibration` are the **shared** ones in [../../core/](../../core/).
This module uses no quantizer, no envelope and no scale tables — it makes
voltages, not notes.

## Signal flow

```text
                    ┌──────────── IN 1 (role: RESET / RESET A / RESET B / FREEZE)
                    ▼
IN2/IN3 ──► CvNorm ──► BuildModBus ──► ModBus ──┐
                                                 ▼
                            GenParams ──► ApplyParams() ──► ChaosWorld
                                                                │
                                             ┌──────────────────┴──────────────────┐
                                             ▼                                     ▼
                                      Generator A  ◄──── COUPLE ────►       Generator B
                                       │       │                             │      │
                                    OUT 1   OUT 2                         OUT 3   OUT 4
```

## Jack map

All four outputs go through the MCP4728, written positionally in
[lib/engine.hpp](lib/engine.hpp). The pairing is the module's proposition, so it
is fixed:

| DAC index | Jack | Signal |
| --------- | ---- | ------ |
| 0 | 1 | generator A, the axis chosen for its first jack |
| 1 | 2 | generator A, the axis chosen for its second jack |
| 2 | 3 | generator B, first jack |
| 3 | 4 | generator B, second jack |

## Key subsystems

**Everything mathematical lives in `attractors.hpp` and it holds no state.** The
systems, their parameter metadata, the integrator and the output-normalisation
constants. That is what makes it trivially testable and what lets both hosts
share it unchanged. State, clocking and scaling belong to `generator.hpp`.

**Determinism is a requirement, not a nicety.** Fixed 1 ms module step, no
wall-clock reads inside `lib/`, time passed in as a parameter. It is what makes
the host tests possible and what makes the Rack port match the hardware under
faster-than-realtime rendering. **Do not introduce `micros()` calls inside
`lib/` DSP code.**

**Two clocks, deliberately.** The module step is a fixed 1 ms of real time; the
integrator's step is in the attractor's own time units and moves with SPEED.
Keeping them separate is what makes SPEED smooth rather than a change of
simulation grid. `ATT_MAX_SUBSTEPS` caps the substeps inside one module step, and
when it binds the integration step grows past `hMax` rather than time slowing
down — an honest clock with degraded accuracy beats a SPEED control that
silently stops speeding up. At `ATT_SPEED_MAX` the worst overshoot across the
twelve systems is 1.2× `hMax`, which all of them survive.

**Every shipped system must be genuinely chaotic at its defaults.** Two of the
twelve were not, at the parameters they are usually published with: Thomas at
b = 0.19 and the Finance system at a = 0.001 are both limit cycles, so the two
generators stayed locked together forever and the module had two outputs instead
of four. `EverySystemKeepsMoving` and `TheTwoGeneratorsDivergeFromEachOther` in
[test/test_native/test_attractors.cpp](test/test_native/test_attractors.cpp) are
the guard. **If you add a system or change a default, measure its largest
Lyapunov exponent** — docs/Design.md §4 has the table and the method.

**Output normalisation is measured, not guessed.** `centre[]`/`halfSpan[]` in
`ATTRACTORS[]` are the 0.2/99.8 percentiles of a ~700-simulated-second run at
each system's published parameters. Percentiles rather than extremes: a chaotic
orbit's excursions are rare and enormous, and sizing the jack to them wastes most
of the range. Change a default parameter and these must be re-measured.

**RANGE ▸ AUTO exists because those constants are only exact at the published
parameters.** It tracks the orbit's own window — instant on the way out, slow on
the way in, because a chaotic orbit's excursions are minutes apart and a fast
relax would re-gain the CV between them.

**COUPLE is diffusive coupling in normalised units.** The systems' natural sizes
differ by two orders of magnitude (Chua's y lives in ±0.4, Chen's in ±23), so the
exchange has to happen in normalised space or one side simply overwhelms the
other. It is squared, because the interesting region — entrainment without lock —
is narrow and sits near the bottom of the control.

**Assert coupling by magnitude, never by mere divergence.** A chaotic system
diverges from any perturbation at all, so a test that only checks "the numbers
changed" passes even when the effect is inaudible. The suite compares *mean
separation over 30 s* between coupled and uncoupled runs.

**CV.** Every modulation target reads through `CvNorm()` (0..1) or `CvBipolar()`
(-1..1) in [lib/cvInputs.hpp](lib/cvInputs.hpp), which layer over the shared
`core/cvInput.hpp` adapters. Keep it that way — the ±5 V hardware change must stay
a one-function edit. A *parameter's* CV depth is expressed as a fraction of that
parameter's own span, because the spans differ by four orders of magnitude across
the twelve systems.

**Randomize** lives in [lib/randomize.hpp](lib/randomize.hpp) so the hardware's
PRESETS ▸ RANDOM and Rack's Randomize (Ctrl+R) roll the same patch. It
deliberately leaves the IN 1 role, the CV matrix, the home view and RANGE alone —
those are patch wiring and preferences, not sound design.

## Common tasks

### Adding a menu parameter

1. Write the getter/setter in [lib/menuHandlers.hpp](lib/menuHandlers.hpp) —
   template it on the generator index if per-generator
2. Add a `MenuItem` to `MENU_ITEMS[]` with the right `group` — max six rows per page
3. Add the field to `LoadSaveParams`, `CollectParams()` and `UpdateParameters()`
   in [lib/presetManager.hpp](lib/presetManager.hpp), and bump `VALID_MAGIC`
4. Call `MarkUnsaved()` inside the setter
5. Decide whether `RandomizeParams()` should roll it (sound-shaping: yes; routing
   or preference: no)
6. If it is a new mutable file-scope global, register it in
   [vcv-plugin/src/engine/engine_state.def](vcv-plugin/src/engine/engine_state.def)

### Adding an attractor

1. Append it to `AttractorId` — **append only**, presets store the raw index
2. Add its case to `AttDerivative()` and its row to `ATTRACTORS[]`
3. **Measure it**: the largest Lyapunov exponent at the defaults you chose (it
   must be clearly positive), then the 0.2/99.8 percentile window per axis for
   `centre[]`/`halfSpan[]`. docs/Design.md §4 describes both measurements
4. If the system has more than four parameters, decide which four are musical and
   fix the rest in the derivative — Aizawa and Dadras already do this
5. `make test-att` — the suite runs every check above against every system

### Touching the integrator or the scaling

Run `make test-att`. The suites cover boundedness, liveness, range use,
determinism, divergence between the two generators, the divergence guard, SPEED,
LEVEL/OFFSET/SMOOTH, both RANGE modes, FREEZE, COUPLE and the trail. They are
cheap and they catch real regressions — the non-chaotic defaults above were found
by them, not by ear.

## Gotchas

- **`constrain()` is a macro that evaluates its argument three times.** Never put
  a PRNG call or anything else with a side effect inside one; compute into a
  local first. `randomize.hpp` shipped that bug and it put parameters outside
  their own legal range about one roll in fifty.
- **The home screen must self-mark dirty** — it is an animation, so
  `HandleDisplay()` marks it dirty every pass rather than waiting for an event.
- **Draw the head from the generator's live output, not from the newest trail
  point.** Trail points are pushed on the orbit's clock — one every 250 ms at
  SPEED 1.00 — so a plot drawn only from the buffer changes four times a second
  however fast the renderer runs. It reads as a stuttering animation while the
  jacks are perfectly smooth, which is exactly how it was first reported.
- **This module raises the redraw rate to 30 fps** (`ATT_DISPLAY_INTERVAL_MS`)
  through `DisplayManager::SetUpdateInterval()`, called from `Begin()` and from
  the Rack port's instance init. It is a runtime setter rather than a build flag
  because the unified firmware links one shared `DisplayManager`, and a
  per-translation-unit macro would give two TUs different definitions of the same
  class.
- **The trail is sampled on the orbit's clock, not the frame clock**, so the drawn
  figure covers the same amount of trajectory at every SPEED. It also has a
  wall-time floor, or SPEED 0.01 leaves the screen frozen for seconds and reads
  as a hung module.
- **Include order within the module**: `presetManager.hpp` uses types from
  `cvInputs.hpp` without including it, so `cvInputs.hpp` must come first (same
  pattern as the other modules). See also the global include-order rule in
  [../../AGENTS.md](../../AGENTS.md).

## The panel

[vcv-plugin/design/ChaosForge-src.svg](vcv-plugin/design/ChaosForge-src.svg) is
the authoring source and keeps real text. Rack renders through nanosvg, which
ignores SVG `<text>`, so the shipped `vcv-plugin/res/ChaosForge.svg` is that file
with every glyph converted to a path:

```sh
inkscape --export-text-to-path --export-plain-svg \
         --export-filename=res/ChaosForge.svg design/ChaosForge-src.svg
```

Edit the source, re-run that command, never hand-edit the converted file. The
source lives in `design/` rather than `res/` because `vcv/Makefile` stages every
file under a module's `res/` into the shipped plugin.

The faint figure behind the panel is a real Lorenz orbit, integrated with the
same RK4 step the firmware uses.

## Tests

`make test-att` (== `pio test -e native_att`) runs the suites in
[test/test_native/](test/test_native/): attractors, generator.
`make isolation-att` runs the two-instance VCV state-isolation test.

---

**For the concept, the decisions and their reasoning, see
[docs/Design.md](docs/Design.md).** Also [Readme.md](Readme.md) · [Manual.md](Manual.md)
