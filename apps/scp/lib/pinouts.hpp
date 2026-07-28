#pragma once

// pinouts.hpp — board wiring, plus this app's jack semantics.
//
// The wiring itself is shared by every ForgeSeries module and lives in
// core/boardPinouts.hpp. This module treats the four outputs as four
// independent jacks and adds nothing to it.

#include "boardPinouts.hpp"

// ForgeView redraws the whole framebuffer continuously, so the display bus is
// the refresh-rate bottleneck. The SSD1306 is specified for 400 kHz but runs
// reliably at 1 MHz on this board, which is what makes the scope feel live.
// Only the display bus — the MCP4728 must stay at 400 kHz (see core/boardIO.hpp).
#define FORGE_DISPLAY_I2C_HZ 1000000
