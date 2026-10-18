#pragma once

// S4, a connected PC's menu (DESIGN.md §4 S4): the header, the action
// column and the box art shelf, or one list in text mode.

#include "mph.h"

// Inside mph_draw_menu's frame. enter is the screen's enter transition, 0 to
// 1 (1 when it isn't playing).
void mph_screen_connected_draw(const menu_view *view, float enter);
// menu_follow: stores the scroll that keeps the selected row in view
void mph_screen_connected_follow(menu_view *view);
