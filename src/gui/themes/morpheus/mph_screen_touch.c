// Morpheus's touch editors (DESIGN.md S7, S8, S9).
//
// The corner buttons and the dead zones are drawn where they really are on
// the screen, as the app always has: that shows exactly where to touch. The
// rows sit on a card above them.

#include "mph_screen_touch.h"

#include "mph.h"

#include "../../../i18n/i18n.h"

#include <math.h>

static mph_list_state list;
static mph_row rows[MPH_LIST_MAX];
static float list_t0 = -1.0f;

static int build_rows(const menu_view *view) {
  int n = view->row_count < MPH_LIST_MAX ? view->row_count : MPH_LIST_MAX;
  for (int i = 0; i < n; i++) {
    rows[i] = mph_row_from_entry(&view->rows[i]);
  }
  return n;
}

// The rows' height as a list w wide, without the padding
static float rows_height(int n, float w) {
  float h = 0;
  for (int i = 0; i < n; i++) {
    h += mph_row_height(&rows[i], w) + (i > 0 ? MPH_ROW_GAP : 0);
  }
  return h;
}

// Rows on a card: SOLID2 at `alpha`, r16, 8 px of padding
static void rows_card(const menu_view *view, int n, float x, float y, float w, float alpha) {
  float inner = w - 16;
  mph_rect card = mph_rect_make(x, y, w, rows_height(n, inner) + 16);
  mph_rrect_fill(card, MPH_R_CARD, mph_alpha(MPH_C_SOLID2, alpha));
  mph_rect rect = mph_rect_make(x + 8, y + 8, inner, card.h - 16);
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

static void draw_special_keys(const menu_view *view, float enter) {
  int n = build_rows(view);
  mph_bg_draw();
  if (view->decorated && view->special_keys != NULL) {
    draw_corners(view, n);
  }
  mph_statusbar(tr(STR_SET_SPECIAL_KEYS));
  if (enter < 1.0f) {
    mph_transform(enter, 1.0f, 0, 0, 18.0f * (1.0f - enter), 0);
  }
  rows_card(view, n, 290, 104, 380, 0.92f);
}

/*
 * S8 Select special key: a list of keys, grouped
 */

static void draw_key_list(const menu_view *view, float enter) {
  int n = build_rows(view);
  mph_rect rect = mph_rect_make(220, 56, 520, MPH_CONTENT_BOTTOM - 56);
  mph_bg_draw();
  mph_statusbar(view->screen->title != NULL ? view->screen->title : tr(STR_SET_SPECIAL_KEYS));
  if (enter < 1.0f) {
    mph_transform(enter, 1.0f, 0, 0, 18.0f * (1.0f - enter), 0);
  }
  mph_list(rect, rows, n, view->cursor_row, mph_list_target(rect, rows, n, view->cursor_row, view->scroll), &list,
           744);
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

static void draw_deadzone(const menu_view *view, float enter) {
  int n = build_rows(view);
  mph_bg_draw();
  if (view->decorated && view->deadzone != NULL) {
    draw_zones(view->deadzone);
  }
  mph_statusbar(tr(STR_SET_BACK_DEADZONE));
  if (enter < 1.0f) {
    mph_transform(enter, 1.0f, 0, 0, 18.0f * (1.0f - enter), 0);
  }
  rows_card(view, n, MPH_GUTTER, 56, 300, 0.94f);
}

bool mph_screen_touch_draw(const menu_view *view, float enter_t0, bool animate) {
  ui_screen_id id = view->screen->id;
  if (id != UI_SCREEN_SPECIAL_KEYS && id != UI_SCREEN_SPECIAL_KEY_LIST && id != UI_SCREEN_DEADZONE) {
    return false;
  }
  if (enter_t0 != list_t0) {
    list_t0 = enter_t0;
    mph_list_reset(&list);
  }
  float enter = animate ? mph_ease_eval(MPH_EASE_OUT, (mph_time() - enter_t0) / MPH_M_ENTER) : 1.0f;
  switch (id) {
    case UI_SCREEN_SPECIAL_KEYS: draw_special_keys(view, enter); break;
    case UI_SCREEN_SPECIAL_KEY_LIST: draw_key_list(view, enter); break;
    default: draw_deadzone(view, enter); break;
  }
  mph_transform_reset();
  return true;
}
