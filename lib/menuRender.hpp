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
extern DisplayManager displayMgr;
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
    display.setCursor(5, 0);
    display.print(getBpm());
    display.print("BPM");

    // Proximity is the module's signature control, so it is on the home screen
    // rather than buried a page away.
    String prx = "PRX" + String((int)lroundf(worldParams.proximity * 100.0f));
    display.setCursor(SCREEN_WIDTH - 2 - (int)prx.length() * 6, 0);
    display.print(prx);

    // ── Containers ──
    HOME_DrawContainer(0, true);
    HOME_DrawContainer(1, false);

    // ── Footer: the live note of each channel ──
    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        String note;
        if (channels[ch].GetSemitone() < 0) {
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
    "A NOTES",   //  5
    "B NOTES",   //  6
    "A GATE",    //  7
    "B GATE",    //  8
    "CV IN",     //  9
    "SETTINGS",  // 10
};

void HandleDisplay() {
    displayMgr.SetUnsavedChanges(unsavedChanges);

    if (menuItem < 1 || menuItem > MENU_ITEM_COUNT) {
        menuItem = 1;
    }
    bool onHome = (MENU_ITEMS[menuItem - 1].group == MENU_GROUP_HOME);

    // Drop back to the physics view after the configured idle period.
    if (displayMgr.ShouldTimeout(onHome, menuMode)) {
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
    if (onHome) {
        displayRefresh = 1;
        displayMgr.MarkDirty();
    }

    if (!displayRefresh || !displayMgr.ShouldUpdate()) {
        return;
    }

    displayMgr.BeginFrame();
    uint8_t grp = MENU_ITEMS[menuItem - 1].group;
    displayMgr.DrawMenuIndicator(menuItem, MENU_ITEM_COUNT, grp == MENU_GROUP_HOME);

    if (grp == MENU_GROUP_HOME) {
        HOME_DrawPage();
        RedrawDisplay();
        return;
    }

    if (grp < (uint8_t)(sizeof(groupTitles) / sizeof(groupTitles[0]))) {
        MD_RenderGroup(groupTitles[grp], menuItem, menuMode, grp);
        MD_PageEnd();
    }
}
