// Morpheus's Settings screens (DESIGN.md S5, S6 and S10):
//
// - Settings: the tab bar, the rows of the current tab, and a help card
//   beside them with the selected row's help (what triangle opens).
// - Apply recommended settings: the live reading as a card, the rows under
//   it, and the same help card.
// - Keyboard layout: the three layouts in a small card.
//
// Only a new look: the rows, their order and what every button does come
// from the shared screen code (ui_settings.c, ui_recommended.c,
// ui_keyboard.c), and every word from the view or an existing string.

#include "mph_screens.h"

#include "../../../i18n/i18n.h"
#include "../../text.h"

#include <stdio.h>
#include <string.h>

#define ROWS_X MPH_GUTTER
#define ROWS_W 600
#define TRACK_X 650  // the scroll indicator, between the rows and the card
#define CARD_X 664
#define CARD_W 256
#define CARD_PAD 18

/*
 * What the screens share: their list, a copy of what update gave (so the tab
 * bar and the help card stay up while a rebuilt menu waits for update), and
 * the help card
 */

static mph_list_state list;
static mph_row rows[MPH_LIST_MAX];
static float seen = -1;
// A summary of the rows, so a new tab starts its list afresh (switches don't
// slide from the last tab's states) but a value change doesn't
static unsigned int rows_key;

// Follows the screen and its rows; the list starts afresh when either
// changes. Returns whether the screen is new.
static bool track(const menu_view *view, const mph_enter *enter) {
  bool fresh = mph_enter_is_new(enter, &seen);
  unsigned int key = 2166136261u ^ (unsigned int)view->row_count;
  for (int i = 0; i < view->row_count; i++) {
    key = (key ^ (unsigned int)view->rows[i].id ^ (unsigned int)view->rows[i].kind << 16) * 16777619u;
  }
  if (fresh || key != rows_key) {
    mph_list_reset(&list);
  }
  rows_key = key;
  return fresh;
}

static int build_rows(const menu_view *view, int first) {
  return first < view->row_count ? mph_rows_from_entries(rows, view->rows + first, view->row_count - first) : 0;
}

#define HELP_TITLE_MAX 256
#define HELP_TEXT_MAX 1024

// The help card's text, kept from the last decorated frame
static char help_title[HELP_TITLE_MAX];
static char help_text[HELP_TEXT_MAX];

static void keep_help(const menu_view *view, const char *title) {
  if (!view->decorated) {
    return;
  }
  const char *help = view->help != NULL ? view->help : "";
  title = view->help != NULL && title != NULL ? title : "";
  if (strcmp(help_text, help) != 0) {
    snprintf(help_text, sizeof(help_text), "%s", help);
  }
  if (strcmp(help_title, title) != 0) {
    snprintf(help_title, sizeof(help_title), "%s", title);
  }
}

static void clear_help(void) {
  help_title[0] = help_text[0] = '\0';
}

// Up to max lines of l from first, the last ending in "…" when more follow
static void draw_lines_cut(mph_type t, const mph_lines *l, float x, float base, float line_h, int max, float w,
                           uint32_t c) {
  if (l->count <= max) {
    mph_draw_lines(t, l, x, base, line_h, 0, l->count, c, MPH_ALIGN_LEFT);
    return;
  }
  if (max <= 0) {
    return;
  }
  mph_draw_lines(t, l, x, base, line_h, 0, max - 1, c, MPH_ALIGN_LEFT);
  static char last[2 * WRAP_LINE_MAX + 2];
  snprintf(last, sizeof(last), "%s %s", l->line[max - 1], l->line[max]);
  mph_text_fit(t, x, base + (max - 1) * line_h, w, c, last);
}

// The card's shape: drawn with the other shapes, before any text
static void help_card_shape(mph_rect r) {
  mph_card(r);
}

// Title (the row's label) and the help under it. Up to three lines of title:
// many labels need three at this width (tools/i18n_widths.py --theme morpheus)
#define HELP_TITLE_LINES 3

static void help_card_text(mph_rect r) {
  float x = r.x + CARD_PAD, w = r.w - 2 * CARD_PAD;
  float base = r.y + 36;
  if (help_title[0]) {
    mph_lines lines;
    int n = mph_wrap(MPH_T_ROW_L, help_title, w, &lines);
    draw_lines_cut(MPH_T_ROW_L, &lines, x, base, 24, HELP_TITLE_LINES, w, MPH_C_FG);
    base += ((n > HELP_TITLE_LINES ? HELP_TITLE_LINES : n) - 1) * 24 + 12 + 22;
  }
  if (help_text[0]) {
    mph_lines body;
    mph_wrap(MPH_T_SMALL, help_text, w, &body);
    int fit = (int)((r.y + r.h - CARD_PAD - base) / 22) + 1;
    draw_lines_cut(MPH_T_SMALL, &body, x, base, 22, fit, w, MPH_C_DIM);
  }
}

