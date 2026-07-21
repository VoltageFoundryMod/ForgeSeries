# ForgeSeries VCV Library

Reusable VCV Rack layer shared by [ForgeSeries](https://github.com/VoltageFoundryMod)
firmware ports. It lets a firmware written for the ForgeSeries hardware (XIAO
RP2040 + SSD1306 OLED + MCP4728 quad DAC + rotary encoder) compile and run
unchanged inside VCV Rack, wrapped in a consistent module UI.

The firmware's own DSP / menu / render code stays in each firmware repo; this
library provides only the host-side glue that would otherwise be copy-pasted
between plugins.

## What's in here

| Path | Purpose |
|------|---------|
| `shim/` | Arduino / Wire / EEPROM / Adafruit shims so firmware `lib/` compiles under Rack. Includes the `HostBridge` that routes firmware I/O (GPIO, ADC, DAC, framebuffer) to the hosting module. |
| `include/forgevcv/IEngine.hpp` | Abstract engine contract. Each firmware wraps its bridge in an `IEngine`. |
| `include/forgevcv/ForgeModule.hpp` | `rack::Module` base: owns the engine, holds the framebuffer, the CV-range and encoder-sensitivity host settings, the UI→audio encoder queue, the control-rate engine step, and shared patch JSON. |
| `include/forgevcv/widgets.hpp` | Shared UI: `FramebufferDisplay` (emulated OLED), `EncoderKnob` (drag/click), `BpmSlider`. |
| `forgevcv.mk` | Makefile fragment: adds the include path, C++17, and exports `FORGEVCV_SHIM`. |

Everything under `include/` is header-only (in-class definitions are inline, so
including from multiple module TUs is ODR-safe).

## Using it in a plugin

Add this repo as a submodule and include the fragment from your plugin Makefile:

```makefile
FORGEVCV ?= ../ForgeSeries-VCVLib      # git submodule path
include $(FORGEVCV)/forgevcv.mk

SOURCES += $(wildcard src/*.cpp) $(wildcard src/engine/*.cpp)

# Only the firmware engine TU sees the Arduino shim + firmware lib/, so the shim
# names never collide with rack.hpp in the UI sources.
build/src/engine/fw_engine.cpp.o: EXTRA_CXXFLAGS += -I$(FORGEVCV_SHIM) -I../lib
```

In your plugin:

1. **Engine** — implement `forgevcv::IEngine` in your engine TU, wrapping your
   firmware bridge. Keep any richer, module-specific parameter accessors (tempo,
   waveform, ...) in your own header for the context menu.
2. **Module** — subclass `forgevcv::ForgeModule`, declare your `InputId` /
   `OutputId` / ... enums, construct your `IEngine` into `engine`, and implement
   `process()` as: gather CV via `mapCvInput` + clock level → `stepEngine(...)` →
   write `outHold` to your outputs. Call `baseToJson` / `baseFromJson` from
   `dataToJson` / `dataFromJson`.
3. **Widget** — place `forgevcv::FramebufferDisplay` and `EncoderKnob`, pointing
   their `module` at your module instance. Add your own ports, panel, keyboard
   shortcuts, and context menu.

## Versioning

Consumers pin a tag (`v0.x.y`). Because the `shim/` surface must match what
firmware `lib/` calls, bump the tag deliberately when the shim changes so an
older module can stay on an older tag.
