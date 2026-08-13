#pragma once
// ============================================================
// menuRender.hpp — MenuHeader, the loom home screen, and
// HandleDisplay().
//
// Included from the app TU after menuDisplay.hpp. Relies on macros
// (SCREEN_WIDTH, REQUEST_DISPLAY_REFRESH) and types defined earlier
// in the translation unit.
// ============================================================

// core/fonts/, shared with ClockForge and ScopeForge. Included from inside the
// app's namespace like everything else here, which is what keeps the unified
// firmware (one binary, every app) and the consolidated Rack plugin from seeing
// several definitions of the same glyph tables.
#include "fonts/helvB12.h"

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
//    0..11  header — tempo (helvB12), rate, weave amount
//   12..13  A's feedback-tap caret
//   14..19  register A, flowing ▸
//      20   A's output windows — which cells each jack is reading
//      21   A's tap leaders — which label belongs to which window
//   22..28  A's output taps
//   29..38  the weave channel
//   39..40  B's feedback-tap caret
//   41..46  register B, flowing ◂
//      47   B's output windows
//      48   B's tap leaders
//   49..55  B's output taps
//   56..63  status line
//
// That is 64 rows with nothing left over, and it is worth knowing what the
// header costs before adding to it: a 12 px tempo took four rows off the loom —
// two from the weave channel and one from each row of tap labels — and there is
// nowhere else for a fifth to come from.
#define LOOM_CELL_W 6
#define LOOM_PITCH 7
#define LOOM_X0 8
#define LOOM_CH_W 112 // 16 cells on a 7 px pitch, less the trailing gap
#define LOOM_A_TAIL_Y 12
#define LOOM_A_Y 14
#define LOOM_A_LEAD_Y 21
#define LOOM_A_TAP_Y 22
#define LOOM_CH_TOP 29
#define LOOM_CH_BOT 38
#define LOOM_B_TAIL_Y 39
#define LOOM_B_Y 41
#define LOOM_B_LEAD_Y 48
#define LOOM_B_TAP_Y 49
#define LOOM_STATUS_Y 56
// The lit block behind a speaking jack's label. SEVEN rows, not the classic
// font's full 8-row cell: the glyphs on this screen are capitals and digits, all
// of which leave the cell's last row empty for descenders, and that row is what
// pays for the leader row above. It is why the block may not simply be widened
// back — row 29 under A's labels is the weave channel's top rail.
#define LOOM_TAP_H 7
// Baseline for the big tempo. A helvB12 digit is 12 rows with yOffset -12, so a
// baseline here puts it in rows 0..11 — the whole header, to the pixel.
#define LOOM_BPM_BASELINE 12
// The small header text, bottom-aligned with the big digits: the classic font's
// 8-row cell ending on row 11.
#define LOOM_HEADER_SMALL_Y 4
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

// The feedback tap — bit LENGTH-1, marked with a caret above its cell.
//
// This is the most important cell in a Turing machine and nothing pointed at it.
// It is the bit about to wrap, the one CHANCE flips on its way round, and the
// one a courier departs from when it crosses — so without the caret you only
// learn where the tail is by waiting for a crossing to happen, and at WEAVE 0
// you never learn it at all.
//
// It also gives LENGTH a second reading. The shading boundary says where the
// loop ends; the caret says which cell closes it. Those are the same fact, and a
// control worth putting on the panel is worth being able to see two ways.
//
// Drawn ABOVE the row, in the two rows the step sweep used to occupy before it
// was removed for being a big moving thing that said nothing (Design.md §6).
static void LOOM_DrawTailMark(int reg, int y) {
    const int len = registers.Reg((uint8_t)reg).Length();
    const int x = LOOM_CellX(reg, len - 1);

    // Points down, at the cell. Three pixels then one, which is the smallest
    // arrowhead that still reads as a direction rather than as dirt.
    display.drawFastHLine(x + 1, y, 3, WHITE);
    display.drawPixel(x + 2, y + 1, WHITE);
}

// A cell at half brightness. There is no grey on a 1-bit panel, but a
// checkerboard is one at arm's length — the SSD1306 is a true pixel grid, and
// the Rack port's OLED widget area-integrates, so both hosts resolve it to a
// tone rather than to a pattern. It buys a THIRD cell state, which is what the
// delay line needs (below).
static inline void LOOM_ShadeCell(int x, int y) {
    for (int j = 0; j < LOOM_CELL_W; j++) {
        for (int i = (j & 1); i < LOOM_CELL_W; i += 2) {
            display.drawPixel(x + i, y + j, WHITE);
        }
    }
}

