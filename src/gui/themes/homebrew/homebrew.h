#pragma once

// Homebrew: the original look. Green-bordered boxes centred on black, a
// monospaced Nerd Font at 18 px, rows 24 px tall. Private to this folder.

#include "../../guilib.h"
#include "../../text.h"
#include "../../theme.h"

#include <vita2d.h>

// Nerd Font icons in assets/nerdfont.ttf
#define ICON_LEFT_ARROW   "\xef\x95\x8c"
#define ICON_RIGHT_ARROW  "\xef\x95\x93"
#define ICON_LEFT_RIGHT_ARROWS ICON_LEFT_ARROW ICON_RIGHT_ARROW
#define ICON_NETWORK      "\xef\xa6\x8c"
#define ICON_SPRINTING    "\xeF\xa5\xaD"

#define HB_FONT_SIZE 18
#define HB_LINE_HEIGHT 22
// Button symbols in 18 px text
#define HB_GLYPH_SIZE 14
// Every menu row is this tall, and no menu box is taller than the default one
#define HB_ROW_HEIGHT 24
#define HB_MENU_MAX_HEIGHT 400

// Where a box sits, and its rows' height
typedef struct hb_geom {
  int x, y, width, height, el, total_y;
} hb_geom;

hb_geom hb_geom_centered(int w, int h);
// Just tall enough for `rows` rows, up to HB_MENU_MAX_HEIGHT
hb_geom hb_geom_fitted(int w, int rows);
void hb_draw_border(hb_geom geom, unsigned int border_color);

// homebrew_text.c
extern vita2d_font *hb_font;
void hb_text_init(void);
void hb_text_fini(void);
// Draws a control symbol size pixels tall whose bottom sits on the text
// baseline y, and returns its width (labelled buttons are wider than size)
int hb_draw_button_glyph(int glyph, int x, int y, int size);
// The same width, without drawing
int hb_button_glyph_width(int glyph, int size);
// Draws text with its button markers as symbols, and returns its width
int hb_draw_rich_text(int x, int y, unsigned int color, int size, const char *text);
int hb_rich_text_width(int size, const char *text);
// The same in fonts of their own, for text added after the look was fixed
// (the new theme hint), so it can't change how any other text renders
int hb_draw_rich_text_aside(int x, int y, unsigned int color, int size, const char *text);
int hb_rich_text_width_aside(int size, const char *text);
void hb_draw_text_hcentered(int x, int y, unsigned int color, const char *text);
// Wraps text at the UI font size
int hb_wrap_text(const char *text, int max_width, char lines[][WRAP_LINE_MAX], int max_lines);

// homebrew_menu.c
void hb_draw_menu(const menu_view *view);
void hb_menu_follow(menu_view *view);

// homebrew_boxart.c
void hb_draw_boxart(const boxart_view *view, ui_hint hint);
void hb_boxart_fini(void);

// homebrew_dialog.c
void hb_draw_alert(const alert_view *view);
int hb_help_prepare(const char *title, const char *message);
void hb_draw_help(const help_view *view);

// homebrew_stream.c
void hb_draw_stats(const stats_view *view);
void hb_draw_stream_overlay(const stream_overlay_view *view);
