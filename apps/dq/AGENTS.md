# NoteForge — AI Coding Agent Instructions

**Read [../../AGENTS.md](../../AGENTS.md) first** — the board, the shell↔app
contract, the core split, storage, CV, naming, menu patterns and the VCV rules
are all shared and documented there, not repeated here. This file covers only
what is NoteForge's own.

NoteForge is the **dual CV quantizer**: it snaps two independent pitch CV inputs
to user-editable scales and emits a quantized CV plus a gate/envelope per
channel. [Readme.md](Readme.md) is the user-facing feature documentation;
[Manual.md](Manual.md) is the shipped manual.

## Module map

| File | Purpose |
| ---- | ------- |
| [src/dq_app.cpp](src/dq_app.cpp) | the `forge::IApp` — `Begin`/`Tick0`/`Tick1`, encoder events, all file-scope state |
| [src/dq_app.hpp](src/dq_app.hpp) | `forge::DqApp()` factory, the only thing the shell includes |
| [lib/engine.hpp](lib/engine.hpp) | the per-iteration step + `DACWriteAll()`, shared with the Rack port |
| [lib/channel.hpp](lib/channel.hpp) | `QuantizerChannel` — one complete voice: note mask, pitch mode, offsets, glide, transposition, sync policy, envelope |
| [lib/cvInputs.hpp](lib/cvInputs.hpp) | ADC reads, filtering, TRIG ISR + edge queue, IN 2 routing + transpose CV |
| [lib/menuDefinitions.hpp](lib/menuDefinitions.hpp) | `MenuItem` struct, `RowStyle` / `MenuItemType` enums |
| [lib/menuHandlers.hpp](lib/menuHandlers.hpp) | `MENU_ITEMS[]`, `MarkUnsaved()`, every setter — **read the guide at the top before editing** |
| [lib/menuRender.hpp](lib/menuRender.hpp) | `HandleDisplay()`, `groupTitles[]`, the keyboard home screen |
| [lib/presetManager.hpp](lib/presetManager.hpp) | `LoadSaveParams`, `LoadDefaultParams()`, `CollectParams()`, `UpdateParameters()` |
| [lib/storage.hpp](lib/storage.hpp) | four-line shim over `core/appStorage.hpp` |
| [lib/version.hpp](lib/version.hpp) | module version string shown in the selector |

The quantizer, envelope and scale tables NoteForge uses are the **shared** ones:
[../../core/quantizer.hpp](../../core/quantizer.hpp),
[../../core/envelope.hpp](../../core/envelope.hpp),
[../../core/scales.hpp](../../core/scales.hpp). So are `boardIO`,
`boardPinouts`, `displayManager`, `menuDisplay`, `encoder` and `calibration`.

## Signal flow

```text
IN2/IN3 (pitch CV) ──► CvRead ──► one-pole filter ──► channelCv[] ──► CvSemitones
                                                                          │
TRIG (IN1) ──► TriggerReceived ISR ──► edge queue ────────────┐           ▼
                                                              └──► QuantizerChannel::Process()
                                                            │
                                   ┌────────────────────────┴─────────┐
                                   ▼                                  ▼
                           Quantizer (+octave, glide)          Envelope (ENV/TRIG/GATE)
                                   │                                  │
                                   ▼                                  ▼
                           CV 1 / CV 2 jacks                 GATE 1 / GATE 2 jacks
```

## Jack map

All four outputs go through the MCP4728, written positionally by `DACWriteAll()`
in [lib/engine.hpp](lib/engine.hpp):

| DAC index | Jack | Signal |
| --------- | ---- | ------ |
| 0 | 1 | CV 1 — quantized pitch, channel 1 |
| 1 | 2 | CV 2 — quantized pitch, channel 2 |
| 2 | 3 | GATE 1 — gate/envelope, channel 1 |
| 3 | 4 | GATE 2 — gate/envelope, channel 2 |

## Key subsystems

**Pitch domain**: everything works in *semitones*, not raw counts. 0–5 V at
1 V/oct is 60 semitones, so one semitone is `QUANT_COUNTS_PER_SEMITONE` =
4095/60 = 68.25 counts. `CountsToSemitones()` / `SemitonesToCounts()` are the
only places that conversion lives.

**Quantization** (`core/quantizer.hpp`): `Build()` expands a 12-note mask into a
sorted table of enabled semitones over 0..60; `Quantize()` snaps to the nearest
entry with a hysteresis band around the midpoint between the held note and its
neighbour, so a CV sitting on a boundary does not chatter. Hysteresis is skipped
when the held note is no longer emittable, so scale changes take effect
immediately. An all-off mask falls back to chromatic rather than freezing the
output.

