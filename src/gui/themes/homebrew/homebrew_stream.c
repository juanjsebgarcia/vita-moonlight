#include "homebrew.h"

// The stats overlay
#define STATS_FONT_SIZE 16
#define STATS_LINE_HEIGHT 18
#define STATS_ORIGIN_X 10
#define STATS_ORIGIN_Y 20

// The start-of-stream hint
#define HINT_FONT_SIZE 20

void hb_draw_stats(const stats_view *view) {
  // Yellow when the signal is weak enough to start costing frames
  unsigned int wifi_color = view->wifi_weak ? RGBA8(0xFF, 0xD0, 0x00, 0xFF) : RGBA8(0xFF, 0xFF, 0xFF, 0xFF);
  if (!view->full) {
    vita2d_font_draw_textf(hb_font, 40, 20, RGBA8(0xFF, 0xFF, 0xFF, 0xFF), STATS_FONT_SIZE,
                           "fps: %.0f / %d", view->fps, view->target_fps);
    if (view->wifi_percent >= 0) {
      vita2d_font_draw_textf(hb_font, 40, 20 + STATS_LINE_HEIGHT, wifi_color, STATS_FONT_SIZE, "Wi-Fi %d%%",
                             view->wifi_percent);
    }
    return;
  }

  // Measuring text is the expensive part, so only redo it when the text changed
  static int measured_version = -1;
  static int box_width = 0;
  if (view->version != measured_version) {
    box_width = 0;
    for (int i = 0; i < view->line_count; i++) {
      int w = vita2d_font_text_width(hb_font, STATS_FONT_SIZE, view->lines + i * view->line_stride);
      if (w > box_width) {
        box_width = w;
      }
    }
    measured_version = view->version;
  }
  vita2d_draw_rectangle(STATS_ORIGIN_X - 6, STATS_ORIGIN_Y - STATS_LINE_HEIGHT + 2,
                        box_width + 12, view->line_count * STATS_LINE_HEIGHT + 8, RGBA8(0, 0, 0, 0xA0));
  for (int i = 0; i < view->line_count; i++) {
    vita2d_font_draw_text(hb_font, STATS_ORIGIN_X, STATS_ORIGIN_Y + i * STATS_LINE_HEIGHT,
                          i == view->wifi_line ? wifi_color : RGBA8(0xFF, 0xFF, 0xFF, 0xFF), STATS_FONT_SIZE,
                          view->lines + i * view->line_stride);
  }
}

void hb_draw_stream_overlay(const stream_overlay_view *view) {
  if (view->hint != NULL) {
    float opacity = view->hint_opacity;
    int text_width = hb_rich_text_width(HINT_FONT_SIZE, view->hint);
    int box_width = text_width + 32;
    int box_height = 36;
    int box_x = (WIDTH - box_width) / 2;
    int box_y = HEIGHT - box_height - 24;
    vita2d_draw_rectangle(box_x, box_y, box_width, box_height, RGBA8(0, 0, 0, (uint8_t)(0xB0 * opacity)));
    hb_draw_rich_text(box_x + 16, box_y + 25, RGBA8(0xFF, 0xFF, 0xFF, (uint8_t)(0xFF * opacity)), HINT_FONT_SIZE,
                      view->hint);
  }

  if (view->poor_network) {
    vita2d_font_draw_text(hb_font, 40, 500, RGBA8(0xFF, 0xFF, 0xFF, view->poor_network_alpha), 64, ICON_NETWORK);
  }

  if (view->sprinting) {
    vita2d_font_draw_text(hb_font, 40, 50, RGBA8(0xFF, 0xFF, 0xFF, 0xAA), 48, ICON_SPRINTING);
  }
}
