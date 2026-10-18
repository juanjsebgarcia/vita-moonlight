#pragma once

// Morpheus's host screens (DESIGN.md §4 S1 to S3): the host list (Home), a
// paired PC's menu (PC options) and the device search (Find a PC).

#include "mph.h"

// Draws the screen if it's one of these three, and returns whether it did.
// entered_at changes whenever a new screen comes on (it starts the layout
// afresh); animate says whether that screen plays the enter transition.
// Called inside mph_frame_begin() ... mph_frame_end().
bool mph_hosts_draw(const menu_view *view, float entered_at, bool animate);
// menu_follow for the same screens: stores the scroll that keeps the
// selected row in view. Returns false for other screens.
bool mph_hosts_follow(menu_view *view);
