# GravityForge — Design

A dual physics-based generative sequencer for the Forge Series hardware.

Two virtual containers hold bouncing balls. Gravity pulls them down, the
containers rotate, and every time a ball strikes a peg on a container wall it
emits a note. The result is a rhythm that is repetitive enough to feel composed
but never quite loops — the Tombola idea, adapted to one encoder and a 128×64
screen.

The novel part of this module is the **proximity coupling**: the two containers
are drawn side by side and a single parameter slides them together. Apart, they
are two independent sequencers. Overlapping, collisions in one shove the balls
in the other. Fully merged, they share one space. It is one continuous knob from
"two sequencers" to "one entangled instrument", and you can see it happening.

---

## 1. Hardware contract

Shared Forge Series platform (XIAO RP2040, SSD1306 128×64, MCP4728 quad DAC,
rotary encoder + switch). See [ForgeSeries-Hardware].

### Inputs

| Jack | Range        | Role                                          |
| ---- | ------------ | --------------------------------------------- |
| IN 1 | gate/trigger | Menu-selectable: CLOCK / RESET / KICK / SPAWN |
| IN 2 | **0–5 V**    | Assignable modulation target                  |
| IN 3 | **0–5 V**    | Assignable modulation target                  |

> **CV range.** The current hardware revision accepts **0–5 V only**. A later
> revision moves to ±5 V. Every modulation read therefore goes through a single
> helper, `CvNorm()` in `lib/cvInputs.hpp`, which returns a normalized `0..1`.
> When the bipolar hardware lands, that one function changes to return `-1..1`
> and the targets that want bipolar behaviour opt in via `CvBipolar()` — no
> target-by-target rewrite.

### Outputs

Mirrors NoteForge exactly, so the jack map is muscle memory across the series:

| DAC index | Jack | Signal                                        |
| --------- | ---- | --------------------------------------------- |
| 0         | 1    | CV A — pitch of the last peg hit, container A |
| 1         | 2    | CV B — pitch of the last peg hit, container B |
| 2         | 3    | GATE A — envelope / trigger / gate            |
| 3         | 4    | GATE B                                        |

`OUT_CV(ch)` / `OUT_GATE(ch)` in `lib/pinouts.hpp` name these indices.

---

## 2. Why pegs live on the wall

Tombola draws notes as pegs _inside_ a rotating container. Simulating free-floating
pegs means testing every ball against every peg each step — O(balls × pegs),
which on a 133 MHz Cortex-M0+ with no FPU is the one thing that would blow the
CPU budget.

Putting the pegs **on the container wall** is both closer to the original (the
notes ring the container) and O(1): the wall collision is already being
computed, so the contact angle falls out for free, and the peg index is one
division.

```
contact angle (world)  φ = atan2(py - cy, px - cx)
container-local        φ' = φ - rotation
peg index              i  = floor(φ' / (2π / pegCount))  mod pegCount
```

No search, no per-peg distance test. This is the single decision that makes the
whole module fit in the time budget.

---

## 3. Float, not fixed-point

The first instinct on an FPU-less M0+ is Q16.16 fixed-point. Working the budget
out, that turns out to be unnecessary:

The steady-state (non-colliding) cost per ball per step is ~14 float ops —
integrate velocity and position (8), then the wall test `dx²+dy² > R²` (6). At
the design limit of 8 balls per container × 2 containers × 1 kHz that is
**224 k soft-float ops/sec**, roughly 11 M cycles/sec, or **~8 % of one core**.

The expensive calls — `sqrtf`, `atan2f` — happen only on an actual collision,
which is a handful of events per second per ball, not per step.

So the physics is written in plain `float`, matching the idiom of the rest of
the series (`lib/envelope.hpp`, `lib/quantizer.hpp`) and keeping it readable and
unit-testable. Fixed-point stays available as an optimization if a future
feature (ball–ball collision, many more balls) changes the arithmetic.

**Budget guard:** physics runs at a fixed 1 kHz on Core 0 with a hard cap of
`PHYS_MAX_STEPS_PER_CALL` catch-up steps, so a display stall or a slow I2C write
can never spiral into an unbounded catch-up loop.

---

## 4. Physics model

State per ball: position `(x, y)`, velocity `(vx, vy)`. Semi-implicit Euler at a
fixed 1 ms timestep — fixed-step is what makes the sim deterministic, and
determinism is what makes it unit-testable and identical in VCV Rack.

```
v += g · dt
p += v · dt
```

### Wall collision

A ball is in contact when `|p − c| > R − ballRadius`. On contact:

