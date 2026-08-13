# WeaveForge — AI Coding Agent Instructions

**Read [../../AGENTS.md](../../AGENTS.md) first** — the board, the shell↔app
contract, the core split, storage, CV, naming, menu patterns and the VCV rules
are all shared and documented there, not repeated here. This file covers only
what is WeaveForge's own.

WeaveForge is the **dual shift-register sequencer**. Two 16-bit Turing Machines
clock in parallel; a WEAVE control decides how much of each one's incoming bit
comes from the other, from unrelated through cross-contaminated to chained into
a single ring as long as both. Four assignable jacks read windows out of those
registers as notes, modulation, gates or triggers.

**[docs/Design.md](docs/Design.md) holds the concept and the reasoning behind
every architectural decision — read it before making structural changes.**

## Module map

| File | Purpose |
| ---- | ------- |
| [src/wea_app.cpp](src/wea_app.cpp) | the `forge::IApp` — `Begin`/`Tick0`/`Tick1`, encoder events, all file-scope state |
| [src/wea_app.hpp](src/wea_app.hpp) | `forge::WeaApp()` factory, the only thing the shell includes |
| [lib/engine.hpp](lib/engine.hpp) | the per-iteration step + `DACWriteAll()`, shared with the Rack port |
| [lib/shiftreg.hpp](lib/shiftreg.hpp) | `ShiftRegister` / `WeavePair` — the bit machinery, **pure: no Arduino, no I/O, no clock** |
| [lib/outputs.hpp](lib/outputs.hpp) | `OutSlot` / `OutputBank`, the four types, `ROUTING_TEMPLATES[]`, the panel jack names |
| [lib/clock.hpp](lib/clock.hpp) | `StepClock` — RATE, over `core/clockSource.hpp` |
| [lib/params.hpp](lib/params.hpp) | `RegParams` / `GlobalParams` / `ModBus` / `LiveParams` + `ApplyParams()` |
| [lib/cvInputs.hpp](lib/cvInputs.hpp) | `CvNorm()`/`CvBipolar()`, `CVTarget`, `BuildModBus()`, the IN 1 ISR |
| [lib/randomize.hpp](lib/randomize.hpp) | `RandomizeParams()` — backs both PRESETS ▸ RANDOM and Rack's Randomize |
| [lib/menuDefinitions.hpp](lib/menuDefinitions.hpp) | `MenuItem` struct, `RowStyle` / `MenuItemType` enums |
| [lib/menuHandlers.hpp](lib/menuHandlers.hpp) | `MENU_ITEMS[]`, the live-view helpers, every setter — **read the guide at the top before editing** |
| [lib/menuRender.hpp](lib/menuRender.hpp) | `HandleDisplay()`, `groupTitles[]`, the loom home screen |
| [lib/presetManager.hpp](lib/presetManager.hpp) | `LoadSaveParams`, `CollectParams()`, `UpdateParameters()` |
| [lib/storage.hpp](lib/storage.hpp) | four-line shim over `core/appStorage.hpp` |
| [lib/version.hpp](lib/version.hpp) | module version string shown in the selector |

`boardIO`, `boardPinouts`, `clockSource`, `quantizer`, `scales`,
`displayManager`, `menuDisplay`, `encoder`, `encoderMenu` and `calibration` are
the **shared** ones in [../../core/](../../core/). This module uses no envelope
generator — a gate here is a level, not a shape.

## Signal flow

```text
IN 1 ──► TriggerReceived (ISR) ──► StepClock ──┐
                                               │ step
IN2/IN3 ──► CvNorm ──► BuildModBus ──► ModBus ─┤
                                               ▼
                       RegParams ──► ApplyParams() ──► LiveParams
                                                          │
                                              ┌───────────┴───────────┐
                                              ▼                       ▼
                                        register A  ◄── WEAVE ──►  register B
                                              └───────────┬───────────┘
                                                          ▼
                                                    OutputBank
                                          (SOURCE · TYPE · DEPTH · ROTATE)
                                              │      │      │      │
                                             A1     B1     A2     B2
```

## What is WeaveForge's own

