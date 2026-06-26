#pragma once
// Clean POD/opaque API to the ClockForge firmware engine.
// This is the ONLY header the Rack-facing module includes — it deliberately
// exposes no Arduino/firmware types so it can coexist with rack.hpp.
#include <cstdint>
#include <string>

namespace cfengine {

struct Engine; // opaque

Engine *createEngine();
void destroyEngine(Engine *);

// Advance the engine by dt seconds.
//   cvVolts[2]    : the two CV input voltages (0..5 V nominal).
//   clockGateHigh : external clock input level (rising edges drive ext sync).
//   outVolts[4]   : filled with the four output voltages (0..5 V nominal).
void process(Engine *, float dt, const float cvVolts[2], bool clockGateHigh, float outVolts[4]);

// Encoder rotation in detents (+clockwise / -counter-clockwise).
void encoderTurn(Engine *, int detents);
// Encoder push-button level; the engine detects press/release edges.
void encoderButton(Engine *, bool pressed);

// Copy the 128x64 monochrome framebuffer (1bpp, row-major, MSB-first = 1024 bytes).
void getFramebuffer(Engine *, uint8_t out[1024]);

// Persistence: the firmware EEPROM blob (presets + calibration) as raw bytes.
std::string serialize(Engine *);
void deserialize(Engine *, const std::string &);

// Current BPM (for tooltips / future param display).
int bpm(Engine *);

} // namespace cfengine
