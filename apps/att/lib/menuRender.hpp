#pragma once
// ============================================================
// menuRender.hpp — MenuHeader, the Lissajous home screen, and
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
// menuMode is already declared extern in menuHandlers.hpp
extern ChaosWorld world;
extern GenParams genParams[2];
extern WorldParams worldParams;
extern uint8_t homeView;

// ── Thin display helpers ─────────────────────────────────────
inline void MenuHeader(const char *header) { displayMgr.DrawMenuHeader(header); }

// ── The Lissajous home screen ────────────────────────────────
//
// Each generator's two output values plotted against each other, which is the
// only view that shows what the module is actually doing: a pair of voltages
// that are obviously related, obviously not equal, and never quite repeating.
// Watching one trace against time would show a wobble; watching the pair shows
// the attractor.
//
// The trace is the generator's own trail buffer (generator.hpp), sampled on the
// orbit's clock rather than the frame clock — so the figure covers the same
// amount of trajectory at every SPEED instead of collapsing to a dot when slow
// and to a scribble when fast.
//
// ── Vertical budget (64 rows) ────────────────────────────────
//   0..8    header — system names, couple / freeze badge
//   10..63  the plot frame(s)
//
// The plotted value is the one BEFORE level and offset, so the figure keeps
// filling the frame however the jacks are scaled. What the screen is for is the
// shape; the voltage is what the jack is for.
#define PLOT_TOP 10
#define PLOT_H 54

// Redraw interval for this module, in ms. Faster than the 50 ms every other
// module uses, because every other module's home screen is a page that changes
// when you touch something, and this one is an animation that has to look like
// motion. 30 fps is where the head stops visibly stepping at the middle of the
// SPEED range; below about 20 fps it reads as a stutter even though the jacks
// are perfectly smooth. See DisplayManager::SetUpdateInterval for what it costs.
#define ATT_DISPLAY_INTERVAL_MS 33

struct PlotBox {
    int x, y, w, h;
};

// Normalised -1..1 to a pixel inside the box. Values outside the window are
// pinned to the frame rather than dropped, so a clipping output is visible as
// the trace flattening against the edge — which is exactly what the jack is
// doing at that moment.
static inline void PLOT_Map(const PlotBox &b, float nx, float ny, int &px, int &py) {
    const float cx = constrain(nx, -1.0f, 1.0f);
    const float cy = constrain(ny, -1.0f, 1.0f);
    px = b.x + 1 + (int)lroundf((cx + 1.0f) * 0.5f * (float)(b.w - 3));
    py = b.y + 1 + (int)lroundf((1.0f - (cy + 1.0f) * 0.5f) * (float)(b.h - 3));
}

static void PLOT_DrawGenerator(int g, const PlotBox &b) {
    display.drawRect(b.x, b.y, b.w, b.h, WHITE);

    const Generator &gen = world.Get(g);
    const int n = gen.TrailCount();

    // Centre cross-hairs, two pixels long, so a stationary orbit still says
    // where the middle of the frame is.
    display.drawPixel(b.x + b.w / 2, b.y + b.h / 2, WHITE);

    int px = 0, py = 0, lx = 0, ly = 0;
    for (int i = 0; i < n; i++) {
        float tx, ty;
        gen.TrailPoint(i, tx, ty);
        PLOT_Map(b, tx, ty, px, py);
        if (i > 0) {
            display.drawLine(lx, ly, px, py, WHITE);
        }
        lx = px;
        ly = py;
    }

    // ── The live end ────────────────────────────────────────────────────────
    // The head is drawn from the generator's CURRENT output, not from the newest
    // stored trail point, and the last segment runs from that point to here.
    //
    // This is what makes the plot look alive. Trail points are pushed on the
    // orbit's clock — at SPEED 1.00 that is one every 250 ms — so a plot drawn
    // only from the buffer changes four times a second however fast the renderer
    // runs, and the whole screen reads as a stuttering 4 fps animation while the
    // jacks are perfectly smooth. Drawing the live position instead costs one
    // line and one dot, and the picture then moves on every frame.
    PLOT_Map(b, gen.OutNorm(0), gen.OutNorm(1), px, py);
    if (n > 0) {
        display.drawLine(lx, ly, px, py, WHITE);
    }

    // Filled, because on a 1bpp screen a plain line is ambiguous about which end
    // is now — and "which end is now" is the only way to see which direction the
    // orbit is travelling.
    display.fillCircle(px, py, 1, WHITE);
}

