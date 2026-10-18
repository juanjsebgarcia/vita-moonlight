#pragma once

// Morpheus's touch editors (DESIGN.md S7, S8, S9): the corner buttons, the
// list that picks a corner's key, and the rear touchpad's dead zones.

#include <stdbool.h>

#include "../../ui_view.h"

// Draws the screen when it's one of the three, between mph_frame_begin()
// and mph_frame_end(), and returns true; returns false for any other screen.
// enter_t0 is when the screen came on (a new value starts the list afresh);
// animate says whether it slides in.
bool mph_screen_touch_draw(const menu_view *view, float enter_t0, bool animate);
