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
| ---- | ------- |
| `shim/` | Arduino / Wire / EEPROM / Adafruit shims so firmware `lib/` compiles under Rack. Includes the `HostBridge` that routes firmware I/O (GPIO, ADC, DAC, framebuffer) to the hosting module. |
| `shim/Fonts/` | `GFXfont` headers usable via `display.setFont(&helvB24)` (see below). |
| `include/forgevcv/IEngine.hpp` | Abstract engine contract. Each firmware wraps its bridge in an `IEngine`. |
| `include/forgevcv/ForgeModule.hpp` | `rack::Module` base: owns the engine, holds the framebuffer, the CV-range and encoder-sensitivity host settings, the UI→audio encoder queue, the control-rate engine step, shared patch JSON, and the Initialize/Randomize actions. |
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

   `serialize()` must **commit the live state into the EEPROM blob first**
   (`Save(CollectParams(), 0)`) — firmwares only write EEPROM on an explicit
   SAVE, so dumping the raw buffer persists a blob with none of the user's edits
   in it and the patch reloads at factory defaults. `deserialize()` reads slot 0
   back. Optionally override `reset()` / `randomize()` to back Rack's Initialize
   and Randomize; they default to no-ops.
2. **Module** — subclass `forgevcv::ForgeModule`, declare your `InputId` /
   `OutputId` / ... enums, construct your `IEngine` into `engine`, and implement
   `process()` as: gather CV via `mapCvInput` + clock level → `stepEngine(...)` →
   write `outHold` to your outputs. Call `baseToJson` / `baseFromJson` from
   `dataToJson` / `dataFromJson`.
3. **Widget** — place `forgevcv::FramebufferDisplay` and `EncoderKnob`, pointing
   their `module` at your module instance. Add your own ports, panel, keyboard
   shortcuts, and context menu.

## Custom fonts

The GFX shim implements the full Adafruit custom-font API (`setFont`,
`getTextBounds`, per-glyph metrics), pixel-identical to the hardware library.
`shim/Fonts/` bundles ready-to-use `GFXfont` headers:

- `helvB08/10/12/14/18/24` — hand-hinted X11 Helvetica Bold bitmaps (the
  "Pam's" look; recommended pairing: `helvB24` big + `helvB12` labels)
- `profont12/22` — monospaced terminal font, digits stay column-aligned
- `pixellari16`, `haxrcorp4089_16`, `helvetipixel16` — 16 px pixel fonts
  (use at ×1/×2)
- `FreeSans*`, `Org_01` — stock Adafruit fonts, unmodified

```cpp
#include <Fonts/helvB24.h>
display.setFont(&helvB24);
display.setCursor(x, y + 25); // custom fonts: cursor y = baseline, not top
display.print("120BPM");
display.setFont(nullptr);     // back to classic 5x7
```

The shim include path already covers `Fonts/`, so the include works unchanged
in Rack. For the physical module, copy the same header into the firmware repo
(the real Adafruit_GFX resolves `Fonts/...` from its own library folder for the
stock fonts, and compiles bundled headers like `helvB24.h` the same way).

## Versioning

Consumers pin a tag (`v0.x.y`). Because the `shim/` surface must match what
firmware `lib/` calls, bump the tag deliberately when the shim changes so an
older module can stay on an older tag.
