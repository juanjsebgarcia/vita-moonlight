// Morpheus: the new theme, opted into with L and R on the host list.
//
// Drawn with the primitives in mph.h (see README.md here). This file holds
// the theme's entry points and dispatches them to the screens in
// mph_screen_*.c (mph_screens.h), plus a plain list for any screen without
// a layout of its own, and the developer gallery. It shares no code with
// Homebrew.

#include "mph.h"
#include "mph_screens.h"

#include "../../theme.h"
#include "../../../config.h"

#include <math.h>
#include <string.h>

static void morpheus_init(void) {
  vita2d_set_clear_color(RGBA8(0x0E, 0x11, 0x18, 0xFF));
  mph_init();
}

static void gallery_fini(void);

static void morpheus_fini(void) {
  gallery_fini();
  mph_fini();
}

static void morpheus_language_changed(void) {
  mph_text_language_changed();
}

static void frame_end(void) {
  mph_frame_end(config.developer_mode);
}

/*
 * Menu screens
 */

// The screen on show. A new one plays the enter transition, except going
// back to the screen before the last one: the parent's loop just carries on.
static struct {
  ui_screen_id id, previous;
  bool shown;
  mph_enter enter;
} screen;

static void track_screen(ui_screen_id id) {
  if (screen.shown && id == screen.id) {
    return;
  }
  screen.enter.animate = !screen.shown || id != screen.previous;
  screen.enter.t0 = mph_time();
  screen.previous = screen.shown ? screen.id : id;
  screen.id = id;
  screen.shown = true;
}

// A plain list, for a screen without a layout of its own (every screen has
// one today)
static mph_list_state list;
static float list_seen = -1;
static mph_row rows[MPH_LIST_MAX];

static mph_rect list_rect(void) {
  return mph_rect_make(180, 56, 600, MPH_CONTENT_BOTTOM - 56);
}

static void list_draw(const menu_view *view, const mph_enter *enter) {
  if (mph_enter_is_new(enter, &list_seen)) {
    mph_list_reset(&list);
  }
  int n = mph_rows_from_entries(rows, view->rows, view->row_count);
  mph_rect rect = list_rect();
  float target = mph_list_target(rect, rows, n, view->cursor_row, view->scroll);
  mph_bg_draw();
  mph_statusbar(view->screen->title != NULL ? view->screen->title : "Moonlight");
  mph_enter_apply(enter);
  mph_list(rect, rows, n, view->cursor_row, target, &list, rect.x + rect.w + 8);
  mph_transform_reset();
  mph_menu_legend(view);
}

static void list_follow(menu_view *view) {
  int n = mph_rows_from_entries(rows, view->rows, view->row_count);
  view->scroll = (int)roundf(mph_list_target(list_rect(), rows, n, view->cursor_row, view->scroll));
}

static void mph_draw_menu(const menu_view *view) {
  mph_frame_begin();
  track_screen(view->screen->id);
  const mph_enter *enter = &screen.enter;
  switch (view->screen->id) {
    case UI_SCREEN_MAIN: mph_screen_main_draw(view, enter); break;
    case UI_SCREEN_HOST: mph_screen_host_draw(view, enter); break;
    case UI_SCREEN_SEARCH: mph_screen_search_draw(view, enter); break;
    case UI_SCREEN_CONNECTED: mph_screen_connected_draw(view, enter); break;
    case UI_SCREEN_SETTINGS: mph_screen_settings_draw(view, enter); break;
    case UI_SCREEN_RECOMMENDED: mph_screen_recommended_draw(view, enter); break;
    case UI_SCREEN_KEYBOARD_LAYOUT: mph_screen_keyboard_draw(view, enter); break;
    case UI_SCREEN_SPECIAL_KEYS: mph_screen_special_keys_draw(view, enter); break;
    case UI_SCREEN_SPECIAL_KEY_LIST: mph_screen_key_list_draw(view, enter); break;
    case UI_SCREEN_DEADZONE: mph_screen_deadzone_draw(view, enter); break;
    default: list_draw(view, enter); break;
  }
  mph_transform_reset();
  frame_end();
}

static void mph_menu_follow(menu_view *view) {
  switch (view->screen->id) {
    case UI_SCREEN_MAIN: mph_screen_main_follow(view); break;
    case UI_SCREEN_HOST: mph_screen_host_follow(view); break;
    case UI_SCREEN_SEARCH: mph_screen_search_follow(view); break;
    case UI_SCREEN_CONNECTED: mph_screen_connected_follow(view); break;
    case UI_SCREEN_SETTINGS: mph_screen_settings_follow(view); break;
    case UI_SCREEN_RECOMMENDED: mph_screen_recommended_follow(view); break;
    case UI_SCREEN_KEYBOARD_LAYOUT: mph_screen_keyboard_follow(view); break;
    case UI_SCREEN_SPECIAL_KEYS:
    case UI_SCREEN_SPECIAL_KEY_LIST:
    case UI_SCREEN_DEADZONE: mph_screen_touch_follow(view); break;
    default: list_follow(view); break;
  }
}