/*
 * Settings (S5)
 */

#define TABS_Y 46
#define TABS_H 37
#define TAB_MAX 8
#define TAB_NAME_MAX 64
#define SETTINGS_TOP 96

// The tab bar as update last gave it
static struct {
  int count, current;
  char names[TAB_MAX][TAB_NAME_MAX];
} tab_bar = {.current = -1};
static mph_focus tab_focus;

static void keep_tabs(const menu_view *view) {
  if (!view->decorated || view->tabs == NULL) {
    return;
  }
  int n = view->tabs->count < TAB_MAX ? view->tabs->count : TAB_MAX;
  tab_bar.count = n;
  tab_bar.current = -1;
  for (int i = 0; i < n; i++) {
    const char *name = view->tabs->tabs[i].name != NULL ? view->tabs->tabs[i].name : "";
    if (strcmp(tab_bar.names[i], name) != 0) {
      snprintf(tab_bar.names[i], TAB_NAME_MAX, "%s", name);
    }
    if (view->tabs->tabs[i].current) {
      tab_bar.current = i;
    }
  }
}

// L, the tabs (the current one on a pill that glides to the next), R
static void draw_tabs(void) {
  if (tab_bar.count == 0) {
    return;
  }
  float xs[TAB_MAX], ws[TAB_MAX];
  float x = MPH_GUTTER;
  x += mph_keycap(x, 53, "L") + 4;
  for (int i = 0; i < tab_bar.count; i++) {
    xs[i] = x;
    ws[i] = mph_text_w(MPH_T_BTN, tab_bar.names[i]) + 24;
    x += ws[i] + 4;
  }
  mph_keycap(x, 53, "R");
  if (tab_bar.current >= 0) {
    int c = tab_bar.current;
    mph_focus_set(&tab_focus, mph_rect_make(xs[c], TABS_Y, ws[c], TABS_H), MPH_R_TAB, MPH_FOCUS_PILL, c);
    mph_rrect_fill(mph_focus_rect(&tab_focus), MPH_R_TAB, MPH_C_HL_BG);
  }
  float base = mph_baseline(MPH_T_BTN, TABS_Y, TABS_H);
  for (int i = 0; i < tab_bar.count; i++) {
    mph_text(MPH_T_BTN, xs[i] + 12, base, i == tab_bar.current ? MPH_C_HL_FG : MPH_C_FAINT, tab_bar.names[i]);
  }
}

static mph_rect settings_rect(void) {
  return mph_rect_make(ROWS_X, SETTINGS_TOP, ROWS_W, MPH_CONTENT_BOTTOM - SETTINGS_TOP);
}

void mph_screen_settings_draw(const menu_view *view, const mph_enter *enter) {
  if (track(view, enter)) {
    // Another screen opened since: start the tab bar and the card afresh
    tab_bar.count = 0;
    mph_focus_hide(&tab_focus);
    clear_help();
  }
  int n = build_rows(view, 0);
  keep_tabs(view);
  keep_help(view, view->cursor_row < n ? rows[view->cursor_row].label : NULL);
  mph_rect rect = settings_rect();
  float target = mph_list_target(rect, rows, n, view->cursor_row, view->scroll);

  mph_bg_draw();
  mph_statusbar(tr(STR_MAIN_SETTINGS));
  mph_enter_apply(enter);
  mph_rect card = mph_rect_make(CARD_X, SETTINGS_TOP, CARD_W, MPH_CONTENT_BOTTOM - SETTINGS_TOP);
  help_card_shape(card);
  draw_tabs();
  mph_list(rect, rows, n, view->cursor_row, target, &list, TRACK_X);
  help_card_text(card);
  mph_transform_reset();
  mph_menu_legend(view);
}

void mph_screen_settings_follow(menu_view *view) {
  int n = build_rows(view, 0);
  float target = mph_list_target(settings_rect(), rows, n, view->cursor_row, view->scroll);
  view->scroll = (int)(target + 0.5f);
}

/*
 * Apply recommended settings (S6). Row 0 is the reading (a disabled row
 * with only a value), drawn as a card; the rest are the list.
 */

#define READING_Y 56
#define READING_H 56
#define REC_TOP 128
#define METER_X 470
#define METER_W 150

static mph_rect recommended_rect(void) {
  return mph_rect_make(ROWS_X, REC_TOP, ROWS_W, MPH_CONTENT_BOTTOM - REC_TOP);
}

