#pragma once

// ── Page layout ──────────────────────────────────────────────────────────────
// ClockForge's generic group pages start lower than the other modules' (20 vs
// the shared default of 12). Set here because menuDefinitions.hpp is included
// before core/menuDisplay.hpp on both the firmware and VCV paths, and that
// header takes this as an #ifndef override.
//
// NOTE: at 20, with 9px rows, a sixth row lands at y=65 and is clipped off the
// 64px screen. Several groups here hold six to eight items, but most of those
// are drawn by custom renderers that pass their own startY at the
// MD_PageBegin call site; only groups falling through to the generic
// MD_RenderGroup path use this value. Whether any of those actually renders a
// sixth visible row has NOT been verified - ROW_HIDDEN items are skipped, so
// the item count is an upper bound on the row count. Unchanged behaviour from
// before core/menuDisplay.hpp was shared; worth an eyeball on hardware.
#define MD_START_Y 20

// ============================================================
// Menu item descriptor — type enum and struct.
// The MENU_ITEMS[] array and all setter/action implementations
// live in menuHandlers.hpp (included after this from main.cpp).
// ============================================================

enum MenuItemType : uint8_t {
    MENU_ACTION = 0, // Click fires action() immediately — no edit mode entered
    MENU_TOGGLE,     // Click fires action() immediately — no edit mode entered
    MENU_EDIT,       // Click enters edit mode; encoder rotate calls setter(±delta)
};

// How the item is rendered on-screen.
enum RowStyle : uint8_t {
    ROW_SINGLE, // label left, value right-aligned at VALUE_X
    ROW_TWOCOL, // two values rendered at col1x / col2x (used for swing, lvl/off, CV attn)
    ROW_ACTION, // label only, no value (tap tempo, save, load)
    ROW_HIDDEN, // item belongs to a page but has no dedicated row (e.g. BPM page)
};

struct MenuItem {
    const char *label;    // Left-side display text (static string)
    String (*valueFn)();  // Right-side / col1 value string (nullptr = no value)
    String (*valueFn2)(); // Second column value (ROW_TWOCOL only; nullptr otherwise)
    uint8_t col1x;        // X pixel for valueFn  (meaningful for ROW_TWOCOL)
    uint8_t col2x;        // X pixel for valueFn2 (meaningful for ROW_TWOCOL)
    uint8_t group;        // Items sharing the same group render on one page
    RowStyle rowStyle;
    MenuItemType type;
    void (*setter)(int delta); // MENU_EDIT: called with ±(int)speedFactor on rotate
    void (*action)();          // MENU_ACTION / MENU_TOGGLE: called on click
};

// Defined in menuHandlers.hpp (included once, from main.cpp).
extern const MenuItem MENU_ITEMS[];
extern const int MENU_ITEM_COUNT;
