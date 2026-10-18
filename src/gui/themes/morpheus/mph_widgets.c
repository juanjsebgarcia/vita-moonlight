// Morpheus widgets: rows, lists and the focus highlight, buttons, chips,
// keycaps, cards, tiles, dialog and sheet frames, PIN tiles, meters, the live
// dot, the spinner and the switch.

#include "mph.h"
#include "mph_internal.h"

#include <math.h>
#include <string.h>

/*
 * Focus
 */

void mph_focus_set(mph_focus *f, mph_rect target, float radius, mph_focus_kind k, int key) {
  float now = mph_time();
  if (!f->shown) {
    f->from = f->to = target;
    f->t0 = now - MPH_M_FOCUS;
    f->appear = now;
    f->shown = true;
  } else if (key != f->key) {
    f->from = mph_focus_rect(f);
    f->to = target;
    f->t0 = now;
  } else if (memcmp(&target, &f->to, sizeof(target)) != 0) {
    // The same item moved: move the whole glide with it
    f->from.x += target.x - f->to.x;
    f->from.y += target.y - f->to.y;
    f->from.w += target.w - f->to.w;
    f->from.h += target.h - f->to.h;
    f->to = target;
  }
  f->radius = radius;
  f->kind = k;
  f->key = key;
}

void mph_focus_hide(mph_focus *f) {
  f->shown = false;
}

mph_rect mph_focus_rect(const mph_focus *f) {
  float k = mph_ease_eval(MPH_EASE_FOCUS, (mph_time() - f->t0) / MPH_M_FOCUS);
  return mph_rect_make(f->from.x + (f->to.x - f->from.x) * k, f->from.y + (f->to.y - f->from.y) * k,
                       f->from.w + (f->to.w - f->from.w) * k, f->from.h + (f->to.h - f->from.h) * k);
}

void mph_focus_draw(const mph_focus *f, float dx, float dy) {
  if (!f->shown) {
    return;
  }
  float alpha = (mph_time() - f->appear) / MPH_M_FOCUS_FADE;
  alpha = alpha > 1.0f ? 1.0f : alpha;
  mph_rect r = mph_focus_rect(f);
  r.x += dx;
  r.y += dy;
  if (f->kind == MPH_FOCUS_PILL) {
    mph_shadow(r, f->radius, 0, 10, 28, mph_alpha(MPH_C_SHADOW, alpha));
    mph_rrect_fill(r, f->radius, mph_alpha(MPH_C_HL_BG, alpha));
  } else {
    // 3 px, 5 px outside the item
    mph_rrect_stroke(mph_rect_grow(r, 6.5f), MPH_R_RING + 1.5f, 3, mph_alpha(MPH_C_HL_BG, alpha));
  }
}

/*
 * Rows
 */

mph_row mph_row_from_entry(const menu_entry *e) {
  mph_row r = {
    .kind = MPH_ROW_PLAIN,
    .label = e->name,
    .value = e->subname[0] ? e->subname : NULL,
    .suffix = e->suffix && e->suffix[0] ? e->suffix : NULL,
    .on = e->on,
    .checked = e->checked,
    .disabled = e->disabled,
    .danger = e->danger,
    .status = e->status,
    .icon = -1,
  };
  bool named = e->name != NULL && e->name[0] != '\0';
  if (!named && r.value != NULL && !e->separator) {
    // A row that's only a value reads as its label
    r.label = r.value;
    r.value = NULL;
  }
  if (e->kind == MENU_ROW_HEADER || (e->separator && named)) {
    r.kind = MPH_ROW_CATEGORY;
    r.label = mph_upper(e->name);
  } else if (e->separator) {
    r.kind = MPH_ROW_SEPARATOR;
  } else if (e->kind == MENU_ROW_NOTE) {
    r.kind = MPH_ROW_NOTE;
  } else if (e->kind == MENU_ROW_TOGGLE) {
    r.kind = MPH_ROW_TOGGLE;
  } else if (e->kind == MENU_ROW_CHOICE || e->adjustable) {
    r.kind = MPH_ROW_CHOICE;
  } else if (e->kind == MENU_ROW_ACTION) {
    r.kind = MPH_ROW_ACTION;
  } else if (e->kind == MENU_ROW_CHECK || e->checked) {
    r.kind = MPH_ROW_CHECK;
  } else if (e->disabled) {
    r.kind = MPH_ROW_INFO;
  } else if (r.value != NULL || r.suffix != NULL) {
    r.kind = MPH_ROW_VALUE;
  }
  return r;
}