// Four cell states, all legible on a 1-bit panel at arm's length:
//   filled  bit = 1, inside the active length
//   hollow  bit = 0, inside the active length
//   shaded  bit = 1, past the feedback point — the delay line (Design.md §2)
//   dot     bit = 0, past the feedback point
//
// The delay line is drawn dimmer rather than omitted because an output ROTATEd
// out there is reading it; dimmer because it is not part of the loop and
// shortening LENGTH is about to overwrite it. It shows its BIT VALUE, which the
// flat dot it replaces did not — a jack reading up there was reading cells the
// screen refused to say anything about, and that is the one thing that made
// ROTATE past LENGTH look like a jack pointed at nothing.
//
// `fresh` marks the bit that arrived on this clock — see LOOM_DrawPage.
static void LOOM_DrawRegister(int reg, int y, bool fresh) {
    const ShiftRegister &r = registers.Reg((uint8_t)reg);
    const int len = r.Length();

    for (int b = 0; b < WEA_REG_BITS; b++) {
        const int x = LOOM_CellX(reg, b);
        if (b >= len) {
            if (r.Bit((uint8_t)b)) {
                LOOM_ShadeCell(x, y);
            } else {
                display.fillRect(x + 2, y + 2, 2, 2, WHITE);
            }
        } else if (r.Bit((uint8_t)b)) {
            display.fillRect(x, y, LOOM_CELL_W, LOOM_CELL_W, WHITE);
        } else {
            display.drawRect(x, y, LOOM_CELL_W, LOOM_CELL_W, WHITE);
        }
    }

    // The newest bit, punched through for the first third of the step. This is
    // what makes a LOCKED pattern look alive: at CHANCE 0 every frame is
    // otherwise identical, so the screen said "running" and "stopped" with the
    // same picture. Drawn as a hole in the cell rather than as a changed cell,
    // so the bit's VALUE stays readable while it is being announced — bit 0 is
    // always inside the length (minimum 2), so it is always a square.
    if (fresh) {
        const int x = LOOM_CellX(reg, 0);
        display.fillRect(x + 2, y + 2, 2, 2, r.Bit(0) ? BLACK : WHITE);
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
        const int leadY = (reg == 0) ? LOOM_A_LEAD_Y : LOOM_B_LEAD_Y;

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
                // A high gate or a live trigger INVERTS its label, so the screen
                // shows which jacks are speaking as well as where they read.
                //
                // Inverted rather than boxed: a 1 px outline is the same weight
                // and the same shape as the hollow cell directly above it, which
                // is the vocabulary for "bit = 0" — two different meanings drawn
                // identically, a row apart. A solid block shares nothing with
                // anything else on the screen and reads across a room.
                //
                // The block starts at the label's own row, not a pixel above
                // it: the row above is the output-window bar, and a lit jack
                // would otherwise paint over the very thing that says which
                // cells it is reading.
                const bool speaking = outputs.GateHigh(j);
                if (speaking) {
                    display.fillRect(x - 1, y, LOOM_TAP_STRIDE, LOOM_TAP_H, WHITE);
                }
                display.setTextColor(speaking ? BLACK : WHITE);
                display.setCursor(x, y);
                display.print(OutJackNames[j]);

                // The leader: from the cell this jack reads to the label that
                // names it. Packing a group left-to-right means only the FIRST
                // label sits over its own cell — in the DUO default, A2 lands two
                // cells right of the one it actually taps, and the screen then
                // states something false about where a jack reads. The leader is
                // what makes the packing legible instead of merely tidy.
                //
                // Row of its own, between the window bars and the labels, so it
                // reads as a connector rather than as more cells being read: the
                // chain is cell ▸ window bar ▸ leader ▸ label, each touching the
                // next. Leaders from jacks sharing one tap merge, which is
                // correct — they do come from the same place.
                //
                // DOTTED, and on an absolute parity rather than one relative to
                // each leader's own start. Solid, it stacked under the window bar
                // into a single two-row slab and read as a fatter bar — the one
                // thing the row exists not to be. The shared parity is what keeps
                // two merging leaders dotted instead of filling each other in.
                const int tapCx = LOOM_CellX(reg, bit) + LOOM_CELL_W / 2;
                const int labelCx = x + LOOM_TAP_STRIDE / 2;
                const int from = tapCx < labelCx ? tapCx : labelCx;
                const int to = tapCx < labelCx ? labelCx : tapCx;
                for (int px = from; px <= to; px++) {
                    if ((px & 1) == 0) {
                        display.drawPixel(px, leadY, WHITE);
                    }
                }

                x += LOOM_TAP_STRIDE;
            }
        }
    }

    display.setTextColor(WHITE); // everything after this draws in ink
}

