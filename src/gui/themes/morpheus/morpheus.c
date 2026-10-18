// Morpheus: the new theme, opted into with L and R on the host list.
//
// A placeholder until it's built from claude_bundle/demo/index.html: plain
// text on black in the Vita's system font (the shared one from text.h,
// which has the forms the UI language needs), so every screen works after the
// switch. It shares no drawing code with Homebrew, only the view models
// (ui_view.h) and the shared text handling (text.h). Replace it screen by
// screen; see src/gui/themes/README.md.

#include "../../guilib.h"
#include "../../text.h"
#include "../../theme.h"

#include <stdio.h>
#include <string.h>
#include <vita2d.h>

#define MPH_SCALE 1.0f
#define MPH_ROW_HEIGHT 28
#define MPH_LIST_TOP 70
#define MPH_LIST_BOTTOM 490
#define MPH_LEFT 40
#define MPH_RIGHT (WIDTH - 40)
#define MPH_VISIBLE_ROWS ((MPH_LIST_BOTTOM - MPH_LIST_TOP) / MPH_ROW_HEIGHT)
#define MPH_MAX_LINES 32

#define MPH_FG RGBA8(0xF2, 0xF2, 0xF2, 0xFF)
#define MPH_DIM RGBA8(0x9A, 0x9A, 0x9A, 0xFF)
#define MPH_ACCENT RGBA8(0xA9, 0xC8, 0xFF, 0xFF)
#define MPH_HIGHLIGHT RGBA8(0x2A, 0x3A, 0x5A, 0xFF)
#define MPH_PANEL RGBA8(0x1C, 0x1C, 0x22, 0xFF)

static void morpheus_init(void) {
  vita2d_set_clear_color(RGBA8(0x0B, 0x0B, 0x0F, 0xFF));
}

static void morpheus_fini(void) {
}

static void morpheus_language_changed(void) {
}

// A short text stand-in for each button symbol, until the icon atlas exists
static const char *glyph_label(int glyph) {
  switch (glyph) {
    case BUTTON_GLYPH_CROSS: return "(X)";
    case BUTTON_GLYPH_CIRCLE: return "(O)";
    case BUTTON_GLYPH_TRIANGLE: return "(/\\)";
    case BUTTON_GLYPH_SQUARE: return "([])";
    case BUTTON_GLYPH_L: return "[L]";
    case BUTTON_GLYPH_R: return "[R]";
    case BUTTON_GLYPH_L2: return "[L2]";
    case BUTTON_GLYPH_R2: return "[R2]";
    case BUTTON_GLYPH_START: return "[START]";
    case BUTTON_GLYPH_SELECT: return "[SELECT]";
    case BUTTON_GLYPH_PS: return "[PS]";
    case BUTTON_GLYPH_DPAD: return "[+]";
    case BUTTON_GLYPH_DPAD_UP: return "[Up]";
    case BUTTON_GLYPH_DPAD_DOWN: return "[Down]";
    case BUTTON_GLYPH_DPAD_LEFT: return "[Left]";
    case BUTTON_GLYPH_DPAD_RIGHT: return "[Right]";
    case BUTTON_GLYPH_LEFT_STICK: return "[LS]";
    case BUTTON_GLYPH_RIGHT_STICK: return "[RS]";
    case BUTTON_GLYPH_L3: return "[L3]";
    case BUTTON_GLYPH_R3: return "[R3]";
    case BUTTON_GLYPH_FRONT_TOUCH: return "[Touch]";
    case BUTTON_GLYPH_REAR_TOUCH: return "[Rear touch]";
    default: return "[?]";
  }
}

