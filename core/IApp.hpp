#pragma once

// IApp.hpp — the contract between the shell and a ForgeSeries firmware.
//
// In the unified firmware one shell owns the board and exactly one app runs at
// a time. The shell brings up the hardware, draws the boot menu, owns
// calibration, and then drives the selected app through this interface.
//
// ── Why not reuse forgevcv::IEngine? ────────────────────────────────────────
// The VCV interface (vcvlib/include/forgevcv/IEngine.hpp) looks similar but is
// pull-based: Rack hands it a dt and a CV array and asks for a framebuffer. That
// is the right shape for a host that owns the clock and the I/O. On hardware the
// app owns neither — it reads the ADC and writes the DAC itself, on a specific
// core, at whatever rate it likes, and any indirection there costs latency in
// the audio path. So an app implements both: this on hardware, and its existing
// fw_engine.cpp adapter for Rack. Two thin adapters over one app core.
//
// ── Threading ───────────────────────────────────────────────────────────────
// Tick0 and Tick1 run on different cores, concurrently, forever. The split is
// the one every ForgeSeries firmware already uses:
//
//   Core 0 (Tick0): encoder, ADC, DAC       — owns Wire1
//   Core 1 (Tick1): GFX render + flush      — owns Wire
//
// They touch separate I2C blocks, which is why neither needs a mutex. An app
// that shares state across the two is responsible for that state itself; the
// shell guarantees only that Begin() has returned before either tick is called.
//
// The shell owns `display` and `encoder` and passes encoder events in, so an app
// must not poll the encoder pins itself — the shell needs those events too (to
// catch the "return to menu" gesture) and two readers would race the detent
// state.

#include <Adafruit_SSD1306.h>
#include <stdint.h>

namespace forge {

struct IApp {
    virtual ~IApp() = default;

    // Shown in the boot menu. Keep Name() short enough for a 128px line.
    virtual const char *Name() const = 0;
    virtual const char *Version() const = 0;

    // One-time init, on Core 0. The shell has already run InitWire(), InitIO(),
    // display.begin() and InitDAC(), and has loaded calibration, so this is for
    // app state only — restoring settings, building tables, seeding the engine.
    virtual void Begin() = 0;

    // Core 0 hot loop: sample inputs, run the engine, write the DAC.
    virtual void Tick0() = 0;

    // Core 1 loop: draw into `display` and flush it. Called continuously; an app
    // that does not need a new frame every pass should rate-limit itself rather
    // than block, since the shell has no other work to give this core.
    virtual void Tick1(Adafruit_SSD1306 &display) = 0;

    // Encoder events, delivered from Core 0 after the shell has consumed the
    // menu gesture. `detents` is +clockwise / -counter-clockwise.
    virtual void EncoderTurn(int detents) = 0;
    virtual void EncoderButton(bool pressed) = 0;

    // Called before the shell switches away, so an app can flush anything it
    // owes to storage. The shell reboots into the new app afterwards, so there
    // is no requirement to return to a clean in-memory state.
    virtual void End() {}
};

} // namespace forge