/*
 * Dialogs, the IME backdrop and the stream overlays: mph_screen_dialogs.c and
 * mph_screen_stream.c. The help dialog also opens the developer gallery.
 */

static void draw_gallery(void);

static void mph_draw_help(const help_view *view) {
  if (config.developer_mode && view->title != NULL && strcmp(view->title, "mph:gallery") == 0) {
    draw_gallery();
    return;
  }
  mph_screen_help_draw(view);
}

/*
 * Developer gallery: every primitive on one screen, for checking them on the
 * Vita. Opened with the developer screen request "help mph:gallery|".
 */

// A stand-in cover for the gallery's textured tile, made the first time
static vita2d_texture *gallery_cover;

static void gallery_fini(void) {
  if (gallery_cover != NULL) {
    vita2d_free_texture(gallery_cover);
    gallery_cover = NULL;
  }
}

static const vita2d_texture *gallery_texture(void) {
  if (gallery_cover == NULL) {
    gallery_cover = vita2d_create_empty_texture(64, 64);
    if (gallery_cover != NULL) {
      uint32_t *data = vita2d_texture_get_datap(gallery_cover);
      unsigned int stride = vita2d_texture_get_stride(gallery_cover) / 4;
      for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
          data[y * stride + x] = RGBA8(40 + x * 3, 30 + y, 120 - y, 0xFF) | (((x ^ y) & 8) ? 0x00202020 : 0);
        }
      }
    }
  }
  return gallery_cover;
}

