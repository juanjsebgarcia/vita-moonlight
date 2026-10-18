// Morpheus stream overlays (DESIGN.md S12): the stats card, the start hint,
// and the poor network and sprint badges.
//
// These run on the video thread on every decoded frame, over the game, so
// they stay as cheap as Homebrew's: flat shapes with a 1 px fringe at most,
// no shadows, the same text, and nothing measured unless it changed.

#include "mph_screens.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define STATS_X 16
#define STATS_Y 14
#define STATS_PAD_X 12
#define STATS_PAD_Y 10
#define STATS_LINE 20
#define STATS_FIRST (STATS_PAD_Y + 15)
#define STATS_MAX_LINES 16

#define HINT_TOP 490
#define HINT_H 36
#define HINT_PAD 16

#define BADGE 36
#define BADGE_Y 16
#define BADGE_RIGHT_X 906
#define BADGE_STEP 44

// Plex Mono is monospaced: an ASCII line is as wide as its length in digits
static float mono_advance;

static float mono_w(const char *s) {
  if (mono_advance == 0.0f) {
    mono_advance = (float)mph_text_w(MPH_T_MONO, "0000000000") / 10.0f;
  }
  return mono_advance * (float)strlen(s);
}

void mph_screen_stats_draw(const stats_view *view) {
  mph_frame_begin();
  const char *lines[STATS_MAX_LINES];
  uint32_t colours[STATS_MAX_LINES];
  int count = 0;
  float width = 0;
  // Fps mode's two lines change every frame; they're the same text as Homebrew's
  char fps[32], wifi[32];
  if (view->full) {
    // Every line, measured again only when they change
    static int measured_version = -1;
    static float measured_width;
    count = view->line_count < STATS_MAX_LINES ? view->line_count : STATS_MAX_LINES;
    for (int i = 0; i < count; i++) {
      lines[i] = view->lines + i * view->line_stride;
      colours[i] = i == view->wifi_line && view->wifi_weak ? MPH_C_LIVE : MPH_C_OVERLAY_TEXT;
    }
    if (view->version != measured_version) {
      measured_version = view->version;
      measured_width = 0;
      for (int i = 0; i < count; i++) {
        float w = mph_text_w(MPH_T_MONO, lines[i]);
        measured_width = w > measured_width ? w : measured_width;
      }
    }
    width = measured_width;
  } else {
    snprintf(fps, sizeof(fps), "fps: %.0f / %d", view->fps, view->target_fps);
    lines[count] = fps;
    colours[count++] = MPH_C_OVERLAY_TEXT;
    if (view->wifi_percent >= 0) {
      snprintf(wifi, sizeof(wifi), "Wi-Fi %d%%", view->wifi_percent);
      lines[count] = wifi;
      colours[count++] = view->wifi_weak ? MPH_C_LIVE : MPH_C_OVERLAY_TEXT;
    }
    for (int i = 0; i < count; i++) {
      float w = mono_w(lines[i]);
      width = w > width ? w : width;
    }
  }
  if (count == 0) {
    mph_frame_end(false);
    return;
  }
  mph_rrect_fill(mph_rect_make(STATS_X, STATS_Y, roundf(width) + 2 * STATS_PAD_X, count * STATS_LINE + 2 * STATS_PAD_Y),
                 MPH_R_OVERLAY, MPH_C_OVERLAY_BG);
  for (int i = 0; i < count; i++) {
    mph_text(MPH_T_MONO, STATS_X + STATS_PAD_X, STATS_Y + STATS_FIRST + i * STATS_LINE, colours[i], lines[i]);
  }
  mph_frame_end(false);
}

// The hint's width, measured once per string (it's a translation, fixed
// while streaming)
static float hint_width(const char *hint) {
  static const char *measured;
  static size_t measured_len;
  static float width;
  size_t len = strlen(hint);
  if (hint != measured || len != measured_len) {
    measured = hint;
    measured_len = len;
    width = (float)mph_text_w(MPH_T_BODY, hint);
  }
  return width;
}

static void badge(float x, float alpha) {
  mph_rrect_fill(mph_rect_make(x, BADGE_Y, BADGE, BADGE), MPH_R_OVERLAY, mph_alpha(MPH_C_OVERLAY_BG, alpha));
}

void mph_screen_stream_overlay_draw(const stream_overlay_view *view) {
  bool hint = view->hint != NULL && view->hint_opacity > 0.0f;
  if (!hint && !view->poor_network && !view->sprinting) {
    return;
  }
  mph_frame_begin();
  float poor_alpha = view->poor_network_alpha / 255.0f;
  float sprint_x = view->poor_network ? BADGE_RIGHT_X - BADGE_STEP : BADGE_RIGHT_X;

  // Shapes
  mph_rect pill = {0};
  if (hint) {
    float w = roundf(hint_width(view->hint)) + 2 * HINT_PAD;
    pill = mph_rect_make(roundf(480 - w / 2), HINT_TOP, w, HINT_H);
    mph_pill(pill, mph_alpha(MPH_C_OVERLAY_BG, view->hint_opacity));
  }
  if (view->poor_network) {
    badge(BADGE_RIGHT_X, poor_alpha);
  }
  if (view->sprinting) {
    badge(sprint_x, 1.0f);
  }

  // Icons: the Wi-Fi pieces (22x18) and the sprint glyph (22x22), centred
  if (view->poor_network) {
    float ix = BADGE_RIGHT_X + (BADGE - 22) / 2, iy = BADGE_Y + (BADGE - 18) / 2;
    uint32_t faint = mph_alpha(MPH_C_FG, 0.25f * poor_alpha);
    mph_icon(MPH_ICON_WIFI_OUT_22, ix, iy, faint);
    mph_icon(MPH_ICON_WIFI_MID_22, ix, iy, faint);
    mph_icon(MPH_ICON_WIFI_DOT_22, ix, iy, mph_alpha(MPH_C_LIVE, poor_alpha));
  }
  if (view->sprinting) {
    mph_icon(MPH_ICON_SPRINT_22, sprint_x + (BADGE - 22) / 2, BADGE_Y + (BADGE - 22) / 2, mph_alpha(MPH_C_FG, 0.85f));
  }

  // The hint's text: its keycaps and every alpha follow the fade
  if (hint) {
    mph_transform(view->hint_opacity, 1.0f, 0, 0, 0, 0);
    mph_text(MPH_T_BODY, pill.x + HINT_PAD, mph_baseline(MPH_T_BODY, pill.y, pill.h), 0xFFFFFFFF, view->hint);
  }
  mph_frame_end(false);
}
