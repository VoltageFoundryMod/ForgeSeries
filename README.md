# ForgeSeries

Monorepo for the Voltage Foundry Modular **ForgeSeries** platform — a Eurorack
module built on a Seeed XIAO RP2040 that runs one of several firmwares, each
also shipping as a VCV Rack plugin built from the same sources.

## Layout

```
core/      The board. Shared by every app and by the shell.
apps/
  clk/     ClockForge    — clock generator / modulation source
  dq/      NoteForge     — dual quantizer
  gen/     GravityForge  — physics-based generative sequencer
  scp/     ForgeView     — oscilloscope / spectrum analyser
unified/   Shell + one TU per app: all firmwares in one image
vcvlib/    Shared VCV Rack layer (Arduino shim, ForgeModule, IEngine, widgets)
```

Each `apps/<app>/` is a self-contained PlatformIO project: `platformio.ini`,
`src/`, `lib/`, `vcv-plugin/`, docs. Nothing copies app sources — the unified
build pulls them in by include path, so each app directory stays the single
source of truth and its standalone firmware and Rack plugin keep working.

### What belongs in `core/`

Every ForgeSeries module is the *same board*: XIAO RP2040, SSD1306 on Wire,
MCP4728 on Wire1, one encoder, 3 in / 4 out. Anything that follows from that is
shared:

| file | |
|------|--|
| `boardPinouts.hpp` | wiring, converter resolution (`MAXDAC`/`MAXADC`) |
| `boardIO.hpp` | I2C bring-up, DAC writes + output calibration |
| `cvInput.hpp` | CV acquisition, calibration, range adapters |
| `calibrationData.hpp` | the calibration blob — one struct, one magic, all apps |
| `encoder.hpp` `displayManager.hpp` `menuDisplay.hpp` `splash.hpp` | UI plumbing |
| `envelope.hpp` `scales.hpp` `utils.hpp` | shared DSP/theory helpers |
| `IApp.hpp` | the shell↔app contract |

What stays in an app's `lib/` is genuinely its own: menu definitions, preset
schema, and the DSP that makes it that module. Jack *semantics* are app-level
too — each app's `lib/pinouts.hpp` includes `core/boardPinouts.hpp` and adds its
own (`NUM_CHANNELS`, `OUT_CV`/`OUT_GATE`), which is why every consumer can keep
including `"pinouts.hpp"` unchanged.

Two headers are still per-app but shouldn't be: `calibration.hpp` and
`storage.hpp` are identical between DQ and GEN but `#include "presetManager.hpp"`,
the app-specific preset schema. They move to `core/` when the shell takes over
calibration and storage moves to LittleFS.

### CV, and the ±5 V hardware

Readings are **normalised floats**: `1.0` is +5 V at the jack on every hardware
revision. Not counts — a count means nothing without also knowing `MAXADC` and
the polarity convention, and presets store values derived from these.

```
unipolar (0..+5 V)   CvRead() ->  0.0 .. 1.0
bipolar  (-5..+5 V)  CvRead() -> -1.0 .. 1.0,  0 V == 0.0
```

Build for the ±5 V revision with `-DFORGE_CV_BIPOLAR`. Apps must read through
the adapters rather than open-coding a mapping, because which one is the
identity swaps when the flag flips:

| | unipolar | bipolar |
|---|---|---|
| `CvUni` → 0..1 | identity | `(v+1)/2` |
| `CvBi` → -1..1 | `v*2-1` | identity |

Pitch is **not** normalised — `CvSemitones()` returns 1 V/oct semitones, and DAC
output stays in counts, scaled at the write. Three domains, three units.

0 V is C0 on both revisions, so the bipolar jack keeps the same five-octave
span. Moving C0 to -3 V for eight octaves is a future opt-in menu setting, not
something implied by the hardware.

### The unified firmware

`unified/` compiles the shell plus one translation unit per app. The shell owns
the board — display, encoder, calibration are single instances — and drives one
app at a time through `forge::IApp` (`Begin`/`Tick0`/`Tick1`/encoder events),
keeping the existing core split: Core 0 does ADC/DAC/encoder, Core 1 renders.

Each app TU wraps its module in `namespace forge::<app>`, which is what lets
several firmwares share one binary: they all define `menuMode`, `switchState`,
`param` and friends at file scope.