// Draws (or only measures) text with its button markers as labels
static int mph_text(bool draw, int x, int y, unsigned int color, const char *text) {
  // Owned by text.c, which reloads it for a new language
  vita2d_pgf *mph_font = text_system_font();
  if (mph_font == NULL || text == NULL) {
    return 0;
  }
  int start_x = x;
  char run[WRAP_LINE_MAX * 4];
  size_t length = 0;
  const char *p = text;
  while (true) {
    int marker_length = 0;
    int glyph = *p ? text_marker(p, &marker_length) : -1;
    // Flushed when full only on a character boundary
    if (*p == '\0' || glyph >= 0 || (length >= sizeof(run) - 8 && ((unsigned char)*p & 0xC0) != 0x80)) {
      run[length] = '\0';
      if (length > 0) {
        if (draw) {
          vita2d_pgf_draw_text(mph_font, x, y, color, MPH_SCALE, run);
        }
        x += vita2d_pgf_text_width(mph_font, MPH_SCALE, run);
        length = 0;
      }
      if (*p == '\0') {
        break;
      }
      if (glyph >= 0) {
        const char *label = glyph_label(glyph);
        if (draw) {
          vita2d_pgf_draw_text(mph_font, x, y, MPH_ACCENT, MPH_SCALE, label);
        }
        x += vita2d_pgf_text_width(mph_font, MPH_SCALE, label);
        p += marker_length;
        continue;
      }
    }
    run[length++] = *p++;
  }
  return x - start_x;
}

static int measure(const char *text, void *context) {
  (void)context;
  return mph_text(false, 0, 0, 0, text);
}

static void draw_hint(ui_hint hint, int y) {
  if (hint.text == NULL) {
    return;
  }
  int x = MPH_LEFT;
  if (hint.glyph != BUTTON_GLYPH_NONE) {
    x += mph_text(true, x, y, MPH_ACCENT, glyph_label(hint.glyph)) + 6;
  }
  mph_text(true, x, y, MPH_DIM, hint.text);
}

static void mph_draw_menu(const menu_view *view) {
  const char *title = view->screen->title ? view->screen->title : "Moonlight";
  mph_text(true, MPH_LEFT, 40, MPH_FG, title);

  if (view->tabs != NULL) {
    int x = MPH_LEFT + 320;
    for (int i = 0; i < view->tabs->count; i++) {
      x += mph_text(true, x, 40, view->tabs->tabs[i].current ? MPH_ACCENT : MPH_DIM, view->tabs->tabs[i].name) + 16;
    }
  }

  for (int i = view->scroll; i < view->row_count && i < view->scroll + MPH_VISIBLE_ROWS; i++) {
    const menu_entry *row = &view->rows[i];
    int top = MPH_LIST_TOP + (i - view->scroll) * MPH_ROW_HEIGHT;
    int baseline = top + 20;
    bool selected = i == view->cursor_row && !row->disabled;
    if (selected) {
      vita2d_draw_rectangle(MPH_LEFT - 8, top, MPH_RIGHT - MPH_LEFT + 16, MPH_ROW_HEIGHT - 2, MPH_HIGHLIGHT);
    }
    unsigned int color = row->disabled ? MPH_DIM : MPH_FG;
    if (row->separator && (row->name == NULL || *row->name == '\0')) {
      vita2d_draw_rectangle(MPH_LEFT, top + MPH_ROW_HEIGHT / 2, MPH_RIGHT - MPH_LEFT, 1, MPH_DIM);
    }
    if (row->name != NULL && *row->name) {
      mph_text(true, MPH_LEFT, baseline, color, row->name);
    }
    char value[1100];
    snprintf(value, sizeof(value), "%s%s%s%s", row->suffix ? row->suffix : "", row->suffix && *row->suffix ? "  " : "",
             row->subname, row->checked ? "  *" : "");
    if (*value) {
      int width = mph_text(false, 0, 0, 0, value);
      mph_text(true, MPH_RIGHT - width, baseline, color, value);
    }
  }

  // The selected box art cover, which confirm on the covers' row starts
  if (view->decorated && view->boxart != NULL && view->boxart->count > 0) {
    char cover[300];
    snprintf(cover, sizeof(cover), "< %s >  %s", view->boxart->cover(view->boxart->selected)->name,
             view->boxart->counter);
    mph_text(true, MPH_LEFT, 496, view->boxart->focused ? MPH_ACCENT : MPH_FG, cover);
  }

  if (view->decorated) {
    draw_hint(view->hint, 524);
  }
}

