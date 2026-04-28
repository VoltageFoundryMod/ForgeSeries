#pragma once
// ============================================================
// menuRender.hpp — High-level menu rendering: MenuIndicator,
// MenuHeader, and HandleDisplay().
//
// Included from main.cpp after menuDisplay.hpp.  Relies on
// macros (SCREEN_WIDTH, REQUEST_DISPLAY_REFRESH, NUM_OUTPUTS)
// and types (DisplayManager, Output) that are defined earlier
// in the main.cpp translation unit.
// ============================================================

// ── Globals owned by main.cpp ────────────────────────────────
extern DisplayManager displayMgr;
extern bool           displayRefresh;
extern bool           unsavedChanges;
extern int            menuItem;
// menuMode is already declared extern in menuHandlers.hpp
extern bool           masterState;
extern int            euclideanOutputSelect;
extern Output         outputs[];

// ── Thin display helpers ─────────────────────────────────────
inline void MenuIndicator() {
    displayMgr.DrawMenuIndicator(menuItem, MENU_ITEM_COUNT);
}

inline void MenuHeader(const char* header) {
    displayMgr.DrawMenuHeader(header);
}

// ── Main display renderer ─────────────────────────────────────
void HandleDisplay() {
    // Sync unsaved changes state to display manager
    displayMgr.SetUnsavedChanges(unsavedChanges);

    // Check for timeout to main screen
    if (displayMgr.ShouldTimeout(menuItem, menuMode)) {
        menuItem = 2;
        menuMode = 0;
        REQUEST_DISPLAY_REFRESH();
    }

    // Only refresh the display if needed and rate-limited
    if (displayRefresh && displayMgr.ShouldUpdate()) {
        // Begin frame prepares the buffer (fast, no I2C)
        displayMgr.BeginFrame();
        MenuIndicator();

        uint8_t grp = MENU_ITEMS[menuItem - 1].group;

        // ── Group 0: Main screen (BPM + play/stop + output boxes) ─────────
        if (grp == 0) {
            String s = String(BPM) + "BPM";
            display.setTextSize(3);
            display.setCursor((SCREEN_WIDTH - (s.length() * 18)) / 2, 0);
            display.print(s);
            if (usingExternalClock) {
                display.setTextSize(1);
                if (extClockBlinkState) {
                    // Phase A: filled box, blank (inverted) "E"
                    display.fillRect(118, 23, 10, 11, WHITE);
                    display.setTextColor(BLACK);
                    display.setCursor(121, 25);
                    display.print("E");
                    display.setTextColor(WHITE);
                } else {
                    // Phase B: outline box, filled "E"
                    display.drawRect(118, 23, 10, 11, WHITE);
                    display.setCursor(121, 25);
                    display.print("E");
                }
            }
            if (menuMode == 0 && menuItem == 1) {
                display.drawTriangle(2, 6, 2, 14, 6, 10, 1);
            } else if (menuMode == menuItem) {
                display.fillTriangle(2, 6, 2, 14, 6, 10, 1);
            }
            if (menuMode >= 0 && menuMode <= 2) {
                display.setTextSize(2);
                display.setCursor(44, 27);
                if (menuItem == 2) {
                    display.drawLine(43, 42, 88, 42, 1);
                }
                if (!masterState) {
                    display.fillRoundRect(23, 26, 17, 17, 2, 1);
                    display.print("STOP");
                } else {
                    display.fillTriangle(23, 26, 23, 42, 39, 34, 1);
                    display.print("PLAY");
                }
            }
            display.setTextSize(1);
            for (int i = 0; i < NUM_OUTPUTS; i++) {
                if (!outputs[i].GetOutputState()) {
                    // Output disabled: show number only, no box
                    display.setCursor((i * 30) + 16, 47);
                    display.setTextColor(WHITE);
                    display.print(i + 1);
                } else if (outputs[i].GetBlinkState()) {
                    // Blink phase A: filled box + inverted number
                    display.fillRect((i * 30) + 14, 46, 9, 9, WHITE);
                    display.setCursor((i * 30) + 16, 47);
                    display.setTextColor(BLACK);
                    display.print(i + 1);
                    display.setTextColor(WHITE);
                } else {
                    // Blink phase B: outline box + white number
                    display.drawRect((i * 30) + 14, 46, 9, 9, WHITE);
                    display.setCursor((i * 30) + 16, 47);
                    display.setTextColor(WHITE);
                    display.print(i + 1);
                }
                display.setTextColor(WHITE);
                String d = outputs[i].GetDividerDescription();
                display.setCursor((i * 30) + 13 + (6 - (d.length() * 3)), 56);
                display.print(d);
            }
            RedrawDisplay();
            return;
        }

        // ── Groups 1–12: data-driven generic renderer ─────────────────────
        static const char* const groupTitles[] = {
            "",                  // 0 = custom above
            "CLOCK DIVIDERS",    // 1
            "OUTPUT STATE",      // 2
            "PROBABILITY",       // 3
            "EUCLIDEAN RHYTHM",  // 4
            "OUTPUT SWING",      // 5
            "PHASE SHIFT",       // 6
            "DUTY CYCLE",        // 7  (duty)
            "WAVEFORM SETTINGS", // 8  (waveforms - all 4)
            "ENVELOPE SETTINGS", // 9
            "CV INPUT TARGETS",  // 10
            "QUANTIZE SETTINGS", // 11
            "",                  // 12 (settings — rendered below)
            "OUTPUT SETTINGS",   // 13 (level/offset - all 4)
        };

        // ── Group 4: Euclidean — custom overlay (pattern grid) ─────────────
        if (grp == 4) {
            MD_PageBegin("EUCLIDEAN RHYTHM", 20);
            for (int i = 0; i < MENU_ITEM_COUNT; i++) {
                const MenuItem& mi = MENU_ITEMS[i];
                if (mi.group != 4) continue;
                int idx = i + 1;
                bool sel  = (idx == menuItem);
                bool edit = sel && (menuMode == idx);
                // Items 20 (ROT) and 21 (PAD) share a row; draw ROT on left, PAD at x=64
                if (idx == 32) { // ROT — left half
                    display.setCursor(MD_LABEL_X, _md_rowY);
                    display.print(mi.label);
                    if (mi.valueFn) {
                        display.setCursor(64, _md_rowY);
                        display.print(mi.valueFn());
                    }
                    _MD_Cursor(_md_rowY, sel, edit);
                    // Don't advance — PAD shares this row
                } else if (idx == 33) { // PAD — right half
                    display.setCursor(64, _md_rowY);
                    display.print(mi.label);
                    if (mi.valueFn) {
                        display.setCursor(64 + 5*6, _md_rowY);
                        display.print(mi.valueFn());
                    }
                    if (sel) {
                        int cx = 64 - 8;
                        if (edit)
                            display.fillTriangle(cx, _md_rowY-1, cx, _md_rowY+7, cx+4, _md_rowY+3, 1);
                        else
                            display.drawTriangle(cx, _md_rowY-1, cx, _md_rowY+7, cx+4, _md_rowY+3, 1);
                    }
                    _md_rowY += MD_ROW_H;
                } else {
                    MD_RowAtX(mi.label, mi.valueFn ? mi.valueFn() : String(""), mi.valueFn != nullptr, 64, sel, edit);
                }
            }
            // Pattern grid overlay (top-right) when euclidean enabled
            if (outputs[euclideanOutputSelect].GetEuclidean()) {
                display.fillTriangle(90, 10, 94, 10, 92, 14, WHITE);
                int py = 15;
                int euclideanSteps   = outputs[euclideanOutputSelect].GetEuclideanSteps();
                int euclideanPadding = outputs[euclideanOutputSelect].GetEuclideanPadding();
                for (int i = 0; i < euclideanSteps + euclideanPadding && i < 47; i++) {
                    int col = i / 8;
                    int row = i % 8;
                    if (i < euclideanSteps && outputs[euclideanOutputSelect].GetRhythmStep(i)) {
                        display.fillRect(90 + (col * 6), py + (row * 6), 5, 5, WHITE);
                    } else {
                        display.drawRect(90 + (col * 6), py + (row * 6), 5, 5, WHITE);
                        if (i >= euclideanSteps) { // padding cell
                            display.writePixel(90 + (col * 6) + 2, py + (row * 6) + 2, WHITE);
                        }
                    }
                }
                if (euclideanSteps + euclideanPadding > 47) {
                    display.fillTriangle(120, 57, 124, 57, 122, 61, WHITE);
                }
            }
            RedrawDisplay();
            return;
        }

        // ── Group 5: Swing — two-column with header + column indicator ─────
        if (grp == 5) {
            MD_PageBegin("OUTPUT SWING", 20);
            // Column headers
            display.setCursor(64, _md_rowY);  display.println("AMT");
            display.setCursor(94, _md_rowY);  display.println("EVERY");
            // Column indicator arrow (points to active column)
            if (menuItem % 2 == 0) { // even items → AMT column
                display.fillTriangle(59, _md_rowY, 59, _md_rowY + 6, 62, _md_rowY + 3, 1);
            } else {                  // odd items  → EVERY column
                display.fillTriangle(89, _md_rowY, 89, _md_rowY + 6, 92, _md_rowY + 3, 1);
            }
            _md_rowY += MD_ROW_H;
            // One visible row per output (items 22,24,26,28 are ROW_TWOCOL; 23,25,27,29 ROW_HIDDEN)
            for (int i = 0; i < MENU_ITEM_COUNT; i++) {
                const MenuItem& mi = MENU_ITEMS[i];
                if (mi.group != 5 || mi.rowStyle == ROW_HIDDEN) continue;
                int idx = i + 1;   // ROW_TWOCOL item (amt)
                int idxEv = idx + 1; // ROW_HIDDEN item (every)
                bool sel  = (idx == menuItem || idxEv == menuItem);
                bool edit = sel && (menuMode == menuItem);
                MD_TwoColRow(mi.label,
                             mi.valueFn  ? mi.valueFn()  : String(""), mi.valueFn  != nullptr,
                             mi.valueFn2 ? mi.valueFn2() : String(""), mi.valueFn2 != nullptr,
                             mi.col1x, mi.col2x, sel, edit);
            }
            RedrawDisplay();
            return;
        }

        // ── Group 13: Level/Offset — LVL/OFF for all 4 outputs ─────────────
        if (grp == 13) {
            MD_PageBegin("OUTPUT SETTINGS", 12);
            // Column headers
            display.setCursor(70, _md_rowY);  display.println("LVL");
            display.setCursor(100, _md_rowY); display.println("OFF");
            // Column indicator arrow (even item → LVL column, odd → OFF column)
            if (menuItem % 2 == 0) {
                display.fillTriangle(65, _md_rowY, 65, _md_rowY + 6, 68, _md_rowY + 3, 1);
            } else {
                display.fillTriangle(95, _md_rowY, 95, _md_rowY + 6, 98, _md_rowY + 3, 1);
            }
            _md_rowY += MD_ROW_H;
            for (int i = 0; i < MENU_ITEM_COUNT; i++) {
                const MenuItem& mi = MENU_ITEMS[i];
                if (mi.group != 13 || mi.rowStyle == ROW_HIDDEN) continue;
                int idx    = i + 1;
                int idxOff = idx + 1;
                bool sel  = (idx == menuItem || idxOff == menuItem);
                bool edit = sel && (menuMode == menuItem);
                MD_TwoColRow(mi.label,
                             mi.valueFn  ? mi.valueFn()  : String(""), mi.valueFn  != nullptr,
                             mi.valueFn2 ? mi.valueFn2() : String(""), mi.valueFn2 != nullptr,
                             mi.col1x, mi.col2x, sel, edit);
            }
            RedrawDisplay();
            return;
        }

        // ── Group 9: Envelope — last two items share one row ───────────────
        if (grp == 9) {
            MD_PageBegin("ENVELOPE SETTINGS", 10);
            for (int i = 0; i < MENU_ITEM_COUNT; i++) {
                const MenuItem& mi = MENU_ITEMS[i];
                if (mi.group != 9) continue;
                int idx  = i + 1;
                bool sel  = (idx == menuItem);
                bool edit = sel && (menuMode == idx);
                if (idx == 55) { // Curv — left half of last row
                    display.setCursor(MD_LABEL_X, _md_rowY);
                    display.print(mi.label);
                    if (mi.valueFn) display.print(mi.valueFn());
                    _MD_Cursor(_md_rowY, sel, edit);
                    // Don't advance; Retr shares this row
                } else if (idx == 56) { // Retr — right half
                    bool sel2  = (idx == menuItem);
                    bool edit2 = sel2 && (menuMode == idx);
                    display.setCursor(64, _md_rowY);
                    display.print(mi.label);
                    if (mi.valueFn) display.print(mi.valueFn());
                    if (sel2) {
                        int cx = 56;
                        if (edit2)
                            display.fillTriangle(cx, _md_rowY-1, cx, _md_rowY+7, cx+4, _md_rowY+3, 1);
                        else
                            display.drawTriangle(cx, _md_rowY-1, cx, _md_rowY+7, cx+4, _md_rowY+3, 1);
                    }
                    _md_rowY += MD_ROW_H;
                } else {
                    MD_RowAtX(mi.label, mi.valueFn ? mi.valueFn() : String(""), mi.valueFn != nullptr, 64, sel, edit);
                }
            }
            RedrawDisplay();
            return;
        }

        // ── Group 10: CV inputs — targets full-width, then two-col attn/off
        if (grp == 10) {
            MD_PageBegin("CV INPUT TARGETS", 20);
            for (int i = 0; i < MENU_ITEM_COUNT; i++) {
                const MenuItem& mi = MENU_ITEMS[i];
                if (mi.group != 10) continue;
                int idx  = i + 1;
                if (mi.rowStyle == ROW_HIDDEN) continue; // skip paired offset items
                bool sel  = (idx == menuItem);
                bool edit = sel && (menuMode == idx);
                if (mi.rowStyle == ROW_SINGLE && idx <= 58) {
                    // Full-width target rows (CV 1:/CV 2: target)
                    display.setCursor(MD_LABEL_X, _md_rowY);
                    display.print(mi.label);
                    if (mi.valueFn) display.print(mi.valueFn());
                    _MD_Cursor(_md_rowY, sel, edit);
                    _md_rowY += MD_ROW_H;
                } else if (mi.rowStyle == ROW_TWOCOL) {
                    // First time through: draw column headers
                    if (idx == 59) {
                        display.setCursor(60,  _md_rowY); display.println("ATTN");
                        display.setCursor(100, _md_rowY); display.println("OFF");
                        // Column indicator
                        int colItem = menuItem; // 59,60 or 61,62
                        if (colItem == 59 || colItem == 61) {
                            display.fillTriangle(55, _md_rowY, 55, _md_rowY+6, 58, _md_rowY+3, 1);
                        } else if (colItem == 60 || colItem == 62) {
                            display.fillTriangle(95, _md_rowY, 95, _md_rowY+6, 98, _md_rowY+3, 1);
                        }
                        _md_rowY += MD_ROW_H;
                    }
                    // Two-col data row
                    int idxOff = idx + 1;
                    bool rowSel  = (idx == menuItem || idxOff == menuItem);
                    bool rowEdit = rowSel && (menuMode == menuItem);
                    MD_TwoColRow(mi.label,
                                 mi.valueFn  ? mi.valueFn()  : String(""), mi.valueFn  != nullptr,
                                 mi.valueFn2 ? mi.valueFn2() : String(""), mi.valueFn2 != nullptr,
                                 mi.col1x, mi.col2x, rowSel, rowEdit);
                }
            }
            RedrawDisplay();
            return;
        }

        // ── Group 12: Settings — tap BPM + preset + save/load ──────────────
        if (grp == 12) {
            display.setTextSize(1);
            int yPosition = 9;
            // Tap tempo row (with BPM in parentheses)
            display.setCursor(MD_LABEL_X, yPosition);
            display.print("TAP TEMPO");
            display.print(" (" + String(BPM) + " BPM)");
            if (menuItem == 68) {
                display.drawTriangle(MD_CURSOR_X, yPosition-1, MD_CURSOR_X, yPosition+7, MD_CURSOR_X+4, yPosition+3, 1);
            }
            yPosition += MD_ROW_H * 2; // gap
            // Preset slot
            display.setCursor(MD_LABEL_X, yPosition);
            display.print("PRESET SLOT: ");
            display.print(saveSlot);
            if (menuItem == 69 && menuMode == 0) {
                display.drawTriangle(MD_CURSOR_X, yPosition-1, MD_CURSOR_X, yPosition+7, MD_CURSOR_X+4, yPosition+3, 1);
            } else if (menuMode == 69) {
                display.fillTriangle(MD_CURSOR_X, yPosition-1, MD_CURSOR_X, yPosition+7, MD_CURSOR_X+4, yPosition+3, 1);
            }
            yPosition += MD_ROW_H;
            // Save
            display.setCursor(MD_LABEL_X, yPosition);
            display.print("SAVE");
            if (menuItem == 70) display.drawTriangle(MD_CURSOR_X, yPosition-1, MD_CURSOR_X, yPosition+7, MD_CURSOR_X+4, yPosition+3, 1);
            yPosition += MD_ROW_H;
            // Load
            display.setCursor(MD_LABEL_X, yPosition);
            display.print("LOAD");
            if (menuItem == 71) display.drawTriangle(MD_CURSOR_X, yPosition-1, MD_CURSOR_X, yPosition+7, MD_CURSOR_X+4, yPosition+3, 1);
            yPosition += MD_ROW_H;
            // Load defaults
            display.setCursor(MD_LABEL_X, yPosition);
            display.print("LOAD DEFAULTS");
            if (menuItem == 72) display.drawTriangle(MD_CURSOR_X, yPosition-1, MD_CURSOR_X, yPosition+7, MD_CURSOR_X+4, yPosition+3, 1);
            RedrawDisplay();
            return;
        }

        // ── All other groups: generic single-column renderer ───────────────
        if (grp < (uint8_t)(sizeof(groupTitles)/sizeof(groupTitles[0]))) {
            MD_RenderGroup(groupTitles[grp], menuItem, menuMode, grp);
            MD_PageEnd();
        }
    }
}
