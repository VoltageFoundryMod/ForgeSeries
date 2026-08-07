#pragma once
// ============================================================
// menuRender.hpp — MenuHeader, the loom home screen, and
// HandleDisplay().
//
// Included from the app TU after menuDisplay.hpp. Relies on macros
// (SCREEN_WIDTH, REQUEST_DISPLAY_REFRESH) and types defined earlier
// in the translation unit.
// ============================================================

// ── Globals owned by the app TU ──────────────────────────────
extern bool displayRefresh;
extern bool unsavedChanges;
extern int menuItem;
extern WeavePair registers;
extern OutputBank outputs;
extern StepClock clockEngine;
extern GlobalParams globalParams;
extern LiveParams liveParams;

// ── Thin display helpers ─────────────────────────────────────
inline void MenuHeader(const char *header) { displayMgr.DrawMenuHeader(header); }

// ── The loom ─────────────────────────────────────────────────
//
// Two rectangular registers facing each other, running in OPPOSITE directions,
// with the weave drawn in the channel between them. Design.md §6.
//
// The counter-flow is the whole reason there are two rows rather than one: at
// WEAVE 100 % the chain becomes a visible racetrack — out of A's right end, down
// through the channel, back along B, up into A's left end — and a bit can be
// followed round it with a finger. Two rows flowing the same way would draw that
// circuit as a crossing tangle; one long row would not draw it at all.
//
// ── Vertical budget (64 rows) ────────────────────────────────
//    0..7   header — tempo, divider, weave amount
//   10..15  register A, flowing ▸
//   17..23  A's output taps
//   26..37  the weave channel
//   40..45  register B, flowing ◂
//   47..53  B's output taps
//   55..63  status line
#define LOOM_CELL_W 6
#define LOOM_PITCH 7
#define LOOM_X0 8
#define LOOM_A_Y 10
#define LOOM_A_TAP_Y 17
#define LOOM_CH_TOP 26
#define LOOM_CH_BOT 37
#define LOOM_B_Y 40
#define LOOM_B_TAP_Y 47
#define LOOM_STATUS_Y 56
// Clears the unsaved dot (x 0..2) with two pixels of air.
#define LOOM_HEADER_X 5

// Redraw interval. The loom changes on clock steps rather than continuously, so
// it does not need ChaosForge's 30 fps — but it is still a moving picture, not a
// page that only changes when you touch something, so it gets a faster interval
// than the menu default.
#define WEA_DISPLAY_INTERVAL_MS 40

// Register A reads left to right; B reads right to left. Everything that draws
// a cell or a marker goes through this, so the counter-flow is stated once.
static inline int LOOM_CellX(int reg, int bit) {
    const int i = (reg == 0) ? bit : (WEA_REG_BITS - 1 - bit);
    return LOOM_X0 + i * LOOM_PITCH;
}

// Three cell states, all legible on a 1-bit panel at arm's length:
//   filled  bit = 1, inside the active length
//   hollow  bit = 0, inside the active length
//   dot     past the feedback point — the delay line (Design.md §2)
//
// Drawn small rather than omitted because an output ROTATEd out there is reading
// them; drawn small because they are not part of the loop and shortening LENGTH
// is about to overwrite them.
static void LOOM_DrawRegister(int reg, int y) {
    const ShiftRegister &r = registers.Reg((uint8_t)reg);
    const int len = r.Length();

    for (int b = 0; b < WEA_REG_BITS; b++) {
        const int x = LOOM_CellX(reg, b);
        if (b >= len) {
            display.fillRect(x + 2, y + 2, 2, 2, WHITE);
        } else if (r.Bit((uint8_t)b)) {
            display.fillRect(x, y, LOOM_CELL_W, LOOM_CELL_W, WHITE);
        } else {
            display.drawRect(x, y, LOOM_CELL_W, LOOM_CELL_W, WHITE);
        }
    }
}

