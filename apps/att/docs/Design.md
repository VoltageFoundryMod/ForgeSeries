# ChaosForge — design notes

Why the module is built the way it is. The user-facing guide is
[../Manual.md](../Manual.md); the coding rules are [../AGENTS.md](../AGENTS.md).

---

## 1. The idea

A modulation source has two usual shapes. An LFO repeats exactly, which is
predictable and eventually boring. A random source never repeats, which is
unpredictable and has no shape at all — sample-and-hold is the same texture
forever, whatever you do to it.

A strange attractor is the third thing. It is completely deterministic, its
trajectory is smooth and it has an unmistakable shape you can learn to hear, and
yet it never repeats and two copies started a thousandth apart end up somewhere
entirely different within seconds. That is the whole proposition: modulation that
behaves like a *system* rather than like a waveform or a die.

The module runs two of them at once, and takes two of each system's three state
variables to a jack. That pairing matters more than the count:

- **two jacks from one orbit are related but never equal.** They are two views of
  one moving object. Patch them to filter cutoff and resonance and the two move
  together in a way no pair of free-running LFOs ever does, but they never line
  up into an obvious ratio the way two synced LFOs do.
- **two jacks from different orbits are unrelated** — until COUPLE says
  otherwise.

Prior art: Hemisphere Suite's Low-rents applet (Lorenz + Rössler, two generators,
a vectorscope screensaver), and Mutable Instruments' Streams, whose Lorenz code
Low-rents ported. This module keeps the pairing and the vectorscope and adds
twelve systems, editable constants, coupling between the generators and output
scaling that adapts to what the constants were changed to.

---

## 2. Integration: RK4 on a fixed 1 ms module step

**Why RK4 rather than Euler.** On a stiff system like Lorenz, Euler does not
merely lose accuracy — it changes the attractor. The orbit spirals outward and
eventually diverges, which on a jack is a CV that slams to a rail and stays
there. RK4 costs four derivative evaluations per step and buys an order of
magnitude in stable step size, so for a *bounded* orbit it is also the cheaper
option.

**Two clocks, deliberately.**

| | unit | changes with |
| --- | --- | --- |
| module step | 1 ms of real time | never |
| integrator step `h` | the attractor's own time units | SPEED |

SPEED decides how much attractor time each 1 ms slice covers; the slice itself is
fixed. Keeping them separate is what makes SPEED a smooth control rather than a
change of simulation grid, and the fixed slice is what makes the simulation
deterministic and testable.

Each system carries two constants for this:

- `hMax` — the largest RK4 step it stays accurate at, taken from the reference
  simulators these systems were tuned in.
- `rate` — attractor time units per real second at SPEED 1.00. This is what makes
  SPEED mean the same thing on every system: Thomas advances 24 units a second
  and Chua 0.96, yet both trace their figure at a comparable, musical rate.
  Without it, changing system would be a wild jump in output rate.

**The substep cap.** A 1 ms slice is split into `ceil(rate x speed x 1 ms / hMax)`
substeps, capped at `ATT_MAX_SUBSTEPS` (8). When the cap binds, the step grows
past `hMax` instead of time slowing down. That is the right way round: an honest
clock with degraded accuracy is a worse-sounding orbit, while a SPEED control that
silently stops speeding up is a broken control. At `ATT_SPEED_MAX` (16x) the worst
overshoot across the twelve systems is 1.2x `hMax`, which all of them survive;
anything past that is caught by `AttDiverged()` and re-seeded.

**Budget.** At SPEED 1 each generator runs ~1000 RK4 steps/s; a step is roughly
70 soft-float operations, so two generators cost a few percent of one M0+ core.
At the top of the SPEED range with both generators maxed it is ~8000 steps/s
each — the worst case the cap exists to bound.

