#pragma once
// ============================================================
// menuRender.hpp — MenuHeader, the physics home screen, and
// HandleDisplay().
//
// Included from main.cpp after menuDisplay.hpp. Relies on macros
// (SCREEN_WIDTH, REQUEST_DISPLAY_REFRESH) and types defined
// earlier in the translation unit.
// ============================================================

// ── Globals owned by main.cpp ────────────────────────────────
extern bool displayRefresh;
extern bool unsavedChanges;
extern int menuItem;
// menuMode is already declared extern in menuHandlers.hpp
extern GravityChannel channels[NUM_CHANNELS];
extern PhysicsWorld physicsWorld;
extern Clock clockEngine;
extern WorldParams worldParams;

// ── Thin display helpers ─────────────────────────────────────
inline void MenuHeader(const char *header) {
    displayMgr.DrawMenuHeader(header);
}

// ── Physics home screen ──────────────────────────────────────
//
// The containers are drawn where the simulation actually puts them: physics.hpp
// works directly in screen pixels, so nothing is transformed here and what you
// see is literally the state. Sliding PROXIMITY moves the circles together on
// screen because it moves them in the sim.
//
// ── Vertical budget (64 rows) ────────────────────────────────
//   0..7    header — tempo left, proximity right
//   12..52  the two containers (centre y=32, r=20)
//   55..63  live note per channel
//
// The containers own the middle band outright; the header and footer are sized
// to what is left. Changing PHYS_R means re-deriving this.
#define HOME_FOOTER_Y 55

static void HOME_DrawContainer(int idx, bool filledBalls) {
    Container &c = physicsWorld.Get(idx);
    int cx = (int)lroundf(c.CentreX());
    int cy = (int)lroundf(c.CentreY());

    display.drawCircle(cx, cy, (int)PHYS_R, WHITE);

    // Pegs on the rim. An enabled peg is a solid dot, a muted one a small hollow
    // ring — the difference has to read at a glance on a 1bpp screen, because
    // muting pegs is how you open up the rhythm.
    //
    // Sizes are set by the tightest case, 16 pegs: the rim is 2*pi*20 = 126 px,
    // so pegs sit 7.9 px apart. A radius-2 dot (5 px) still leaves clear air
    // between neighbours, and the radius-3 fire flash (7 px) just fills the gap,
    // which is what makes a hit unmistakable.
    for (int i = 0; i < c.GetPegCount(); i++) {
        float px, py;
        c.PegPosition(i, px, py);
        int x = (int)lroundf(px);
        int y = (int)lroundf(py);
        if (c.PegFlash(i)) {
            display.fillCircle(x, y, 3, WHITE); // just fired
        } else if (c.GetPegEnabled(i)) {
            display.fillCircle(x, y, 2, WHITE);
        } else {
            display.drawCircle(x, y, 1, WHITE);
        }
    }

    // Balls, drawn at their true simulated radius (PHYS_BALL_R). Container A is
    // filled and B hollow so the two populations stay tellable apart once
    // PROXIMITY merges the circles into one space.
    for (int i = 0; i < c.GetBallCount(); i++) {
        const Ball &b = c.GetBall(i);
        int x = (int)lroundf(b.x);
        int y = (int)lroundf(b.y);
        int r = (int)PHYS_BALL_R;
        if (filledBalls) {
            display.fillCircle(x, y, r, WHITE);
        } else {
            // Hollow needs a bright centre pixel too: an unfilled ring this
            // small reads as a peg at a glance, and the ball is the thing the
            // eye should track.
            display.drawCircle(x, y, r, WHITE);
            display.drawPixel(x, y, WHITE);
        }
    }
}

