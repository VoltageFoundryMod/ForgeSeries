#pragma once

// encoderMenu.hpp — what a detent does to a menu, shared by every module.
//
// The navigate/edit behaviour was identical in ClockForge and NoteForge and
// differed in GravityForge by exactly two things, both of which are now hooks.
//
// Include INSIDE the app's namespace: it reads the app's menu state, and the
// unified firmware gives each module its own. See core/appDisplay.hpp for the
// same reasoning.
//
// REQUIRES from the including TU:
//   menuItem, menuMode, lastEncoderUpdate, MENU_ITEMS[], MENU_ITEM_COUNT
//   REQUEST_DISPLAY_REFRESH()
//   MenuApplyEdit(int item, int delta)  — apply one edit step to `item`
//   OnMenuNavigate()                    — cursor moved off an item
//   OnItemActivated(const MenuItem &)   — a MENU_ACTION/MENU_TOGGLE was clicked
//   OnEnterEdit(int item)               — just entered edit mode on `item`
//   OnExitEdit(int item)                — about to leave edit mode on `item`
//
// Both hooks are trivial for most modules; menuHandlers.hpp is the natural
// place for them. GravityForge is the one that uses them for real — it drops a
// toggle-armed physics preview when you navigate away.

#include "encoderAccel.hpp"

// Item visibility. A module whose menu has rows that only apply to hardware
// that may not be attached — ClockForge's expander pages — defines this to
// hide them; navigation then steps over them as though they were not in the
// table. Guarded so the modules that have no such rows define nothing.
//
// Cheap by construction: it is only consulted while the cursor is moving.
#ifndef FORGE_MENU_ITEM_ENABLED
#define FORGE_MENU_ITEM_ENABLED(item) true
#endif

// One acted-on detent. `detents` is +clockwise / -counter-clockwise; the shell
// (or the Rack port) has already done the quarter-step edge detection, so this
// only ever sees a direction.
inline void MenuEncoderTurn(int detents) {
    if (detents == 0)
        return;
    const int dir = (detents > 0) ? 1 : -1;
    UpdateSpeedFactor(dir);
    REQUEST_DISPLAY_REFRESH();
    lastEncoderUpdate = millis();

    if (menuMode == 0) {
        // Step until a visible item, wrapping. The guard bounds the walk at one
        // full lap so an all-disabled table cannot spin here.
        for (int guard = 0; guard < MENU_ITEM_COUNT; guard++) {
            menuItem += dir;
            if (menuItem < 1)
                menuItem = MENU_ITEM_COUNT;
            else if (menuItem > MENU_ITEM_COUNT)
                menuItem = 1;
            if (FORGE_MENU_ITEM_ENABLED(menuItem))
                break;
        }
        OnMenuNavigate();
    } else if (menuMode >= 1 && menuMode <= MENU_ITEM_COUNT) {
        MenuApplyEdit(menuMode, dir * (int)speedFactor);
    }
}

// The encoder button. Acts on the RELEASE edge, which is also what makes the
// shell's hold-to-switch gesture safe: a hold ends in a reboot, so the release
// never arrives and the app never treats it as a click.
//
// Hook ordering is not arbitrary. OnExitEdit() runs BEFORE menuMode is cleared,
// because ClockForge commits its pending CV target using it; OnEnterEdit() runs
// after menuMode is set, and OnItemActivated() after the item's own action().
inline void MenuEncoderButton(bool pressed) {
    oldSwitchState = switchState;
    switchState = pressed ? 0 : 1; // active-low, matching the raw pin
    if (switchState != 1 || oldSwitchState != 0)
        return; // act on release only

    lastEncoderUpdate = millis();
    REQUEST_DISPLAY_REFRESH();

    if (menuMode != 0) {
        OnExitEdit(menuMode);
        menuMode = 0;
        return;
    }

    if (menuItem >= 1 && menuItem <= MENU_ITEM_COUNT) {
        const MenuItem &mi = MENU_ITEMS[menuItem - 1];
        if (mi.type == MENU_ACTION || mi.type == MENU_TOGGLE) {
            if (mi.action)
                mi.action();
            OnItemActivated(mi);
        } else { // MENU_EDIT
            menuMode = menuItem;
            OnEnterEdit(menuItem);
        }
    }
}
