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

// Custom fonts for the main screen's BPM/PLAY only. Menus keep the classic
// 5x7 font — their row/column layout is built on its fixed 6x8 cell metrics,
// and the proportional font reads cluttered at menu density.
#include "fonts/helvB12.h"
#include "fonts/helvB24.h"

// ── Globals owned by main.cpp ────────────────────────────────
extern bool displayRefresh;
extern bool unsavedChanges;
extern int menuItem;
// menuMode is already declared extern in menuHandlers.hpp
extern bool masterState;
extern int euclideanOutputSelect;
extern Output outputs[];

// ── Thin display helpers ─────────────────────────────────────
// ClockForge's home screen is the transport view — menu items 1 and 2 (BPM and
// Play/Stop), drawn together by a custom full-width renderer. It suppresses the
// scroll indicator, and it is what the screen timeout returns to, so it never
// times out itself.
//
// Naming this rather than repeating `menuItem == 1 || menuItem == 2` is what
// lets CLK share core/displayManager.hpp with the other modules: they each have
// a differently-shaped home screen (NoteForge a keyboard, GravityForge a live
// physics view) and the shared code only needs the yes/no answer.
static inline bool OnHomePage() { return menuItem == 1 || menuItem == 2; }

inline void MenuIndicator() {
    // Visible position out of visible total — see MenuVisibleCount().
    displayMgr.DrawMenuIndicator(MenuVisibleIndex(menuItem), MenuVisibleCount(),
                                 OnHomePage());
}

inline void MenuHeader(const char *header) {
    displayMgr.DrawMenuHeader(header);
}

