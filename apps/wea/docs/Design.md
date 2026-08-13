# WeaveForge — design notes

A dual shift-register sequencer for the Forge Series hardware.

Two 16-bit shift registers clock in parallel, each one a Turing Machine in the
Tom Whitwell sense: a ring of bits that either loops, drifts, or dissolves into
noise depending on one probability control. The novel part is **WEAVE** — a
single control that decides how much of each register's bit stream comes from
the _other_ one. At zero they are two independent sequencers; at full they chain
tail-to-head into a single ring as long as the two of them together.

That is the same proposition as GravityForge's PROXIMITY and ChaosForge's
COUPLE, but here the coupling is not a metaphor — it is the actual wiring of the
registers, and the screen draws it.

Inspired by Tom Whitwell's [Turing Machine], Hemisphere Suite's ShiftReg, and
the output-assignment idea from Phazerville's [Enigma] (see §9 for what was
deliberately not taken from it).

[Turing Machine]: https://www.musicthing.co.uk/Turing-Machine/
[Enigma]: https://firmware.phazerville.com/Enigma

---

## 1. Hardware contract

Shared Forge Series platform (XIAO RP2040, SSD1306 128×64, MCP4728 quad DAC,
rotary encoder + switch). See [ForgeSeries-Hardware].

[ForgeSeries-Hardware]: https://github.com/VoltageFoundryMod/ForgeSeries-Hardware

### Inputs

| Jack | Range        | Role                          |
| ---- | ------------ | ----------------------------- |
| IN 1 | gate/trigger | **CLOCK** — always, see below |
| IN 2 | **0–5 V**    | Assignable modulation target  |
| IN 3 | **0–5 V**    | Assignable modulation target  |

**IN 1 has no role menu, and that is a deliberate departure** from GravityForge
and ChaosForge. Those modules run a simulation that has its own life, so IN 1 is
free to mean RESET or KICK. A shift register has no state evolution between
clocks — unclocked, this module is a static voltage. Spending the only
interrupt-capable jack on anything but the clock would make the module
inoperable in its default patch, so the role list is one entry long and is not
shown.

An internal clock covers the unpatched case (see §7); RESET and the ShiftReg
"unlock" gate are reachable as CV targets on IN 2/IN 3.

> **CV range.** Current hardware is 0–5 V; a later revision moves to ±5 V. Every
> modulation read goes through `CvNorm()` / `CvBipolar()` in `lib/cvInputs.hpp`,
> which defer to `core/cvInput.hpp`, so the swap stays the `-DFORGE_CV_BIPOLAR`
> build flag and nothing else.

### Outputs

All four go through the MCP4728. Unlike every other module in the series, **the
jack map is not fixed** — it is the output matrix of §5. What is fixed is the
_default_ map, which mirrors NoteForge and GravityForge exactly so the muscle
memory survives:

| DAC index | Position     | Panel | Default (ROUTING ▸ DUO) |
| --------- | ------------ | ----- | ----------------------- |
| 0         | top-left     | A1    | NOTE, register A        |
| 1         | top-right    | B1    | NOTE, register B        |
| 2         | bottom-left  | A2    | GATE, register A        |
| 3         | bottom-right | B2    | GATE, register B        |

**Each register owns a COLUMN** in the default routing — A down the left, B down
the right — and the panel is labelled to match, exactly as ChaosForge's
generators are. Which half of the module a cable belongs to is then readable from
across a room, where two identical-looking rows are not.

The naming deliberately collides with the registers, and the collision is the
point: in DUO, jack `A2` really is register A. It is free to stop being true —
a jack named `A2` may perfectly well read register B in any other routing. The
jack name says where the cable goes; SOURCE (§5) says what comes out of it.

---

## 2. The register

Each channel holds a `uint16_t`. On a clock edge, with length `N` (2–16) and
chance `P` (0–100 %):

```cpp
bool tail = (reg >> (N - 1)) & 1;   // the bit about to wrap
if (RandomPercent(P)) tail = !tail; // ...possibly flipped
reg = (uint16_t)((reg << 1) | tail);
```

The register is always shifted as a full 16 bits; `N` only chooses which bit is
fed back, not how much of the register moves.

**What that does to the bits above `N` is worth understanding, because it looks
like a bug and is not.** They are not preserved. They keep marching up and fall
off the top at position 15, while position `N-1` copies whatever passes through
it into the region above. So the upper region is a running copy of the loop: at
CHANCE 0 it is the same loop at a different phase, and with CHANCE up it is the
loop's recent past. It is a delay line, not an attic.

Two consequences, both load-bearing:

