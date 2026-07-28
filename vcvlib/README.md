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

## Display rendering

`FramebufferDisplay` rasterizes the 128x64 framebuffer on the CPU, straight to
whatever resolution the screen currently occupies, then blits it 1:1 onto
device-pixel-snapped coordinates so the GPU never resamples it.

This matters because a ForgeSeries screen cutout is ~35 mm ≈ 103 px at 100 %
zoom, i.e. the panel *downscales* the framebuffer. Sampling that with
`NVG_IMAGE_NEAREST` drops 25 of the 128 columns and 13 of the 64 rows outright,
so 1 px font stems vanish; linear sampling smears them instead. Integrating the
exact area each output texel covers keeps a stem that falls between two texels
alive as two dimmer ones, which is why hardware looks sharper than a naive blit.

Above 3 device pixels per OLED pixel it switches to an integer supersample, so
every pixel stays exactly the same size when you zoom in.

Three corrections keep the downscaled result as bright and tight as hardware,
measured on a glyph-like test pattern at 103x51 (0.80 texels per OLED pixel):

| | total light | 1 px stem | solid fill | mid-tone texels |
| - | - | - | - | - |
| plain area resample | — | 209 | 210 | 144 |
| + gap fade | +41.6 % | 246 | **255** | 113 |
| + isolated-pixel gain | +56.0 % | **255** | **255** | 81 |
| + `sharpen` | +58.1 % | **255** | **255** | **4** |

- **Gap fade** — a dark inter-pixel grid only reads as a grid once there are
  enough texels to draw one. Below that it is invisible and just costs
  `(1-gap)²` of the light, so it fades in over 4..8 texels per pixel.
- **Gain** — when downscaling, an isolated lit pixel can only ever cover `s` of
  a texel, so without a `1/s` gain a 1 px stem could never reach the full
  brightness it has on hardware.
- **`sharpen`** — pulls partial coverage away from the middle, so an edge that
  lands well inside a texel goes fully on or off instead of grey. Mid-tone
  texels are what read as blur, and block-heavy UIs are the worst case for
  them: on a keyboard/grid screen this takes them from 647 to 50.

`sharpen` is safe to push hard because the gain pins an evenly-split 1 px stem
to coverage exactly 0.5 — the curve's pivot — so it is invariant under any
amount of sharpening. Measured over every column and row at ten zoom levels, the
dimmest a stem ever gets is 189/255, and it is a full 255 at every zoom at or
above 1:1. That is the difference from the nearest-neighbour blit this replaced:
comparably hard edges, but nothing can ever vanish.

Tweakable per instance (set `dirty = true` if you change one after the first
frame):

| Field | Default | Effect |
| ----- | ------- | ------ |
| `litColor` / `bgColor` | OLED blue / near-black | pixel and glass colours |
| `pixelGap` | `0.16` | dark fraction of each cell — the inter-pixel grid a real OLED has, faded in as it becomes resolvable. `0` fills cells solid |
| `sharpen` | `4` | >1 snaps edges toward fully on/off; `1` is plain area coverage. Higher approaches nearest-neighbour hardness without its dropped pixels. Backed off automatically as `pixelGap` fades in, since the grid ring is a soft edge worth keeping |
| `gamma` | `2.2` | coverage → alpha encode. OLED light adds linearly but NanoVG blends in sRGB, so without this partial coverage comes out far too dark. `1` disables |
| `bloom` | `0` | 0..1 light spill into the 4 neighbours; a little sells the emissive look at the cost of some sharpness |
| `glassSheen` | `false` | diagonal reflection over the glass; lifts the black floor, so it costs screen contrast |

The lit pixels are drawn on Rack's light layer, so the screen keeps glowing when
the room is dimmed; the glass and sheen stay on the panel layer and darken with
it. The texture is rebuilt only when the framebuffer actually changes, so a
static screen costs one `nvgFill` per frame.

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