// ── Main display renderer ─────────────────────────────────────
void HandleDisplay() {
    // Sync unsaved changes state to display manager
    displayMgr.SetUnsavedChanges(unsavedChanges);

    // Check for timeout back to the home (transport) screen
    if (displayMgr.ShouldTimeout(OnHomePage(), menuMode)) {
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
            // Big BPM digits with a smaller baseline-aligned "BPM" suffix, so the
            // block stays narrow enough to clear the selection arrow at x=2..6.
            String s = String(BPM);
            int16_t bpmX, bpmY, sufX, sufY;
            uint16_t bpmW, bpmH, sufW, sufH;
            display.setFont(&helvB24);
            // Custom fonts position by baseline; baseline 25 puts the cap top at y=0.
            display.getTextBounds(s, 0, 25, &bpmX, &bpmY, &bpmW, &bpmH);
            display.setFont(&helvB12);
            display.getTextBounds("BPM", 0, 25, &sufX, &sufY, &sufW, &sufH);
            int bpmX0 = (SCREEN_WIDTH - ((int)bpmW + 3 + (int)sufW)) / 2;
            if (bpmX0 < 9)
                bpmX0 = 9; // never under the selection arrow
            display.setFont(&helvB24);
            display.setCursor(bpmX0 - bpmX, 25);
            display.print(s);
            display.setFont(&helvB12);
            display.setCursor(bpmX0 + (int)bpmW + 3 - sufX, 25);
            display.print("BPM");
            display.setFont(nullptr);
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
                    // Phase B: outline box, filled "E" (clear interior first — the
                    // large BPM text can extend under the box)
                    display.fillRect(118, 23, 10, 11, BLACK);
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
                const char *runLbl = masterState ? "PLAY" : "STOP";
                display.setFont(&helvB12);
                int16_t runX, runY;
                uint16_t runW, runH;
                // Baseline 40 centers the ~13px caps on the 26..42 icon row.
                display.getTextBounds(runLbl, 44, 40, &runX, &runY, &runW, &runH);
                if (menuItem == 2) {
                    display.drawLine(43, 42, runX + runW + 1, 42, 1);
                }
                if (!masterState) {
                    display.fillRoundRect(22, 28, 13, 13, 2, 1);
                } else {
                    // Same 13px box as the pause square (y 28..40); tip centered
                    // on y=34 so the top/bottom slopes are symmetric.
                    display.fillTriangle(22, 28, 22, 40, 34, 34, 1);
                }
                display.setCursor(44, 40);
                display.print(runLbl);
                display.setFont(nullptr);
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
                    display.fillRect((i * 30) + 13, 45, 11, 11, WHITE);
                    display.setCursor((i * 30) + 16, 47);
                    display.setTextColor(BLACK);
                    display.print(i + 1);
                    display.setTextColor(WHITE);
                } else {
                    // Blink phase B: outline box + white number
                    display.drawRect((i * 30) + 13, 45, 11, 11, WHITE);
                    display.setCursor((i * 30) + 16, 47);
                    display.setTextColor(WHITE);
                    display.print(i + 1);
                }
                display.setTextColor(WHITE);
                String d = outputs[i].GetDividerDescription();
                display.setCursor((i * 30) + 13 + (6 - (d.length() * 3)), 57);
                display.print(d);
            }
            RedrawDisplay();
            return;
        }

        // ── Groups 1–12: data-driven generic renderer ─────────────────────
        static const char *const groupTitles[] = {
            "",                  // 0 = custom above
            "CLOCK DIVIDERS",    // 1
            "OUTPUT STATE",      // 2
            "PROBABILITY",       // 3
            "EUCLIDEAN RHYTHM",  // 4
            "OUTPUT SWING",      // 5
            "PHASE / DUTY",      // 6  (custom two-column renderer)
            "",                  // 7  unused — duty merged into group 6
            "WAVEFORM SETTINGS", // 8  (waveforms - all 4)
            "ENVELOPE SETTINGS", // 9
            "CV INPUT TARGETS",  // 10
            "QUANTIZE SETTINGS", // 11
            "SETTINGS",          // 12 (tap tempo, timeout, boot menu)
            "OUTPUT SETTINGS",   // 13 (level/offset - all 4)
            "CROSS OPS",         // 14 (cross operations - all 4)
            "LOOPS",             // 15 (loop reset + nap/wake, per-output)
            "PRESETS",           // 16 (slot, save, load, defaults)
            // 17-24: the same pages again for the expander's outputs 5-8.
            "CLOCK DIVIDERS 5-8", // 17
            "WAVEFORM 5-8",       // 18
            "OUTPUT SETTINGS 5-8",// 19 (level/offset)
            "OUTPUT STATE 5-8",   // 20
            "PROBABILITY 5-8",    // 21
            "OUTPUT SWING 5-8",   // 22
            "PHASE / DUTY 5-8",   // 23
            "CROSS OPS 5-8",      // 24
            "CV ATTEN / OFFSET",  // 25 (split from 10 - see the renderer)
        };

        // ── Group 4: Euclidean — custom overlay (pattern grid) ─────────────
        if (grp == 4) {
            MD_PageBegin("EUCLIDEAN RHYTHM", 20);
            for (int i = 0; i < MENU_ITEM_COUNT; i++) {
                const MenuItem &mi = MENU_ITEMS[i];
                if (mi.group != 4)
                    continue;
                int idx = i + 1;
                bool sel = (idx == menuItem);
                bool edit = sel && (menuMode == idx);
                // ROT and PAD share a row; draw ROT on the left, PAD at x=50
                if (idx == MI_EUC_ROT) { // ROT — left half
                    display.setCursor(MD_LABEL_X, _md_rowY);
                    display.print(mi.label);
                    if (mi.valueFn) {
                        display.setCursor(mi.col1x, _md_rowY);
                        display.print(mi.valueFn());
                    }
                    _MD_Cursor(_md_rowY, sel, edit);
                    // Don't advance — PAD shares this row
                } else if (idx == MI_EUC_PAD) { // PAD — right half
                    display.setCursor(50, _md_rowY);
                    display.print(mi.label);
                    if (mi.valueFn) {
                        display.setCursor(50 + 4 * 6, _md_rowY);
                        display.print(mi.valueFn());
                    }
                    if (sel) {
                        int cx = 50 - 8;
                        if (edit)
                            display.fillTriangle(cx, _md_rowY - 1, cx, _md_rowY + 7, cx + 4, _md_rowY + 3, 1);
                        else
                            display.drawTriangle(cx, _md_rowY - 1, cx, _md_rowY + 7, cx + 4, _md_rowY + 3, 1);
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
                int euclideanSteps = outputs[euclideanOutputSelect].GetEuclideanSteps();
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
            MD_RenderTwoColPage("OUTPUT SWING", menuItem, menuMode, 5,
                                {"AMT", 64, 59}, {"EVERY", 94, 89}, true);
            RedrawDisplay();
            return;
        }

        // ── Group 6: Phase / Duty — one row per output, two values each ────
        // Was two separate four-row pages (phase group 6, duty group 7); group
        // 7 is now unused. Geometry mirrors group 13, the other per-output pair.
        if (grp == 6) {
            MD_RenderTwoColPage("PHASE / DUTY", menuItem, menuMode, 6,
                                {"PH", 70, 65}, {"DTY", 100, 95}, true);
            RedrawDisplay();
            return;
        }

        // ── Group 2: Output state — STATE/INV for all 4 outputs ────────────
        if (grp == 2) {
            // allowEdit=false: these are MENU_TOGGLEs, which never enter edit
            // mode, so the cursor stays hollow.
            MD_RenderTwoColPage("OUTPUT STATE", menuItem, menuMode, 2,
                                {"STATE", 58, 52}, {"INV", 100, 94}, false);
            RedrawDisplay();
            return;
        }

        // ── Group 13: Level/Offset — LVL/OFF for all 4 outputs ─────────────
        if (grp == 13) {
            MD_RenderTwoColPage("OUTPUT SETTINGS", menuItem, menuMode, 13,
                                {"LVL", 70, 65}, {"OFF", 100, 95}, true);
            RedrawDisplay();
            return;
        }

        // ── Group 14: Cross ops — OP/SRC for all 4 outputs ─────────────────
        if (grp == 14) {
            MD_RenderTwoColPage("CROSS OPS", menuItem, menuMode, 14,
                                {"OP", 48, 42}, {"SRC", 92, 86}, true);
            RedrawDisplay();
            return;
        }

        // ── Groups 19-24: the two-column pages again, for outputs 5-8 ──────
        // Identical geometry to their 1-4 counterparts, so the expander half of
        // the menu reads the same as the base half. Groups 17, 18 and 21 are
        // single-column and fall through to the generic renderer.
        if (grp == 19) {
            MD_RenderTwoColPage("OUTPUT SETTINGS 5-8", menuItem, menuMode, 19,
                                {"LVL", 70, 65}, {"OFF", 100, 95}, true);
            RedrawDisplay();
            return;
        }
        if (grp == 20) {
            MD_RenderTwoColPage("OUTPUT STATE 5-8", menuItem, menuMode, 20,
                                {"STATE", 58, 52}, {"INV", 100, 94}, false);
            RedrawDisplay();
            return;
        }
        if (grp == 22) {
            MD_RenderTwoColPage("OUTPUT SWING 5-8", menuItem, menuMode, 22,
                                {"AMT", 64, 59}, {"EVERY", 94, 89}, true);
            RedrawDisplay();
            return;
        }
        if (grp == 23) {
            MD_RenderTwoColPage("PHASE / DUTY 5-8", menuItem, menuMode, 23,
                                {"PH", 70, 65}, {"DTY", 100, 95}, true);
            RedrawDisplay();
            return;
        }
        if (grp == 24) {
            MD_RenderTwoColPage("CROSS OPS 5-8", menuItem, menuMode, 24,
                                {"OP", 48, 42}, {"SRC", 92, 86}, true);
            RedrawDisplay();
            return;
        }

        // ── Group 9: Envelope — last two items share one row ───────────────
        if (grp == 9) {
            MD_PageBegin("ENVELOPE SETTINGS", 10);
            for (int i = 0; i < MENU_ITEM_COUNT; i++) {
                const MenuItem &mi = MENU_ITEMS[i];
                if (mi.group != 9)
                    continue;
                int idx = i + 1;
                bool sel = (idx == menuItem);
                bool edit = sel && (menuMode == idx);
                if (idx == MI_ENV_CURVE) { // Curv — left half of last row
                    display.setCursor(MD_LABEL_X, _md_rowY);
                    display.print(mi.label);
                    if (mi.valueFn)
                        display.print(mi.valueFn());
                    _MD_Cursor(_md_rowY, sel, edit);
                    // Don't advance; Retr shares this row
                } else if (idx == MI_ENV_RETRIG) { // Retr — right half
                    bool sel2 = (idx == menuItem);
                    bool edit2 = sel2 && (menuMode == idx);
                    display.setCursor(70, _md_rowY);
                    display.print(mi.label);
                    if (mi.valueFn)
                        display.print(mi.valueFn());
                    if (sel2) {
                        int cx = 62;
                        if (edit2)
                            display.fillTriangle(cx, _md_rowY - 1, cx, _md_rowY + 7, cx + 4, _md_rowY + 3, 1);
                        else
                            display.drawTriangle(cx, _md_rowY - 1, cx, _md_rowY + 7, cx + 4, _md_rowY + 3, 1);
                    }
                    _md_rowY += MD_ROW_H;
                } else {
                    MD_RowAtX(mi.label, mi.valueFn ? mi.valueFn() : String(""), mi.valueFn != nullptr, mi.col1x, sel, edit);
                }
            }
            RedrawDisplay();
            return;
        }

        // ── Group 10: CV input targets — one full-width row per input ─────
        // Full-width rather than label+value-at-a-column: a target name runs to
        // 13 characters ("Swing 8 Evry") and would not clear a column stop.
        if (grp == 10) {
            MD_PageBegin("CV INPUT TARGETS", 20);
            for (int i = 0; i < MENU_ITEM_COUNT; i++) {
                const MenuItem &mi = MENU_ITEMS[i];
                if (mi.group != 10)
                    continue;
                const int idx = i + 1;
                // IN 4's row is only there when an expander is.
                if (!MenuItemEnabled(idx))
                    continue;
                const bool sel = (idx == menuItem);
                display.setCursor(MD_LABEL_X, _md_rowY);
                display.print(mi.label);
                if (mi.valueFn)
                    display.print(mi.valueFn());
                _MD_Cursor(_md_rowY, sel, sel && (menuMode == idx));
                _md_rowY += MD_ROW_H;
            }
            RedrawDisplay();
            return;
        }

        // ── Group 25: CV attenuation / offset ──────────────────────────────
        // Split off from group 10 when IN 4 arrived. Targets plus attenuation
        // pairs came to seven rows on one page, and six is the ceiling — the
        // third input's row was landing at y=65 and being silently clipped.
        if (grp == 25) {
            MD_RenderTwoColPage("CV ATTEN / OFFSET", menuItem, menuMode, 25,
                                {"ATTN", 60, 55}, {"OFF", 100, 95}, true);
            RedrawDisplay();
            return;
        }

        // Groups 12 (SETTINGS) and 16 (PRESETS) are plain lists and fall through
        // to the generic renderer below. They used to share one hand-written
        // page keyed on item numbers 85–90, which is what made splitting them
        // worth doing: the split is now two table entries and a title.

        // ── All other groups: generic single-column renderer ───────────────
        if (grp < (uint8_t)(sizeof(groupTitles) / sizeof(groupTitles[0]))) {
            MD_RenderGroup(groupTitles[grp], menuItem, menuMode, grp);
            MD_PageEnd();
        }
    }
}
