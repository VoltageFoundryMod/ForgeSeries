<div align="center">

<img src="assets/VFM-Logo-Full.png" width="110" alt="Voltage Foundry Modular">

# Voltage Foundry Modular

**Open-source Eurorack modules, forged in code and copper.**

Every module ships as real open hardware, and the firmware-driven ones also as a
pixel-accurate VCV Rack plugin — the same firmware that runs on the metal runs
inside Rack, so you can try them before you build.

[Website & manuals](https://vfmod.com) ·
[GitHub org](https://github.com/VoltageFoundryMod) ·
[Support on Patreon](https://patreon.com/carlosedp)

</div>

---

This directory builds the **aggregate VCV Rack plugin** — the one the VCV
Library publishes. The Library ships one plugin per repository, so every module
is linked into a single binary here.

Sources come straight from `../apps/<module>/vcv-plugin/`. Each module also
keeps its own standalone plugin, which is what you build while working on one
module; this directory is what gets released.

## Modules

| Module            | Series       | What it does                                                       | Links                                                                             |
| ----------------- | ------------ | ------------------------------------------------------------------ | --------------------------------------------------------------------------------- |
| **Clock Forge**   | Forge Series | Advanced clock generator and modulator with four flexible outputs. | [Manual](../apps/clk/Manual.md) · [Page](https://vfmod.com/modules/clockforge/)   |
| **Note Forge**    | Forge Series | Dual CV quantizer with per-channel scales, envelopes and glide.    | [Manual](../apps/dq/Manual.md) · [Page](https://vfmod.com/modules/noteforge/)     |
| **Forge View**    | Forge Series | Oscilloscope, spectrum analyzer, X-Y display and tuner in one.     | [Manual](../apps/scp/Manual.md) · [Page](https://vfmod.com/modules/forgeview/)    |
| **Gravity Forge** | Forge Series | Dual physics-based generative sequencer with proximity coupling.   | [Manual](../apps/gen/Manual.md) · [Page](https://vfmod.com/modules/gravityforge/) |
| **Chaos Forge**   | Forge Series | Dual chaotic attractor modulation source, four related CV outputs. | [Manual](../apps/att/Manual.md) · [Page](https://vfmod.com/modules/chaosforge/)   |
| **Weave Forge**   | Forge Series | Dual shift-register sequencer with continuous coupling.            | [Manual](../apps/wea/Manual.md) · [Page](https://vfmod.com/modules/weaveforge/)   |

> The browsable versions of these manuals — with images — live on the
> [website](https://vfmod.com/).

## Building

Drive it from the repository root, which knows where the toolchain is on every
platform:

```sh
make vcv           # build plugin.{so,dylib,dll}
make vcv-install   # install into the local Rack user dir
make vcv-dist      # package the .vcvplugin for the VCV library
```

Requires the [VCV Rack SDK](https://vcvrack.com/manual/Building). It is looked
for as a sibling of the repository — `../Rack-SDK` from the repo root — and
overridden with `RACK_DIR`:

```sh
make vcv RACK_DIR=/path/to/Rack-SDK
```

`make -C vcv` and `make -C vcv dist` do the same thing directly. On Windows use
the root targets instead: only the root Makefile knows where the msys2 tools
are, and `plugin.mk` needs `jq` and a mingw64 `g++` on PATH. Run
`. .\tools\env.ps1` first.

To build the per-module standalone plugins instead — the ones you want while
working on a single module — `make plugins` at the root, or
`make -C apps/<module>/vcv-plugin`.

## Adding a module

Three places, all in this directory:

1. `VFM_MODULES` in the [`Makefile`](Makefile) — currently `clk dq scp gen att wea`.
2. The Model declaration in [`src/plugin.hpp`](src/plugin.hpp) and its
   registration in [`src/plugin.cpp`](src/plugin.cpp).
3. An entry in [`plugin.json`](plugin.json).

A hardware-only module — fully analog, no firmware to emulate — has no VCV build
and belongs in none of them.

## How it fits together

Worth knowing before changing anything here:

- **`res/` is generated.** Rack resolves panel assets against the plugin
  directory, so each module's `vcv-plugin/res/` is staged into one top-level
  `res/` at build time. It is gitignored — edit the panels in the module, not
  here.

- **The firmware really runs.** For modules with an
  `vcv-plugin/src/engine/fw_engine.cpp`, that translation unit compiles the
  actual RP2040 firmware against an Arduino shim from
  [`../vcvlib`](../vcvlib) (override with `FORGEVCV`). It is the *only* file
  that sees the shim and that module's hardware `lib/`, and it is wrapped in an
  anonymous namespace — that internal linkage is what lets six modules'
  identically-named globals (`micros()`, `Serial`, `display`, `MENU_ITEMS`)
  coexist in one binary.

- **`make isolation` at the root** is the check that catches firmware state
  leaking between Rack instances. A green plugin build does not imply it passes.

## The website

The company site and module catalog — [vfmod.com](https://vfmod.com) — lives in
its own repository,
[`VoltageFoundryMod/VFM-Website`](https://github.com/VoltageFoundryMod/VFM-Website),
and pulls the manuals and images out of this one at build time, so nothing about
the site is duplicated here.

## License

Source code is GPL-3.0-or-later — see the [LICENSE](../LICENSE) at the
repository root. Panel designs, graphics, module names and the Voltage Foundry
Modular brand are copyright and are not covered by it; see
[LICENSE-ASSETS.md](../LICENSE-ASSETS.md).

Hardware design files carry their own license in the
[hardware repository](https://github.com/VoltageFoundryMod/ForgeSeries-Hardware).
