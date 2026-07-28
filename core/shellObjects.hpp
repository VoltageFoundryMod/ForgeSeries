#pragma once

// shellObjects.hpp — the board-owned singletons.
//
// There is one display, one encoder and one calibration per module, no matter
// how many firmwares are linked into the image. In a standalone build the app's
// main.cpp defines them; in the unified build the shell does. Either way these
// declarations name the same objects.
//
// ── Why this is a separate header ───────────────────────────────────────────
// It exists to be included at GLOBAL scope, before an app TU opens its
// namespace. core/menuDisplay.hpp used to declare `extern Adafruit_SSD1306
// display;` alongside its app-facing hooks (RedrawDisplay, MenuHeader), and
// those two pull in opposite directions once apps are namespaced:
//
//   * the hooks must resolve INSIDE forge::<app> — each app supplies its own;
//   * `display` must resolve to the ONE global.
//
// Included inside the namespace, `display` would have become a second per-app
// object with its own 1 KB framebuffer — a link-time success and a runtime mess,
// with two cores drawing into different buffers behind one panel. Splitting the
// board-owned names out means menuDisplay.hpp carries only app hooks and is safe
// to include from inside a namespace, while these stay global.
//
// Apps never define these. They reach the display through IApp::Tick1's argument
// and the encoder through IApp's event calls; this header is for the shared
// headers that still refer to them by name.

#include <Adafruit_SSD1306.h>

#include "calibrationData.hpp"
#include "displayManager.hpp"
#include "encoder.hpp"

// The OLED. Owned by Core 1 at runtime (Wire / I2C1).
extern Adafruit_SSD1306 display;

// Non-blocking refresh/timeout bookkeeping around `display`.
extern DisplayManager displayMgr;

// The rotary encoder. Polled by Core 0; in the unified build only the shell
// reads it, and forwards events, so nothing races the detent state.
extern Encoder encoder;

// Board calibration: CV input coefficients and DAC output correction. Describes
// the hardware, not the module, which is why one instance serves every app.
extern CalibrationData cal;