static void draw_reading(const menu_view *view) {
  mph_rect card = mph_rect_make(ROWS_X, READING_Y, ROWS_W, READING_H);
  mph_card(card);
  int percent = view->decorated && view->recommended != NULL ? view->recommended->wifi_percent : -1;
  float text_x = card.x + 18;
  float text_right = card.x + card.w - 18;
  if (percent >= 0) {
    int filled = percent / 10;
    mph_meter10(METER_X, 80, METER_W, filled > 10 ? 10 : filled);
    int lit = percent >= 60 ? 3 : percent >= 30 ? 2 : 1;
    mph_icon(MPH_ICON_WIFI_OUT_22, 58, 75, lit >= 3 ? MPH_C_ACCENT : MPH_C_W18);
    mph_icon(MPH_ICON_WIFI_MID_22, 58, 75, lit >= 2 ? MPH_C_ACCENT : MPH_C_W18);
    mph_icon(MPH_ICON_WIFI_DOT_22, 58, 75, MPH_C_ACCENT);
    text_x = 92;
    text_right = METER_X - 14;
  }
  const char *reading = view->row_count > 0 ? view->rows[0].subname : "";
  mph_text_fit(MPH_T_BTN, text_x, 90, text_right - text_x, MPH_C_FG, reading);
}

void mph_screen_recommended_draw(const menu_view *view, const mph_enter *enter) {
  if (track(view, enter)) {
    clear_help();
  }
  int n = build_rows(view, 1);
  int focused = view->cursor_row - 1;
  for (int i = 0; i < n; i++) {
    // "(fits now)" beside the choice that suits the reading
    if (rows[i].kind == MPH_ROW_ACTION && rows[i].value != NULL) {
      rows[i].value_colour = MPH_C_OK;
    }
  }
  keep_help(view, focused >= 0 && focused < n ? rows[focused].label : NULL);
  mph_rect rect = recommended_rect();
  float target = mph_list_target(rect, rows, n, focused, view->scroll);

  mph_bg_draw();
  mph_statusbar(view->screen->title);
  mph_enter_apply(enter);
  mph_rect card = mph_rect_make(CARD_X, READING_Y, CARD_W, MPH_CONTENT_BOTTOM - READING_Y);
  help_card_shape(card);
  draw_reading(view);
  mph_list(rect, rows, n, focused, target, &list, TRACK_X);
  help_card_text(card);
  mph_transform_reset();
  mph_menu_legend(view);
}

void mph_screen_recommended_follow(menu_view *view) {
  int n = build_rows(view, 1);
  float target = mph_list_target(recommended_rect(), rows, n, view->cursor_row - 1, view->scroll);
  view->scroll = (int)(target + 0.5f);
}

/*
 * Keyboard layout (S10): the layouts, the current one ticked, in a card
 * in the middle of the screen
 */

#define KB_X 300
#define KB_W 360
#define KB_PAD 8

static mph_rect keyboard_card(int n) {
  float h = n * (MPH_ROW_H + MPH_ROW_GAP) - MPH_ROW_GAP + 2 * KB_PAD;
  float max = MPH_CONTENT_BOTTOM - MPH_CONTENT_TOP;
  h = h > max ? max : h;
  // Centred under the status bar
  float y = (float)(int)(MPH_STATUSBAR_H + (MPH_H - MPH_STATUSBAR_H - h) / 2);
  y = y < MPH_CONTENT_TOP ? MPH_CONTENT_TOP : y;
  return mph_rect_make(KB_X, y, KB_W, h);
}

static mph_rect keyboard_rect(mph_rect card) {
  return mph_rect_make(card.x + KB_PAD, card.y + KB_PAD, card.w - 2 * KB_PAD, card.h - 2 * KB_PAD);
}

void mph_screen_keyboard_draw(const menu_view *view, const mph_enter *enter) {
  track(view, enter);
  int n = build_rows(view, 0);
  mph_rect card = keyboard_card(n);
  mph_rect rect = keyboard_rect(card);
  float target = mph_list_target(rect, rows, n, view->cursor_row, view->scroll);

  mph_bg_draw();
  mph_statusbar(view->screen->title);
  mph_enter_apply(enter);
  mph_rrect_fill(card, MPH_R_CARD, MPH_C_SOLID2);
  mph_list(rect, rows, n, view->cursor_row, target, &list, card.x + card.w + 8);
  mph_transform_reset();
}

void mph_screen_keyboard_follow(menu_view *view) {
  int n = build_rows(view, 0);
  mph_rect rect = keyboard_rect(keyboard_card(n));
  float target = mph_list_target(rect, rows, n, view->cursor_row, view->scroll);
  view->scroll = (int)(target + 0.5f);
}