static void draw_gallery(void) {
  static mph_list_state gallery_list;
  static const mph_row demo[] = {
    {.kind = MPH_ROW_CATEGORY, .label = "CATEGORY", .icon = -1},
    {.kind = MPH_ROW_VALUE, .label = "Resolution", .value = "960x544", .icon = -1},
    {.kind = MPH_ROW_CHOICE, .label = "Frame rate", .value = "60 fps", .icon = -1},
    {.kind = MPH_ROW_TOGGLE, .label = "Optimise game settings", .on = true, .icon = -1},
    {.kind = MPH_ROW_TOGGLE, .label = "Also play sound on the PC", .on = false, .icon = -1},
    {.kind = MPH_ROW_ACTION, .label = "Corner buttons", .value = "4 set", .icon = -1},
    {.kind = MPH_ROW_SEPARATOR, .icon = -1},
    {.kind = MPH_ROW_CHECK, .label = "English (US)", .checked = true, .icon = -1},
    {.kind = MPH_ROW_PLAIN, .label = "DESKTOP-RYZEN", .status = MENU_STATUS_ONLINE, .icon = -1},
    {.kind = MPH_ROW_VALUE, .label = "Laptop", .value = "[IP changed]", .status = MENU_STATUS_CHANGED, .icon = -1},
    {.kind = MPH_ROW_PLAIN, .label = "Delete", .danger = true, .icon = -1},
    {.kind = MPH_ROW_PLAIN, .label = "Settings", .icon = MPH_ICON_GEAR_18},
    {.kind = MPH_ROW_NOTE, .label = "Changes apply to the next stream", .disabled = true, .icon = -1},
  };
  int n = sizeof(demo) / sizeof(demo[0]);
  int focused = 2 + (int)(mph_time() / 1.5f) % 3;

  mph_frame_begin();
  mph_bg_draw();
  mph_statusbar("mph:gallery");
  mph_rect view = mph_rect_make(40, 52, 420, 448);
  mph_list(view, demo, n, focused, mph_list_target(view, demo, n, focused, 0), &gallery_list, 464);

  // Shapes
  mph_card(mph_rect_make(490, 52, 200, 96));
  mph_shadow(mph_rect_make(510, 66, 70, 46), 12, 0, 10, 28, MPH_C_SHADOW);
  mph_rrect_fill(mph_rect_make(510, 66, 70, 46), 12, MPH_C_HL_BG);
  mph_rrect_stroke(mph_rect_make(600, 66, 70, 46), 12, 3, MPH_C_ACCENT);
  mph_rrect_vgradient(mph_rect_make(490, 160, 200, 30), 8, MPH_C_ACCENT, MPH_C_BAD);
  mph_circle_fill(510, 214, 6, MPH_C_OK);
  mph_circle_stroke(532, 214, 5, 1.5f, MPH_C_FAINT);
  mph_diamond(554, 214, 4.5f, MPH_C_LIVE);
  mph_live_dot(578, 214);
  mph_spinner(606, 214);
  mph_line(630, 206, 680, 222, 1.5f, MPH_C_FG);
  mph_switch(490, 236, 0, false, false);
  mph_switch(540, 236, 1, false, false);
  mph_switch(590, 236, fmodf(mph_time(), 1.0f), false, false);
  mph_meter10(490, 276, 150, 7);
  mph_dashed_rect(mph_rect_make(490, 296, 90, 50), 1.5f, 4, 3, MPH_C_OK);
  mph_hatch(mph_rect_make(590, 296, 90, 50), MPH_C_DEAD_STRIPE);
  float sweep = fmodf(mph_time(), 2.4f) / 2.4f * 2 * (float)M_PI;
  mph_circle_fill(740, 330, 32, RGBA8(0xA9, 0xC8, 0xFF, 0x24));
  mph_sector_gradient(740, 330, 74, sweep - (float)M_PI_2, sweep, RGBA8(0xA9, 0xC8, 0xFF, 0), RGBA8(0xA9, 0xC8, 0xFF, 0x59));
  mph_tile(NULL, "Steam Big Picture", mph_rect_make(820, 60, 100, 132), 1.07f, 2, false);
  mph_tile(gallery_texture(), NULL, mph_rect_make(840, 280, 70, 92), 1.0f, 1, false);
  mph_tile(gallery_texture(), NULL, mph_rect_make(840, 390, 70, 92), 1.0f, 0, true);

  // Controls and icons
  float x = 490;
  x += mph_button(x, 360, BUTTON_GLYPH_CROSS, "Resume", MPH_BTN_PRIMARY, true) + 14;
  mph_button(x, 360, BUTTON_GLYPH_NONE, "Cancel", MPH_BTN_SECONDARY | MPH_BTN_SMALL, false);
  x = 490;
  x += mph_chip(x, 416, "10.0.0.42:47989", MPH_C_DIM) + 8;
  x += mph_chip(x, 416, "Intel Arc B580", MPH_C_DIM) + 8;
  mph_keycap(x, 417, "START");
  static const mph_icon_id icons[] = {MPH_ICON_CHECK_18, MPH_ICON_CHEVRON_14, MPH_ICON_GEAR_18, MPH_ICON_POWER_18,
                                   MPH_ICON_SEARCH_30, MPH_ICON_PLUS_30, MPH_ICON_PC_30, MPH_ICON_SPRINT_22,
                                   MPH_ICON_WIFI_OUT_22, MPH_ICON_WIFI_MID_22, MPH_ICON_WIFI_DOT_22};
  x = 490;
  for (size_t i = 0; i < sizeof(icons) / sizeof(icons[0]); i++) {
    mph_icon(icons[i], x, 446, MPH_C_FG);
    x += mph_icon_w(icons[i]) + 8;
  }
  mph_text(MPH_T_SMALL, 490, 490, MPH_C_DIM,
           "{confirm} {cancel} {triangle} {square} {l} {r} {start} {select} {up} {left} {dpad} {left_stick} {front_touch}");

  // Type ramp
  mph_text(MPH_T_HERO, 700, 120, MPH_C_FG, "Hero");
  mph_text(MPH_T_TITLE_S, 700, 156, MPH_C_FG, "Title S");
  mph_text(MPH_T_DIALOG, 700, 186, MPH_C_FG, "Dialog");
  mph_text_tracked(MPH_T_STRONG_S, 700, 210, 2.08f, MPH_C_LIVE, mph_upper("current connection"));
  mph_text(MPH_T_META, 700, 230, MPH_C_DIM, "Meta 13 · ÁÉÍÓÚ ñ ç ß ğ İ ж Ω 日本語");
  mph_text_fit(MPH_T_BODY, 700, 252, 200, MPH_C_FG, "An ellipsis on a long line of text");
  mph_text(MPH_T_MONO, 700, 272, MPH_C_OVERLAY_TEXT, "fps: 59.9 / 60");
  mph_legend((const mph_hint[]){{BUTTON_GLYPH_TRIANGLE, "Help"}, {BUTTON_GLYPH_CROSS, "Change"}}, 2);
  frame_end();
}

const struct theme THEME_MORPHEUS = {
  .name = "Morpheus",
  .init = morpheus_init,
  .fini = morpheus_fini,
  .language_changed = morpheus_language_changed,
  .draw_menu = mph_draw_menu,
  .menu_follow = mph_menu_follow,
  .draw_alert = mph_screen_alert_draw,
  .help_prepare = mph_screen_help_prepare,
  .draw_help = mph_draw_help,
  .draw_ime_backdrop = mph_screen_ime_backdrop_draw,
  .draw_stats = mph_screen_stats_draw,
  .draw_stream_overlay = mph_screen_stream_overlay_draw,
};
