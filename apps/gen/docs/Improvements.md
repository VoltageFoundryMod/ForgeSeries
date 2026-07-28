# GravityForge — Improvement Ideas

Feature-wise the module is complete; this is the parking lot for what could come
next, roughly in order of musical payoff per unit of work. Each entry says what
it is and why it belongs on *this* module rather than being a generic feature.

## Gaps in what already exists

- [ ] **Peg muting on the hardware.** `pegMask` is simulated, saved with presets
      and rolled by RANDOM, and the home screen already draws muted pegs as
      hollow rings — but there is no way to *set* it from the panel. It is only
      reachable from the VCV context menu. Design.md calls peg muting the way you
      open up the rhythm and choose which coupled transfers speak, so the panel
      is missing the control the design leans on hardest.
      Suggested shape: a custom page per container (like HOME) drawing that
      container's ring with a cursor — turn to walk the pegs, click to toggle.
      The six-row list cannot hold 16 toggles, so it has to be a drawn page.

- [ ] **SPIN FREE is unreachable on hardware.** `SpinFree` exists in the enum and
      `Clock::OmegaFor()` honours it, but the A/B PHYSICS pages are already six
      rows deep so neither the FREE entry nor its rate control fits. Folding DIR
      into SPIN as signed entries (`-16 … -1/2, 1/2 … 16, FREE`) frees the row
      that FREE HZ needs and removes a menu item at the same time.

- [ ] **No pitch-domain CV targets.** The matrix modulates physics and coupling
      but nothing musical. `BIAS` in particular re-registers the melody without
      touching the rhythm — a different axis from everything else on the list —
      and `SPREAD` and `ROT` (below) would join it cheaply.

## Concept extensions

- [x] **Loop / phrase mode.** *Shipped.* Snapshot/restore lives in
      `PhysicsWorld` (~220 bytes per container), the parameters in `WorldParams`,
      and the LOOP page is group 2 with BEATS / WAKE / NAP / A SHIFT / B SHIFT /
      NEW PHRASE. The restore happens on an exact step boundary as planned —
      and the sim needed its own step-derived clock (`_simUs`) to go with it,
      because the peg refractory windows were being measured against wall time
      and could resolve differently between two identical ball states. See
      Design.md §6 "Loop mode".

- [ ] **Peg ring rotation (`ROT`).** Offset the peg→degree mapping relative to
      the physical ring, so the note at the bottom of the container changes
      without touching the physics. One index offset inside `SemitoneForPeg()`,
      and it gives the same kind of movement Euclidean rotation gives on
      ClockForge. Excellent CV target.

- [ ] **Hit density / probability.** A per-container 0–100 % chance that a valid
      strike speaks. It is a couple of lines in the emit path and it is the most
      direct control of the parameter that most needs controlling — at factory
      settings a container fires ~7 times a second, and every other way of
      thinning it out (fewer balls, less gravity, less bounce) also changes the
      character of the motion. Also the obvious next CV target.

- [ ] **Gravity direction / TILT.** Gravity is hard-wired downward. A per-container
      angle turns falling into tumbling; slaving the angle to the container's own
      rotation gives centrifugal behaviour where the balls ride the rim. Two
      floats and one sin/cos per parameter change, and it is a genuinely
      different motion rather than a variation on the current one.

- [ ] **Ball variety.** All balls are identical, so every hit is roughly the same
      strength (measured window ~29–170, averaging ~85) and ACCENT has less to
      work with than it could. Per-ball mass/radius drawn from a spread control
      would give heavier balls that hit harder and settle lower, making both the
      dynamics and the pattern less uniform.

- [ ] **Portals.** Ball *transfer* between overlapping containers.
      `PhysicsWorld::OverlapArc()` is already written and tested for exactly
      this. Deferred because ball counts drift and it is easy to end up with
      everything in one container and silence in the other — a balance rule
      (transfer only toward the container with fewer balls, or cap the imbalance
      at two) makes it safe enough to ship.

- [ ] **Alternate output modes.** OUT 3/4 are always gates. An option to emit
      the accent value, or a continuous CV derived from ball height, would give a
      free modulation source out of the simulation without adding jacks. Costs
      one menu row per container.

## Quality of life

- [ ] **Link container B's scale to A.** B follows A's scale and root with an
      interval offset, so editing one keeps both in key. One row on B NOTES.

- [ ] **Per-container mute / solo.** Useful while dialing one container in, and
      an obvious performance control. Could live on the COUPLING page.

- [ ] **Decay-versus-density warning.** An envelope longer than the gap between
      hits never returns to zero, which is the module's documented failure mode
      and the first thing a new user hits. The hit rate is already known — flag
      DECAY on screen when it exceeds the measured average gap.

- [ ] **Hits-per-second readout.** A small number on the NOTES or GATE page makes
      density a thing you can set rather than guess at, and it makes the point
      above self-explanatory.

## Documentation and assets

- [ ] Capture the display screenshots referenced by [Manual.md](../Manual.md):
      `MainScreen`, `Clock`, `Coupling`, `Physics`, `Notes`, `Gate`,
      `CVTargets`, `Settings` under `images/display/`.
- [ ] Panel front photo (`images/Front.png`), logo (`images/GravityForge_Logo.png`)
      and the MCU photo (`images/XIAORP2040.png`).
- [ ] ModularGrid entry, and link it from the Readme like ClockForge does.
