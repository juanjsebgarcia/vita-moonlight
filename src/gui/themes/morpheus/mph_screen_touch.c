// Morpheus's touch editors (DESIGN.md S7, S8, S9).
//
// The corner buttons and the dead zones are drawn where they really are on
// the screen, as the app always has: that shows exactly where to touch. The
// rows sit on a card above them.

#include "mph_screens.h"

#include "../../../i18n/i18n.h"

#include <math.h>

static mph_list_state list;
static mph_row rows[MPH_LIST_MAX];
static float list_seen = -1.0f;

// The rows, and the list started afresh on a new screen
static int begin(const menu_view *view, const mph_enter *enter) {
  if (mph_enter_is_new(enter, &list_seen)) {
    mph_list_reset(&list);
  }
  return mph_rows_from_entries(rows, view->rows, view->row_count);
}

// The rows' height as a list w wide, without the padding
static float rows_height(int n, float w) {
  float h = 0;
  for (int i = 0; i < n; i++) {
    h += mph_row_height(&rows[i], w) + (i > 0 ? MPH_ROW_GAP : 0);
  }
  return h;
}

// Where each screen's rows go: on a card (S7, S9) as tall as they are, 8 px
// inside it, or a full height list (S8)
#define KEYS_CARD_X 290
#define KEYS_CARD_Y 104
#define KEYS_CARD_W 380
#define DEADZONE_CARD_Y 56
#define DEADZONE_CARD_W 300
#define CARD_PAD 8

static mph_rect card_rows_rect(int n, float x, float y, float w) {
  return mph_rect_make(x + CARD_PAD, y + CARD_PAD, w - 2 * CARD_PAD, rows_height(n, w - 2 * CARD_PAD));
}

static mph_rect rows_rect(ui_screen_id id, int n) {
  switch (id) {
    case UI_SCREEN_SPECIAL_KEYS: return card_rows_rect(n, KEYS_CARD_X, KEYS_CARD_Y, KEYS_CARD_W);
    case UI_SCREEN_DEADZONE: return card_rows_rect(n, MPH_GUTTER, DEADZONE_CARD_Y, DEADZONE_CARD_W);
    default: return mph_rect_make(220, 56, 520, MPH_CONTENT_BOTTOM - 56);
  }
}

void mph_screen_touch_follow(menu_view *view) {
  int n = mph_rows_from_entries(rows, view->rows, view->row_count);
  mph_rect rect = rows_rect(view->screen->id, n);
  view->scroll = (int)roundf(mph_list_target(rect, rows, n, view->cursor_row, view->scroll));
}

// The rows on their card: SOLID2 at `alpha`, r16
static void rows_card(const menu_view *view, int n, float alpha) {
  mph_rect rect = rows_rect(view->screen->id, n);
  mph_rrect_fill(mph_rect_grow(rect, CARD_PAD), MPH_R_CARD, mph_alpha(MPH_C_SOLID2, alpha));
  mph_list(rect, rows, n, view->cursor_row, mph_list_target(rect, rows, n, view->cursor_row, view->scroll), &list, -1);
}

/*
 * S7 Touchscreen special keys: the four corners at their real place and size
 */

static void draw_corners(const menu_view *view, int n) {
  const special_keys_view *keys = view->special_keys;
  int size = keys->size, offset = keys->offset;
  if (size <= 0) {
    return;
  }
  // The assignment rows, top left to bottom right as the corners: their
  // values are the corners' labels
  const char *names[4] = {NULL, NULL, NULL, NULL};
  for (int i = 0, k = 0; i < n && k < 4; i++) {
    if (view->rows[i].kind == MENU_ROW_ACTION) {
      names[k++] = rows[i].value;
    }
  }
  mph_rect zone[4];
  for (int i = 0; i < 4; i++) {
    float x = (i & 1) ? MPH_W - size - offset : offset;
    float y = (i & 2) ? MPH_H - size - offset : offset;
    zone[i] = mph_rect_make(x, y, size, size);
    mph_rrect_fill(zone[i], 8, i == keys->focused ? MPH_C_ZONE_CUR : MPH_C_ZONE);
    mph_rrect_stroke(mph_rect_grow(zone[i], -1), 7, 2, MPH_C_ACCENT);
  }
  if (size < 36) {
    return;
  }
  float base_dy = mph_baseline(MPH_T_STRONG_S, 0, size);
  for (int i = 0; i < 4; i++) {
    if (names[i] == NULL) {
      continue;
    }
    float max = size - 8;
    float w = mph_text_w(MPH_T_STRONG_S, names[i]);
    w = w > max ? max : w;
    mph_text_fit(MPH_T_STRONG_S, roundf(zone[i].x + (size - w) / 2), zone[i].y + base_dy, max, MPH_C_FG, names[i]);
  }
}

