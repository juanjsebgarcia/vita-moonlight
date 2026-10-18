#pragma once

// Morpheus's Settings screens (DESIGN.md S5, S6 and S10): Settings with its
// tabs and help card, Apply recommended settings, and Keyboard layout.
// morpheus.c's list screen hands these screens over by id, after starting
// the frame; each draws everything and ends the frame.

#include "mph.h"

void mph_screen_settings_draw(const menu_view *view);
void mph_screen_settings_follow(menu_view *view);
void mph_screen_recommended_draw(const menu_view *view);
void mph_screen_recommended_follow(menu_view *view);
void mph_screen_keyboard_draw(const menu_view *view);
void mph_screen_keyboard_follow(menu_view *view);

// From morpheus.c: applies the screen's enter slide (when it plays) to what's
// drawn next, and returns when the screen on show appeared, which changes
// whenever the screen does (the slide doesn't replay on a return to the
// screen before)
float mph_screen_enter(void);