// Each jack's PANEL NAME at its ROTATE column, under the register it reads.
// Seeing "A1" and "A2" four cells apart is the only thing that makes ROTATE stop
// being an abstract number — and a jack sourced from AB landing on whichever row
// its offset falls in is how you learn what AB means without the manual.
//
// The panel's own names rather than 1..4, because the panel prints no numbers:
// a "3" on this screen would be a jack the user cannot find.
// A two-character label at 6 px a glyph, plus a pixel of air.
#define LOOM_TAP_STRIDE 13

// Does jack `j` read cell `bit` of register `reg` right now?
static bool LOOM_TapIsAt(int j, int reg, int bit) {
    const uint8_t pos = outputs.TapPosition(j, liveParams.rotate);
    return (pos >= WEA_REG_BITS ? 1 : 0) == reg && (pos % WEA_REG_BITS) == bit;
}

static void LOOM_DrawTaps() {
    display.setTextSize(1);
    display.setTextColor(WHITE);

    // Several jacks routinely read the SAME position — DUO puts a NOTE and its
    // GATE on register A at ROTATE 0, which is the default patch — so labels are
    // packed left to right from the tap rather than each drawn at the tap. Two
    // jacks on one cell read as "A1A2" hanging off it; drawing both at x would
    // print one on top of the other, and the default routing would be the first
    // thing to show it.
    for (int reg = 0; reg < 2; reg++) {
        const int y = (reg == 0) ? LOOM_A_TAP_Y : LOOM_B_TAP_Y;

        for (int bit = 0; bit < WEA_REG_BITS; bit++) {
            // Count first, then place the WHOLE group — because the group is
            // what has to fit, not each label.
            //
            // Clamping labels one at a time put the second one on top of the
            // first whenever a group ran up against the right edge, which is
            // exactly what register B does: it reads right-to-left, so its bit 0
            // is the RIGHTMOST cell, and DUO stacks both of B's jacks there.
            // Register A's bit 0 is at the left edge with the whole screen to
            // pack into, which is why only B showed it.
            int n = 0;
            for (int j = 0; j < WEA_NUM_OUTS; j++) {
                if (LOOM_TapIsAt(j, reg, bit)) {
                    n++;
                }
            }
            if (n == 0) {
                continue;
            }

            const int total = n * LOOM_TAP_STRIDE - 1;
            int x = LOOM_CellX(reg, bit) + 1;
            if (x + total > SCREEN_WIDTH) {
                x = SCREEN_WIDTH - total; // slide the group left to fit
            }
            if (x < 0) {
                x = 0;
            }

            for (int j = 0; j < WEA_NUM_OUTS; j++) {
                if (!LOOM_TapIsAt(j, reg, bit)) {
                    continue;
                }
                display.setCursor(x, y);
                display.print(OutJackNames[j]);
                // A high gate or a live trigger boxes its label, so the screen
                // shows which jacks are speaking as well as where they read.
                if (outputs.GateHigh(j)) {
                    display.drawRect(x - 1, y - 1, LOOM_TAP_STRIDE, 9, WHITE);
                }
                x += LOOM_TAP_STRIDE;
            }
        }
    }
}

// The channel IS the weave, and it is the only part of this screen that is a
// picture rather than a readout: flat parallel rails at 0, strands crossing in
// proportion as the control comes up, a full braid at 100. The module is named
// for this.
static void LOOM_DrawChannel() {
    display.drawFastHLine(LOOM_X0, LOOM_CH_TOP, 112, WHITE);
    display.drawFastHLine(LOOM_X0, LOOM_CH_BOT, 112, WHITE);

    const int strands = ((int)liveParams.weave * 8) / 100;
    if (strands <= 0) {
        return;
    }

    const int span = 112 / strands;
    for (int k = 0; k < strands; k++) {
        const int x = LOOM_X0 + k * span + (span - 8) / 2;
        const bool down = globalParams.dir != WeaveBtoA; // A feeds B
        const bool up = globalParams.dir != WeaveAtoB;   // B feeds A
        if (down) {
            display.drawLine(x, LOOM_CH_TOP + 1, x + 8, LOOM_CH_BOT - 1, WHITE);
        }
        if (up) {
            display.drawLine(x + 8, LOOM_CH_TOP + 1, x, LOOM_CH_BOT - 1, WHITE);
        }
    }
}

