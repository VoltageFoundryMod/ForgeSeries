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
    Adafruit_SSD1306& display;

    // Timing control
    unsigned long lastUpdateTime = 0;
    unsigned long lastInteractionTime = 0;
    static const unsigned long UPDATE_INTERVAL_MS = 50;  // 20Hz max refresh
    static const unsigned long TIMEOUT_MS = 7000;  // Return to main screen

    // State tracking
    bool isDirty = false;
    bool unsavedChanges = false;

public:
    DisplayManager(Adafruit_SSD1306& disp) : display(disp) {}

    // Mark display as needing update (cheap call)
    void MarkDirty() {
        isDirty = true;
        lastInteractionTime = millis();
    }

    // Set unsaved changes indicator
    void SetUnsavedChanges(bool hasChanges) {
        if (unsavedChanges != hasChanges) {
            unsavedChanges = hasChanges;
            MarkDirty();
        }
    }

    // Check if display needs updating based on rate limiting
    bool ShouldUpdate() {
        unsigned long now = millis();
        return isDirty && (now - lastUpdateTime >= UPDATE_INTERVAL_MS);
    }

    // Check if should timeout to main screen
    bool ShouldTimeout(int menuItem, int menuMode) {
        if (menuItem == 1 || menuItem == 2 || menuMode != 0) {
            return false;
        }
        return (millis() - lastInteractionTime > TIMEOUT_MS);
    }

    // Begin frame - clears display and prepares for drawing
    void BeginFrame() {
        display.clearDisplay();
    }

    // End frame - performs the actual I2C transmission
    // Returns true if display was actually transmitted (for metrics)
    bool EndFrame() {
        // Draw unsaved changes indicator
        if (unsavedChanges) {
            display.fillCircle(1, 1, 1, WHITE);
        }

        // This is the slow part (43-403ms) - measure only this I2C call
        display.display();

        lastUpdateTime = millis();
        isDirty = false;
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
    void DrawMenuHeader(const char* header) {
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
    Adafruit_SSD1306& GetDisplay() {
        return display;
    }
};

#endif // DISPLAY_MANAGER_HPP
