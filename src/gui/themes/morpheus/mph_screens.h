#pragma once

// The screens with their own layout (DESIGN.md §4), one file each, and how
// morpheus.c reaches them.
//
// Menu screens come in pairs, dispatched by view->screen->id:
//   draw:   called between mph_frame_begin() and mph_frame_end(), which the
//           dispatcher calls; draws the whole screen, applying the enter
//           transition with mph_enter_apply(enter) (or mph_enter_k() for
//           its own opacities) and leaving the transform reset at the end.
//           mph_enter_is_new() tells when to start its state afresh.
//   follow: menu_follow: stores in view->scroll the scroll that keeps the
//           selected row in view, measured the same way draw lays it out.
// Dialogs and overlays bracket their own frames.

#include "mph.h"

// mph_screen_hosts.c: S1 the host list as Home, S2 a paired PC's menu, S3
// the device search
void mph_screen_main_draw(const menu_view *view, const mph_enter *enter);
void mph_screen_main_follow(menu_view *view);
void mph_screen_host_draw(const menu_view *view, const mph_enter *enter);
void mph_screen_host_follow(menu_view *view);
void mph_screen_search_draw(const menu_view *view, const mph_enter *enter);
void mph_screen_search_follow(menu_view *view);

// mph_screen_connected.c: S4 a connected PC's menu, with the box art shelf
void mph_screen_connected_draw(const menu_view *view, const mph_enter *enter);
void mph_screen_connected_follow(menu_view *view);

// mph_screen_settings.c: S5 Settings, S6 Apply recommended settings, S10
// Keyboard layout
void mph_screen_settings_draw(const menu_view *view, const mph_enter *enter);
void mph_screen_settings_follow(menu_view *view);
void mph_screen_recommended_draw(const menu_view *view, const mph_enter *enter);
void mph_screen_recommended_follow(menu_view *view);
void mph_screen_keyboard_draw(const menu_view *view, const mph_enter *enter);
void mph_screen_keyboard_follow(menu_view *view);

// mph_screen_touch.c: S7 the touch corner buttons, S8 a corner's key, S9
// the rear touchpad's dead zones
void mph_screen_special_keys_draw(const menu_view *view, const mph_enter *enter);
void mph_screen_key_list_draw(const menu_view *view, const mph_enter *enter);
void mph_screen_deadzone_draw(const menu_view *view, const mph_enter *enter);
void mph_screen_touch_follow(menu_view *view);

// mph_screen_dialogs.c: S11 alerts, errors, confirmations, flash messages
// and help; S13 the IME backdrop
void mph_screen_alert_draw(const alert_view *view);
int mph_screen_help_prepare(const char *title, const char *message);
void mph_screen_help_draw(const help_view *view);
void mph_screen_ime_backdrop_draw(void);

// mph_screen_stream.c: S12 the stream overlays, drawn by the video thread
// on every decoded frame
void mph_screen_stats_draw(const stats_view *view);
void mph_screen_stream_overlay_draw(const stream_overlay_view *view);
