// Morpheus chrome: the status bar, the button legend, category labels,
// separators and the scroll indicator.

#include "mph.h"
#include "mph_internal.h"

#include "../../guilib.h"

#include <stdio.h>

void mph_statusbar(const char *title) {
  // The clock is formatted again only when the minute changes
  static char clock[8];
  static int clock_minute = -1, clock_w;
  ui_status status;
  ui_status_read(&status);
  int minute = status.hour * 60 + status.minute;
  if (minute != clock_minute) {
    clock_minute = minute;
    snprintf(clock, sizeof(clock), "%02d:%02d", status.hour, status.minute);
    clock_w = mph_text_w(MPH_T_BODY, clock);
  }
  float clock_x = 938 - clock_w;

  // Battery: a 30x14 outline, its level inside, a nub on the right
  float bx = clock_x - 16 - 33;
  int percent = status.battery_percent < 0 ? 0 : status.battery_percent > 100 ? 100 : status.battery_percent;
  uint32_t level = status.battery_charging ? MPH_C_ACCENT : percent < 20 ? MPH_C_BAD : MPH_C_DIM;
  mph_rrect_stroke(mph_rect_make(bx + 1, 14, 28, 12), 3, 2, MPH_C_DIM);
  mph_rrect_fill4(mph_rect_make(bx + 31, 17, 2, 6), 0, 1, 1, 0, MPH_C_DIM);
  if (percent > 0) {
    mph_rrect_fill(mph_rect_make(bx + 3, 16, 24.0f * percent / 100.0f, 8), 1, level);
  }

  if (title != NULL) {
    mph_text_fit(MPH_T_BODY_B, 22, 26, 600 - 22, MPH_C_FG, title);
  }
  mph_text(MPH_T_BODY, clock_x, 26, MPH_C_DIM, clock);
}

#define LEGEND_RIGHT 936
#define LEGEND_GLYPH_GAP 7
#define LEGEND_ITEM_GAP 18

void mph_legend(const mph_hint *hints, int n) {
  float widths[8];
  n = n > 8 ? 8 : n;
  float total = 0;
  for (int i = 0; i < n; i++) {
    float w = mph_text_w(MPH_T_SMALL, hints[i].caption);
    if (hints[i].glyph != BUTTON_GLYPH_NONE) {
      w += mph_glyph_w(hints[i].glyph, MPH_T_ROW) + LEGEND_GLYPH_GAP;
    }
    widths[i] = w;
    total += w + (i > 0 ? LEGEND_ITEM_GAP : 0);
  }
  float x = LEGEND_RIGHT - total;
  for (int i = 0; i < n; i++) {
    float gx = x;
    if (hints[i].glyph != BUTTON_GLYPH_NONE) {
      // 22 px symbols (as in 17 px text), centred on the caption's line
      gx += mph_glyph(hints[i].glyph, MPH_T_ROW, x, MPH_LEGEND_BASELINE - 1, MPH_C_DIM) + LEGEND_GLYPH_GAP;
    }
    mph_text(MPH_T_SMALL, gx, MPH_LEGEND_BASELINE, MPH_C_DIM, hints[i].caption);
    x += widths[i] + LEGEND_ITEM_GAP;
  }
}

void mph_menu_legend(const menu_view *view) {
  if (view->decorated && view->hint.text != NULL) {
    mph_hint hint = {view->hint.glyph, view->hint.text};
    mph_legend(&hint, 1);
  }
}

void mph_category(float x, float y, float w, const char *upper_text, uint32_t c) {
  // .cat: padding 12px 16px 6px, so the text sits low in its 32 px
  float tracking = 1.68f;
  float max_w = w - 32;
  if (mph_text_tracked_w(MPH_T_LABEL, tracking, upper_text) > max_w) {
    mph_text_fit(MPH_T_LABEL, x + 16, y + 24, max_w, c, upper_text);
    return;
  }
  mph_text_tracked(MPH_T_LABEL, x + 16, y + 24, tracking, c, upper_text);
}

void mph_separator(float x, float y, float w) {
  mph_rect_fill(mph_rect_make(x + 16, y + 6, w - 32, 1), MPH_C_W08);
}

void mph_scrollbar(mph_rect track, float content_h, float view_h, float offset) {
  if (content_h <= view_h + 0.5f || view_h <= 0) {
    return;
  }
  float thumb = track.h * view_h / content_h;
  thumb = thumb < 24 ? 24 : thumb;
  float max_offset = content_h - view_h;
  float pos = offset < 0 ? 0 : offset > max_offset ? max_offset : offset;
  float y = track.y + (track.h - thumb) * pos / max_offset;
  mph_pill(mph_rect_make(track.x, track.y, 3, track.h), MPH_C_W12);
  mph_pill(mph_rect_make(track.x, y, 3, thumb), mph_alpha(MPH_C_FG, 0.45f));
}
