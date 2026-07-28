#pragma once
// forgevcv::IEngine — the abstract contract between a ForgeSeries firmware and
// the reusable VCV Rack layer.
//
// Each firmware ports its DSP/menu/render code into a VCV plugin via an Arduino
// shim (see ../../shim/) and exposes it to Rack through a small POD/opaque bridge
// that never leaks Arduino types into rack.hpp-facing code. This interface is
// that bridge, virtualized: the reusable base module (ForgeModule) drives a
// firmware through these six calls without knowing the concrete engine type.
//
// A firmware's engine translation unit implements this interface (typically by
// wrapping its existing free-function bridge) and hands a fresh instance to its
// ForgeModule subclass, which owns and deletes it.
//
// Any richer, module-specific parameter access used by a plugin's context menu
// (tempo, waveform, divider, ...) stays in that firmware's own header — it is
// deliberately NOT part of this shared interface.

#include <cstdint>
#include <string>

namespace forgevcv {

struct IEngine {
    virtual ~IEngine() = default;

    // Advance the engine by dt seconds.
    //   cv[nCv]     : CV input voltages already mapped into the engine's domain
    //                 (0..5 V nominal); see ForgeModule::mapCvInput.
    //   clockHigh   : external clock input level (rising edges drive ext sync).
    //   out[nOut]   : filled with output voltages (0..5 V nominal).
    virtual void process(float dt, const float *cv, int nCv,
                         bool clockHigh, float *out, int nOut) = 0;

    // Encoder rotation in detents (+clockwise / -counter-clockwise).
    virtual void encoderTurn(int detents) = 0;
    // Encoder push-button level; the engine detects press/release edges.
    virtual void encoderButton(bool pressed) = 0;

    // Copy the 128x64 monochrome framebuffer (1bpp, row-major, MSB-first).
    virtual void getFramebuffer(uint8_t out[1024]) = 0;

    // Persistence: the firmware's EEPROM blob (presets + calibration) as bytes.
    //
    // IMPORTANT: serialize() must first commit the *live* firmware state into the
    // blob. Firmwares only touch EEPROM on an explicit SAVE, so an engine that
    // merely dumps the buffer hands Rack a blob that does not contain anything
    // the user changed, and the patch reloads at factory defaults.
    virtual std::string serialize() = 0;
    virtual void deserialize(const std::string &) = 0;

    // Rack's "Initialize" (Ctrl+I) and "Randomize" (Ctrl+R) module actions.
    // Default to no-ops so an engine can adopt them independently; ForgeModule
    // calls them after Rack has reset/randomized the module's own params.
    //
    // reset() restores the firmware's factory defaults. randomize() should touch
    // only the sound-shaping parameters — leave I/O routing, jack roles and CV
    // assignments alone, the same way Rack leaves ports alone.
    virtual void reset() {}
    virtual void randomize() {}
};

} // namespace forgevcv
