---
name: forge-series-shared-vcv-layer
description: Sibling repos ForgeSeries-VCVLib and ForgeSeries-CLK are the reference for this port
metadata:
  type: project
---

The Forge Series is a family of Eurorack firmwares sharing one RP2040 hardware
platform, split across sibling checkouts under `C:/Users/carlosedp/projects/`:

- `ForgeSeries-VCVLib` — the Arduino/RP2040 shim plus the reusable `forgevcv`
  Rack layer (`ForgeModule`, `IEngine`, widgets, `forgevcv.mk`). Consumed by
  every plugin; `FORGEVCV` defaults to `../../ForgeSeries-VCVLib`.
- `ForgeSeries-CLK` (ClockForge) — the first module ported to RP2040 + VCV. Its
  `lib/` layout, menu framework and `vcv-plugin/` structure are the pattern the
  other modules follow.
- `ForgeSeries-Hardware` — schematics; note its `Kicad/Panels/DQ-Panel.kicad_pcb`
  is actually a LoopCrafter panel, not the quantizer's, so it is not a usable
  reference for jack labels.

**Why:** Porting a module means copying the CLK infrastructure rather than
inventing one, and knowing the VCVLib exists avoids reimplementing the shim.

**How to apply:** When touching another Forge firmware, read the CLK equivalents
first. See also [[forge-series-toolchain-paths]].