static void HOME_DrawPage() {
    display.setTextSize(1);
    display.setTextColor(WHITE);

    // ── Header ──
    // Measured rather than assumed: the tempo readout grows an "E" under an
    // external clock and PRX loses a digit below 100 %, so both ends of the
    // header move and the loop badge between them has to be placed from their
    // real widths.
    String bpm = getBpm() + "BPM";
    display.setCursor(5, 0);
    display.print(bpm);
    int bpmEnd = 5 + (int)bpm.length() * 6;

    // Proximity is the module's signature control, so it is on the home screen
    // rather than buried a page away.
    String prx = "PRX" + String((int)lroundf(worldParams.proximity * 100.0f));
    int prxX = SCREEN_WIDTH - 2 - (int)prx.length() * 6;
    display.setCursor(prxX, 0);
    display.print(prx);

    // Loop position, centred in the gap the two readouts leave. Without it a
    // repeating phrase is indistinguishable from a lucky patch — the counter is
    // what tells you the module is looping and where in the bar it is, which is
    // the whole point of setting a length in beats.
    if (physicsWorld.LoopActive()) {
        String lp = "L" + String(physicsWorld.LoopBeat()) + "/" +
                    String(physicsWorld.LoopBeats());
        int w = (int)lp.length() * 6;
        int x = (SCREEN_WIDTH - w) / 2;
        // Tempo and proximity are the readouts that must stay legible, so when
        // the gap is too narrow the badge is what gets dropped.
        if (x >= bpmEnd + 2 && x + w <= prxX - 2) {
            display.setCursor(x, 0);
            display.print(lp);
        }
    }

    // ── Containers ──
    HOME_DrawContainer(0, true);
    HOME_DrawContainer(1, false);

    // ── Footer: the live note of each channel ──
    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        String note;
        // A napping channel is still bouncing on screen, so without a marker the
        // silence reads as a broken output rather than a rest the module was
        // told to take. "zz" says asleep, and it says it in two characters.
        if (channels[ch].IsMuted()) {
            note = "zz";
        } else if (channels[ch].GetSemitone() < 0) {
            note = "--";
        } else {
            note = String(noteNames[channels[ch].GetNoteIndex()]) +
                   String(channels[ch].GetOctaveOut());
        }
        String label = String(ch == 0 ? "A:" : "B:") + note;
        int w = (int)label.length() * 6;
        int x = (ch == 0) ? 2 : (SCREEN_WIDTH - 2 - w);

        // Invert while the gate is up — the same "something is happening now"
        if (channels[ch].IsGateActive()) {
            display.fillRect(x - 1, HOME_FOOTER_Y - 1, w + 2, 9, WHITE);
            display.setTextColor(BLACK);
        }
        display.setCursor(x, HOME_FOOTER_Y);
        display.print(label);
        display.setTextColor(WHITE);
    }

    // ── Coupling spark ──
    // An expanding ring where a strike in one container transmitted into the
    // other. Drawn last so it sits on top of both rims.
    //
    // This is the only thing that makes PROXIMITY legible. The impulse itself is
    // strong (it displaces balls by ~13 px) but it fires only a handful of times
    // a second while both containers are already bouncing, so with nothing to
    // tie cause to effect the whole control reads as if it does nothing.
    if (physicsWorld.CoupleFlashActive()) {
        int cx = (int)lroundf(physicsWorld.CoupleX());
        int cy = (int)lroundf(physicsWorld.CoupleY());
        int r = 7 - (int)physicsWorld.CoupleFlash(); // expands as it fades
        display.drawCircle(cx, cy, r, WHITE);
        display.drawCircle(cx, cy, r + 2, WHITE);
    }

    // Flashes are measured in frames, so they decay here rather than in the
    // simulation — that keeps them a fixed visible duration regardless of how
    // many physics steps happened to run between redraws.
    physicsWorld.Get(0).DecayPegFlash();
    physicsWorld.Get(1).DecayPegFlash();
    physicsWorld.DecayCoupleFlash();
}

// ── Main display renderer ─────────────────────────────────────
static const char *const groupTitles[] = {
    "",          //  0 — physics home screen (custom renderer)
    "CLOCK",     //  1
    "COUPLING",  //  2
    "A PHYSICS", //  3
    "B PHYSICS", //  4
    "LOOP",      //  5
    "A NOTES",   //  6
    "B NOTES",   //  7
    "A GATE",    //  8
    "B GATE",    //  9
    "CV IN",     // 10
    "SETTINGS",  // 11
};

