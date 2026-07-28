# ForgeSeries

Monorepo for the Voltage Foundry Modular **ForgeSeries** platform — a Eurorack
module built on a Seeed XIAO RP2040 that runs one of several firmwares, each
also shipping as a VCV Rack plugin built from the same sources.

## Layout

```
apps/
  clk/     ClockForge    — clock generator / modulation source
  dq/      NoteForge     — dual quantizer
  gen/     GravityForge  — physics-based generative sequencer
  scp/     ForgeView     — oscilloscope / spectrum analyser
vcvlib/    Shared VCV Rack layer (Arduino shim, ForgeModule, IEngine, widgets)
```

Each `apps/<app>/` is a self-contained PlatformIO project exactly as it was in
its own repository: `platformio.ini`, `src/`, `lib/`, `vcv-plugin/`, docs.

## Building firmware

Each app builds independently — there is no root PlatformIO project, because
PlatformIO scopes `src_dir`/`lib_dir` per *project*, not per environment.

```sh
make clk            # or: pio run -d apps/clk -e xiao_rp2040
make all            # every app
make upload-clk     # build + flash
make clean
```

Reference sizes on `xiao_rp2040` (release, at import time):

| app | RAM | Flash |
|-----|-----|-------|
| clk | 25344 (9.7%) | 163968 (7.8%) |
| dq  | 18732 (7.1%) | 132400 (6.3%) |
| gen | 19940 (7.6%) | 143584 (6.9%) |
| scp | 19056 (7.3%) | 127128 (6.1%) |

All four together are ~32% of RAM and ~27% of flash, which is the headroom the
unified-firmware work relies on.

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