static void LOOM_DrawPage() {
    display.setTextSize(1);
    display.setTextColor(WHITE);

    // ── Header ──
    // Indented past the unsaved-changes dot, which core/displayManager.hpp
    // draws as a filled circle at (1,1) r=1 — so it covers x 0..2 and would sit
    // on top of the tempo's first digit at x=0.
    display.setCursor(LOOM_HEADER_X, 0);
    display.print(clockEngine.IsExternalLive()
                      ? String((int)lroundf(clockEngine.GetEffectiveBpm())) + "E"
                      : String(clockEngine.GetBpm()));
    display.print(F(" "));
    display.print(clockEngine.RateName());

    String w = String("W ") + String(liveParams.weave) + "%";
    display.setCursor(SCREEN_WIDTH - (int)w.length() * 6, 0);
    display.print(w);

    // ── The loom ──
    LOOM_DrawRegister(0, LOOM_A_Y);
    LOOM_DrawChannel();
    LOOM_DrawRegister(1, LOOM_B_Y);
    LOOM_DrawTaps();

    // Flow arrows at the ends the bits leave from, so the counter-flow is
    // stated on the screen and not only in the manual.
    display.setCursor(0, LOOM_A_Y - 1);
    display.print(F("A"));
    display.setCursor(SCREEN_WIDTH - 6, LOOM_A_Y - 1);
    display.print(F(">"));
    display.setCursor(0, LOOM_B_Y - 1);
    display.print(F("<"));
    display.setCursor(SCREEN_WIDTH - 6, LOOM_B_Y - 1);
    display.print(F("B"));

    // ── Status ──
    display.setCursor(0, LOOM_STATUS_Y);
    display.print(F("LEN "));
    display.print(liveParams.length[0]);
    display.print(F("/"));
    display.print(liveParams.length[1]);

    String c = String(liveParams.chance[0]) + "/" + String(liveParams.chance[1]) + "%";
    display.setCursor(SCREEN_WIDTH - (int)c.length() * 6, LOOM_STATUS_Y);
    display.print(c);
}

// ── Page titles ──────────────────────────────────────────────
static const char *const groupTitles[] = {
    "HOME",  "REG A", "REG B", "WEAVE", "CLOCK", "SCALE", "ROUTING",
    "OUT A1", "OUT B1", "OUT A2", "OUT B2", "CV IN", "SETTINGS", "PRESETS"};

#define WEA_GROUP_ROUTING 6
#define WEA_GROUP_OUT1 7 // OUT 1..4 are groups 7..10, in order
// Second row down: the ROUTING row itself is row 1 at MD_START_Y, and the jack
// summary starts under it.
#define WEA_ROUTING_INFO_Y (MD_START_Y + MD_ROW_H + 3)

// Which jack the live view is previewing, or -1 for anything else.
static int LOOM_LiveJack(int item) {
    if (item < 1 || item > MENU_ITEM_COUNT) {
        return -1;
    }
    const uint8_t g = MENU_ITEMS[item - 1].group;
    if (g < WEA_GROUP_OUT1 || g > WEA_GROUP_OUT1 + WEA_NUM_OUTS - 1) {
        return -1;
    }
    return (int)(g - WEA_GROUP_OUT1);
}

