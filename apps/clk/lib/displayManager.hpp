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
 *
 * Performance target: Reduce display impact from 138ms avg to <20ms avg
 */

class DisplayManager {
  private:
    Adafruit_SSD1306 &display;

    // Timing control
    unsigned long lastUpdateTime = 0;
    unsigned long lastInteractionTime = 0;
    static const unsigned long UPDATE_INTERVAL_MS = 50; // 20Hz max refresh
    unsigned long timeoutMs = 5000;                     // Return to main screen (0 = disabled)

    // State tracking
    bool isDirty = false;
    bool unsavedChanges = false;

  public:
    DisplayManager(Adafruit_SSD1306 &disp) : display(disp) {}

    // Mark display as needing update (cheap call) — does NOT reset the interaction
    // timer, so the screen-timeout clock keeps ticking. Use for clock/ISR-driven updates.
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
        return isDirty && (now - lastUpdateTime >= UPDATE_INTERVAL_MS);
    }

    // Set the menu timeout duration (0 = disabled)
    void SetMenuTimeout(unsigned long ms) { timeoutMs = ms; }

    // Check if should timeout to main screen
    bool ShouldTimeout(int menuItem, int menuMode) {
        if (timeoutMs == 0)
            return false;
        if (menuItem == 1 || menuItem == 2 || menuMode != 0) {
            return false;
        }
        return (millis() - lastInteractionTime > timeoutMs);
    }

    // Begin frame - clears display and prepares for drawing
    void BeginFrame() {
        display.clearDisplay();
    }

    // Reset the rate-limiter timer without clearing isDirty.
    // Use when you want to defer flushing but ensure ShouldUpdate() re-triggers
    // after the next UPDATE_INTERVAL_MS without hammering immediately.
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
    // Call this once per frame when you actually intend to flush (i.e. before display.display()).
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
    // Returns true if display was actually transmitted (for metrics)
    bool EndFrame() {
        PrepareFrame();

        // This is the slow part (28ms) — blocks the calling core for the entire transfer
        display.display();

        return true; // Display was transmitted
    }

    // Helper: Draw menu position indicator
    void DrawMenuIndicator(int menuItem, int totalItems) {
        if (menuItem == 1 || menuItem == 2) {
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
