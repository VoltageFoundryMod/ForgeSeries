# GravityForge — AI Coding Agent Instructions

**Read [../../AGENTS.md](../../AGENTS.md) first** — the board, the shell↔app
contract, the core split, storage, CV, naming, menu patterns and the VCV rules
are all shared and documented there, not repeated here. This file covers only
what is GravityForge's own.

GravityForge is the **dual physics-based generative sequencer**. Two rotating
containers hold bouncing balls; every ball that strikes a peg on a container wall
emits a note. A single PROXIMITY control slides the containers together, taking
them from independent sequencers through overlapping-and-coupled to a single
merged space. Inspired by Teenage Engineering's Tombola.

**[docs/Design.md](docs/Design.md) holds the concept and the reasoning behind
every architectural decision — read it before making structural changes.**

## Module map

| File | Purpose |
| ---- | ------- |
| [src/gen_app.cpp](src/gen_app.cpp) | the `forge::IApp` — `Begin`/`Tick0`/`Tick1`, encoder events, all file-scope state |
| [src/gen_app.hpp](src/gen_app.hpp) | `forge::GenApp()` factory, the only thing the shell includes |
| [lib/engine.hpp](lib/engine.hpp) | the per-iteration step + `DACWriteAll()`, shared with the Rack port |
| [lib/physics.hpp](lib/physics.hpp) | `Ball`, `Container`, `PhysicsWorld` — the simulation and proximity coupling |
| [lib/sequencer.hpp](lib/sequencer.hpp) | `GravityChannel` — peg→note mapping, quantize deferral, envelope |
| [lib/clock.hpp](lib/clock.hpp) | internal/external tempo, rotation rate, the quantize grid |
| [lib/params.hpp](lib/params.hpp) | `ContainerParams` / `WorldParams` / `ModBus` + `ApplyParams()` |
| [lib/cvInputs.hpp](lib/cvInputs.hpp) | `CvNorm()` / `CvBipolar()`, `CVTarget`, `CVTargetNames[]`, `BuildModBus()`, IN 1 role |
| [lib/randomize.hpp](lib/randomize.hpp) | `RandomizeParams()` — backs both PRESETS ▸ RANDOM and Rack's Randomize |
| [lib/menuDefinitions.hpp](lib/menuDefinitions.hpp) | `MenuItem` struct, `RowStyle` / `MenuItemType` enums |
| [lib/menuHandlers.hpp](lib/menuHandlers.hpp) | `MENU_ITEMS[]`, `MarkUnsaved()`, every setter — **read the guide at the top before editing** |
| [lib/menuRender.hpp](lib/menuRender.hpp) | `HandleDisplay()`, `groupTitles[]`, the physics home screen |
| [lib/presetManager.hpp](lib/presetManager.hpp) | `LoadSaveParams`, `CollectParams()`, `UpdateParameters()` |
| [lib/storage.hpp](lib/storage.hpp) | four-line shim over `core/appStorage.hpp` |
| [lib/version.hpp](lib/version.hpp) | module version string shown in the selector |

The quantizer, envelope and scale tables are the **shared** ones in
[../../core/](../../core/), as are `boardIO`, `boardPinouts`, `displayManager`,
`menuDisplay`, `encoder` and `calibration`.

## Signal flow