// A strand is exactly 45°: one pixel across per pixel down, so it spans as many
// columns as the channel has rows.
//
// That is not a style choice, it is the only slope this can be drawn at. The
// first version spanned a fixed 8 columns over however many rows the channel had
// and stepped `x0 + (i * 8) / h`. With h = 7 that yields offsets 0,1,2,3,4,5,6,8
// — offset 7 never appears, so every strand ran straight for seven rows and then
// jumped two pixels sideways. It read as a bent line, and the detached last pixel
// read as dirt, most obviously on the rightmost strand where nothing follows it
// to make the pattern legible. Nine columns over eight rows is not a line.
#define LOOM_STRAND_W (LOOM_CH_BOT - LOOM_CH_TOP - 1)

// One strand, clipped to the channel rather than to the screen.
//
// Stepped per ROW rather than handed to drawLine because the braid scrolls, so
// strands routinely run off both ends — clipping to the rails is what keeps them
// from spilling into the 8 px margins where the flow arrows live. A partially
// clipped strand at either edge is correct: the cloth continues past the frame.
static void LOOM_Strand(int x0, bool down) {
    const int yTop = LOOM_CH_TOP + 1;
    const int yBot = LOOM_CH_BOT - 1;

    for (int i = 0; i < LOOM_STRAND_W; i++) {
        const int x = x0 + i;
        if (x < LOOM_X0 || x >= LOOM_X0 + LOOM_CH_W) {
            continue;
        }
        display.drawPixel(x, down ? (yTop + i) : (yBot - i), WHITE);
    }
}

// The channel IS the weave, and it is the only part of this screen that is a
// picture rather than a readout: flat parallel rails at 0, strands crossing in
// proportion as the control comes up, a full braid at 100. The module is named
// for this.
//
// The braid TRAVELS, in the direction register A runs. A static braid says how
// much weave is set; a moving one says the cloth is being drawn through the
// loom, and it is the same picture either way when the module is stopped —
// which is exactly the state worth being able to see.
//
// It travels ONE CELL PITCH PER CLOCK — the same distance and the same rate the
// bits themselves move, which is the only speed on this screen that means
// anything. The first version scrolled a whole strand-span per clock so that the
// phase alone made it seamless, and the span is 112/strands: the cloth then flew
// past at a low WEAVE and crawled at a high one, i.e. fastest exactly where
// least was crossing. Speed has to be independent of the control, so the wrap
// comes from the step COUNT instead — integer pixels, modulo the span, so
// stepping to phase 1 of one clock lands exactly on phase 0 of the next and the
// seam stays invisible however the span divides.
static void LOOM_DrawChannel(float phase) {
    const int strands = ((int)liveParams.weave * 8) / 100;

    // Dashed rails when the braid rounds to nothing — WEAVE 0, and the 1..12 %
    // below one strand's worth. The channel is 12 rows of blank screen there, and
    // two solid rules around blank screen read as a wall promising traffic that
    // is not coming; dashes read as an open line. The couriers still cross it on
    // the steps a bit actually does jump, which is the whole picture at a low
    // WEAVE: an open channel, quiet, with something through it now and then.
    if (strands <= 0) {
        for (int x = LOOM_X0; x < LOOM_X0 + LOOM_CH_W; x += 2) {
            display.drawPixel(x, LOOM_CH_TOP, WHITE);
            display.drawPixel(x, LOOM_CH_BOT, WHITE);
        }
        return;
    }

    display.drawFastHLine(LOOM_X0, LOOM_CH_TOP, LOOM_CH_W, WHITE);
    display.drawFastHLine(LOOM_X0, LOOM_CH_BOT, LOOM_CH_W, WHITE);

    const int span = LOOM_CH_W / strands;
    // (n·pitch) mod span, folded first so the product cannot overflow however
    // long the module has been running.
    const int travelled =
        (int)((clockEngine.StepCount() % (uint32_t)span) * LOOM_PITCH) % span;
    const int scroll = (travelled + (int)(phase * (float)LOOM_PITCH)) % span;
    // liveParams rather than globalParams: it is what the engine actually clocks
    // with, and it is what the WEAVE percentage two lines up already reads from.
    const bool down = liveParams.dir != WeaveBtoA; // A feeds B
    const bool up = liveParams.dir != WeaveAtoB;   // B feeds A

    // From -1, so the strand scrolling in from the left is drawn as a strand
    // rather than appearing once it has fully cleared the rail.
    for (int k = -1; k < strands; k++) {
        const int x = LOOM_X0 + k * span + (span - LOOM_STRAND_W) / 2 + scroll;
        if (down) {
            LOOM_Strand(x, true);
        }
        if (up) {
            LOOM_Strand(x, false);
        }
    }
}