bool mph_row_selectable(const mph_row *r) {
  return !r->disabled && r->kind != MPH_ROW_CATEGORY && r->kind != MPH_ROW_SEPARATOR && r->kind != MPH_ROW_NOTE;
}

#define NOTE_TEXT_X 32

float mph_row_height(const mph_row *r, float w) {
  switch (r->kind) {
    case MPH_ROW_CATEGORY: return 32;
    case MPH_ROW_SEPARATOR: return 13;
    case MPH_ROW_NOTE: {
      mph_lines lines;
      return mph_wrap(MPH_T_SMALL, r->label, w - NOTE_TEXT_X - 16, &lines) > 1 ? 54 : 32;
    }
    default: return MPH_ROW_H;
  }
}

enum { PASS_SHAPES = 1, PASS_TEXT = 2, PASS_ALL = 3 };

static void row_draw(mph_rect r, const mph_row *row, bool focused, float switch_t, int pass) {
  bool shapes = pass & PASS_SHAPES, text = pass & PASS_TEXT;
  if (row->kind == MPH_ROW_CATEGORY) {
    if (text) {
      mph_category(r.x, r.y, r.w, row->label, MPH_C_FAINT);
    }
    return;
  }
  if (row->kind == MPH_ROW_SEPARATOR) {
    if (shapes) {
      mph_separator(r.x, r.y, r.w);
    }
    return;
  }
  if (row->kind == MPH_ROW_NOTE) {
    if (shapes) {
      mph_circle_fill(r.x + 20, r.y + 16, 3.5f, MPH_C_LIVE);
    }
    if (text) {
      mph_lines lines;
      mph_wrap(MPH_T_SMALL, row->label, r.w - NOTE_TEXT_X - 16, &lines);
      mph_draw_lines(MPH_T_SMALL, &lines, r.x + NOTE_TEXT_X, r.y + 21, 22, 0, 2, MPH_C_LIVE, MPH_ALIGN_LEFT);
    }
    return;
  }

  focused = focused && !row->disabled;
  uint32_t label_c = focused          ? MPH_C_HL_FG
                     : row->disabled && row->kind != MPH_ROW_INFO ? MPH_C_FAINT
                     : row->kind == MPH_ROW_INFO ? MPH_C_DIM
                     : row->danger ? MPH_C_BAD
                     : row->status == MENU_STATUS_WARNING ? MPH_C_LIVE
                     : MPH_C_FG;
  uint32_t value_c = focused ? MPH_C_HL_FG : row->disabled && row->kind != MPH_ROW_INFO ? MPH_C_FAINT : MPH_C_DIM;
  float base = mph_baseline(MPH_T_ROW, r.y, r.h);
  float cy = r.y + r.h / 2.0f;
  float right = r.x + r.w - 16;

  // Right to left: the trailing control, then the value, then the suffix
  switch (row->kind) {
    case MPH_ROW_TOGGLE:
      if (shapes) {
        mph_switch(right - 40, r.y + (r.h - 24) / 2.0f, switch_t, focused, row->disabled);
      }
      right -= 40 + 14;
      break;
    case MPH_ROW_ACTION:
      if (text) {
        mph_icon(MPH_ICON_CHEVRON_14, right - 14, roundf(cy - 7), focused ? MPH_C_HL_FG : MPH_C_FAINT);
      }
      right -= 14 + 10;
      break;
    case MPH_ROW_CHECK:
      if (text && row->checked) {
        mph_icon(MPH_ICON_CHECK_18, right - 18, roundf(cy - 9), focused ? MPH_C_HL_FG : MPH_C_ACCENT);
      }
      right -= 18 + 10;
      break;
    default:
      break;
  }
  switch (row->status) {
    case MENU_STATUS_ONLINE:
      if (shapes) {
        mph_circle_fill(right - 4, cy, 4, focused ? MPH_C_HL_FG : MPH_C_OK);
      }
      right -= 8 + 12;
      break;
    case MENU_STATUS_OFFLINE:
      if (shapes) {
        mph_circle_stroke(right - 4, cy, 3.25f, 1.5f, focused ? MPH_C_HL_FG : MPH_C_FAINT);
      }
      right -= 8 + 12;
      break;
    case MENU_STATUS_CHANGED:
      if (shapes) {
        mph_diamond(right - 4.5f, cy, 4.5f, focused ? MPH_C_HL_FG : MPH_C_LIVE);
      }
      right -= 9 + 12;
      value_c = focused ? MPH_C_HL_FG : MPH_C_LIVE;
      break;
    default:
      break;
  }

  float value_max = r.w * 0.45f;
  if (row->value != NULL && row->kind != MPH_ROW_TOGGLE) {
    bool arrows = row->kind == MPH_ROW_CHOICE;
    uint32_t arrow_c = focused ? MPH_C_HL_FG : MPH_C_FAINT;
    if (arrows) {
      int aw = text ? mph_text_right(MPH_T_ROW, right, base, arrow_c, "\xe2\x80\xba") : mph_text_w(MPH_T_ROW, "\xe2\x80\xba");
      right -= aw + 10;
    }
    float vw = mph_text_w(MPH_T_ROW, row->value);
    vw = vw > value_max ? value_max : vw;
    if (text) {
      mph_text_fit(MPH_T_ROW, right - vw, base, vw, value_c, row->value);
    }
    right -= vw;
    if (arrows) {
      int lw = mph_text_w(MPH_T_ROW, "\xe2\x80\xb9");
      right -= 10 + lw;
      if (text) {
        mph_text(MPH_T_ROW, right, base, arrow_c, "\xe2\x80\xb9");
      }
    }
    right -= 10;
  }
  if (row->suffix != NULL) {
    float sw = mph_text_w(MPH_T_ROW, row->suffix);
    sw = sw > value_max ? value_max : sw;
    if (text) {
      mph_text_fit(MPH_T_ROW, right - sw, base, sw, focused ? MPH_C_HL_FG : MPH_C_DIM, row->suffix);
    }
    right -= sw + 10;
  }

  float left = r.x + 16;
  if (row->icon >= 0) {
    if (text) {
      mph_icon(row->icon, left, roundf(cy - mph_icon_h(row->icon) / 2.0f), focused ? MPH_C_HL_FG : MPH_C_DIM);
    }
    left = r.x + 44;
  }
  if (text && row->label != NULL) {
    mph_text_fit(MPH_T_ROW, left, base, right - 4 - left, label_c, row->label);
  }
}