static void HOME_DrawPage() {
    display.setTextSize(1);
    display.setTextColor(WHITE);

    const int view = constrain((int)homeView, 0, 2);

    // ── Header ──
    if (view == 0) {
        display.setCursor(2, 0);
        String leftAttr = String(AttSpec(genParams[0].system).shortName) + "(" + String(genParams[0].speed, 1) + "x)";
        display.print(leftAttr);
        String bn = String(AttSpec(genParams[1].system).shortName) + "(" + String(genParams[1].speed, 1) + "x)";
        display.setCursor(SCREEN_WIDTH - 2 - (int)bn.length() * 6, 0);
        display.print(bn);
    } else {
        // One generator gets the whole screen, so the header has room for the
        // system's full name and the rate it is being traced at — the two things
        // you are looking at the plot to judge.
        const int g = view - 1;
        display.setCursor(2, 0);
        display.print(g == 0 ? "A " : "B ");
        display.print(AttSpec(genParams[g].system).name);
        String sp = FmtSpeed(genParams[g].speed);
        display.setCursor(SCREEN_WIDTH - 2 - (int)sp.length() * 6, 0);
        display.print(sp);
    }

    // Centre badge. FREEZE wins over COUPLE: a held orbit explains everything
    // else on the screen, and it is the state most easily mistaken for a crash.
    {
        String badge;
        if (world.IsFrozen()) {
            badge = "FRZ";
        } else if (worldParams.couple > 0.0f) {
            badge = "C" + String((int)lroundf(worldParams.couple * 100.0f));
        }
        if (badge.length() > 0) {
            const int w = (int)badge.length() * 6;
            display.setCursor((SCREEN_WIDTH - w) / 2, 0);
            display.print(badge);
        }
    }

    // ── Plots ──
    if (view == 0) {
        const PlotBox a = {0, PLOT_TOP, 63, PLOT_H};
        const PlotBox b = {65, PLOT_TOP, 63, PLOT_H};
        PLOT_DrawGenerator(0, a);
        PLOT_DrawGenerator(1, b);
    } else {
        const PlotBox full = {0, PLOT_TOP, SCREEN_WIDTH, PLOT_H};
        PLOT_DrawGenerator(view - 1, full);
    }
}

// ── Main display renderer ─────────────────────────────────────
static const char *const groupTitles[] = {
    "",         // 0 — the plot (custom renderer)
    "A SYSTEM", // 1
    "A OUTPUT", // 2
    "B SYSTEM", // 3
    "B OUTPUT", // 4
    "LINK",     // 5
    "CV IN",    // 6
    "SETTINGS", // 7
    "PRESETS",  // 8
};

// ── Live parameter strip ──────────────────────────────────────
// The readout that rides on the plot while a livePreview parameter is being
// turned (see the live-view section of menuHandlers.hpp).
//
// Blacked out and framed rather than printed straight onto the plot: a trace
// passing behind bare text makes the digits unreadable at exactly the moment you
// are trying to read them.
#define LIVE_STRIP_Y 53
#define LIVE_STRIP_H 11

