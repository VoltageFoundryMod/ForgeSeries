# ClockForge — AI Coding Agent Instructions

**Read [../../AGENTS.md](../../AGENTS.md) first** — the board, the shell↔app
contract, the core split, storage, CV, naming, menu patterns and the VCV rules
are all shared and documented there, not repeated here. This file covers only
what is ClockForge's own.

ClockForge is the **clock generator / modulation source**: four outputs producing
clocks, gates, Euclidean rhythms, envelopes and CV waveforms, with swing,
quantization and external sync. [Readme.md](Readme.md) is the user-facing
feature documentation; [Manual.md](Manual.md) is the shipped manual.

## Module map

| File | Purpose |
| ---- | ------- |
| [src/clk_app.cpp](src/clk_app.cpp) | the `forge::IApp` — `Begin`/`Tick0`/`Tick1`, encoder events, all file-scope state |
| [src/clk_app.hpp](src/clk_app.hpp) | `forge::ClkApp()` factory, the only thing the shell includes |
| [lib/engine.hpp](lib/engine.hpp) | `HandleOutputs()` — the per-iteration step, shared with the Rack port |
| [lib/clockEngine.hpp](lib/clockEngine.hpp) | `PPQN`, BPM, `tickCounter`, external clock sync, `ClockPulse()` ISR |
| [lib/outputs.hpp](lib/outputs.hpp) | `Output` class — all waveform / rhythm / envelope logic |
| [lib/cvInputs.hpp](lib/cvInputs.hpp) | `CVTarget` enum, `CVTargetDescription[]`, `HandleCVInputs()`, `HandleCVTarget()` |
| [lib/euclidean.hpp](lib/euclidean.hpp) | Bjorklund's Euclidean rhythm algorithm |
| [lib/quantizer.cpp](lib/quantizer.cpp) | pitch quantization — ClockForge's own, not `core/`'s |
| [lib/scales.cpp](lib/scales.cpp) | musical scale definitions |
| [lib/menuDefinitions.hpp](lib/menuDefinitions.hpp) | `MenuItem` struct, `RowStyle` / `MenuItemType` enums |
| [lib/menuHandlers.hpp](lib/menuHandlers.hpp) | `MENU_ITEMS[]` + every setter/getter — **read the guide at the top before editing** |
| [lib/menuRender.hpp](lib/menuRender.hpp) | `HandleDisplay()`, `groupTitles[]`, per-group renderers |
| [lib/presetManager.hpp](lib/presetManager.hpp) | `LoadSaveParams`, `LoadDefaultParams()`, `CollectParams()`, `UpdateParameters()` |
| [lib/storage.hpp](lib/storage.hpp) | four-line shim over `core/appStorage.hpp` |
| [lib/version.hpp](lib/version.hpp) | module version string shown in the selector |

Everything board-level — `boardIO`, `boardPinouts`, `displayManager`,
`menuDisplay`, `encoder`, `metrics`, `utils`, `calibrationData`, `calibration` —
lives in [../../core/](../../core/).

`quantizer.cpp` and `scales.cpp` are `#include`d directly by
[lib/outputs.hpp](lib/outputs.hpp); they are not compiled separately.
ClockForge's quantizer is deliberately **not** the shared `core/quantizer.hpp` —
it is a separate implementation reached through `Output` rather than a channel,
so folding it in would be a port, not a merge.

## Timing core

PPQN = 960. An RP2040 `repeating_timer_t` fires `ClockPulse()` → `HandleOutputs()`
→ `outputs[i].Pulse()`. At 133 MHz that is 208 µs/tick at 300 BPM, so there is
ISR headroom — but **keep the ISR fast**: no heap allocation, no blocking calls.
`tickCounter` increments every tick.

External sync arrives on `HandleExternalClock()`, a rising-edge interrupt on
`CLK_IN_PIN`. Out-of-range or inconsistent pulses reset the confirmation counter
rather than yanking the tempo.

## Outputs

Four outputs, all through the MCP4728. 21 waveform types in `WaveformType`:
`Square`, `Triangle`, `Sine`, `Parabolic`, `Sawtooth`, `ExpEnvelope`,
`LogEnvelope`, `InvExpEnvelope`, `InvLogEnvelope`, `Hatchet2`, `Hatchet4`,
`Noise`, `SmoothNoise`, `SampleHold`, `ResetTrig`, `Play`, `ADEnvelope`,
`AREnvelope`, `ADSREnvelope`, `CVInput1`, `CVInput2` — the last two mirror a CV
input to the output, quantised via the Quantize toggle.

## Common tasks

### Adding a waveform

1. Add the enum value to `WaveformType` in [lib/outputs.hpp](lib/outputs.hpp)
2. Add its description to `WaveformTypeDescriptions[]`
3. Implement it in `Output::GeneratePulse()`
4. Update [Readme.md](Readme.md) and [Manual.md](Manual.md)

### Adding a CV modulation target

1. Add the enum to `CVTarget` in [lib/cvInputs.hpp](lib/cvInputs.hpp)
2. Add its description to `CVTargetDescription[]`
3. Implement it in `HandleCVTarget()` in the same file

### Adding a menu parameter

1. Write the `valueFn` getter and `setter`/`action` in [lib/menuHandlers.hpp](lib/menuHandlers.hpp)
2. Add a `MenuItem` to `MENU_ITEMS[]` — max six rows per page
3. Add the field to `LoadSaveParams`, `CollectParams()` and `UpdateParameters()`
   in [lib/presetManager.hpp](lib/presetManager.hpp), and bump `VALID_MAGIC`
4. Mark the state dirty in the setter — `displayMgr.SetUnsavedChanges(true)` for
   the indicator, and `unsavedChanges = true` for the raw flag the save/load
   checks read
5. If it should also appear in Rack's context menu, add a bridge pair in
   `vcv-plugin/src/engine/fw_engine.{hpp,cpp}` and a menu entry in
   `vcv-plugin/src/ClockForge.cpp`

## Gotchas

- **ClockForge is the RAM outlier** — ~7 KB of the image's ~29 KB, roughly 10×
  NoteForge. If static RAM ever gets tight this module is the lever, not the
  module count. Be deliberate about new file-scope arrays.
- The `PPQN` ISR path is the one place in the series with a hard real-time
  budget. Profile with `core/metrics.hpp` rather than guessing.

## Tests

`make test-clk` (== `pio test -e native_clk`) runs
[test/test_native/test_outputs.cpp](test/test_native/test_outputs.cpp).

---

**Docs**: [Readme.md](Readme.md) · [Manual.md](Manual.md) ·
[docs/Hardware_Design.md](docs/Hardware_Design.md) ·
[docs/VCVRack_Plugin.md](docs/VCVRack_Plugin.md) ·
[docs/VCVRack_Plugin_Development.md](docs/VCVRack_Plugin_Development.md) ·
[docs/Improvements.md](docs/Improvements.md)