> **The include order in an app TU is not negotiable.** Standard library,
> third-party and `core/` headers go at *global* scope first, so their include
> guards are already satisfied; only then the app's own headers, inside the
> namespace. Miss one — `<cstring>`, say — and libstdc++ ends up inside the
> namespace, producing hundreds of errors in `stringfwd.h` that never name the
> file responsible. See the header comment in `unified/src/apps/scp_app.cpp`.

Anything defined in a `core/` header must be `inline` (C++17 inline variables
included). The per-app builds include each header from exactly one TU and get
away without it; the unified image has a shell TU plus one per app and will not
link — or worse, a file-scope `static` gives each TU a private copy of what
should be shared hardware state.

Status: the shell hosts ForgeView. App selection is still a stub that boots the
first app — a boot-slot byte would collide with ForgeView's EEPROM sector, and
the whole layout moves to LittleFS, so the boot menu and persistence land with
that work.

## Building firmware

Each app builds independently — there is no root PlatformIO project, because
PlatformIO scopes `src_dir`/`lib_dir` per *project*, not per environment.

```sh
make                # the firmware: unified/ (shell + every module)
make upload-unified # build + flash
make test           # native unit tests for every app
make plugins        # every VCV Rack plugin (needs jq + mingw64 g++)
make everything     # firmware + all plugins
```

There is ONE hardware build: `unified/`. Each `apps/<app>/` is now a native
test project only — its hardware environment is gone, and `src/main.cpp` is
kept for reference until the unified image has been verified on hardware for
every module. Nothing compiles it.

Current sizes on `xiao_rp2040` (release):

| build | RAM | Flash |
|-------|-----|-------|
| clk | 25356 (9.7%) | 163792 (7.8%) |
| dq  | 18760 (7.2%) | 132448 (6.3%) |
| gen | 19952 (7.6%) | 143528 (6.9%) |
| scp | 19124 (7.3%) | 127384 (6.1%) |
| unified (shell + scp) | 19144 (7.3%) | 127500 (6.1%) |

### How the unified image scales with app count

Only one app *runs* at a time, but all of them are *linked*, so static RAM is
the sum of every app's state rather than the maximum. The per-app totals above
badly overstate that, though: most of each is the Arduino/TinyUSB framework
baseline, which is paid once. What an app actually adds is its own translation
unit's `data+bss`:

| app | RAM added | flash added |
|-----|-----------|-------------|
| clk | 7066 | 46325 |
| gen | 1860 | 27562 |
| scp | 1177 | 14182 |
| dq  |  661 | 18582 |
| shell | 189 | 1928 |

Baseline is ~17.8 KB (SCP standalone is 19124 total, its own TU 1338). The
unified image checks out against that: 17.8 KB + 189 + 1177 = 19144.

So four apps is ~28.7 KB, 11 % of RAM. Ten CLK-weight apps would be ~88 KB
(34 %); ten typical ones ~38 KB (15 %). Flash is looser still — 14-46 KB per
app against 2 MB.

If it ever does get tight, the escape hatch is already half-built: each app's
`vcv-plugin/src/engine/engine_state.def` enumerates every mutable global it
owns, because the Rack port needs per-instance state. The same X-macro could
place app state in a shared arena sized to the largest app, making RAM
max(app) instead of sum(app). Not needed at four apps, and probably not at
ten — but the enumeration exists if it is.

## Building the VCV Rack plugins

Each app's plugin lives in `apps/<app>/vcv-plugin/` and builds with the Rack
plugin Makefile. Both paths are `?=` defaults, so they can be overridden:

```sh
cd apps/gen/vcv-plugin && make          # RACK_DIR ?= ../../../../Rack-SDK
make RACK_DIR=/path/to/Rack-SDK         # out-of-tree SDK
```

`FORGEVCV` defaults to `../../../vcvlib` (in-repo). It no longer needs a
sibling `ForgeSeries-VCVLib` checkout.

## History

This repo was assembled from five separate repositories with `git subtree`, so
all 311 original commits are preserved and reachable.

Because the files moved into `apps/<app>/`, git's default history simplification
hides pre-import commits. To see them, ask for the **old** path with
`--full-history`:

```sh
git log --full-history -- lib/storage.hpp        # CLK's pre-import history
git log --full-history -- include/forgevcv/      # VCVLib's
```

Post-import history uses the new paths as normal.

The original repositories remain untouched on disk and on GitHub. They are the
authoritative record for anything predating the import.