- **ROTATE beyond LENGTH is musically meaningful**, not a jack reading dead
  bits. On a locked pattern it is another phase of the same loop — which is what
  makes §5's canon trick work at any length, not only at 16.
- **Shortening LENGTH and lengthening it again does not bring the long pattern
  back.** It is gone within `16 − N` clocks. That is the faithful Whitwell /
  ShiftReg behaviour and it is kept, but it is also the reason the register
  contents are part of a preset (§8): saving the slot is how you keep a pattern,
  not turning LENGTH back up.

### CHANCE is the original module's knob, unrolled

The Turing Machine's front-panel geometry is not three features, it is one
probability read at different points, and a plain 0–100 % control reproduces all
of it:

| CHANCE  | Original knob | Result                                            |
| ------- | ------------- | ------------------------------------------------- |
| 0 %     | 5 o'clock     | Locked. Repeats every `N` steps, forever.         |
| ~50 %   | noon          | Maximum drift — a new sequence every pass.        |
| 100 %   | 7 o'clock     | Locked, inverting: period **2N**, not N.          |
| 10–30 % | 3 / 9 o'clock | "Slip" — a loop that changes a note now and then. |

The 100 % case is worth calling out in the manual: always-flip is just as
deterministic as never-flip, and it buys a pattern twice as long as the LENGTH
setting says. Users of the original know this trick; users who don't will find
it by turning the control to the end.

### Locking and editing

CHANCE = 0 is the lock, so there is no separate freeze control. What the module
adds instead is a **bit editor** on the home screen: the encoder walks a cursor
along the register's row and flips the bit under it.

The gesture has one platform constraint that decides its shape. The shell
reserves a **2 s hold** of the encoder for returning to the module selector
(`APP_SWITCH_HOLD_MS` in [src/main.cpp](../../../src/main.cpp)), and it forwards
the raw press/release to the app on every poll — so an app can time a press
itself, but anything it defines has to finish well inside two seconds. That
leaves:

| Gesture | Does |
| ------- | ---- |
| click on HOME | enter cursor mode |
| turn | move the cursor (register A's row, then B's) |
| hold ~500 ms | flip the bit under the cursor |
| click | leave cursor mode |

Click cannot be both "flip" and "leave", which is what forces flip onto the
short hold rather than the more obvious click. **Not yet implemented** — the
home screen is currently read-only and the encoder navigates the menu as on
every other module.

This is the feature that turns a Turing Machine into something you can call a
sequencer. The workflow it is designed for is: let it drift until something good
goes past, pull CHANCE to 0 to keep it, then fix the two notes that bother you.
One encoder is a genuinely good fit for this — the row is the map, it runs in
the direction the encoder turns, and there is only ever one cursor.

`CLEAR` / `FILL` / `INVERT` / `RANDOMIZE` actions live on the register page for
the cases where editing bit by bit is the wrong tool.

---

## 3. WEAVE

The coupling. Per clock, each register's incoming bit is drawn either from its
own tail or from the other register's tail:

```
        ┌──────────── register A (16 bits) ────────────┐
   ┌───►│ bit0  ●  ○  ●  ●  ○  ○  ●  ○ ... bit N-1 ────┼──┐
   │    └──────────────────────────────────────────────┘  │
   │                          WEAVE  0..100 %             │
   │    ┌──────────── register B (16 bits) ────────────┐  │
   └────┼──── bit N-1 ... ○  ●  ○  ●  ●  ○  ○  ●  bit0 │◄─┘
        └──────────────────────────────────────────────┘
```

WEAVE is the probability, evaluated independently per register per clock, that
the incoming bit is taken from the other register instead of its own. CHANCE
still applies afterwards, to whichever bit was chosen.

| WEAVE | Behaviour                                                     |
| ----- | ------------------------------------------------------------- |
| 0 %   | Two unrelated Turing Machines sharing a clock.                |
| ~50 % | Motifs leak across and drift back — related, never identical. |
| 100 % | Tails swap every clock: A and B are **one ring of NA + NB**.  |

The endpoint is the point. At WEAVE 100 % a bit entering A at position 0 walks
to `NA-1` over `NA` clocks, crosses into B, walks `NB` more, and returns — a
single circulating ring of period **NA + NB**. Combined with CHANCE = 0 that is
a locked 32-step phrase spread across two outputs, which no amount of turning a
single Turing Machine's knob will give you.

Nothing requires the two lengths to match, and the general case is the useful
one: 5 and 3 chain into a ring of 8, 7 and 2 into 9. "Twice the length" is this
with `NA == NB`, which is only the default because both registers start at 16.