**Divergence is caught, not clamped.** The parameter ranges are wide enough to
include settings where a system has no bounded attractor at all, and a divergent
orbit reaches 1e30 in a handful of steps. `AttDiverged()` tests `-1e5 < s < 1e5`
as a positive range check, which catches NaN in the same expression (every
comparison against NaN is false), and the generator re-seeds. A re-seed is heard
as the pattern restarting; a clamp would be heard as a dead module.

---

## 3. Getting an orbit into 0–5 V

The jack is 0–5 V. A Lorenz z lives in 3..47, a Chua y in ±0.4. Something has to
map one onto the other, and what it does when the constants are moved is the
whole question.

**FIXED — the published window.** `centre[]`/`halfSpan[]` per axis in
`ATTRACTORS[]`, measured rather than guessed: each system was run for ~700
simulated seconds at its published parameters and these are the **0.2/99.8
percentiles** of the resulting orbit.

Percentiles rather than true extremes, because a chaotic orbit's excursions are
rare and enormous. Sizing the jack to Lorenz's rarest z spike would spend most of
the range on a voltage you hear once a minute; the occasional clamped excursion is
a hundred times less audible than permanently halving the swing.
`PublishedWindowFillsMostOfTheJack` asserts every axis of every system still uses
more than 55 % of the range, which is what catches a mistyped constant.

**AUTO — the tracked window.** Those constants are only exact at the published
parameters. Move Rössler's C from 5.7 to 2.5 and the orbit is less than half the
size it was measured at, so the jack quietly loses more than half its swing. AUTO
tracks the orbit's own min/max instead:

- **instant on the way out**, because a clipped peak is the thing this exists to
  prevent;
- **slow on the way in** (5 % of the window per second, per side), because a
  chaotic orbit's excursions can be minutes apart and a fast relax would re-gain
  the CV between them;
- **floored**, because a system parked near a fixed point has a near-zero span and
  dividing by it turns numerical dust into full-scale CV.

FIXED is the default: it is predictable and identical from one boot to the next,
which is what you want when a patch depends on the voltage. AUTO is one click away
when it stops being.

**LEVEL and OFFSET are applied after normalisation, never inside the
simulation.** What the orbit does must not depend on how it is being listened to.
LEVEL scales around the centre of the range, so LEVEL 0 parks the jack at 2.5 V
rather than at a rail — a modulation depth control that jumped to 0 V when turned
down would be unusable as a VCA target.

---

## 4. Which twelve systems, and why those defaults

The set is drawn from the two reference simulators this module started as:
Lorenz, Rössler, Thomas, Chua, Halvorsen, Chen, Burke-Shaw, Aizawa, Dadras,
Sprott B, Sprott C and the Ma–Chen finance system.

**Four parameters, never more.** A menu page is six rows, and the SYSTEM page
carries TYPE and SPEED as well. Two systems naturally have more — Aizawa has six,
Dadras five — and rather than drop them, the constants that only reshape the
figure (Aizawa's e and f, Dadras's e) are fixed at their published values and the
four musical ones are exposed.

**Every shipped system must be genuinely chaotic at its defaults.** Two were not,
at the parameters they are usually published with. Measuring the largest Lyapunov
exponent by the two-orbit renormalisation method (two copies separated by 1e-9,
re-normalised every step, the mean log growth rate) gives:

| system | λ | e-folds/sec at SPEED 1 |
| --- | --- | --- |
| Lorenz | +0.91 | 2.2 |
| Rössler | +0.07 | 0.7 |
| Thomas (b = 0.19) | **+0.0001** | **0.0** |
| Chua | +0.41 | 0.4 |
| Halvorsen | +0.67 | 2.0 |
| Chen | +2.05 | 2.5 |
| Burke-Shaw | +0.70 | 1.7 |
| Aizawa | +0.10 | 0.6 |
| Dadras | +0.39 | 0.9 |
| Sprott B | +0.21 | 1.5 |
| Sprott C | +0.16 | 1.2 |
| Finance (a = 0.001) | **−0.0000** | **0.0** |