void mph_screen_special_keys_draw(const menu_view *view, const mph_enter *enter) {
  int n = begin(view, enter);
  mph_bg_draw();
  if (view->decorated && view->special_keys != NULL) {
    draw_corners(view, n);
  }
  mph_statusbar(tr(STR_SET_SPECIAL_KEYS));
  mph_enter_apply(enter);
  rows_card(view, n, 0.92f);
  mph_transform_reset();
}

/*
 * S8 Select special key: a list of keys, grouped
 */

void mph_screen_key_list_draw(const menu_view *view, const mph_enter *enter) {
  int n = begin(view, enter);
  mph_rect rect = rows_rect(view->screen->id, n);
  mph_bg_draw();
  mph_statusbar(view->screen->title != NULL ? view->screen->title : tr(STR_SET_SPECIAL_KEYS));
  mph_enter_apply(enter);
  mph_list(rect, rows, n, view->cursor_row, mph_list_target(rect, rows, n, view->cursor_row, view->scroll), &list,
           rect.x + rect.w + 4);
  mph_transform_reset();
}

/*
 * S9 Back touch deadzone: the dead strips and the active area, 1:1 on the
 * screen, and a circle under each finger on the rear touchpad
 */

static int clampi(int v, int lo, int hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

static void draw_zones(const deadzone_view *zone) {
  int top = clampi(zone->top, 0, MPH_H), bottom = clampi(zone->bottom, 0, MPH_H);
  int left = clampi(zone->left, 0, MPH_W), right = clampi(zone->right, 0, MPH_W);
  int w = MPH_W - left - right, h = MPH_H - top - bottom;
  if (w <= 0 || h <= 0) {
    mph_hatch(mph_rect_make(0, 0, MPH_W, MPH_H), MPH_C_DEAD_STRIPE);
    return;
  }
  // The strips: one textured run
  mph_hatch(mph_rect_make(0, 0, MPH_W, top), MPH_C_DEAD_STRIPE);
  mph_hatch(mph_rect_make(0, MPH_H - bottom, MPH_W, bottom), MPH_C_DEAD_STRIPE);
  mph_hatch(mph_rect_make(0, top, left, h), MPH_C_DEAD_STRIPE);
  mph_hatch(mph_rect_make(MPH_W - right, top, right, h), MPH_C_DEAD_STRIPE);

  // The active area, its centre lines and the fingers: one shape run
  mph_rect active = mph_rect_make(left, top, w, h);
  mph_rect_fill(active, MPH_C_ACTIVE_AREA);
  mph_dashed_rect(mph_rect_grow(active, -0.75f), 1.5f, 4, 3, MPH_C_OK);
  uint32_t line = mph_alpha(MPH_C_FG, 0.45f);
  mph_rect_fill(mph_rect_make(left + w / 2, top, 1, h), line);
  mph_rect_fill(mph_rect_make(left, top + h / 2, w, 1), line);
  for (int i = 0; i < zone->touch_count && i < DEADZONE_MAX_TOUCHES; i++) {
    mph_circle_fill(zone->touches[i].x, zone->touches[i].y, 30, mph_alpha(MPH_C_FG, 0.85f));
  }
}

void mph_screen_deadzone_draw(const menu_view *view, const mph_enter *enter) {
  int n = begin(view, enter);
  mph_bg_draw();
  if (view->decorated && view->deadzone != NULL) {
    draw_zones(view->deadzone);
  }
  mph_statusbar(tr(STR_SET_BACK_DEADZONE));
  mph_enter_apply(enter);
  rows_card(view, n, 0.94f);
  mph_transform_reset();
}