static void HOME_DrawLiveStrip(int item) {
    const MenuItem &mi = MENU_ITEMS[item - 1];

    // Both generators have a SPEED, so the bare label is ambiguous once the page
    // it came from is off screen. The group title already carries the side
    // ("A SYSTEM"), so borrow its first letter.
    String s;
    if (mi.group < (uint8_t)(sizeof(groupTitles) / sizeof(groupTitles[0]))) {
        const char *gt = groupTitles[mi.group];
        if ((gt[0] == 'A' || gt[0] == 'B') && gt[1] == ' ') {
            s = String(gt[0]) + " ";
        }
    }
    s += mi.label;
    // A parameter row's label is "P2"; on its own that says nothing, so the
    // strip carries the system's own name for it as well.
    if (mi.valueFn) {
        s += " " + mi.valueFn();
    }
    if (mi.valueFn2) {
        s += " " + mi.valueFn2();
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

void HandleDisplay() {
    displayMgr.SetUnsavedChanges(unsavedChanges);

    if (menuItem < 1 || menuItem > MENU_ITEM_COUNT) {
        menuItem = 1;
    }
    bool onHome = (MENU_ITEMS[menuItem - 1].group == MENU_GROUP_HOME);

    // The hold running out is a timer, not a user action, so nothing else is
    // going to mark the screen dirty. Catch the expiry edge here and force one
    // redraw, or the last plot frame — strip and all — stays frozen on the panel
    // until you happen to touch the encoder again.
    if (liveViewUntil != 0 && !LiveViewActive()) {
        LiveViewClear(); // fires exactly once: the deadline is now 0
        displayRefresh = 1;
        displayMgr.MarkDirty(); // dirty, not an interaction — see below
    }

    // Which item the strip reports: the one in edit mode, or the selected one
    // when a flagged MENU_TOGGLE (RANGE) armed the view from a click and so left
    // menuMode at 0.
    int liveItem = (menuMode >= 1 && menuMode <= MENU_ITEM_COUNT) ? menuMode : menuItem;
    bool liveView = !onHome && LiveViewActive() && MenuItemIsLive(liveItem);

    // Drop back to the plot after the configured idle period. The live view
    // already IS the plot, so it counts as home here: without that, a flagged
    // toggle — which never sets menuMode — would trip the timeout mid-preview
    // and strand you on the home screen rather than returning to the page you
    // clicked from.
    if (displayMgr.ShouldTimeout(onHome || liveView, menuMode)) {
        menuItem = 1;
        menuMode = 0;
        onHome = true;
        REQUEST_DISPLAY_REFRESH();
    }

    // The plot is an animation, not a static page: it has to redraw on the rate
    // limiter alone, without waiting for something to mark it dirty.
    //
    // BOTH flags have to be re-armed. RedrawDisplay() clears displayRefresh
    // after every frame, and the guard below requires displayRefresh AND
    // ShouldUpdate() — so marking dirty alone leaves the plot frozen on its
    // first frame forever. MarkDirty() rather than MarkInteraction(): this is not
    // user input, and resetting the interaction timer here would stop the menu
    // ever timing back out to this screen.
    if (onHome || liveView) {
        displayRefresh = 1;
        displayMgr.MarkDirty();
    }

    if (!displayRefresh || !displayMgr.ShouldUpdate()) {
        return;
    }

    displayMgr.BeginFrame();
    uint8_t grp = MENU_ITEMS[menuItem - 1].group;
    bool plotView = (grp == MENU_GROUP_HOME) || liveView;
    // No scroll bar over the live view: it is the plot for the moment, and the
    // strip already says where in the menu you are.
    displayMgr.DrawMenuIndicator(menuItem, MENU_ITEM_COUNT, plotView);

    if (plotView) {
        HOME_DrawPage();
        if (liveView) {
            HOME_DrawLiveStrip(liveItem);
        }
        RedrawDisplay();
        return;
    }

    if (grp < (uint8_t)(sizeof(groupTitles) / sizeof(groupTitles[0]))) {
        MD_RenderGroup(groupTitles[grp], menuItem, menuMode, grp);
        MD_PageEnd();
    }
}