// ── Live parameter strip ──────────────────────────────────────
// The readout that rides on the physics view while a livePreview parameter is
// being turned (see the live-view section of menuHandlers.hpp).
//
// It occupies rows 53..63 — the band between the bottom of the containers
// (centre y=32 plus PHYS_R=20) and the bottom of the screen. That band normally
// carries the A:/B: note footer, and the footer is what gives way: while you are
// turning GRAVITY the value in your hand matters more than the note that just
// fired, and the strip is only up for as long as you keep turning.
//
// Blacked out and framed rather than printed straight onto the physics: a ball
// passing behind bare text makes the digits unreadable at exactly the moment you
// are trying to read them.
#define LIVE_STRIP_Y 53
#define LIVE_STRIP_H 11

static void HOME_DrawLiveStrip(int item) {
    const MenuItem &mi = MENU_ITEMS[item - 1];

    // Both containers have a GRAVITY, so the bare label is ambiguous once the
    // page it came from is off screen. The group title already carries the side
    // ("A PHYSICS"), so borrow its first letter.
    String s;
    if (mi.group < (uint8_t)(sizeof(groupTitles) / sizeof(groupTitles[0]))) {
        const char *gt = groupTitles[mi.group];
        if ((gt[0] == 'A' || gt[0] == 'B') && gt[1] == ' ') {
            s = String(gt[0]) + " ";
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

    // Blank the whole band, not just the box: the A:/B: note labels start at
    // x=2 and run past where a long readout's frame lands, so clearing only the
    // box footprint leaves half a glyph poking out either side of it.
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
    // redraw, or the last physics frame — strip and all — stays frozen on the
    // panel until you happen to touch the encoder again.
    if (liveViewUntil != 0 && !LiveViewActive()) {
        LiveViewClear(); // fires exactly once: the deadline is now 0
        displayRefresh = 1;
        displayMgr.MarkDirty(); // dirty, not an interaction — see below
    }

    // Which item the strip reports: the one in edit mode, or the selected one
    // when a flagged MENU_TOGGLE (DIR) armed the view from a click and so left
    // menuMode at 0.
    int liveItem = (menuMode >= 1 && menuMode <= MENU_ITEM_COUNT) ? menuMode : menuItem;
    bool liveView = !onHome && LiveViewActive() && MenuItemIsLive(liveItem);

    // Drop back to the physics view after the configured idle period. The live
    // view already *is* the physics view, so it counts as home here: without
    // that, a flagged toggle — which never sets menuMode — would trip the
    // timeout mid-preview and strand you on the home screen rather than
    // returning to the page you clicked from.
    if (displayMgr.ShouldTimeout(onHome || liveView, menuMode)) {
        menuItem = 1;
        menuMode = 0;
        onHome = true;
        REQUEST_DISPLAY_REFRESH();
    }

    // The home screen is an animation, not a static page: it has to redraw on
    // the rate limiter alone, without waiting for something to mark it dirty.
    //
    // BOTH flags have to be re-armed. RedrawDisplay() clears displayRefresh
    // after every frame, and the guard below requires displayRefresh AND
    // ShouldUpdate() — so marking dirty alone leaves the physics view frozen on
    // its first frame forever. MarkDirty() rather than MarkInteraction(): this
    // is not user input, and resetting the interaction timer here would stop the
    // menu ever timing back out to this screen's own idle return.
    //
    // The live view is the same animation and needs the same treatment, or the
    // balls freeze between detents and the preview shows nothing worth seeing.
    if (onHome || liveView) {
        displayRefresh = 1;
        displayMgr.MarkDirty();
    }

    if (!displayRefresh || !displayMgr.ShouldUpdate()) {
        return;
    }

    displayMgr.BeginFrame();
    uint8_t grp = MENU_ITEMS[menuItem - 1].group;
    bool physicsView = (grp == MENU_GROUP_HOME) || liveView;
    // No scroll bar over the live view: it is the physics screen for the moment,
    // and the strip already says where in the menu you are.
    displayMgr.DrawMenuIndicator(menuItem, MENU_ITEM_COUNT, physicsView);

    if (physicsView) {
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