static void mph_menu_follow(menu_view *view) {
  int row = view->cursor_row < view->row_count ? view->cursor_row : 0;
  if (row < view->scroll) {
    view->scroll = row;
  } else if (row >= view->scroll + MPH_VISIBLE_ROWS) {
    view->scroll = row - MPH_VISIBLE_ROWS + 1;
  }
}

static void draw_panel(int x, int y, int w, int h) {
  vita2d_draw_rectangle(x, y, w, h, MPH_PANEL);
}

static void mph_draw_alert(const alert_view *view) {
  char lines[MPH_MAX_LINES][WRAP_LINE_MAX];
  int count = text_wrap(view->message, 520, lines, MPH_MAX_LINES, measure, NULL);
  int height = 60 + count * MPH_ROW_HEIGHT + (view->button_count > 0 ? 40 : 0);
  height = height > HEIGHT - 40 ? HEIGHT - 40 : height;
  int y = (HEIGHT - height) / 2;
  draw_panel(200, y, 560, height);
  for (int i = 0; i < count; i++) {
    mph_text(true, 220, y + 40 + i * MPH_ROW_HEIGHT, MPH_FG, lines[i]);
  }
  int x = 220;
  for (int i = 0; i < view->button_count; i++) {
    x += mph_text(true, x, y + height - 20, MPH_ACCENT, glyph_label(view->glyphs[i])) + 6;
    x += mph_text(true, x, y + height - 20, MPH_FG, view->captions[i]) + 20;
  }
}

static char help_lines[MPH_MAX_LINES * 2][WRAP_LINE_MAX];
static int help_count;
#define MPH_HELP_VISIBLE 12

static int mph_help_prepare(const char *title, const char *message) {
  (void)title;
  help_count = text_wrap(message, 640, help_lines, MPH_MAX_LINES * 2, measure, NULL);
  return help_count > MPH_HELP_VISIBLE ? help_count - MPH_HELP_VISIBLE : 0;
}

static void mph_draw_help(const help_view *view) {
  draw_panel(140, 60, 680, 424);
  mph_text(true, 160, 100, MPH_ACCENT, view->title);
  for (int i = 0; i < MPH_HELP_VISIBLE && view->offset + i < help_count; i++) {
    mph_text(true, 160, 136 + i * MPH_ROW_HEIGHT, MPH_FG, help_lines[view->offset + i]);
  }
  int x = 160;
  x += mph_text(true, x, 464, MPH_ACCENT, glyph_label(view->close_glyph)) + 6;
  mph_text(true, x, 464, MPH_FG, view->close_caption);
}

static void mph_draw_ime_backdrop(void) {
}

static void mph_draw_stats(const stats_view *view) {
  if (!view->full) {
    char text[64];
    snprintf(text, sizeof(text), "fps: %.0f / %d", view->fps, view->target_fps);
    mph_text(true, 16, 30, MPH_FG, text);
    return;
  }
  for (int i = 0; i < view->line_count; i++) {
    mph_text(true, 16, 30 + i * 20, MPH_FG, view->lines + i * view->line_stride);
  }
}

static void mph_draw_stream_overlay(const stream_overlay_view *view) {
  if (view->hint != NULL) {
    unsigned int alpha = (unsigned int)(0xFF * view->hint_opacity);
    mph_text(true, MPH_LEFT, 500, RGBA8(0xFF, 0xFF, 0xFF, alpha), view->hint);
  }
}

const struct theme THEME_MORPHEUS = {
  .name = "Morpheus",
  .init = morpheus_init,
  .fini = morpheus_fini,
  .language_changed = morpheus_language_changed,
  .draw_menu = mph_draw_menu,
  .menu_follow = mph_menu_follow,
  .draw_alert = mph_draw_alert,
  .help_prepare = mph_help_prepare,
  .draw_help = mph_draw_help,
  .draw_ime_backdrop = mph_draw_ime_backdrop,
  .draw_stats = mph_draw_stats,
  .draw_stream_overlay = mph_draw_stream_overlay,
};