// A bit that ACTUALLY crossed on this clock, carried across the channel.
//
// This is the difference between drawing the setting and drawing the machine.
// The braid above is WEAVE the parameter — how much crossing is likely — and at
// anything between the endpoints it is the same picture on the step where a bit
// crossed and the step where none did. The courier is the event, and it runs
// between the two cells the transfer is actually between: out of the sender's
// TAIL — the cell LENGTH selects, which is the one cell on the row with no other
// marking — and into the receiver's bit 0, where the next frame will draw it as
// the fresh bit. So the two halves of the animation join up.
//
// It replays a crossing that has already happened: the bit is in the receiver
// before the first frame of the step is drawn. Showing it in flight over the
// step it belongs to is the only reading that puts it on screen long enough to
// see at all, and it lands exactly where it went.
static void LOOM_DrawCouriers(float phase) {
    for (int r = 0; r < 2; r++) {
        if (!registers.Crossed((uint8_t)r)) {
            continue;
        }
        const int src = r ^ 1;
        const int fromX = LOOM_CellX(src, liveParams.length[src] - 1) + LOOM_CELL_W / 2 - 1;
        const int toX = LOOM_CellX(r, 0) + LOOM_CELL_W / 2 - 1;

        const bool down = (r == 1); // A ▸ B crosses downwards
        const int fromY = down ? (LOOM_CH_TOP + 1) : (LOOM_CH_BOT - 2);
        const int toY = down ? (LOOM_CH_BOT - 2) : (LOOM_CH_TOP + 1);

        display.fillRect(fromX + (int)((float)(toX - fromX) * phase),
                         fromY + (int)((float)(toY - fromY) * phase), 2, 2, WHITE);
    }
}

// ── The output windows ───────────────────────────────────────
//
// The cells one jack is actually reading, barred underneath, between the row and
// that jack's label.
//
// This is the answer to "where does the note come from". The label alone says
// only where a jack STARTS reading; a NOTE at DEPTH 5 is built from five cells,
// and until they are drawn the number on the OUT page is the only place the
// window exists. With the bar there, ROTATE walks it along the row and DEPTH
// grows it, and the pitch you are hearing has a visible source.
//
// Barred rather than boxed: the cell outline already means "bit = 0", so a
// second rectangle around a cell would read as a bit value.
//
// Drawn at CELL width normally and at PITCH width for the jack being previewed,
// which closes the 1 px gaps between adjacent cells into one solid bar. On a row
// where several jacks overlap — the DUO default puts a NOTE and its GATE on the
// same cells — that is what picks the one you are editing out of the pile,
// without needing a row the screen does not have.
static void LOOM_DrawWindow(int j, bool emphasis) {
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
        display.drawFastHLine(x, y, emphasis ? LOOM_PITCH : LOOM_CELL_W, WHITE);
    }
}

static void LOOM_DrawWindows() {
    for (int j = 0; j < WEA_NUM_OUTS; j++) {
        LOOM_DrawWindow(j, false);
    }
}