1. **Project back** onto the wall (prevents tunnelling / sticking).
2. **Reflect** the normal velocity component, scaled by restitution (BOUNCE).
3. **Add the wall's tangential velocity.** The wall is rotating at ω, so the
   contact point is moving at `ω × r`. Transferring a fraction of that
   (the SPIN coupling) is what makes a rotating container actually stir the
   balls rather than just spinning a decorative ring behind them. This is the
   mechanism that makes rotation musical.
4. **Emit** the peg hit for the contact angle, if that peg is enabled.

### Keeping the gate a gate

Three guards decide whether a contact becomes a note, and they are the
difference between a sequencer and a buzzer:

1. **`PEG_MIN_IMPACT_SPEED`** — a peg only speaks when it is actually struck.
   Grazes and settling contacts still resolve as collisions and still transmit
   through the coupling; they just make no note.
2. **Refractory windows** — each ball remembers the last peg it fired.
   Re-striking the same peg needs the full `PEG_REFRACTORY_US`; moving to a
   different one needs the shorter `PEG_MIN_INTERVAL_US`.
3. **`PHYS_MIN_BOUNCE_SPEED`** — a ball that has lost its energy is topped back
   up, so a patch never dies in a silent pile at the bottom.

The interaction between (2) and (3) is the subtle part, and getting it wrong is
what turned the GATE jack into a continuous sawtooth in the first version. A
ball settling against a **rotating** wall registers a contact every step, and
because rotation keeps sliding a *new* peg underneath it, the short "different
peg" window applies every time rather than the long one. With the energy floor
set low (6 px/s) the ball vibrated against the rim instead of bouncing, and the
container fired ~33 times a second — the envelope was reset at ~90 % every time
and the output never returned to zero.

The floor therefore has to sit comfortably **above** the impact threshold. At
60 px/s a settled ball hangs for `2×60/220 ≈ 0.55 s`, so it ticks along at about
2 Hz instead of buzzing, and every one of those bounces is hard enough to
count as a strike. The result is ~7 hits/sec per container.

That number then sets the envelope defaults: decay must be shorter than the gap
between notes (~140 ms) or the gate cannot finish, which is why the factory
decay is 100 ms rather than something pad-like.
`Sequencer.GateReturnsToZeroBetweenHits` is the regression guard.

---

## 5. Proximity coupling

Both containers have radius `R` and sit on the screen midline. A single
**PROXIMITY** parameter (0–100 %) sets the distance `D` between their centres:

```
D = D_MAX − proximity/100 × (D_MAX − D_MIN)

PROXIMITY 0%              PROXIMITY 55%           PROXIMITY 100%
   ___     ___              ___ _ ___                 _____
  / A \   / B \            / A X B \                 / AB  \
 |     | |     |          |    |    |               |       |
  \___/   \___/            \___|___/                 \_____/
    independent            overlap: coupled          merged: one space
```

Overlap fraction `k = clamp((2R − D) / 2R, 0, 1)` is the coupling strength.

When `k > 0` and a ball in A strikes the wall **at a contact point that lies
inside container B**, the collision transmits: an impulse is applied to B's
balls, directed away from the contact point, scaled by `k` and by the collision
energy, and falling off with distance. Symmetrically for B → A.

This is deliberately _energy_ transfer, not ball transfer. It means:

- the ball count in each container is stable and predictable
- each container keeps its own scale, peg layout and gravity

### What a transmitted strike plays

The energy arrives at a point on the receiving rim, so **the peg there rings** —
a real note and gate on that channel. The note is entirely the receiving
container's own (its peg ring, its scale, its SPREAD/BIAS, its muted pegs), so
nothing is copied across and a transfer can never sound out of key. Muting a peg
absorbs the transfer silently, which is how you shape which transfers speak.

Transmitted energy is scaled by COUPLE **before** the impact test, which is what
turns COUPLE into a musical gradient rather than an on/off switch — light
coupling only nudges trajectories, and the containers begin answering each other
audibly only as it comes up:

| PROXIMITY | COUPLE | A notes/s | B notes/s | transmitted/s |     |
| --------- | ------ | --------- | --------- | ------------- | --- |
| 0 %       | 60 %   | 6.1       | 4.1       | 0.0           | apart — baseline |
| 60 %      | 0 %    | 6.4       | 4.6       | 0.0           | overlapping, coupling off |
| 60 %      | 30 %   | 6.0       | 4.2       | 3.8           | transmitting, too weak to ring |
| 60 %      | 100 %  | 7.6       | 6.1       | 3.5           | starting to answer |
| 100 %     | 100 %  | 10.3      | 9.9       | 10.5          | fully entangled |

