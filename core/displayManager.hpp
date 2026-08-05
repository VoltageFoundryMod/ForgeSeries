#ifndef DISPLAY_MANAGER_HPP
#define DISPLAY_MANAGER_HPP

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>

/**
 * Display Manager - Non-blocking OLED display handler
 *
 * Solves the critical timing issue where display.display() blocks for 138-403ms.
 *
 * Strategy:
 * 1. Separate buffer preparation from I2C transmission
 * 2. Dirty flag to skip updates when nothing changed
 * 3. Rate limiting to prevent excessive updates
 * 4. Minimal overhead when display is idle
 */

class DisplayManager {
  private:
    Adafruit_SSD1306 &display;

    // Timing control
    unsigned long lastUpdateTime = 0;
    unsigned long lastInteractionTime = 0;
    static const unsigned long UPDATE_INTERVAL_MS = 50; // 20Hz max refresh
    // Live rate limit. A member rather than the constant above so a module whose
    // home screen is an ANIMATION can ask for a faster one — see
    // SetUpdateInterval(). It has to be settable at runtime rather than by a
    // build flag: the unified firmware links one shared DisplayManager, so a
    // per-translation-unit macro would give two TUs different definitions of the
    // same class.
    unsigned long updateIntervalMs = UPDATE_INTERVAL_MS;
    unsigned long timeoutMs = 5000;                     // Return to home screen (0 = disabled)

    // State tracking
    bool isDirty = false;
    bool unsavedChanges = false;

  public:
    DisplayManager(Adafruit_SSD1306 &disp) : display(disp) {}

    // Mark display as needing update (cheap call) — does NOT reset the interaction
    // timer, so the screen-timeout clock keeps ticking. Use for note-change updates.
    void MarkDirty() {
        isDirty = true;
    }

    // Mark display dirty AND record a user interaction (resets screen-timeout timer).
    // Use for encoder turns, button presses, menu changes, etc.
    void MarkInteraction() {
        isDirty = true;
        lastInteractionTime = millis();
    }

    // Set unsaved changes indicator
    void SetUnsavedChanges(bool hasChanges) {
        if (unsavedChanges != hasChanges) {
            unsavedChanges = hasChanges;
            MarkDirty(); // state change only — do not reset interaction timer
        }
    }

    // Check if display needs updating based on rate limiting
    bool ShouldUpdate() {
        unsigned long now = millis();
        return isDirty && (now - lastUpdateTime >= updateIntervalMs);
    }

    // Minimum interval between redraws, in ms. The 50 ms default suits a menu,
    // where a frame only follows an encoder detent; a module whose home screen
    // animates continuously can ask for less.
    //
    // The cost is Core 1's duty cycle and nothing else: a full 1 KB flush at the
    // 1 MHz display bus takes ~9 ms, Core 1 has no other work, and the DAC lives
    // on the other bus behind Core 0 — so a faster screen cannot slow the
    // outputs down. If a core cannot keep up it simply renders late.
    void SetUpdateInterval(unsigned long ms) { updateIntervalMs = ms ? ms : 1; }

    // Set the menu timeout duration (0 = disabled)
    void SetMenuTimeout(unsigned long ms) { timeoutMs = ms; }

    // Should the UI drop back to the home screen?
    // Never times out while already home or while a value is being edited.
    bool ShouldTimeout(bool onHomePage, int menuMode) {
        if (timeoutMs == 0 || onHomePage || menuMode != 0) {
            return false;
        }
        return (millis() - lastInteractionTime > timeoutMs);
    }

    // Begin frame - clears display and prepares for drawing
    void BeginFrame() {
        display.clearDisplay();
    }

    // Reset the rate-limiter timer without clearing isDirty.
    void TouchUpdateTimer() {
        lastUpdateTime = millis();
    }

    // Draw overlay indicators (unsaved dot) into current buffer.
    // Does NOT touch rate-limiter state — safe to call when deciding whether to flush.
    void DrawOverlays() {
        if (unsavedChanges) {
            display.fillCircle(1, 1, 1, WHITE);
        }
    }

    // Commit the frame: update rate-limiter state so ShouldUpdate() resets correctly.
    void CommitFrame() {
        lastUpdateTime = millis();
        isDirty = false;
    }

    // Prepare frame: draw housekeeping overlays and update timing state.
    // Does NOT call display.display(). On RP2040 Core 0 calls this, then sets
    // _displayFrameReady = true so Core 1 can call display.display() without I2C
    // contention with the rest of Core 0.
    bool PrepareFrame() {
        DrawOverlays();
        CommitFrame();
        return true;
    }

    // On RP2040, prefer PrepareFrame() + let Core 1 call display.display().
    bool EndFrame() {
        PrepareFrame();
        display.display();
        return true;
    }

    // Helper: Draw the menu scroll-position indicator down the right edge.
    // Suppressed on the home screen, which uses the full width for the keyboards.
    void DrawMenuIndicator(int menuItem, int totalItems, bool onHomePage) {
        if (onHomePage) {
            return;
        }
        display.drawLine(127, 0, 127, 63, WHITE);
        display.drawRect(125, map(menuItem, 1, totalItems, 0, 62), 3, 3, WHITE);
    }

    // Helper: Draw centered header
    void DrawMenuHeader(const char *header) {
        display.setTextSize(1);
        int headerLength = (strlen(header) * 6) + 24;
        display.setCursor((128 - headerLength) / 2, 1);
        display.println("- " + String(header) + " -");
    }

    // Helper: Draw selection triangle
    void DrawTriangle(int x, int y, bool filled) {
        if (filled) {
            display.fillTriangle(x, y, x, y + 8, x + 4, y + 4, WHITE);
        } else {
            display.drawTriangle(x, y, x, y + 8, x + 4, y + 4, WHITE);
        }
    }

    // Direct access to display for complex drawing
    Adafruit_SSD1306 &GetDisplay() {
        return display;
    }
};

#endif // DISPLAY_MANAGER_HPP
