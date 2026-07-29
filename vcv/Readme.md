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

This repository is the **meta repo**: it pulls every module in as a git
submodule and links them into a single VCV Rack plugin,
_Voltage Foundry Modular_. Each module keeps its own repository (hardware
firmware + VCV port + manual); here they are assembled and published together.

## Modules

| Module          | Series       | What it does                                                       | Links                                                                                                                    |
| --------------- | ------------ | ------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------ |
| **Clock Forge** | Forge Series | Advanced clock generator and modulator with four flexible outputs. | [Manual](modules/ForgeSeries-CLK/Manual.md) · [Repo](https://github.com/VoltageFoundryMod/ForgeSeries-CLK)               |
| **Note Forge**  | Forge Series | Dual CV quantizer with per-channel scales, envelopes and glide.    | [Manual](modules/ForgeSeries-DQ/Manual.md) · [Repo](https://github.com/VoltageFoundryMod/ForgeSeries-DQ)                 |
| **Forge View**  | Forge Series | Oscilloscope, spectrum analyzer, X-Y display and tuner in one.     | [Manual](modules/ForgeSeries-SCP/Manual.md) · [Repo](https://github.com/VoltageFoundryMod/ForgeSeries-SCP)               |

> The browsable versions of these manuals — with images — live on the
> [website](https://vfmod.com/).

## Building the plugin

```sh
git clone --recursive https://github.com/VoltageFoundryMod/VFM-VCV
cd VFM-VCV
make            # build plugin.{so,dylib,dll}
make install    # install into the local Rack user dir
make dist       # package for the VCV library
```

Requires the [VCV Rack SDK](https://vcvrack.com/manual/Building) at
`../Rack-SDK` (override with `RACK_DIR=/path/to/Rack-SDK`).

## Updating the modules

Pull the latest commit of every submodule, rebuild, and commit the new pins:

```sh
make repos-update    # bump the module repos to their branch tips
make && make install

git add -A
git commit -m "Update submodules"
git push
```

`repos-update` (and `repos-pull`, which just checks out the pinned commits) works
from a bare clone with no Rack SDK. Both operate on the `REPOS` list in the
[`Makefile`](Makefile) — the submodules the plugin links in. To grab every
submodule in the repo instead:

```sh
git submodule update --init --recursive
```

## The website

The company site and module catalog — [vfmod.com](https://vfmod.com) — lives in
its own repository,
[`VoltageFoundryMod/VFM-Website`](https://github.com/VoltageFoundryMod/VFM-Website).
It pulls each module's manual and images from the module repos at build time, so
nothing about the site is duplicated here.

## License

Module firmware and hardware are open-source under each module's own license.
The assembled VCV Rack plugin is distributed under the VCV Rack EULA.