// The cells one jack is actually reading, underlined.
//
// This is what makes DEPTH and ROTATE worth previewing at all. ROTATE already
// moves the tap digit, but DEPTH is otherwise invisible — the window is the one
// thing about an output that the loom does not otherwise draw, so a preview
// without this would be a strip showing a number with nothing changing behind
// it. Underlined rather than boxed: the cell outline already means "bit = 0",
// and a second rectangle around it would read as a bit value.
static void LOOM_DrawWindow(int j) {
    const OutSlot &s = outputs.Slot(j);
    const uint8_t base = outputs.TapPosition(j, liveParams.rotate);
    const bool ab = (s.source == SrcAB);

    for (int k = 0; k < (int)s.depth; k++) {
        int pos;
        if (ab) {
            pos = (base + k) % WEA_COMBINED_BITS;
        } else {
            // The window wraps within its own register rather than spilling into
            // the other one — which is exactly the difference between sourcing A
            // and sourcing AB, and the reason to draw it.
            const int half = (s.source == SrcB) ? WEA_REG_BITS : 0;
            pos = half + (((int)base % WEA_REG_BITS) + k) % WEA_REG_BITS;
        }
        const int reg = (pos >= WEA_REG_BITS) ? 1 : 0;
        const int x = LOOM_CellX(reg, pos % WEA_REG_BITS);
        const int y = (reg == 0) ? (LOOM_A_Y + LOOM_CELL_W) : (LOOM_B_Y + LOOM_CELL_W);
        display.drawFastHLine(x, y, LOOM_CELL_W, WHITE);
    }
}

// ── The live strip ───────────────────────────────────────────
// While a live-flagged parameter is being turned, the loom takes the screen and
// the value rides in a strip along the bottom — over the status line, which is
// showing the same two numbers in a less useful form at that moment.
#define LIVE_STRIP_Y 54
#define LIVE_STRIP_H 10

static void LOOM_DrawLiveStrip(int item) {
    const MenuItem &mi = MENU_ITEMS[item - 1];

    // Both registers have a LENGTH and a CHANCE, and all four jacks have a DEPTH
    // and a ROTATE, so the bare label is ambiguous once the page it came from is
    // off screen. The group title already carries which one ("REG A", "OUT 3"),
    // so borrow its last character.
    String s;
    if (mi.group < (uint8_t)(sizeof(groupTitles) / sizeof(groupTitles[0]))) {
        const char *gt = groupTitles[mi.group];
        const bool isReg = (gt[0] == 'R' && gt[1] == 'E' && gt[2] == 'G');
        const bool isOut = (gt[0] == 'O' && gt[1] == 'U' && gt[2] == 'T');
        if (isReg || isOut) {
            s = String(gt[4]) + " ";
        }
    }
    s += mi.label;
    if (mi.valueFn) {
        s += " " + mi.valueFn();
    }

    int w = (int)s.length() * 6;
    int x = (SCREEN_WIDTH - w) / 2;
    if (x < 3) {
        x = 3;
    }

    display.fillRect(0, LIVE_STRIP_Y, SCREEN_WIDTH, LIVE_STRIP_H, BLACK);
    display.drawRect(x - 3, LIVE_STRIP_Y, w + 6, LIVE_STRIP_H, WHITE);
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(x, LIVE_STRIP_Y + 2);
    display.print(s);
}

// Like core's MD_RenderGroup, but a row whose label is empty asks
// menuHandlers.hpp what it is called right now. The two contextual rows on each
// OUT page mean different things per TYPE (RANGE/LEVEL/THRESH, SLEW/WIDTH), and
// resolving the label at draw time means switching a jack from NOTE to TRIG
// relabels the page with no state to keep in sync.
static void WEA_RenderGroup(const char *title, int curItem, int curMode, int groupId) {
    MD_PageBegin(title);
    int rowInGroup = 0;
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        const MenuItem &mi = MENU_ITEMS[i];
        if (mi.group != groupId) {
            continue;
        }
        const int idx = i + 1;
        const bool sel = (idx == curItem);
        const bool edit = sel && (curMode == idx);

        const char *label = mi.label;
        if (label != nullptr && label[0] == '\0') {
            const char *dynamic = OutRowLabel((uint8_t)groupId, rowInGroup);
            label = dynamic ? dynamic : "";
        }

        switch (mi.rowStyle) {
        case ROW_ACTION:
            MD_ActionRow(label, sel);
            break;
        case ROW_HIDDEN:
            break;
        case ROW_SINGLE:
        default:
            MD_RowAtX(label, mi.valueFn ? mi.valueFn() : String(""),
                      mi.valueFn != nullptr, mi.col1x, sel, edit);
            break;
        }
        rowInGroup++;
    }

    // The ROUTING page is one row on a six-row page, so the space below it goes
    // to saying what the choice actually DID — by panel name, so it reads as a
    // patching instruction rather than a table — otherwise "DUO" is a word you
    // have to have read the manual to decode, and the whole point of the macro
    // is to be the fast way to reconfigure the four jacks.
    if (groupId == WEA_GROUP_ROUTING) {
        display.setTextSize(1);
        display.setTextColor(WHITE);
        for (int j = 0; j < WEA_NUM_OUTS; j++) {
            const OutSlot &s = outputs.Slot(j);
            String line = String(OutJackNames[j]) + " " + OutTypeNames[s.type] +
                          " " + RegSourceNames[s.source];
            // ROTATE only when it is doing something — a column of "r0" is
            // noise, and a non-zero one is the thing worth noticing.
            if (s.rotate != 0) {
                line += " r" + String(s.rotate);
            }
            display.setCursor(10, WEA_ROUTING_INFO_Y + j * 9);
            display.print(line);
        }
    }

    MD_PageEnd();
}

