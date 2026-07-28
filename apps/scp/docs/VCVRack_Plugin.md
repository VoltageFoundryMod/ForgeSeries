# ForgeView — RP2040 port + VCV Rack plugin

ForgeView is the Forge Series **oscilloscope / spectrum** module. It shares the
Seeed XIAO RP2040 hardware with ClockForge and is built the same way: one set of
scope logic (`lib/scope.hpp`) drives both the hardware firmware and a VCV Rack
plugin through an Arduino compatibility shim.

## Scope modes

| Mode         | What it shows                               | Parameters                                                 |
| ------------ | ------------------------------------------- | ---------------------------------------------------------- |
| **Dual**     | Dual-trace slow scope of CV1 + CV2          | Timebase, Off1, Off2, Vert, Info                           |
| **Single**   | Single-channel oscilloscope (CV1)           | Timebase, Off1, Vert, Refresh, Info                        |
| **Shot**     | Triggered single-shot capture of CV1        | Timebase, Trig (Off/Rising/Falling/Auto), Off1, Vert, Info |
| **Spectrum** | 128-point FFT magnitude of CV1              | HF emphasis, Noise floor, Info                             |
| **X-Y**      | Lissajous scatter — CV1 = X, CV2 = Y        | Vert, Pers (persistence Live–60s), Info                    |
| **Tuner**    | Frequency meter (Hz + nearest note + cents) | Chan (CV1/CV2)                                             |

The encoder navigates a **per-mode named parameter list**: rotate to move the
cursor through that mode's parameters, click to edit the highlighted row, click
again to commit. The overlay scrolls when a mode has more rows than fit. **Mode
and Trig are shown by name** (not numbers). A **long-press** of the encoder
(~800 ms) toggles **Freeze-frame** (a `HOLD` marker appears). The **Info** row
toggles engineering-unit axis labels (V/div and time/div; approximate on hardware,
derived from the measured feed interval — exact-ish in Rack where the sample rate
is known).

## Architecture

Same three-layer split as ClockForge (see that repo's
`docs/VCVRack_Plugin_Development.md` for the full rationale):

```
lib/scope.hpp   ── the scope engine: state + acquisition + render + encoder
   ▲                     (platform-independent, no hardware calls)
   │
   ├── src/main.cpp                      RP2040 firmware (dual-core)
   │      Core 0: encoder + ADC sampling + DAC pass-through
   │      Core 1: ScopeRender() + display.display()
   │
   └── vcv-plugin/src/engine/fw_engine.cpp   VCV Rack engine
          Arduino shim + per-instance context-swap (engine_state.def)
          exposes the POD API in fw_engine.hpp (namespace fvengine)
```

### Push-based acquisition (the one real adaptation)

The original SAMD21 scope *pulled* samples in a blocking `analogRead()` +
`delayMicroseconds()` loop. That cannot work under Rack's one-sample-at-a-time
`process()`. So acquisition was rearchitected to be **push-based**:
`ScopeFeedSample(ch1, ch2, clk)` is called once per input sample — per ADC read on
hardware, per audio sample in Rack — and the engine decimates / captures
internally. The timebase knob became a sample-decimation factor. This unifies both
targets and is strictly cleaner than the busy-wait original.

### `fix_fft`

Vendored into `lib/fixfft.cpp` (PROGMEM/AVR access stripped) and `#include`d from
`scope.hpp`, so the same source compiles on RP2040 and in the Rack engine with no
external dependency — mirroring how ClockForge vendors `quantizer.cpp`/`scales.cpp`.

### VCV specifics

- **Inputs**: CLK/Trigger, CV1, CV2. **Outputs**: OUT1/OUT2 = buffered pass-through
  of CV1/CV2 (oscilloscope "through"). The engine produces the display; the module
  wires the pass-through directly (no DAC shim needed).
- Engine runs at control rate (`ENGINE_DECIM = 2`, ~22 kHz acquisition); the OLED
  re-renders at ~60 Hz.
- Context menu mirrors the firmware menu (Mode / Timebase / Par2) plus host-side
  conveniences (Input CV Range, Encoder Sensitivity). Scope state persists with the
  patch via `dataToJson`/`dataFromJson`.

## Building

**Firmware** (from repo root):

```bash
pio run -e xiao_rp2040        # build; double-tap reset to flash the UF2
```

**VCV plugin** (needs the VCV Rack SDK + a GCC toolchain; on Windows, MSYS2
MinGW64):

```bash
export PATH="/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH"
cd vcv-plugin
make RACK_DIR="<path-to>/Rack-SDK"                    # -> plugin.dll
make RACK_DIR="<path-to>/Rack-SDK" RACK_USER_DIR="<Rack2>" install
```

**Host isolation/render test** (no Rack needed):

```bash
cd vcv-plugin && test/build_isolation_test.sh
```

## Panel art

`vcv-plugin/res/ForgeView.svg` is a clean lightweight placeholder panel. If you
have the real hardware panel export, drop it in at that path (keep the widget mm
coordinates in `ForgeView.cpp` in sync with the cutouts).
