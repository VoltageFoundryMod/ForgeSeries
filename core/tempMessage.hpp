#pragma once

// tempMessage.hpp — the brief full-screen overlay ("SAVED", "LOADED").
//
// Identical in NoteForge and GravityForge; ClockForge's differed only by
// missing an x clamp and not polling CV during the wait. This is that shared
// version.
//
// ── Include INSIDE the app's namespace ──────────────────────────────────────
// It reads names the app owns, and in the unified firmware each app has its own
// set. Included at global scope there, one inline definition would be bound to
// three different sets of statics — an ODR violation. Namespaced, each app gets
// its own function, the same way core/menuDisplay.hpp works.
//
// REQUIRES, already declared by the including TU:
//   display, SCREEN_WIDTH, SCREEN_HEIGHT
//   _displayLocked, _displayFrameReady   (the Core 0 / Core 1 handshake)
//   HandleCVInputs(), HandleOutputs()    (kept running so held notes survive)
//   REQUEST_DISPLAY_REFRESH()
//
// NOT used by the VCV Rack ports: blocking for a second inside Rack's audio
// thread is not an option, so they stash the message and draw it as an overlay.

#include <Arduino.h>
#include <cstring>

inline void ShowTemporaryMessage(const char *msg, uint32_t durationMs) {
    _displayLocked = true;
    delay(10); // let Core 1 finish any in-flight HandleDisplay()

    display.clearDisplay();
    display.setTextSize(2);
    // Clamped: a message wider than the screen gives a negative x, which would
    // otherwise start drawing off the left edge.
    const int x = (SCREEN_WIDTH - (int)strlen(msg) * 12) / 2;
    display.setCursor(x < 0 ? 0 : x, SCREEN_HEIGHT / 2 - 8);
    display.print(msg);
    _displayFrameReady = true; // Core 1 flushes over Wire; Core 0 never touches it

    // Keep the engine running for the whole message, so held notes do not drop
    // out and CV modulation does not freeze.
    const uint32_t start = millis();
    while (millis() - start < durationMs) {
        HandleCVInputs();
        HandleOutputs();
    }

    _displayLocked = false;    // resume normal Core 1 rendering
    REQUEST_DISPLAY_REFRESH(); // force a clean redraw on the way out
}