void mph_row_draw(mph_rect r, const mph_row *row, bool focused, float switch_t) {
  row_draw(r, row, focused, switch_t, PASS_ALL);
}

/*
 * Lists
 */

void mph_list_reset(mph_list_state *s) {
  s->started = false;
  s->focus.shown = false;
}

float mph_list_target(mph_rect view, const mph_row *rows, int n, int focused, float current) {
  float y = 0, top = -1, bottom = 0;
  for (int i = 0; i < n; i++) {
    float h = mph_row_height(&rows[i], view.w);
    if (i == focused) {
      top = y;
      bottom = y + h;
    }
    y += h + MPH_ROW_GAP;
  }
  float content = n > 0 ? y - MPH_ROW_GAP : 0;
  float target = current;
  if (top >= 0) {
    float margin = MPH_ROW_H;
    if (top - target < margin) {
      target = top - margin;
    }
    if (bottom - target > view.h - margin) {
      target = bottom - view.h + margin;
    }
  }
  float max = content - view.h;
  max = max < 0 ? 0 : max;
  return target < 0 ? 0 : target > max ? max : target;
}

void mph_list(mph_rect view, const mph_row *rows, int n, int focused, float scroll_target, mph_list_state *s,
              float track_x) {
  n = n > MPH_LIST_MAX ? MPH_LIST_MAX : n;
  if (!s->started) {
    mph_tween_snap(&s->scroll, scroll_target);
    for (int i = 0; i < n; i++) {
      s->knob_on[i] = rows[i].on;
      mph_tween_snap(&s->knob[i], rows[i].on ? 1.0f : 0.0f);
    }
    s->focus.shown = false;
    s->started = true;
  }
  mph_tween_to(&s->scroll, scroll_target, MPH_M_SCROLL, MPH_EASE_OUT);
  float offset = roundf(mph_tween_value(&s->scroll));

  static float tops[MPH_LIST_MAX], heights[MPH_LIST_MAX];
  float y = 0;
  for (int i = 0; i < n; i++) {
    tops[i] = y;
    heights[i] = mph_row_height(&rows[i], view.w);
    y += heights[i] + MPH_ROW_GAP;
    if (rows[i].kind == MPH_ROW_TOGGLE && rows[i].on != s->knob_on[i]) {
      s->knob_on[i] = rows[i].on;
      mph_tween_to(&s->knob[i], rows[i].on ? 1.0f : 0.0f, MPH_M_SWITCH, MPH_EASE_CSS);
    }
  }
  float content = n > 0 ? y - MPH_ROW_GAP : 0;
  bool clip = content > view.h;

  if (focused >= 0 && focused < n && mph_row_selectable(&rows[focused])) {
    mph_focus_set(&s->focus, mph_rect_make(view.x, view.y + tops[focused], view.w, heights[focused]), MPH_R_ROW,
                  MPH_FOCUS_PILL, focused);
  } else {
    mph_focus_hide(&s->focus);
  }

  if (clip) {
    mph_clip(view);
  }
  mph_focus_draw(&s->focus, 0, -offset);
  // Shapes for every row, then their icons and text: a few draw calls in all
  for (int pass = PASS_SHAPES; pass <= PASS_TEXT; pass <<= 1) {
    for (int i = 0; i < n; i++) {
      float top = view.y + tops[i] - offset;
      if (top + heights[i] < view.y || top > view.y + view.h) {
        continue;
      }
      row_draw(mph_rect_make(view.x, top, view.w, heights[i]), &rows[i], i == focused, mph_tween_value(&s->knob[i]),
               pass);
    }
  }
  if (clip) {
    mph_clip_end();
  }
  if (track_x >= 0) {
    mph_scrollbar(mph_rect_make(track_x, view.y, 3, view.h), content, view.h, offset);
  }
}