Density roughly doubles at the extreme, which is the point of that setting. The
GATE jack still articulates there — measured 10.3 Hz, 0.00–5.00 V, idle 39 % of
the time with the 100 ms factory decay.

Transmitted strikes carry their own refractory, separate from the per-ball one,
because there is no ball behind them.

### Making it legible

The impulse is strong — measured against an identical uncoupled world it
displaces balls by **~13 px on a 40 px container**:

| PROXIMITY | separation | overlap | transmitted strikes/s | displacement |
| --------- | ---------- | ------- | --------------------- | ------------ |
| 0 %       | 48         | 0.00    | 0.0                   | 0.00 px      |
| 40 %      | 28.8       | 0.28    | 3.0                   | 13.5 px      |
| 100 %     | 0          | 1.00    | 12.4                  | 13.6 px      |

But strength was never the problem. Coupling fires a handful of times a second
while both containers are *already* bouncing several times a second, so with
nothing tying cause to effect the control reads as if it does nothing at all —
you cannot see a counterfactual.

So a transmitted strike raises a **spark**: an expanding double ring drawn at the
contact point for a few frames. That one cue is what turns PROXIMITY from
"apparently inert" into something you can watch working.
`Physics.CouplingRaisesAVisibleSpark` guards it.

Note the tests here deliberately assert *magnitude*, not mere non-zero
divergence. An earlier version only checked the two worlds differed by 0.01 px,
which any chaotic perturbation satisfies — it would have passed even if coupling
were imperceptible.

### Portals — deferred

Ball _transfer_ between containers is a natural extension of exactly this
geometry: the arc of A's wall that lies inside B is already computed, so opening
it and moving the ball into B is a small change. It is deliberately **not** in
v1 — it makes ball counts drift, and it is easy to end up with every ball in one
container and silence in the other. `PhysicsWorld::OverlapArc()` is written and
tested so this stays cheap to add later.

---

## 6. Timing

Physics **always free-runs.** Collisions produce note events the instant they
happen — that organic, never-quite-on-grid feel is the whole point.

Optionally, those events can be deferred to a clock grid:

```
QUANTIZE:  OFF | 1/4 | 1/8 | 1/16 | 1/8T | 1/16T
```

With quantize on, a peg hit is queued and released on the next division
boundary. **Drop rule:** if a second hit arrives for the same channel before the
pending one is released, the newer hit replaces it (last-wins). Stacking would
produce a burst of retriggers at the boundary; last-wins keeps the density
sounding like the physics that produced it.

The clock itself is the internal BPM (20–300, default 120), or an external clock
at IN 1 when that jack's role is CLOCK (interval between edges → BPM, median of
the last few intervals so one jittery edge does not lurch the tempo).

### Configured vs actually running

`Clock` tracks two separate things, and conflating them is a bug:

- `IsExternal()` — IN 1 is *configured* as a clock input. A menu setting.
- `IsExternalLive()` — and pulses are *actually arriving* right now.

The hardware has no switched jacks, so it cannot tell a patched cable from an
unpatched one — but it can tell whether edges are coming in, and that is the
thing that matters. CLOCK is IN 1's default role, so keying anything off
`IsExternal()` means the module declares itself externally clocked from the
moment it powers on, and the "e" badge on the home screen never tells you
anything.

Liveness needs a measured *interval*, not just one edge, and it lapses if no
pulse arrives within three intervals (clamped to 0.5–5 s). Scaling the timeout
to the observed tempo matters: a 30 BPM clock at 1 ppqn is 2 s between pulses,
so a short fixed timeout would declare it dead between every beat, while a long
fixed one would leave a fast clock hanging for seconds after it stops.

When liveness lapses the tempo falls back to the internal BPM and the interval
history is cleared. Without that, unplugging a clock leaves the containers
turning at whatever tempo was last derived, with the internal setting ignored
for ever.

Rotation is tied to the clock rather than set in raw degrees/sec, so the
containers stay musically related to the patch: **SPIN** selects beats per
revolution (`1/2/4/8/16`, plus reverse), and there is a FREE setting for when you
want it detached.

### Loop mode — keeping a phrase

The module's one real weakness is that a passage you like is gone before you can
reach the encoder. The fix falls out of determinism: the same state, stepped the
same number of times, produces the same notes. So a phrase is nothing more than a
snapshot plus a step count. Capture the balls, the rotation and the `PhysRandom`
state; run N beats' worth of steps; put it all back.