### Direction

One field qualifies the coupling. **DIR** — who feeds whom: `BOTH` · `A▸B` ·
`B▸A`.

One-way is the most useful of the three in practice and deserves its own entry
rather than being buried: with `A▸B`, register B becomes a variation on A that
cannot contaminate it. A stays the theme; B is the answer.

There is no coupling _mode_. An XOR variant (new bit = own tail ⊕ other tail)
was considered and dropped: it produces long deterministic non-repeating
structure, which is genuinely different from SWAP's endpoint, but it is a second
mechanism to explain in the manual and — worse — it is the one thing on this
module you cannot see happening on the screen. Bit exchange draws as strands
crossing between two rows (§6); a bitwise XOR draws as nothing at all. A control
whose only feedback is "the pattern sounds different now" is exactly the kind of
control this series avoids.

---

## 4. Why coupling belongs on this module at all

A fair objection: two Turing Machines and a mixer is not a new module. The test
this design holds itself to is that **WEAVE must be musically meaningful in
every routing mode of §5**, not just the two-voice one. It is:

| Routing | What WEAVE does there                                                        |
| ------- | ---------------------------------------------------------------------------- |
| DUO     | Two basslines → two voices playing one long shared phrase.                   |
| MONO    | The second modulation slides from unrelated to locked with the melody.       |
| PULSE   | Two independent 2-track drum patterns → one 32-step pattern across 4 tracks. |

If a future routing mode cannot answer that question, it does not belong in the
list.

---

## 5. The output matrix

Enigma's genuinely good idea is that registers and outputs are decoupled: an
output picks a source and a _type_, rather than the module hard-wiring which
jack is pitch. That is cheap to implement and it is what lets a 4-jack module be
several different instruments.

Each of the four jacks owns a slot:

| Field          | Values                                                   |
| -------------- | -------------------------------------------------------- |
| SOURCE         | `A` · `B` · `AB` (the woven 32-bit ring — see §3)        |
| TYPE           | `NOTE` · `MOD` · `GATE` · `TRIG`                         |
| DEPTH          | 1–8 bits — the window width read out of the ring         |
| ROTATE         | 0–15 (0–31 for `AB`) — where in the ring the window sits |
| _(contextual)_ | NOTE ▸ RANGE · MOD ▸ LEVEL · GATE/TRIG ▸ THRESH          |
| _(contextual)_ | NOTE/MOD ▸ SLEW · TRIG ▸ WIDTH · GATE ▸ —                |

Six fields, which is exactly the six rows a menu page holds — so each jack is
one page, four pages, no scrolling and no seventh row silently clipped off the
bottom at y=57.

**Those four pages are walked in panel order — A1, A2, B1, B2 — not in DAC
order.** The DAC order is the rows (top-left, top-right, bottom-left,
bottom-right) and it is fixed by the hardware, but the panel is labelled by
column and each register owns one in the default routing (§1), so a list in DAC
order puts B1 between A1 and A2 and interleaves the two halves of the module on
the one screen where you are setting up a jack you are looking at.
`WEA_JACK_COLUMN_ORDER` in `lib/outputs.hpp` is the single source of that order:
the OUT pages, the ROUTING page summary and the Rack context menu all walk it,
the last via `jackAt()` on the engine bridge so the plugin cannot drift. The
menu group IDs still map straight to jack indices — it is the order the blocks
sit in `MENU_ITEMS[]` that the encoder follows.

### ROTATE earns its place

Enigma offers rotation only on favourited registers. Here every jack has it, and
it costs one modulo add. Four jacks tapping the _same_ register at different
offsets are four phase-shifted copies of one pattern — canons, rounds, a bass
line and its own echo four steps late. It is the highest musical return per line
of code in the module.

### The types

**NOTE** — the DEPTH-bit window becomes a scale degree:

```
index  = (window * DegreesPerOctave() * RANGE) >> DEPTH
semitone = quantizer.SemitoneAt(index) + rootOffset
```

DEPTH sets _resolution_ and RANGE (1–5 octaves) sets _span_, which keeps the two
independent — 3 bits over 2 octaves is a sparse, singable line; 7 bits over the
same 2 octaves is a fine-grained wander through it. Uses
[core/quantizer.hpp](../../../core/quantizer.hpp) and
[core/scales.hpp](../../../core/scales.hpp) unchanged; the scale mask is
module-wide (one root, one mask, §7), not per jack.

**MOD** — the window scaled to 0–LEVEL % of 5 V. This is the original module's
main CV output, and at DEPTH 8 it is the same signal.

