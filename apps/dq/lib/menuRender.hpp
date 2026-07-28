#pragma once
// ============================================================
// menuRender.hpp — High-level menu rendering: MenuIndicator,
// MenuHeader, the keyboard home screen, and HandleDisplay().
//
// Included from main.cpp after menuDisplay.hpp.  Relies on
// macros (SCREEN_WIDTH, REQUEST_DISPLAY_REFRESH) and types
// (DisplayManager, QuantizerChannel) defined earlier in the
// translation unit.
// ============================================================

// ── Globals owned by main.cpp ────────────────────────────────
extern bool displayRefresh;
extern bool unsavedChanges;
extern int menuItem;
// menuMode is already declared extern in menuHandlers.hpp
extern QuantizerChannel channels[NUM_CHANNELS];

// ── Thin display helpers ─────────────────────────────────────
inline void MenuHeader(const char *header) {
    displayMgr.DrawMenuHeader(header);
}

// ── Keyboard (home) screen ───────────────────────────────────
// Two piano keyboards stacked vertically, one per channel, with the enabled
// notes filled in and the currently sounding note ringed. This is the screen
// the module boots to and the one the screen-timeout returns to.
//
// Horizontal position of each of the 12 keys, and which sit on the upper
// (accidental) row. The geometry is carried over from the original firmware so
// the panel keeps its familiar look.
static const uint8_t KB_NOTE_X[12] = {0, 7, 14, 21, 28, 42, 50, 56, 64, 70, 78, 84};
static const bool KB_NOTE_BLACK[12] = {false, true, false, true, false, false,
                                       true, false, true, false, true, false};

static const int KB_KEY_W = 11;
static const int KB_KEY_H = 13;
static const int KB_KEY_R = 2;
static const int KB_INFO_X = 98; // left edge of the per-channel text column

// ── Vertical budget (64 rows) ────────────────────────────────
// Channel 1 accidentals   0..12
// Channel 1 naturals     15..27
// Cursor band            28..35   <- the two keyboards' only separation
// Channel 2 accidentals  36..48
// Channel 2 naturals     51..63
//
// The cursor lives alone in the band and must keep clear air on both sides:
// an arrow whose apex touches a key reads as though it is overlapping it. The
// band is 8 rows, the arrow is 4, leaving a 2px gap above and below. Channel 2
// starts at 36 rather than 34 to buy those rows from the bottom of the screen,
// so its naturals now end exactly on the last row — there is no slack left. Any
// future change to KB_KEY_H or the row spacing has to re-derive this table.
static const int KB_CURSOR_Y = 30; // top of the arrow
static const int KB_CURSOR_H = 4;  // arrow height; 2px clear above and below

// Top Y of each channel's accidental row; naturals sit 15px below.
static inline int KB_ChannelY(int ch) { return ch == 0 ? 0 : 36; }

static inline int KB_KeyY(int ch, int note) {
    return KB_ChannelY(ch) + (KB_NOTE_BLACK[note] ? 0 : 15);
}

static void KB_DrawKey(int x, int y, bool enabled, bool sounding) {
    if (enabled && sounding) {
        // Ring + core: unmistakable at a glance even on a 1bpp screen.
        display.drawRoundRect(x, y, KB_KEY_W, KB_KEY_H, KB_KEY_R, WHITE);
        display.fillRoundRect(x + 3, y + 3, KB_KEY_W - 6, KB_KEY_H - 6, KB_KEY_R, WHITE);
    } else if (enabled) {
        display.fillRoundRect(x, y, KB_KEY_W, KB_KEY_H, KB_KEY_R, WHITE);
    } else {
        display.drawRoundRect(x, y, KB_KEY_W, KB_KEY_H, KB_KEY_R, WHITE);
        if (sounding) {
            // Reached only when every key is off and the quantizer has fallen
            // back to chromatic — mark the note so the output is not a mystery.
            display.fillRect(x + 4, y + 5, 3, 3, WHITE);
        }
    }
}