/*
 * Controls
 */

static mph_type button_type(int style) {
  return (style & MPH_BTN_SMALL) ? MPH_T_BODY_B : MPH_T_BTN;
}

float mph_button_w(int glyph, const char *caption, int style) {
  float w = mph_text_w(button_type(style), caption);
  if (glyph == BUTTON_GLYPH_NONE) {
    return w + 36;
  }
  return 16 + mph_glyph_w(glyph, MPH_T_ROW) + ((style & MPH_BTN_SMALL) ? 8 : 10) + w + 20;
}

float mph_button(float x, float y, int glyph, const char *caption, int style, bool focused) {
  bool small = style & MPH_BTN_SMALL;
  int kind = style & 0x0F;
  float h = small ? 40 : 44;
  float w = mph_button_w(glyph, caption, style);
  mph_rect r = mph_rect_make(x, y, w, h);
  uint32_t fill = kind == MPH_BTN_PRIMARY ? MPH_C_HL_BG : MPH_C_SURFACE2;
  uint32_t fg = kind == MPH_BTN_PRIMARY ? MPH_C_HL_FG : kind == MPH_BTN_DANGER ? MPH_C_BAD : MPH_C_FG;
  if (focused) {
    mph_rrect_stroke(mph_rect_grow(r, 6.5f), h / 2 + 6.5f, 3, MPH_C_HL_BG);
  }
  mph_pill(r, fill);
  mph_type t = button_type(style);
  float base = mph_baseline(t, y, h);
  float tx = x + 18;
  if (glyph != BUTTON_GLYPH_NONE) {
    // 22 px symbols, centred on the button
    float gw = mph_glyph(glyph, MPH_T_ROW, x + 16, roundf(y + h / 2 + 0.365f * mph_type_size(MPH_T_ROW)), fg);
    tx = x + 16 + gw + (small ? 8 : 10);
  }
  mph_text(t, tx, base, fg, caption);
  return w;
}

float mph_chip(float x, float y, const char *text, uint32_t text_colour) {
  float w = mph_text_w(MPH_T_CHIP, text) + 16;
  mph_rrect_fill(mph_rect_make(x, y, w, 22), MPH_R_CHIP, MPH_C_W08);
  mph_text(MPH_T_CHIP, x + 8, mph_baseline(MPH_T_CHIP, y, 22), text_colour, text);
  return w;
}

float mph_keycap_w(const char *label) {
  return mph_text_w(MPH_T_TAG, label) + 12;
}

float mph_keycap(float x, float y, const char *label) {
  float w = mph_keycap_w(label);
  mph_rrect_fill(mph_rect_make(x, y, w, 20), MPH_R_KEY, MPH_C_W10);
  mph_text(MPH_T_TAG, x + 6, mph_baseline(MPH_T_TAG, y, 20), MPH_C_FG, label);
  return w;
}

void mph_card(mph_rect r) {
  mph_rrect_fill(r, MPH_R_CARD, MPH_C_SURFACE);
}