**GATE / TRIG** — see below. GATE holds high while the condition is true and
falls at the first clock where it is not; TRIG emits a fixed-width pulse.

### THRESH, and why GATE is not just bit 0

The obvious implementation — "output high when bit 0 is 1" — gives every gate
output a fixed ~50 % density that nothing can change. Four such outputs is not a
drum machine, it is four coins being flipped.

Instead the DEPTH-bit window is compared against a threshold:

```cpp
const int span  = 1 << DEPTH;
const int limit = (threshold * span) / 100;
bool fire = window >= span - limit;   // the TOP `limit` values of the range
```

THRESH 12 % is a sparse kick, 88 % is a busy hat, and the whole thing is still
driven by — and locked to — the same register as the melody. At DEPTH = 1 and
THRESH = 50 % it collapses exactly to the classic bit-0 behaviour, so the
original is a point in this space rather than a special case.

**Both halves of that comparison are load-bearing, and the first version got the
first half wrong** — it fired on `window < limit`, i.e. on the LOW values. A set
bit has to be the one that plays: the screen draws bit 1 as a filled cell and bit
0 as a hollow one, and every shift-register sequencer since the Turing Machine
fires on the 1, so firing on the low values meant a row of empty cells played
while a row of full ones sat silent. The panel, the screen and the manual all
described the opposite of what the jack did.

Counting the qualifying values down from the TOP of the range rather than up from
the bottom is what keeps THRESH meaning what it says. `limit` values out of `span`
qualify either way, so the density is `threshold` % whichever end they are taken
from — the naive repair, `window >= limit`, fixes the polarity and silently
inverts the control, turning a sparse kick at 12 % into a busy one.

### ROUTING is a macro, not a mode

The three configurations the matrix is _for_ are one menu item at the top of the
outputs page:

| ROUTING | A1     | B1             | A2            | B2             | The module is...                       |
| ------- | ------ | -------------- | ------------- | -------------- | -------------------------------------- |
| `DUO`   | NOTE A | NOTE B         | GATE A        | GATE B         | two voices                             |
| `MONO`  | NOTE A | GATE A         | MOD A (rot 8) | MOD B          | one voice + two correlated modulations |
| `PULSE` | TRIG A | TRIG A (rot 8) | TRIG B        | TRIG B (rot 8) | the Pulses expander, in software       |

**Selecting one stamps the four slots and nothing more.** The slots remain the
source of truth and stay individually editable; when they no longer match any
template the item reads `CUSTOM`.

This is the pattern [core/scales.hpp](../../../core/scales.hpp) already
establishes for note masks — "a scale here is only a helper for _populating_ the
mask; the mask is the source of truth, and the user is free to edit it
afterwards, which leaves it no longer matching any named scale." Following it
here means no mode/expert split, no settings that exist in one mode and vanish
in another, and no second code path.

`CUSTOM` is **recomputed by comparing the slots against the three templates**,
not stored. A stored mode index goes stale the moment a slot is edited from CV
or a preset load, and a preset that happens to match DUO should say DUO.

---

## 6. The screen

Home is **the loom**: two rectangular registers facing each other, running in
opposite directions, with the weave itself drawn in the channel between them.

```
 ▶ 124  ÷2                        WEAVE 65%
A ■□■■□□■□■■□■ · · · ·                    ▸
  └──────── len 12 ────────┘ ▲
     ╲   ╱ ╲       ╱ ╲   ╱
      ╳ ╱   ╲     ╳   ╲ ╳
     ╱ ╲     ╲   ╱ ╲   ╱ ╲
  ◂        └──── len 8 ────┘
B □■■□■□□■ · · · · · · · ·
   1▲      3▲        2▲   4▲
  CHANCE A                        24%
```

**The two rows flow in opposite directions** — A left to right, B right to left
— and that is the whole reason there are two rows rather than one. At WEAVE
100 % the chain becomes a visible racetrack: out of A's right end, down
through the channel, back along B, up into A's left end. A bit can be followed
around it with a finger. Two rows flowing the same way would draw that circuit
as a crossing tangle, and one long row would not draw it at all.

**The channel between them is the weave**, and it is the only animated element:
at WEAVE 0 the two rails run flat and parallel with nothing between them; strand
crossings appear as the control comes up, their count proportional to it; at
100 % it is a full braid. DIR shows as which way the arrowheads on the strands
point. The module is named for this and the screen draws it literally — the
picture of woven cloth **is** the parameter.

### What a cell says

Four states, all legible on a 1-bit panel at arm's length:

| Drawn         | Means                                        |
| ------------- | -------------------------------------------- |
| ■ 6×6 filled  | bit = 1, inside the active length            |
| □ 6×6 hollow  | bit = 0, inside the active length            |
| ▨ 6×6 shaded  | bit = 1, past the feedback point             |
| · 2×2 dot     | bit = 0, past the feedback point             |

LENGTH is therefore a _visible boundary_, not a number to read: the point where
the row goes dim. The delay line of §2 is drawn dimmer rather than omitted
because an output ROTATEd out there is reading it — and it shows its BIT VALUE
for the same reason. A single flat marker for the whole region, which is what
the first version drew, says "something is here" about cells a jack is actively
reading and refuses to say what; it is drawn dim, not small, because it is not
part of the loop and shortening LENGTH is about to overwrite it.

The third tone is a **checkerboard**. There is no grey on a 1-bit panel, but the
SSD1306 is a true pixel grid and the Rack port's OLED widget area-integrates, so
both hosts resolve one to a tone rather than to a visible pattern.

### What moves

The screen has to distinguish a running module from a stopped one, and at
CHANCE 0 — the setting the module is _sold_ on — every frame is otherwise
byte-identical. Three things animate, all driven by `StepClock::StepPhase()` and
`StepCount()`, both display-only: nothing that produces a voltage may read them,
or the outputs would depend on the frame rate.

| Element | Says |
| ------- | ---- |
| the fresh-bit hole, at each row's entering end | which bit arrived on this clock |
| the travelling braid | the cloth is being drawn through the loom |
| the crossing courier | a bit **actually** jumped on this step |

A caret above each row marks the **feedback tap**, bit `LENGTH-1`. It is the most
important cell in a Turing machine — the bit about to wrap, the one CHANCE flips
on the way round, and the one a courier departs from — and nothing pointed at it,
so the only way to find the tail was to wait for a crossing, and at WEAVE 0 there
was no way at all. It also gives LENGTH a second reading: the shading boundary
says where the loop ends, the caret says which cell closes it.

**A strand is exactly 45°** — one pixel across per pixel down — so it spans as
many columns as the channel has rows. That is forced, not chosen. The first
version spanned a fixed 8 columns over however many rows the channel happened to
have, stepping `x0 + (i * 8) / h`; at `h = 7` that yields offsets 0,1,2,3,4,5,6,8,
so offset 7 never appears and every strand ran straight for seven rows and then
jumped two pixels sideways. Nine columns over eight rows is not a line. It read
as a bent strand with a detached pixel at the end, worst on the rightmost one
where nothing follows it to make the pattern legible — and the fixed width also
made the marks along the rails alternate 8 px / 6 px apart at WEAVE 100 instead
of falling evenly. Any change to the channel's height has to keep the strand
width tied to it (`LOOM_STRAND_W`), or the slope stops being 45° and the kink
comes back.

**Every animation moves at the rate the bits do**, and that constraint decides
each one's speed rather than taste. The braid travels one **cell pitch per
clock** — the same distance the registers shift — which is why it needs
`StepCount()` and not the phase alone: the first version scrolled a whole strand
span per clock, and the span is `112/strands`, so the cloth flew past at a low
WEAVE and crawled at a high one. Fastest exactly where least was crossing.
Speed must not depend on the control the picture is describing.

The courier earns its place twice over. WEAVE is a probability, so the braid
draws the _setting_ and is the same picture on the step where a bit crossed and
the step where none did; the courier draws the _event_. It runs between the two
cells the transfer is really between — out of the sender's tail, the cell LENGTH
selects, and into the receiver's bit 0, where the next frame draws it as the
fresh bit — so the animation joins up with the register rows rather than floating
between them. `WeavePair::Crossed()` is what it reads, and that flag is
observation only: nothing in `shiftreg.hpp` reads it back, so the file stays a
pure function of its inputs (§10).

Below about two frames per step the phase is a lie — successive redraws sample
unrelated points in the step — so the marks that depend on landing inside a
fraction of it drop out. The registers are a blur at those rates anyway.

**A full-width sweep line under the header was tried and removed.** It was the
largest moving thing on the screen and the least informative one; it read as a
progress bar for something that is not loading. The three animations above
already say the module is running _while also_ saying something about the
pattern, which is the bar an element on a 64-row screen has to clear.

### Where an output reads

Each jack's window — the DEPTH cells it is reading, at its ROTATE offset — is
barred underneath its row, between the cells and that jack's label. Without it
the label says only where a jack _starts_ reading, and a NOTE at DEPTH 5 is built
from five cells; the window exists nowhere on screen and the pitch you are
hearing has no visible source. Barred rather than boxed, because the cell outline
already means "bit = 0" and a second rectangle would read as a bit value.