Three decisions make it hold together, and each of them is the difference
between a phrase that repeats and one that only nearly repeats:

**The rewind lands on an exact step, not on elapsed time.** `LoopTick()` runs
inside `PhysicsWorld::Advance()`'s stepping loop and counts steps. Scheduling it
from wall time would put the boundary a step early or late depending on how the
caller was interrupted, and one step of divergence in a chaotic system is a
different phrase within a few repeats.

**The simulation runs on its own clock.** `_simUs` advances exactly
`PHYS_STEP_US` per step and is what the peg refractory windows are measured
against. Previously the wall time was passed down, which meant identical ball
states could clear a 12 ms window on one pass and miss it on the next depending
on how many catch-up steps a call happened to batch. Free-running that is
invisible; looping it is fatal.

**Hit timestamps travel as ages, not absolute times.** The refractory guards ask
"how long since this ball last spoke". A restored absolute timestamp would sit
further into the past on every repeat, so the first bounce of each loop would
eventually clear a window it had not cleared the first time and the phrase would
gain a note it never contained.

What is deliberately _not_ snapshotted is the parameter set — gravity, spin, the
peg mask, proximity, the scale. Restoring those would make the loop fight the
menu and freeze CV modulation, when the reason to lock the motion is so you can
keep playing everything else over the top of it.

**NAP/WAKE** then mutes whole loops per container, with **SHIFT** offsetting each
container's place in that cycle — wake 1 / nap 1 with B shifted by one is
call-and-response for the cost of one menu row. The physics keeps running through
a nap and only the voice is silenced (`GravityChannel::SetMuted`), so a container
returns exactly where it would have been rather than restarting.

A change to **BEATS** re-arms and captures a new phrase; a change of _tempo_ does
not, or an external clock wandering by a BPM would re-arm every pass and the loop
would never repeat at all. `Reset()` re-arms too, which is what gives Randomize a
freshly captured phrase without any code of its own.

---

## 7. Pitch

Reuses NoteForge's `Quantizer` and `scales.hpp` unchanged — the peg ring is
just an index into the quantizer's table of enabled semitones.

Everything is counted in **scale degrees**, never semitones, so no combination of
settings can produce an out-of-key note, and the interval between adjacent pegs
follows the scale — a third in major, something else in pentatonic. Same
reasoning as NoteForge's degree-based transposition.

### SPREAD and BIAS

There is deliberately **no octave control.** A single octave offset only slides
the ring up and down; it says nothing about how wide the ring is or how the notes
sit inside it. Two controls do more with the same panel space:

- **SPREAD** — how many octaves the ring covers (1–5), independent of peg count.
- **BIAS** — where inside that span the notes crowd (−100 low … 0 even … +100 high).

```
peg i   t     = i / (pegCount - 1)              fraction around the ring
        gamma = 2^(-|bias| / 70)                always <= 1
        warp  = bias >= 0 ?  t^gamma            crowd high
                          :  1 - (1-t)^gamma    crowd low  (mirror image)
        degree   = base + round(warp * span)
        semitone = quantizer.SemitoneAt(degree)
```

Warping `t` moves the notes in between **without moving the ends** (measured,
and asserted by `Sequencer.BiasTableMatchesTheDocumentedNotes`):

```
8 pegs, C major, SPREAD 2

BIAS   0  (even)    C2  E2  G2  B2  D3  F3  A3  C4
BIAS -100 (low)     C2  D2  E2  F2  G2  A2  C3  C4
BIAS +100 (high)    C2  C3  E3  F3  G3  A3  B3  C4
```

Fixed endpoints are what make the two controls independent: the ring always
covers exactly the octaves SPREAD asks for, and BIAS only decides the
distribution inside it.

The two directions are **mirror images** rather than `gamma` vs `1/gamma`,
because the warp is discretised onto whole scale degrees. A steep curve near
`t = 0` rounds several pegs onto the same bottom note while the same curve near
`t = 1` spreads them out, so a plain `t^gamma` crowded low far harder than it
crowded high — at full negative bias it stacked three of eight pegs onto the
root while full positive bias produced no duplicates at all. Mirroring makes the
control behave identically in both directions, and neither extreme stacks pegs.

### Where the span sits

With no octave control something has to anchor the register. The span is
**centred in the 0–5 V output range and snapped to whole octaves**, so at the
default SPREAD 2 the ring runs C2–C4 — a usable register with nothing to set —
and the lowest peg always lands on the root rather than an arbitrary degree
part-way up the scale. ROOT still chooses the pitch class.

