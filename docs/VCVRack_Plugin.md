# NoteForge for VCV Rack

NoteForge is available as a VCV Rack plugin. It is not a reimplementation — the
plugin runs the **actual hardware firmware** inside Rack, with the OLED emulated
pixel for pixel. Everything you learn on the panel applies to the module, and
vice versa.

![6HP, same panel as the hardware](../vcv-plugin/res/NoteForge.svg)

## Installing

Grab the release for your platform and drop the `NoteForge` folder into your
Rack user plugins directory, or build it yourself:

```bash
cd vcv-plugin
make install
```

See [VCVRack_Plugin_Development.md](VCVRack_Plugin_Development.md) for build
requirements.

## Ports

| Jack | Direction | Meaning |
|------|-----------|---------|
| TRIG | in | Trigger input. Rising edges fire the gate/envelope on channels set to `TRIG` or `BOTH` sync; the level is what `GATE` mode follows. |
| IN 1 | in | Pitch CV for channel 1 (0–5 V, 1V/oct). |
| IN 2 | in | Pitch CV for channel 2. |
| CV 1 | out | Quantized pitch, channel 1. |
| CV 2 | out | Quantized pitch, channel 2. |
| GATE 1 | out | Gate / envelope, channel 1. |
| GATE 2 | out | Gate / envelope, channel 2. |

## Driving the panel

- **Encoder**: drag to turn, click to select. Dragging locks the cursor, so you
  can spin as far as you like without running off the module.
- **Keyboard shortcuts** (while hovering the module):
  - `[` / `]` — one encoder detent counter-clockwise / clockwise
  - `Space` — encoder push

The screen behaves exactly as the hardware does: the keyboard page is home, the
encoder walks through the 12 keys of each channel (click toggles a note), and
continuing past them moves into the SCALES, PITCH, CH1 GATE, CH2 GATE and
SETTINGS pages.

## Context menu

Everything the on-screen menu can do is also reachable from the right-click menu,
with proper option lists instead of encoder turns. Changes made here drive the
same live firmware state, so the emulated OLED tracks them and they are saved
with the patch.

**Hardware**

- **Input CV Shift** — `0V` / `+1V` / `+2V` / `+3V`. The hardware quantizes a
  0–5 V window. Rack sequencers commonly emit bipolar pitch CV, so this *shifts*
  the input up into that window. It deliberately does **not** rescale: pitch is
  1V/oct, and squeezing ±5 V into 0–5 V would make an octave 2 V wide.
- **Encoder Sensitivity** — pixels of drag per detent.

**Input routing**

- **IN 2** — `PITCH` (default) leaves IN 2 as channel 2's pitch input. `TRANSP`
  turns it into a transposition CV, and channel 2 then quantizes IN 1 alongside
  channel 1: two voicings of the same melody, transposed together.
- **Transpose range** — `+7`, `+12`, `-7/+7`, `-12/+12`, in scale degrees.
  Transposition steps along the scale, so +2 degrees is a third in a major scale
  and a fourth in a pentatonic — always in-scale, which semitone transposition
  could not guarantee.

**Per channel** (Channel 1 / Channel 2, showing the live note at the right)

- **Notes** — the 12-note mask; this is the real source of truth for quantization.
- **Scale** / **Root** — written straight into the note mask when chosen, exactly
  as on the hardware. Hand-edit individual **Notes** afterwards if you want
  something that is not a named scale; changing the scale or root again rebuilds
  the mask and replaces those edits.
- **Pitch mode** — `TRACK` follows the input; `S&H` latches the note on a TRIG
  edge and holds it, so nothing the input does between triggers is heard.
- **Octave** — −3…+3, applied after quantization.
- **Glide** — 0–100 %, portamento between notes.
- **Settle** — 0–50 ms. How long a new note must hold before the output plays
  it, so the notes an input sweeps through on the way between two pitches are
  never heard. A genuinely slow input still plays every note. Default 5 ms.
- **Follow transpose CV** — whether this channel responds to the transposition
  input (see *Input routing* below).
- **Gate mode** — `ENV` (attack/decay), `TRIG` (fixed 10 ms pulse), `GATE`
  (follows the TRIG input level).
- **Sync** — `TRIG`, `NOTE` (fire on note change) or `BOTH`.
- **Attack** / **Decay** — 0–2000 ms / 0–4000 ms.

## Patch persistence

The module's entire EEPROM image — all 10 preset slots — is saved with the patch,
so preset slots work in Rack just as they do on hardware, and a patch reopens
with exactly the state you left.

## Differences from the hardware

- **No calibration wizard.** Rack's inputs and outputs are ideal, so there is
  nothing to trim; the module uses the nominal 0–5 V mapping and quantizes
  exactly.
- **The screen refreshes at the firmware's 20 Hz rate.** That is the hardware
  behaviour, kept deliberately.
- Multiple instances are fully independent, despite the firmware keeping its
  state in globals — see the developer reference for how.
