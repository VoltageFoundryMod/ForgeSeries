#pragma once
// ============================================================
// menuDisplay.hpp — Composable display primitives for the menu
// system.  All functions operate on the global `display` object
// and the global `menuItem` / `menuMode` state from main.cpp.
//
// Included once from main.cpp (after menuDefinitions.hpp and
// menuHandlers.hpp).
// ============================================================

// Forward declarations for main.cpp globals used by this header.
// The actual definitions are in main.cpp (included before this file).
extern Adafruit_SSD1306 display;
extern void RedrawDisplay();
extern void MenuHeader(const char* header);

// ── Layout constants ─────────────────────────────────────────
static const int MD_ROW_H   = 9;    // pixels per row
static const int MD_START_Y = 20;   // first row Y (below header)
static const int MD_LABEL_X = 10;   // left edge for row labels
static const int MD_VALUE_X = 70;   // default single-column value X
static const int MD_CURSOR_X = 1;   // cursor triangle left edge

// Cursor triangle: hollow = selected, filled = editing
static inline void _MD_Cursor(int y, bool sel, bool editing) {
    if (!sel) return;
    if (editing)
        display.fillTriangle(MD_CURSOR_X, y - 1, MD_CURSOR_X, y + 7, MD_CURSOR_X + 4, y + 3, 1);
    else
        display.drawTriangle(MD_CURSOR_X, y - 1, MD_CURSOR_X, y + 7, MD_CURSOR_X + 4, y + 3, 1);
}

// Draw header + reset internal row counter.  Returns starting Y.
static int _md_rowY;
static inline void MD_PageBegin(const char* title, int startY = MD_START_Y) {
    display.setTextSize(1);
    MenuHeader(title);
    _md_rowY = startY;
}

// Single-column row: label at MD_LABEL_X, value at MD_VALUE_X.
static inline void MD_Row(const char* lbl, String val, bool hasVal,
                           bool sel, bool editing) {
    display.setCursor(MD_LABEL_X, _md_rowY);
    display.print(lbl);
    if (hasVal) {
        display.setCursor(MD_VALUE_X, _md_rowY);
        display.print(val);
    }
    _MD_Cursor(_md_rowY, sel, editing);
    _md_rowY += MD_ROW_H;
}

// Single-column row with a custom value X.
static inline void MD_RowAtX(const char* lbl, String val, bool hasVal, int valX,
                              bool sel, bool editing) {
    display.setCursor(MD_LABEL_X, _md_rowY);
    display.print(lbl);
    if (hasVal) {
        display.setCursor(valX, _md_rowY);
        display.print(val);
    }
    _MD_Cursor(_md_rowY, sel, editing);
    _md_rowY += MD_ROW_H;
}

// Action row: label only, no value.
static inline void MD_ActionRow(const char* lbl, bool sel) {
    display.setCursor(MD_LABEL_X, _md_rowY);
    display.print(lbl);
    if (sel)
        display.drawTriangle(MD_CURSOR_X, _md_rowY - 1, MD_CURSOR_X, _md_rowY + 7, MD_CURSOR_X + 4, _md_rowY + 3, 1);
    _md_rowY += MD_ROW_H;
}

// Two-column header line (drawn at _md_rowY, does NOT advance _md_rowY).
static inline void MD_TwoColHeader(const char* c1, const char* c2, int x1, int x2) {
    display.setCursor(x1, _md_rowY);
    display.print(c1);
    display.setCursor(x2, _md_rowY);
    display.print(c2);
}

// Two-column row: label, val1 at x1, val2 at x2.
static inline void MD_TwoColRow(const char* lbl, String v1, bool hasV1,
                                 String v2, bool hasV2,
                                 int x1, int x2, bool sel, bool editing) {
    display.setCursor(MD_LABEL_X, _md_rowY);
    display.print(lbl);
    if (hasV1) { display.setCursor(x1, _md_rowY); display.print(v1); }
    if (hasV2) { display.setCursor(x2, _md_rowY); display.print(v2); }
    _MD_Cursor(_md_rowY, sel, editing);
    _md_rowY += MD_ROW_H;
}

// Blank gap (just advances _md_rowY without printing anything).
static inline void MD_Gap(int px = MD_ROW_H) {
    _md_rowY += px;
}

// Finish a page — calls RedrawDisplay().
static inline void MD_PageEnd() {
    RedrawDisplay();
}

// ── Generic group renderer ────────────────────────────────────
// Renders all items that share MENU_ITEMS[menuItem-1].group using
// their rowStyle / valueFn / valueFn2 / label fields.
// Caller is responsible for any special-case overlays (e.g. the
// Euclidean pattern grid) that must be drawn after this call.
//
// Returns true if the page was rendered generically; false if
// it was skipped (the item belongs to the special groups 0 or 5
// where a fully custom renderer takes over).
static bool MD_RenderGroup(const char* title,
                            int curItem,   // 1-based menuItem
                            int curMode,   // menuMode
                            int groupId,
                            int startY = MD_START_Y) {
    MD_PageBegin(title, startY);
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        const MenuItem& mi = MENU_ITEMS[i];
        if (mi.group != groupId) continue;
        int idx = i + 1; // 1-based item number
        bool sel  = (idx == curItem);
        bool edit = sel && (curMode == idx);

        switch (mi.rowStyle) {
            case ROW_ACTION:
                MD_ActionRow(mi.label, sel);
                break;
            case ROW_TWOCOL:
                MD_TwoColRow(mi.label,
                             mi.valueFn  ? mi.valueFn()  : String(""), mi.valueFn  != nullptr,
                             mi.valueFn2 ? mi.valueFn2() : String(""), mi.valueFn2 != nullptr,
                             mi.col1x, mi.col2x, sel, edit);
                break;
            case ROW_HIDDEN:
                break;
            case ROW_SINGLE:
            default:
                MD_RowAtX(mi.label,
                          mi.valueFn ? mi.valueFn() : String(""), mi.valueFn != nullptr,
                          mi.col1x, sel, edit);
                break;
        }
    }
    return true;
}