**Gate/envelope**: three modes — `ENV` (AD, retriggerable), `TRIG` (fixed 10 ms
pulse), `GATE` (follows the TRIG input level). The 200-point curve table is
carried over verbatim from the SAMD21 firmware so the envelope keeps its
original snap.

**Scale selection** ([lib/channel.hpp](lib/channel.hpp)): `SelectScale()` /
`SelectRoot()` record the choice *and* rebuild the note mask;
`SetScaleIndex()` / `SetRootIndex()` only record it. **The split is
load-bearing** — a preset stores the mask alongside the scale/root, and the mask
is the source of truth because it may have been hand-edited. `UpdateParameters()`
must use the plain setters, or restoring a preset would overwrite the saved mask
with a freshly generated scale. Anything user-driven uses the `Select*` pair.

**Settle window** ([lib/channel.hpp](lib/channel.hpp)): a new note must hold for
`_settleMs` before it is committed. The input smoother takes a few loop
iterations to converge after a step, and a follow-the-input quantizer plays every
note it crosses on the way — heard as a sweep. The settle window drops those
transients while still letting a genuinely slow input play every note. Two cases
bypass it deliberately: the first note after power-on, and a held note that has
left the scale (so scale edits are never delayed). It replaces the old SENS gain
trim, which detuned the module once calibration fit the input scale.

**Pitch mode**: `TRACK` follows the input (subject to the settle window); `S&H`
latches the quantized note on a TRIG edge and holds it, so nothing the input does
between triggers reaches the jack. In S&H the settle window doubles as the
*sample delay* — a sequencer emits pitch and gate together, so at the edge the
pitch CV may still be in transit. A scale change re-snaps the *held* note rather
than re-reading the input, preserving the S&H contract.

**Transposition**: `IN 2` can be switched from channel 2's pitch input to a
transposition CV, in which case channel 2 quantizes IN 1 alongside channel 1.
Transposition steps along the enabled-note table (`Quantizer::TransposeDegrees`),
so it is always in-scale and the interval follows the scale. It clamps at the
table ends rather than wrapping. The CV→degrees mapping has its own deadband for
the same reason the note quantizer does.

**Sync policy**: `TRIG` (TRIG input edge), `NOTE` (quantized note changed), or
`BOTH`.

**Octave shift** folds by whole octaves rather than clamping at the ends of the
0–60 range, so a shift can never change the pitch class the quantizer chose.

**Calibration matters more here than on any other Forge module** — an
uncalibrated ADC gain error walks the output off by a semitone or more toward
5 V. Two-point linear fit per CV input (1 V + 3 V references) plus a per-output
DAC command remap.

## Common tasks

### Adding a scale

Scales are shared, so this affects every module:

1. Append the name to `scaleNames[]` and a ≤4-char form to `scaleShortNames[]`
   in [../../core/scales.hpp](../../core/scales.hpp)
2. Append its semitone offsets to `scaleNotes[]` **and** its length to
   `scaleNoteCount[]`
3. Appending never moves existing indices, so stored presets keep resolving
4. [test/test_native/test_scales.cpp](test/test_native/test_scales.cpp)
   cross-checks the length table against the generated mask

### Adding a menu parameter

1. Write the getter/setter in [lib/menuHandlers.hpp](lib/menuHandlers.hpp) —
   template it on the channel index if it is per-channel
2. Add a `MenuItem` to `MENU_ITEMS[]` with the right `group` — max six rows per page
3. Add the field to `LoadSaveParams`, `CollectParams()` and `UpdateParameters()`
   in [lib/presetManager.hpp](lib/presetManager.hpp), and bump `VALID_MAGIC`
4. Call `MarkUnsaved()` inside the setter — it keeps the raw flag and the display
   manager in sync
5. If it should also appear in Rack's context menu, add a bridge pair in
   `vcv-plugin/src/engine/fw_engine.{hpp,cpp}` and a menu entry in
   `vcv-plugin/src/NoteForge.cpp`

**Per-channel handlers are templates**: `setScale<0>` / `setScale<1>` share one
implementation. Add a per-channel parameter once, instantiate it twice in
`MENU_ITEMS[]`.

## Gotchas

- **`micros()` is the only clock**: envelopes and glide are driven entirely by the
  caller-supplied timestamp, never by a wall clock. That is what makes them
  testable and what makes the VCV port deterministic.

## Tests

`make test-dq` (== `pio test -e native_dq`) runs the suites in
[test/test_native/](test/test_native/): channel, envelope, quantizer, scales.

---

**Docs**: [Readme.md](Readme.md) · [Manual.md](Manual.md) ·
[docs/VCVRack_Plugin.md](docs/VCVRack_Plugin.md) ·
[docs/VCVRack_Plugin_Development.md](docs/VCVRack_Plugin_Development.md)