// The per-channel text column: scale, root + octave, and the live note.
static void KB_DrawInfo(int ch) {
    int y = KB_ChannelY(ch);
    display.setTextSize(1);
    display.setTextColor(WHITE);

    display.setCursor(KB_INFO_X, y);
    display.print(scaleShortNames[channels[ch].GetScaleIndex()]);

    // Second line normally shows root + octave. While this channel follows the
    // transpose CV it shows the live transposition instead — a mode that
    // silently rewrites every note must never be invisible, and the root is
    // still one page away on SCALES.
    display.setCursor(KB_INFO_X, y + 9);
    if (in2Role == In2Transpose && channels[ch].GetTransposeEnabled()) {
        int t = channels[ch].GetTransposeDegrees();
        display.print("T");
        display.print(t > 0 ? "+" + String(t) : String(t));
    } else {
        int oct = channels[ch].GetOctave();
        display.print(noteNames[channels[ch].GetRootIndex()]);
        display.print(oct > 0 ? "+" + String(oct) : String(oct));
    }

    // Live note, inverted while the gate/envelope is running.
    String note = String(noteNames[channels[ch].GetNoteIndex()]) +
                  String(channels[ch].GetOctaveOut());
    int w = (int)note.length() * 6 + 1;
    if (channels[ch].IsGateActive()) {
        display.fillRect(KB_INFO_X - 1, y + 17, w + 1, 9, WHITE);
        display.setTextColor(BLACK);
    }
    display.setCursor(KB_INFO_X, y + 18);
    display.print(note);
    display.setTextColor(WHITE);
}

// Selection cursor in the band between the two keyboards: it points up at the
// channel-1 key above it, and down at the channel-2 key below it. Direction is
// what identifies the channel, so the arrow is drawn wider than it is tall —
// squat enough to leave clearance, broad enough for the direction to read at a
// glance.
static void KB_DrawCursor(int ch, int note) {
    int cx = KB_NOTE_X[note] + KB_KEY_W / 2;
    int top = KB_CURSOR_Y;
    int bottom = KB_CURSOR_Y + KB_CURSOR_H - 1;
    if (ch == 0) {
        display.fillTriangle(cx, top, cx - 3, bottom, cx + 3, bottom, WHITE);
    } else {
        display.fillTriangle(cx, bottom, cx - 3, top, cx + 3, top, WHITE);
    }
}

static void KB_DrawPage() {
    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        // Ring the note actually being played, not the raw quantizer result:
        // transposition moves it, and the octave shift leaves the pitch class
        // alone, so GetNoteIndex() is the right source for both.
        int sounding = channels[ch].GetQuantizedSemitone() >= 0 ? channels[ch].GetNoteIndex() : -1;
        for (int n = 0; n < 12; n++) {
            KB_DrawKey(KB_NOTE_X[n], KB_KeyY(ch, n), channels[ch].GetActiveNote(n), n == sounding);
        }
        KB_DrawInfo(ch);
    }
    // menuItem 1..12 = channel 1 notes, 13..24 = channel 2 notes.
    int idx = menuItem - 1;
    KB_DrawCursor(idx / 12, idx % 12);
}

// ── Main display renderer ─────────────────────────────────────
static const char *const groupTitles[] = {
    "",          // 0 — keyboard home screen (custom renderer)
    "SCALES",    // 1
    "CH1 PITCH", // 2
    "CH2 PITCH", // 3
    "CH1 GATE",  // 4
    "CH2 GATE",  // 5
    "ROUTING",   // 6
    "SETTINGS",  // 7
};

void HandleDisplay() {
    // Sync unsaved changes state to display manager
    displayMgr.SetUnsavedChanges(unsavedChanges);

    if (menuItem < 1 || menuItem > MENU_ITEM_COUNT) {
        menuItem = 1;
    }
    bool onHome = (MENU_ITEMS[menuItem - 1].group == MENU_GROUP_HOME);

    // Drop back to the keyboard after the configured idle period.
    if (displayMgr.ShouldTimeout(onHome, menuMode)) {
        menuItem = 1;
        menuMode = 0;
        onHome = true;
        REQUEST_DISPLAY_REFRESH();
    }

    if (!displayRefresh || !displayMgr.ShouldUpdate()) {
        return;
    }

    displayMgr.BeginFrame();
    uint8_t grp = MENU_ITEMS[menuItem - 1].group;
    displayMgr.DrawMenuIndicator(menuItem, MENU_ITEM_COUNT, grp == MENU_GROUP_HOME);

    if (grp == MENU_GROUP_HOME) {
        KB_DrawPage();
        RedrawDisplay();
        return;
    }

    if (grp < (uint8_t)(sizeof(groupTitles) / sizeof(groupTitles[0]))) {
        MD_RenderGroup(groupTitles[grp], menuItem, menuMode, grp);
        MD_PageEnd();
    }
}