**`lib/shiftreg.hpp` is pure and must stay that way.** It includes `<stdint.h>`
and nothing else — no Arduino, no `micros()`, no display. That is what lets the
host test runner, the RP2040 and the Rack port produce identical sequences from
one seed, and this module's whole premise is that a locked pattern is exactly
reproducible. The PRNG is a self-contained xorshift for the same reason.

**`WeaveRandom::Percent()` answers 0 and 100 without drawing.** Not an
optimisation: it is what makes "CHANCE 0 locks the pattern" and "WEAVE 100
chains the registers" exact rather than very likely, and both are claims on the
panel. It also keeps a frozen register from consuming the stream and shifting
the other one's drift.

**Both tails are sampled before either register shifts** (`WeavePair::Clock`).
Shifting A first and reading its tail for B hands B a bit one clock too new, and
the chain at WEAVE 100 % collapses. This is the one ordering bug this file can
have.

**RATE means steps per beat on BOTH clock sources.** The first version counted
input edges externally and scaled the period internally, so "/2" ran at half
speed on an external clock and double on the internal one. `test_clock.cpp`
asserts every rate against both sources *and* that the two agree — that last
test is the one that catches it.

**A GATE fires on a HIGH window, from the top of the range.** `OutputBank::Fires`
is `window >= span - limit`, and both halves have a test on them. Firing on the
low values (the original `window < limit`) makes a row of empty cells play and a
row of full ones go silent, which contradicts the screen, the manual and every
shift-register sequencer there has ever been; and repairing that as
`window >= limit` inverts THRESH, turning the sparse kick at 12 % into a busy one.
Density must stay `thresh` % whichever end the qualifying values come from.

**ROUTING is recomputed, never stored.** `RoutingOf()` compares the four slots
against the templates. A stored index goes stale the moment a slot is edited
from the menu, from CV or from a preset load.

**`StepClock::StepPhase()` is display-only, and `WeavePair::Crossed()` is
observation-only.** The loom animates off both (Design.md §6 "What moves"):
nothing that produces a voltage may read the phase, or the outputs would depend
on the frame rate; and nothing in `shiftreg.hpp` may read the crossing flags
back, or the file stops being a pure function of its inputs and the three hosts
stop agreeing. `StepPhase()` also has one non-obvious case with a test on it — a
DIVIDED EXTERNAL clock re-zeroes the accumulator every beat while the step spans
several, so the whole elapsed beats have to be added back or the sweep stutters
against a clock it is locked to.

**The register contents are part of the preset.** Four bytes, and without them a
preset restores a machine that makes a different pattern — which for this module
means it restores nothing. It is also the only way to keep a pattern, since
shortening LENGTH destroys the region above it (Design.md §2).

## Menu

Fourteen groups; the map is at the top of
[lib/menuHandlers.hpp](lib/menuHandlers.hpp). Two things beyond the shared
pattern:

- **Contextual row labels.** The last two rows of each OUT page mean different
  things per TYPE (RANGE/LEVEL/THRESH, SLEW/WIDTH). Their `label` is `""` and
  `OutRowLabel()` resolves it at draw time, which is why this module has its own
  `WEA_RenderGroup()` instead of core's `MD_RenderGroup()`.
- **The live loom view.** `livePreview` on a `MenuItem` hands the screen to the
  loom while that row is being turned. Flag a row only if the loom can actually
  *show* the change — LENGTH, CHANCE, WEAVE, DIR, BPM, RATE, DEPTH and ROTATE
  qualify; SOURCE, TYPE and the contextual fields do not, and a preview of those
  would be a strip in front of a still picture.

## Panel and jacks

The four jacks are two rows of two, named by **column**: `A1`/`A2` down the
left, `B1`/`B2` down the right, matching ChaosForge. In the DUO routing that is
literally register A on the left and B on the right.

**The naming deliberately collides with the registers.** A jack called `A2` and
a SOURCE of `A` mean different things, and they agree only in DUO. The jack name
says where the cable goes; SOURCE says what comes out of it. Do not "fix" this
by renaming one of them without reading Design.md §1.

## Not yet built

- **The bit editor** — the gesture is specified in Design.md §2 and constrained
  by the shell's 2 s hold; the home screen is read-only today.
- **A scale keyboard page** — the 12-note mask is already the source of truth and
  already in the preset, so this is UI only.