```text
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

## Jack map

All four outputs go through the MCP4728, written positionally by `DACWriteAll()`
in [lib/engine.hpp](lib/engine.hpp). Deliberately identical to NoteForge:

| DAC index | Jack | Signal |
| --------- | ---- | ------ |
| 0 | 1 | CV A — pitch of the last peg hit, container A |
| 1 | 2 | CV B |
| 2 | 3 | GATE A |
| 3 | 4 | GATE B |

## Key subsystems

**Pegs live on the container wall.** Free-floating pegs would be
O(balls × pegs) per step and would not fit the CPU budget. On the wall it is
O(1): the wall collision already computes the contact angle, and the peg index is
one division. This is the decision the whole module's performance rests on.

**Float, not fixed-point.** ~14 soft-float ops per ball per step; at 8 balls × 2
containers × 1 kHz that is ~8 % of one core. `sqrtf`/`atan2f` run only on actual
collisions. See [docs/Design.md](docs/Design.md) §3 for the worked budget.

**Determinism is a requirement, not a nicety.** Fixed 1 ms timestep, a
self-contained `PhysRandom`, and time passed in as a parameter — never read from a
wall clock. That is what makes the sim unit-testable and what makes the VCV port
match the hardware under faster-than-realtime rendering. **Do not introduce
`micros()` calls inside `lib/` DSP code.**

**Base params vs live sim.** `ContainerParams` / `WorldParams` are what the *user*
set; `ModBus` is this loop's CV modulation; `ApplyParams()` combines them into the
live `Container`. Never write CV modulation straight into a `Container` — the menu
would show the modulated value and presets would save it.

**Proximity coupling.** `PROXIMITY` sets the centre separation; `Overlap()` is the
coupling strength. A wall strike whose contact point falls inside the *other*
container transmits an impulse into it, **and rings the peg nearest that point on
the receiving rim** (`Container::RingPegNear`) so the transfer is heard as well as
seen. The note is always the receiving container's own peg, so a transfer can
never be out of key. This is energy transfer, not ball transfer — ball counts stay
stable. `PhysicsWorld::OverlapArc()` exists for the deferred "portal" feature and
is kept correct and tested.

Coupling fires only a few times a second amid much busier bouncing, so it is
invisible without a cue: a transmitted strike raises a **spark** (an expanding
ring drawn at the contact point). Do not remove it — without it the whole control
reads as if it does nothing, which is exactly how it was first reported. Assert
coupling by *magnitude*, never by mere non-zero divergence; a chaotic system
diverges from any perturbation, so a "> 0.01 px" test passes even when the effect
is imperceptible.

**Loop mode is snapshot + step count.** `PhysicsWorld` captures both containers
(balls, rotation, `PhysRandom`) and rewinds every N beats, so a deterministic sim
becomes a repeating phrase. Two things it rests on, neither optional:

- The rewind happens on an exact **step** boundary inside `Advance()`, never on
  elapsed wall time. One step of drift is a different phrase within a few repeats.
- The sim runs on `_simUs` — its own clock, exactly 1 ms per step — because the peg
  refractory windows are measured against it. On wall time the same ball state can
  clear a 12 ms window on one pass and miss it on the next, which is invisible
  free-running and fatal to a loop.

Hit timestamps travel through the snapshot as **ages**, not absolute times, or
each repeat would clear a refractory window it did not clear the first time.
Parameters are deliberately *not* snapshotted: the point is to keep playing the
controls over a locked phrase. `Reset()` re-arms, which is what gives Randomize a
fresh phrase for free.

**Quantize is last-wins.** With the grid on, a newer peg hit replaces a pending
one rather than stacking. Stacking would release a burst of retriggers at the
boundary instead of something that sounds like the physics that made it.

**CV.** Every modulation target reads through `CvNorm()` (0..1) or `CvBipolar()`
(-1..1) in [lib/cvInputs.hpp](lib/cvInputs.hpp), which layer over the shared
`core/cvInput.hpp` adapters. Keep it that way — the ±5 V hardware change must stay
a one-function edit.

**Randomize** lives in [lib/randomize.hpp](lib/randomize.hpp) so the hardware's
PRESETS ▸ RANDOM action and Rack's Randomize (Ctrl+R) roll the same patch. It
deliberately leaves tempo, the IN 1 role and the CV matrix alone — those are patch
wiring, not sound design.

## Common tasks

### Adding a menu parameter

1. Write the getter/setter in [lib/menuHandlers.hpp](lib/menuHandlers.hpp) —
   template it on the container index if per-container
2. Add a `MenuItem` to `MENU_ITEMS[]` with the right `group` — max six rows per page
3. Add the field to `LoadSaveParams`, `CollectParams()` and `UpdateParameters()`
   in [lib/presetManager.hpp](lib/presetManager.hpp), and bump `VALID_MAGIC`
4. Call `MarkUnsaved()` inside the setter
5. Decide whether `RandomizeParams()` should roll it (sound-shaping: yes; routing
   or sync: no)

**Per-container handlers are templates**: `setGravity<0>` / `setGravity<1>` share
one implementation. Add a per-container parameter once, instantiate it twice.

### Adding a CV modulation target

1. Add the enum to `CVTarget` in [lib/cvInputs.hpp](lib/cvInputs.hpp)
2. Add its name to `CVTargetNames[]` — keep it ≤ 7 chars, it has to fit the row
3. Add a field to `ModBus` if it is a new destination, and handle it in
   `BuildModBus()` and `ApplyParams()`

### Touching the physics

Run `make test-gen`. The suites cover containment (balls never escape, including
at max gravity and spin), determinism, peg-index range, the energy floor that
stops the sequencer dying, coupling symmetry, and the catch-up guard. They are
cheap and they catch real regressions.

## Gotchas

- **The home screen must self-mark dirty** — it is an animation, so
  `HandleDisplay()` marks it dirty every pass rather than waiting for an event.
- **`physics.hpp` works in screen pixels** — the renderer transforms nothing.
  Changing `PHYS_R` means re-deriving the home screen's vertical budget.
- **Include order within the module**: `presetManager.hpp` uses types from
  `cvInputs.hpp` without including it, so `cvInputs.hpp` must come first (same
  pattern as NoteForge). See also the global include-order rule in
  [../../AGENTS.md](../../AGENTS.md).

## Tests

`make test-gen` (== `pio test -e native_gen`) runs the suites in
[test/test_native/](test/test_native/): clock, physics, sequencer.

---

**For the concept, the decisions and their reasoning, see
[docs/Design.md](docs/Design.md).** Also
[Readme.md](Readme.md) · [Manual.md](Manual.md) ·
[docs/Improvements.md](docs/Improvements.md)