Thomas at b = 0.19 and Finance at a = 0.001 are **limit cycles**, not attractors.
On a limit cycle two nearby orbits converge and stay in phase, so the two
generators tracked each other forever and the module had two outputs rather than
four. The fix was to move both defaults into a genuinely chaotic band —
Thomas b = 0.05 (λ = +0.12) and Finance a = 0.95 (λ = +0.10) — and to draw their
parameter ranges around those bands rather than around the equations'
mathematically legal values.

This was found by `TheTwoGeneratorsDivergeFromEachOther`, not by ear, which is
why that test exists and why adding a system means measuring it.

**The periodic windows are left reachable on purpose.** Thomas's B in particular
is mostly *not* chaotic across its range, and a limit cycle there is a pair of
smooth, perfectly repeating LFOs with an unusual shape — a useful thing for the
module to be able to do, and one it can only do because the ranges were not
trimmed to the chaotic bands alone.

---

## 5. COUPLE

The signature control, and the reason the two generators are in one module rather
than being two copies of a simpler one.

It is **diffusive coupling**: each orbit is pulled toward the other by a fraction
of their difference, every module step, on all three axes. What comes out of it is
the real phenomenon of chaotic synchronisation — turned up far enough, two copies
of one system converge onto a single orbit and stay there, and two *different*
systems are entrained: they start sharing their timing while keeping their own
shapes.

Three decisions inside it:

- **In normalised units.** The systems' natural sizes differ by two orders of
  magnitude. Exchanging raw state would mean Chen simply overwriting Chua.
- **Symmetric.** Each orbit is pulled the same distance toward the other, so
  neither is master. Two copies of one system converging to a single orbit — four
  jacks becoming two — is the intended extreme and the clearest demonstration of
  what the control does.
- **Squared.** The interesting region, entrainment without lock, is narrow and
  sits near the bottom; a linear taper puts all of it in the first few percent.

At full coupling two *different* systems are dragged off their own attractors,
which is a legitimate place to be and is why
`CouplingLeavesDifferentSystemsBoundedAndAlive` exists: it must not flatten either
one into a stuck voltage.

---

## 6. The screen

Two Lissajous plots, one per generator: each generator's two output values
plotted against each other. It is the only view that shows what the module is
doing — one trace against time shows a wobble, the pair shows the attractor.

The trail is sampled **on the orbit's own clock**, not the frame clock: a point
every 1/40th of the attractor time the system covers in a second at SPEED 1. So
the drawn arc covers the same amount of *trajectory* at every SPEED, and the
figure looks like itself whether it is being traced in a second or in a minute. A
wall-time floor sits under that, or SPEED 0.01 would leave the screen unchanged
for seconds at a time and read as a hung module.

What is plotted is the value **before** LEVEL and OFFSET, so the figure keeps
filling the frame however the jacks are scaled. The screen is for the shape; the
jack is for the voltage. Values outside the window are pinned to the frame rather
than dropped, so a clipping output is visible as the trace flattening against the
edge — which is exactly what the jack is doing at that moment.

---

## 7. Deliberately left out

**Gate or trigger outputs from threshold crossings.** Low-rents can XOR and sum
its generators into gate-ish outputs, and a threshold crossing on a Lorenz wing is
a decent event source. It was left out because all four jacks are spoken for by
the pairing above, and a jack that is sometimes a CV and sometimes a gate is a
worse version of both. A comparator downstream does this better and costs the
patch one module.

**Clock sync.** A chaotic system has no period to sync to, so anything honest here
would be a rate control locked to tempo — which is what SPEED already is, minus
the tempo. If it lands later it belongs on the CV page as a target, not as a
transport.

**Systems with more than four exposed parameters.** See §4: the page is six rows.

**Randomising RANGE, the IN 1 role, the CV matrix or the view.** Those are patch
wiring and preferences. Rerolling them turns "give me a new shape" into "break my
patch", which is the same line VCV Rack draws when it leaves ports alone on
randomize.