static void LOOM_DrawPage() {
    display.setTextSize(1);
    display.setTextColor(WHITE);

    // ── Header ──
    // The tempo large, everything else small and sitting on its baseline. The
    // whole point is the size CONTRAST: one number reads from across a room and
    // the rest is there when you look, which is a hierarchy the 6 px font cannot
    // express however it is arranged.
    //
    // Indented past the unsaved-changes dot, which core/displayManager.hpp draws
    // as a filled circle at (1,1) r=1 — so it covers x 0..2 and would sit on top
    // of the tempo's first digit at x=0.
    const bool ext = clockEngine.IsExternalLive();
    const String bpm = ext ? String((int)lroundf(clockEngine.GetEffectiveBpm()))
                           : String(clockEngine.GetBpm());

    // setFont BEFORE setCursor, always: Adafruit_GFX shifts cursor_y by 6 on the
    // classic↔custom changeover, so a cursor set first would be moved under us.
    display.setFont(&helvB12);
    display.setCursor(LOOM_HEADER_X, LOOM_BPM_BASELINE);
    display.print(bpm);
    // Back to the classic font immediately. Everything downstream — the loom, the
    // live strip, the menu pages, Rack's temporary-message overlay — assumes it,
    // and a font left set here would render all of them in 12 px.
    display.setFont(nullptr);

    // helvB12 digits advance 9 px each; +3 for a word space.
    const int bpmRight = LOOM_HEADER_X + (int)bpm.length() * 9 + 3;
    display.setCursor(bpmRight, LOOM_HEADER_SMALL_Y);
    if (ext) {
        // Spelled out rather than the old trailing "E" on the number: the E is
        // now 12 px tall and reads as a digit at a glance.
        display.print(F("EXT "));
    }
    display.print(clockEngine.RateName());

    String w = String("W ") + String(liveParams.weave) + "%";
    display.setCursor(SCREEN_WIDTH - (int)w.length() * 6, LOOM_HEADER_SMALL_Y);
    display.print(w);

    // ── The loom ──
    // Where we are between one clock and the next. Nothing draws it directly —
    // it is what the braid, the couriers and the fresh-bit mark move on.
    //
    // A full-width sweep line under the header was tried and removed: it was the
    // largest moving thing on a screen where it was the least informative, it
    // read as a progress bar for something that is not loading, and the three
    // animations below already say the module is running while also saying
    // something about the pattern.
    //
    // Below about two frames per step, the phase is a lie — successive redraws
    // sample unrelated points in the step — so the marks that depend on landing
    // inside a fraction of it are dropped. The registers are a blur at those
    // rates anyway.
    const float phase = clockEngine.StepPhase();
    const unsigned long stepUs = clockEngine.StepPeriodUs();
    const bool phaseReadable = stepUs > (unsigned long)(2 * WEA_DISPLAY_INTERVAL_MS) * 1000UL;
    const bool fresh = phaseReadable && phase < 0.35f;

    LOOM_DrawTailMark(0, LOOM_A_TAIL_Y);
    LOOM_DrawRegister(0, LOOM_A_Y, fresh);
    LOOM_DrawChannel(phase);
    LOOM_DrawCouriers(phase);
    LOOM_DrawTailMark(1, LOOM_B_TAIL_Y);
    LOOM_DrawRegister(1, LOOM_B_Y, fresh);
    LOOM_DrawTaps();
    LOOM_DrawWindows();

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
        // Down the columns — A1, A2, B1, B2 — not in jack-index order, which is
        // the rows. This list is read as a patching instruction while looking at
        // the panel, and the panel is laid out by column (Design.md §1). Same
        // order as the OUT pages either side of it and as the Rack context menu.
        for (int k = 0; k < WEA_NUM_OUTS; k++) {
            const int j = WEA_JACK_COLUMN_ORDER[k];
            const OutSlot &s = outputs.Slot(j);
            String line = String(OutJackNames[j]) + " " + OutTypeNames[s.type] +
                          " " + RegSourceNames[s.source];
            // ROTATE only when it is doing something — a column of "r0" is
            // noise, and a non-zero one is the thing worth noticing.
            if (s.rotate != 0) {
                line += " r" + String(s.rotate);
            }
            display.setCursor(10, WEA_ROUTING_INFO_Y + k * 9);
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
            // The loom already drew every jack's window; this redraws the one
            // being edited at full pitch, so it stands out of the stack.
            const int jack = LOOM_LiveJack(liveItem);
            if (jack >= 0) {
                LOOM_DrawWindow(jack, true);
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
