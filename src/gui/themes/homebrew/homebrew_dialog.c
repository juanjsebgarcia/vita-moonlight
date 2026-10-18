#include "homebrew.h"

#define ALERT_TOP_PADDING 30
#define ALERT_SIDE_PADDING 14
#define ALERT_MAX_LINES 24
#define ALERT_WIDTH 400
#define ALERT_MIN_HEIGHT 200
// Room under the text for the button captions, which sit 10 px above the
// bottom edge, plus a gap so the last line never touches them
#define ALERT_BUTTON_AREA 52
#define HELP_MAX_LINES 48

// Draws "<button> caption" pairs right-aligned along the bottom of the box
static void draw_button_captions(hb_geom geom, const int glyphs[], const char *const captions[], int buttons_count) {
  const int gap = 6;
  const int spacing = 16;

  int total_width = 0;
  for (int i = 0; i < buttons_count && i < ALERT_MAX_BUTTONS; i++) {
    total_width += HB_GLYPH_SIZE + gap + hb_rich_text_width(HB_FONT_SIZE, captions[i]) + spacing;
  }

  int x = geom.x + geom.width - total_width;
  int y = geom.total_y - 10;
  for (int i = 0; i < buttons_count && i < ALERT_MAX_BUTTONS; i++) {
    x += hb_draw_button_glyph(glyphs[i], x, y, HB_GLYPH_SIZE) + gap;
    // Rich text, so captions the app's font lacks fall back to the system font
    x += hb_draw_rich_text(x, y, 0xffffffff, HB_FONT_SIZE, captions[i]) + spacing;
  }
}

void hb_draw_alert(const alert_view *view) {
  if (view->kind == ALERT_PROGRESS) {
    vita2d_draw_rectangle(0, 0, WIDTH, HEIGHT, 0xff000000);
  }

  char lines[ALERT_MAX_LINES][WRAP_LINE_MAX];
  int line_count = hb_wrap_text(view->message, ALERT_WIDTH - ALERT_SIDE_PADDING * 2, lines, ALERT_MAX_LINES);

  // Tall enough for the wrapped text and the buttons, never shorter than the
  // standard 200 px, and no taller than the screen allows
  int height = ALERT_TOP_PADDING + line_count * HB_LINE_HEIGHT + ALERT_BUTTON_AREA;
  height = height < ALERT_MIN_HEIGHT ? ALERT_MIN_HEIGHT : height;
  height = height > HEIGHT - 40 ? HEIGHT - 40 : height;
  hb_geom geom = hb_geom_centered(ALERT_WIDTH, height);

  vita2d_draw_rectangle(geom.x, geom.y, geom.width, geom.height, 0x18fffff);

  long border_color = 0xff006000;
  hb_draw_border(geom, border_color);

  // A single line sits in the middle of the box, longer text starts at the top
  int y = line_count == 1 ? geom.height / 2 : ALERT_TOP_PADDING;
  for (int i = 0; i < line_count; i++) {
    hb_draw_text_hcentered(geom.x + geom.width / 2, y + geom.y, 0xffffffff, lines[i]);
    y += HB_LINE_HEIGHT;
  }

  draw_button_captions(geom, view->glyphs, view->captions, view->button_count);
}

/*
 * Help: larger than the standard alert, so a few sentences fit without
 * crowding, and scrolled when they don't
 */

static char help_lines[HELP_MAX_LINES][WRAP_LINE_MAX];
static int help_line_count;
static int help_visible;

static hb_geom help_geom(void) {
  return hb_geom_centered(600, 320);
}

static int help_title_y(hb_geom geom) {
  return geom.y + ALERT_TOP_PADDING;
}

static int help_body_top(hb_geom geom) {
  return help_title_y(geom) + HB_LINE_HEIGHT + 10;
}

static int help_body_bottom(hb_geom geom) {
  // Leave room for the button captions along the bottom
  return geom.total_y - 34;
}

int hb_help_prepare(const char *title, const char *message) {
  (void)title;
  hb_geom geom = help_geom();
  help_line_count = hb_wrap_text(message, geom.width - ALERT_SIDE_PADDING * 2 - 12, help_lines, HELP_MAX_LINES);
  help_visible = (help_body_bottom(geom) - help_body_top(geom)) / HB_LINE_HEIGHT + 1;
  return help_line_count > help_visible ? help_line_count - help_visible : 0;
}

void hb_draw_help(const help_view *view) {
  hb_geom geom = help_geom();
  int title_y = help_title_y(geom);
  int body_top = help_body_top(geom);
  int body_bottom = help_body_bottom(geom);
  int offset = view->offset;

  vita2d_draw_rectangle(geom.x, geom.y, geom.width, geom.height, 0x18fffff);
  hb_draw_border(geom, 0xff006000);
  hb_draw_text_hcentered(geom.x + geom.width / 2, title_y, 0xffffffff, view->title);

  for (int i = 0; i < help_visible && offset + i < help_line_count; i++) {
    hb_draw_rich_text(geom.x + ALERT_SIDE_PADDING, body_top + i * HB_LINE_HEIGHT,
                      0xffffffff, HB_FONT_SIZE, help_lines[offset + i]);
  }

  // A scroll bar on the right when the text doesn't fit
  if (view->max_offset > 0) {
    int track_x = geom.x + geom.width - 10;
    int track_top = body_top - HB_LINE_HEIGHT + 6;
    int track_height = body_bottom - track_top;
    int thumb_height = track_height * help_visible / help_line_count;
    int thumb_top = track_top + (track_height - thumb_height) * offset / view->max_offset;
    vita2d_draw_rectangle(track_x, track_top, 3, track_height, RGBA8(0xFF, 0xFF, 0xFF, 0x40));
    vita2d_draw_rectangle(track_x, thumb_top, 3, thumb_height, RGBA8(0xFF, 0xFF, 0xFF, 0xC0));
  }

  draw_button_captions(geom, &view->close_glyph, &view->close_caption, 1);
}
