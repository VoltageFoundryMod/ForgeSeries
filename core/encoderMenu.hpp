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
//
// Both hooks are trivial for most modules; menuHandlers.hpp is the natural
// place for them. GravityForge is the one that uses them for real — it drops a
// toggle-armed physics preview when you navigate away.

#include "encoderAccel.hpp"

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
        menuItem += dir;
        if (menuItem < 1)
            menuItem = MENU_ITEM_COUNT;
        else if (menuItem > MENU_ITEM_COUNT)
            menuItem = 1;
        OnMenuNavigate();
    } else if (menuMode >= 1 && menuMode <= MENU_ITEM_COUNT) {
        MenuApplyEdit(menuMode, dir * (int)speedFactor);
    }
}