void HandleDisplay() {
    displayMgr.SetUnsavedChanges(unsavedChanges);

    if (menuItem < 1 || menuItem > MENU_ITEM_COUNT) {
        menuItem = 1;
    }
    bool onHome = (MENU_ITEMS[menuItem - 1].group == MENU_GROUP_HOME);

    // The hold running out is a timer, not a user action, so nothing else is
    // going to mark the screen dirty. Catch the expiry edge here and force one
    // redraw, or the last loom frame — strip and all — stays frozen on the panel
    // until you happen to touch the encoder again.
    if (liveViewUntil != 0 && !LiveViewActive()) {
        LiveViewClear(); // fires exactly once: the deadline is now 0
        displayRefresh = 1;
        displayMgr.MarkDirty(); // dirty, not an interaction — see below
    }

    const int liveItem = (menuMode >= 1 && menuMode <= MENU_ITEM_COUNT) ? menuMode : menuItem;
    const bool liveView = !onHome && LiveViewActive() && MenuItemIsLive(liveItem);

    // The live view already IS the loom, so it counts as home for the timeout —
    // otherwise adjusting WEAVE for a while would time out mid-preview and
    // strand you on the home screen rather than the page you came from.
    if (displayMgr.ShouldTimeout(onHome || liveView, menuMode)) {
        menuItem = 1;
        menuMode = 0;
        onHome = true;
        REQUEST_DISPLAY_REFRESH();
    }

    // The loom moves on its own — the registers shift whether or not anything
    // was touched — so it has to redraw on the rate limiter alone rather than
    // waiting to be marked dirty. BOTH flags have to be re-armed: RedrawDisplay()
    // clears displayRefresh after every frame, and the guard below wants
    // displayRefresh AND ShouldUpdate(). MarkDirty() rather than
    // MarkInteraction(): this is not user input, and resetting the interaction
    // timer would stop the menu ever timing back out to this screen.
    if (onHome || liveView) {
        displayRefresh = 1;
        displayMgr.MarkDirty();
    }

    if (!displayRefresh || !displayMgr.ShouldUpdate()) {
        return;
    }

    displayMgr.BeginFrame();
    const uint8_t grp = MENU_ITEMS[menuItem - 1].group;
    const bool loomView = (grp == MENU_GROUP_HOME) || liveView;
    // No scroll bar over the live view: it is the loom for the moment, and the
    // strip already says where in the menu you are.
    displayMgr.DrawMenuIndicator(menuItem, MENU_ITEM_COUNT, loomView);

    if (loomView) {
        LOOM_DrawPage();
        if (liveView) {
            const int jack = LOOM_LiveJack(liveItem);
            if (jack >= 0) {
                LOOM_DrawWindow(jack);
            }
            LOOM_DrawLiveStrip(liveItem);
        }
        RedrawDisplay();
        return;
    }

    if (grp < (uint8_t)(sizeof(groupTitles) / sizeof(groupTitles[0]))) {
        WEA_RenderGroup(groupTitles[grp], menuItem, menuMode, grp);
    }
}