Because a peg's pitch now depends on the peg *count* (it sets where each peg
falls within the span), the count travels with the hit — including a deferred
one, whose count must be the one that was in force when the ball actually
struck.

Per container: scale, root, octave, peg count (4–16), and a peg enable mask so
individual pegs can be silenced to open up the rhythm.

---

## 8. Screen

`R = 20 px`, centres on `y = 32`, so the containers occupy rows 12–52 and leave
a header row and a footer row.

```
 0        BPM 120   SPIN 4   CPL 55%          <- header
 12   .-""-.   .-""-.
      /  o   \ /   *  \                        <- containers slide together
 32  |    o   X   *    |                          with PROXIMITY
      \   .  / \  o   /
 52    '-..-'   '-..-'
 54       C3            G4                     <- live note per channel
```

Pegs are drawn on the wall: a solid 5 px dot when enabled, a small hollow ring
when muted, and a 7 px blob for a few frames after firing. Sizes are set by the
tightest case — at 16 pegs the rim gives 7.9 px of spacing, so a 5 px dot still
leaves clear air between neighbours while the 7 px flash fills the gap and makes
a hit unmistakable.

Balls are drawn at their true simulated radius (`PHYS_BALL_R`, 5 px across) —
filled for container A, hollow-with-a-centre-dot for B, so the two populations
stay distinguishable once PROXIMITY merges them into one space. Drawing the real
radius is the point of working in screen pixels: the ball you see is the ball
being simulated, so it never appears to sink into or float off the wall.

Rendering runs on **Core 1** exactly as in NoteForge, so the 20 Hz redraw never
stalls the 1 kHz physics on Core 0.

---

## 9. Menu map

| Group | Page      | Contents                                           |
| ----- | --------- | -------------------------------------------------- |
| 0     | HOME      | the physics view (custom renderer)                 |
| 1     | CLOCK     | BPM, spin, quantize, IN1 role                      |
| 2     | COUPLING  | proximity, coupling amount, reset/kick actions     |
| 3     | A PHYSICS | gravity, bounce, balls, spin ratio                 |
| 4     | B PHYSICS | same, container B                                  |
| 5     | LOOP      | phrase length, nap/wake, shift, new phrase         |
| 6     | A NOTES   | scale, root, octave, peg count                     |
| 7     | B NOTES   | same, container B                                  |
| 8     | A GATE    | mode, attack, decay, level                         |
| 9     | B GATE    | same, container B                                  |
| 10    | CV        | IN2 target, IN3 target, depths                     |
| 11    | SETTINGS  | preset save/load, screen timeout, calibration info |

LOOP sits after the two PHYSICS pages rather than next to CLOCK: its shift and
nap/wake settings are read per container, so it belongs with the rest of the
per-container editing rather than in the clock-domain block at the top.

Every page except HOME is a plain list handled by the existing generic
`MD_RenderGroup()` — no new rendering code per page.

---

## 10. File map

New to this module:

| File                 | Purpose                                                           |
| -------------------- | ----------------------------------------------------------------- |
| `lib/physics.hpp`    | `Ball`, `Container`, `PhysicsWorld` — the simulation and coupling |
| `lib/sequencer.hpp`  | `GravityChannel` — container + peg→note mapping + envelope        |
| `lib/clock.hpp`      | internal/external clock, spin rate, quantize grid                 |
| `lib/cvInputs.hpp`   | 0–5 V acquisition, `CvNorm()`, target matrix, IN 1 role           |
| `lib/menuRender.hpp` | the physics home screen                                           |

Carried over from NoteForge essentially unchanged: `quantizer.hpp`, `scales.hpp`,
`envelope.hpp`, `displayManager.hpp`, `menuDisplay.hpp`, `menuDefinitions.hpp`,
`boardIO.hpp`, `storage.hpp`, `calibration.hpp`, `calibrationData.hpp`,
`encoder.hpp`, `utils.hpp`, `splash.hpp`.

---

## 11. Constraints inherited from the platform

- Anything in `lib/` must compile for **both** the RP2040 and the VCV Rack shim.
- Core 1 owns `Wire` (display); Core 0 owns `Wire1` (DAC). Never cross them.
- `micros()` is the only clock — every time-dependent subsystem takes the
  timestamp as a parameter, which is what makes it testable and makes the VCV
  port deterministic under faster-than-realtime rendering.
- New mutable file-scope globals must be registered in the VCV port's
  `engine_state.def`, or Rack instances will share them.
- Changing `LoadSaveParams` invalidates saved slots — bump `VALID_MAGIC`.