Several jacks routinely read the same cells — the DUO default puts a NOTE and its
GATE both on register A at ROTATE 0 — so the bars merge. The jack being edited is
redrawn at cell _pitch_ rather than cell _width_, closing the 1 px gaps into one
solid bar, which picks it out of the pile without needing a row the screen does
not have.

### The leaders, and why labels cannot simply sit on their taps

Jacks sharing a tap have their labels packed left to right from it, because two
labels drawn at the same x print on top of each other. That packing means only
the FIRST label sits over its own cell: in the DUO default `A2` lands two cells
to the right of the one it taps, and the screen then states something false about
where a jack reads. This was live until the screenshot tool (`make screen-wea`)
made it visible — a photo of the panel cannot resolve it.

So each label gets a **leader** on a row of its own between the window bars and
the labels, running from the cell to the label that names it. The chain reads
cell ▸ window bar ▸ leader ▸ label, each touching the next.

Two details are load-bearing. The leader is **dotted**: drawn solid it stacked
under the window bar into a two-row slab and read as a fatter bar, which is the
one thing the row exists not to be. And the dots are on an **absolute parity**
rather than one relative to each leader's own start, so leaders that merge —
which they should, when jacks share a tap — stay dotted instead of filling each
other in.

The row was paid for by the labels, not the loom: a tap label is the classic
font's 8-row cell, but the glyphs here are capitals and digits, all of which
leave the last row empty for descenders. The lit block behind a speaking jack is
therefore 7 rows. It may not be widened back — row 29, under register A's
labels, is the weave channel's top rail.

The bracket under each row marks the active span and carries two more things:
the **bit-editor cursor** (`▲`), and the **output taps** — each jack's digit sits
at its ROTATE column under its source register. Seeing `1▲` and `3▲` four cells
apart is the only thing that makes ROTATE stop being an abstract number. Jacks
sourced from `AB` address the combined 32-cell ring and their digit lands on
whichever row that offset falls in, which is also how you learn what `AB` means
without reading the manual.

### Pixel budget

128×64 exactly, no scrolling, at 16 cells on a 7 px pitch (6 px cell + 1 px gap
= 112 px, centred with 8 px margins):

| Rows  | Content                                            |
| ----- | -------------------------------------------------- |
| 0–11  | header — tempo in helvB12, rate and WEAVE % small  |
| 12–13 | A's feedback-tap caret                             |
| 14–19 | register A, flowing ▸                              |
| 20    | A's output windows                                 |
| 21    | A's tap leaders                                    |
| 22–28 | A's output taps (length bracket, cursor: unbuilt)  |
| 29–38 | the weave channel — braid and couriers             |
| 39–40 | B's feedback-tap caret                             |
| 41–46 | register B, flowing ◂                              |
| 47    | B's output windows                                 |
| 48    | B's tap leaders                                    |
| 49–55 | B's output taps (length bracket, cursor: unbuilt)  |
| 56–63 | status line — the two lengths, the two chances     |
| 54–63 | the live strip, over the status line while editing |