void mph_tile(const vita2d_texture *t, const char *name, mph_rect r, float scale, int focus, bool dim) {
  float cx = r.x + r.w / 2, cy = r.y + r.h / 2;
  r = mph_rect_make(cx - r.w * scale / 2, cy - r.h * scale / 2, r.w * scale, r.h * scale);
  mph_shadow(r, MPH_R_ROW, 0, 10, 28, MPH_C_SHADOW);
  if (focus == 2) {
    mph_rrect_stroke(mph_rect_grow(r, 6.5f), MPH_R_RING + 1.5f, 3, MPH_C_HL_BG);
  } else if (focus == 1) {
    mph_rrect_stroke(mph_rect_grow(r, 4), 14 + 1, 2, MPH_C_DIM);
  }
  if (t != NULL) {
    mph_texture_rrect(t, r, MPH_R_ROW, 0xFFFFFFFF);
  } else {
    mph_rrect_fill(r, MPH_R_ROW, MPH_C_SURFACE2);
    if (name != NULL) {
      mph_lines lines;
      int n = mph_wrap(MPH_T_TILE, name, r.w - 20, &lines);
      n = n > 3 ? 3 : n;
      float first = cy - (n * 17) / 2.0f + 12;
      mph_draw_lines(MPH_T_TILE, &lines, cx, roundf(first), 17, 0, n, MPH_C_FG, MPH_ALIGN_CENTER);
    }
  }
  if (dim) {
    mph_rrect_fill(r, MPH_R_ROW, MPH_C_TILE_DIM);
  }
}

void mph_dialog_frame(mph_rect r) {
  mph_shadow(r, MPH_R_DIALOG, 0, 30, 80, MPH_C_SHADOW_DIALOG);
  mph_rrect_fill(r, MPH_R_DIALOG, MPH_C_SOLID);
}

void mph_sheet_frame(float x) {
  mph_rect r = mph_rect_make(x, 0, MPH_W - x, MPH_H);
  mph_shadow(mph_rect_make(x, -80, MPH_W - x + 80, MPH_H + 160), 0, -20, 0, 60, MPH_C_SHADOW_SHEET);
  mph_rect_fill(r, MPH_C_SOLID);
}

#define PIN_W 86
#define PIN_H 104
#define PIN_GAP 12

void mph_pin_tiles(float cx, float y, const char *digits) {
  int n = digits ? (int)strlen(digits) : 0;
  n = n > 8 ? 8 : n;
  float x = roundf(cx - (n * PIN_W + (n - 1) * PIN_GAP) / 2.0f);
  for (int i = 0; i < n; i++) {
    mph_rrect_fill(mph_rect_make(x + i * (PIN_W + PIN_GAP), y, PIN_W, PIN_H), MPH_R_CARD, MPH_C_SURFACE2);
  }
  float base = mph_baseline(MPH_T_PIN, y, PIN_H);
  for (int i = 0; i < n; i++) {
    char d[2] = {digits[i], '\0'};
    mph_text_center(MPH_T_PIN, x + i * (PIN_W + PIN_GAP) + PIN_W / 2.0f, base, MPH_C_FG, d);
  }
}

void mph_meter10(float x, float y, float w, int filled) {
  float bar = (w - 9 * 3) / 10.0f;
  for (int i = 0; i < 10; i++) {
    mph_rrect_fill(mph_rect_make(x + i * (bar + 3), y, bar, 8), 2, i < filled ? MPH_C_ACCENT : MPH_C_W10);
  }
}

void mph_live_dot(float cx, float cy) {
  float phase = fmodf(mph_time(), 2.0f) / 2.0f;
  if (phase < 0.7f) {
    float k = phase / 0.7f;
    mph_circle_fill(cx, cy, 4 + 10 * k, mph_alpha(MPH_C_LIVE, 0.6f * (1 - k)));
  }
  mph_circle_fill(cx, cy, 4, MPH_C_LIVE);
}

void mph_spinner(float cx, float cy) {
  float a = fmodf(mph_time(), 0.9f) / 0.9f * 2.0f * (float)M_PI;
  mph_circle_stroke(cx, cy, 7.75f, 2.5f, MPH_C_W18);
  mph_arc_stroke(cx, cy, 7.75f, 2.5f, a, a + (float)M_PI_2, MPH_C_FG);
}

void mph_switch(float x, float y, float t, bool focused, bool disabled) {
  uint32_t track = focused ? MPH_C_SW_TRACK_ON_PILL : mph_mix(MPH_C_W14, MPH_C_SW_ON_TRACK, t);
  uint32_t knob = focused ? MPH_C_HL_FG : disabled ? MPH_C_FAINT : mph_mix(MPH_C_DIM, MPH_C_OK, t);
  mph_pill(mph_rect_make(x, y, 40, 24), track);
  mph_circle_fill(x + 3 + 9 + 16 * t, y + 12, 9, knob);
}