**The budget is exactly full, and the header is what fills it.** A helvB12 digit
is 12 rows, so the tempo cost four rows off the loom — two from the weave channel
(12 → 10) and one from each row of tap labels (9 → 8, which is the classic font's
cell exactly, so a lit label's block now has no padding row above it). There is
nowhere for a fifth row to come from. Anything else wanting large type has to
take it from something on this list, and the channel has no more to give: the
strands are diagonals, and below about 8 rows they stop reading as crossings.

The font is `core/fonts/helvB12.h`, shared with ClockForge and ScopeForge — it
used to be three byte-identical copies, one per app plus the Rack shim's. It is
included from inside the app's namespace like every other header here, which is
what keeps the unified firmware and the consolidated plugin from seeing several
definitions of the same glyph tables.

### Why not rings

Circles were the first sketch and are the wrong answer here. They are
GravityForge's face, so the series would have two modules that look alike from
across a room; the counter-flow reading that makes the chain obvious costs a
crossing tangle once both rings are round; a 32-position combined ring is
cramped at this diameter; and rectangular cells put the bit editor's cursor on a
straight line the encoder walks in one direction, which is what one encoder is
good at.

The row-of-LEDs every other Turing Machine uses is not this either — that is one
row, unlabelled, with no length boundary, no taps and no weave.

---

## 7. Clock, scale and CV

**Clock.** IN 1 with an internal fallback so the module does something on a
bench with nothing patched.

The shared part is promoted to **`core/clockSource.hpp`**, lifted out of
[apps/gen/lib/clock.hpp](../../gen/lib/clock.hpp) and trimmed to what a module
needs to know what time it is:

| Promoted to `core/` | Stays in GravityForge's `lib/clock.hpp` |
| ------------------- | --------------------------------------- |
| BPM range, the external-PPQN enum and its tables | beats-per-revolution rotation mapping |
| external edge → interval → BPM, with outlier rejection | the `QuantizeDiv` hit grid |
| internal/external source selection and fallback | `NoteSpace` thinning |
| time always **passed in**, never read from a wall clock | `ConsumeBoundary()` and the LOOP phrase machinery |

The passed-in-time rule is the load-bearing one and the reason this promotion is
safe: it is what makes the clock testable on the host and deterministic in the
Rack port, and it is already how GravityForge works.

**GravityForge is migrated by composition, not rewrite** — its `Clock` keeps its
name and its API and gains a `ClockSource` member. A shipping module under test
should not be restructured to serve an unwritten one, and its native tests in
[apps/gen/test/](../../gen/test/) are the guard that the lift changed nothing.

### ClockForge is not a third consumer, and should not be made one

Worth settling now, because it looks like it ought to be. ClockForge's time base
is [apps/clk/lib/clockEngine.hpp](../../clk/lib/clockEngine.hpp): a 960-PPQN
**hardware-timer ISR** with file-scope `BPM` and `tickCounter` globals, tap
tempo, and the whole module driven from the interrupt. GravityForge's is a
passed-in-time value object with no timer and no globals. These are not two
copies of one thing — they are opposite architectures that happen to share the
word "clock", and folding one into the other means rewriting ClockForge's core
so that a new module can save eighty lines.

This is the same call [README.md](../../../README.md) already records for the
quantizer: _"ClockForge's quantizer is deliberately not shared: it is a separate
implementation reached through `Output` rather than a channel, so folding it in
would be a port rather than a merge."_ The clock is that situation again, with
more at stake, since ClockForge's ISR is the one piece of real-time code in the
series with no slack in it.

What would have to be true first: ClockForge would need its ISR to consume a
tempo rather than own one, and that refactor should be justified by ClockForge's
own needs, not by WeaveForge's. Until then `core/clockSource.hpp` has two
consumers, which is the threshold `core/` is meant to have anyway.

Also on the clock page: CLOCK DIV (÷1–÷16) and, for the internal clock, RATE.
The divider stays module-local — it is three lines and GravityForge has no use
for it, since its physics free-runs and is never stepped.

**Scale** is module-wide — one root, one mask, one keyboard page, reusing
NoteForge's. Per-jack scales were considered and dropped: two NOTE outputs in
different keys is a patch you can build with two modules, and the shared mask is
what makes the DUO routing sound like one instrument rather than two.

**CV targets** for IN 2 / IN 3, same `BuildModBus()` shape as
[apps/gen/lib/cvInputs.hpp:177](../../gen/lib/cvInputs.hpp#L177):

```
OFF · LEN A · LEN B · LEN AB · CHANCE A · CHANCE B · CHANCE AB
    · WEAVE · TRANSPOSE · ROTATE ALL · RESET · LOCK
```

`RESET` and `LOCK` are threshold-detected gates rather than continuous targets —
`LOCK` forces CHANCE to 0 while high, which is ShiftReg's Digital-2 trick
reachable without a second digital jack. As in every other module, **append to
this enum, never insert**: presets store the target as a raw index.

---

## 8. Preset schema

Ten slots via [core/appStorage.hpp](../../../core/appStorage.hpp), slot 0
auto-loaded at boot.

**The register contents are part of the preset.** Four bytes, and without them a
preset restores a machine that makes a different pattern — which for this module
means it restores nothing at all. Saving the pattern is the whole point.

```cpp
struct LoadSaveParams {
    uint8_t  valid;
    uint16_t reg[2];          // ← the patterns themselves
    uint8_t  length[2], chance[2];
    uint8_t  weave, weaveDir;
    uint8_t  outSource[4], outType[4], outDepth[4],
             outRotate[4], outParam[4], outParam2[4];   // the four slots
    uint16_t noteMask;  uint8_t scaleIndex, rootIndex; int8_t octave;
    uint8_t  clockDiv, clockRate, clockSource;
    uint8_t  cvTarget[NUM_CV_INS], cvDepth[NUM_CV_INS];
    int      menuScreenTimeout;
};
```

No ROUTING index — §5. Every field here must appear in `CollectParams()`; a
parameter reachable from the menu but missing from it is silently not persisted,
and in Rack silently lost on patch reload.

---

## 9. Deliberately left out

**Enigma's library, banks and song mode.** 40 registers in five banks, four
tracks of up to 99 steps, transposition per step, SysEx dump. It is a fine
design _for its hardware_ — two encoders, four buttons, and a screen you can
put a table on. Reproducing it on one encoder would mean a four-level nested
edit for every step, and a preset schema an order of magnitude larger than
anything else in the series. The part of Enigma worth having is the output
assignment matrix, and that is §5.

**A coupling mode (XOR)**, §3 — dropped because it is the one mechanism on this
module the screen cannot draw.

**Register stride** (shift by 2 or 3 instead of 1). Genuinely interesting — it
fragments a loop into interleaved sub-patterns — but it breaks the one-to-one
relationship between the row drawn on screen and the order bits are heard in,
which is the thing making this module legible. Revisit only with a screen design
that can show it.

**Folding ClockForge into `core/clockSource.hpp`**, §7 — a port, not a merge.

**Per-jack scales**, §7. **A second digital input**, §1.

**An IN 1 role menu.** §1 — the clock is not optional here.

---

## 10. File map

Mirrors ChaosForge; see [../AGENTS.md](../AGENTS.md) once written.

| File                                                                  | Purpose                                                                           |
| --------------------------------------------------------------------- | --------------------------------------------------------------------------------- |
| `src/wea_app.cpp`                                                     | the `forge::IApp` — `Begin`/`Tick0`/`Tick1`, encoder events, all file-scope state |
| `src/wea_app.hpp`                                                     | `forge::WeaApp()` factory, the only thing the shell includes                      |
| `lib/engine.hpp`                                                      | the per-iteration step + `DACWriteAll()`, **shared with the Rack port**           |
| `lib/shiftreg.hpp`                                                    | `ShiftRegister` + `WeavePair` — the bit machinery, pure, no I/O                   |
| `lib/outputs.hpp`                                                     | the four slots, the four types, `ROUTING_TEMPLATES[]`                             |
| `lib/clock.hpp`                                                       | the ÷1–÷16 divider and the step edge, over `core/clockSource.hpp` (§7)            |
| `lib/params.hpp`                                                      | `RegParams` / `WeaveParams` / `ModBus` + `ApplyParams()`                          |
| `lib/cvInputs.hpp`                                                    | `CvNorm()`/`CvBipolar()`, `CVTarget`, `BuildModBus()`                             |
| `lib/randomize.hpp`                                                   | `RandomizeParams()` — backs PRESETS ▸ RANDOM and Rack's Randomize                 |
| `lib/menuDefinitions.hpp` `lib/menuHandlers.hpp` `lib/menuRender.hpp` | the menu, its setters, the loom home screen                                       |
| `lib/presetManager.hpp` `lib/storage.hpp` `lib/version.hpp`           | §8, and the four-line storage shim                                                |

Plus `vcv-plugin/` (standalone Rack plugin, slug `WeaveForge`), an entry in
`vcv/` for the consolidated build, `test/test_native/`, an `env:xiao_wea` in the
root [platformio.ini](../../../platformio.ini), and the `kApps[]` entry in
[src/main.cpp](../../../src/main.cpp).

---

## 11. Constraints inherited from the platform

- **Every mutable file-scope global in `lib/` must be listed in**
  `vcv-plugin/src/engine/engine_state.def`, or two Rack instances share it.
  `make isolation` is the guard and only the Rack build catches a stale entry.
  For this module that is both registers, so the failure mode is two WeaveForges
  playing the same pattern — obvious, at least.
- **Six rows per menu page.** `MD_START_Y=12` + `MD_ROW_H=9` puts row 6 at y=57;
  a seventh is clipped with no error. §5's six fields per jack is not a
  coincidence.
- **All four outputs are DAC** (MCP4728). Gates and triggers are DAC levels, and
  minimum trigger width is bounded by the `Tick0` period — TRIG ▸ WIDTH must
  clamp to something the loop can actually produce.
- **DAC channel swap**: hardware swaps DACB↔DACC, compensated by `_chanMap[]` in
  [core/boardIO.hpp](../../../core/boardIO.hpp).
- **Panel SVG**: `vcv-plugin/res/WeaveForge.svg` is the single source of jack
  labels; nanosvg ignores `<text>`, so every glyph is a path. Keep an editable
  `-src.svg` and re-run the Inkscape conversion rather than hand-editing.
- **Cost.** Two `uint16_t` plus a few dozen bytes of parameters: this is the
  smallest engine in the series, below NoteForge's ~0.7 KB. The work is the
  plumbing, not the DSP.
