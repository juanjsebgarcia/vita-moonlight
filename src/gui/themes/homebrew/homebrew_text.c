#include "homebrew.h"

#include <math.h>
#include <string.h>

vita2d_font *hb_font;
// The same font again, only for the small labels inside button glyphs.
// vita2d caches each character once, at the size it's first drawn, so small
// labels drawn with the main font made later full-size letters (such as the
// L in "Licence", after an L button glyph) come out small.
static vita2d_font *glyph_font;

// A text font and its glyph label font
typedef struct {
  vita2d_font *text;
  vita2d_font *labels;
} font_pair;

static font_pair ui_fonts;
// A second pair for the new theme hint on the host list, loaded when first
// drawn. vita2d packs each character into its font's atlas in the order
// they're first drawn, and a glyph's edges can pick up its neighbours there,
// so text added to the shared fonts would shift how later screens' letters
// come out. With its own fonts, the hint leaves every other screen exactly
// as it was.
static font_pair aside_fonts;

static vita2d_font *load_ui_font(void) {
  return vita2d_load_font_file("app0:assets/nerdfont.ttf");
}

void hb_text_init(void) {
  hb_font = load_ui_font();
  glyph_font = load_ui_font();
  ui_fonts = (font_pair){hb_font, glyph_font};
}

static void free_font(vita2d_font **font) {
  if (*font != NULL) {
    vita2d_free_font(*font);
    *font = NULL;
  }
}

void hb_text_fini(void) {
  free_font(&hb_font);
  free_font(&glyph_font);
  free_font(&aside_fonts.text);
  free_font(&aside_fonts.labels);
  ui_fonts = (font_pair){NULL, NULL};
}

static const font_pair *get_aside_fonts(void) {
  if (aside_fonts.text == NULL) {
    aside_fonts.text = load_ui_font();
    aside_fonts.labels = load_ui_font();
  }
  return &aside_fonts;
}

#define CIRCLE_SEGMENTS 16
// The system font's baseline, relative to the app font's
#define SYSTEM_FONT_Y_OFFSET 0

static void draw_thick_line(float x0, float y0, float x1, float y1, unsigned int color) {
  // vita2d lines are a single pixel, which is too faint for an icon
  vita2d_draw_line(x0, y0, x1, y1, color);
  vita2d_draw_line(x0 + 1, y0, x1 + 1, y1, color);
  vita2d_draw_line(x0, y0 + 1, x1, y1 + 1, color);
}

static void fill_triangle(float x0, float y0, float x1, float y1, float x2, float y2, unsigned int color) {
  // Vertices must live in vita2d's per-frame GPU pool
  vita2d_color_vertex *v = vita2d_pool_memalign(3 * sizeof(vita2d_color_vertex), sizeof(vita2d_color_vertex));
  if (v == NULL) {
    return;
  }
  float xs[3] = {x0, x1, x2};
  float ys[3] = {y0, y1, y2};
  for (int i = 0; i < 3; i++) {
    v[i].x = xs[i];
    v[i].y = ys[i];
    v[i].z = 0.5f;
    v[i].color = color;
  }
  vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, v, 3);
}

static void draw_outline_rect(float x, float y, float w, float h, unsigned int color) {
  draw_thick_line(x, y, x + w, y, color);
  draw_thick_line(x + w, y, x + w, y + h, color);
  draw_thick_line(x + w, y + h, x, y + h, color);
  draw_thick_line(x, y + h, x, y, color);
}

static void draw_outline_circle(float cx, float cy, float radius, unsigned int color) {
  for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
    float a0 = (float)i / CIRCLE_SEGMENTS * 2.0f * (float)M_PI;
    float a1 = (float)(i + 1) / CIRCLE_SEGMENTS * 2.0f * (float)M_PI;
    draw_thick_line(cx + radius * cosf(a0), cy + radius * sinf(a0),
                    cx + radius * cosf(a1), cy + radius * sinf(a1), color);
  }
}

// Centres a short label in a box, using a font size that fits its height
static void draw_label(vita2d_font *labels, const char *label, float cx, float top, float height, unsigned int color) {
  int font_size = (int)(height * 0.62f);
  if (font_size < 8) {
    font_size = 8;
  }
  int width = vita2d_font_text_width(labels, font_size, label);
  // y is the baseline, so drop it by roughly half the cap height below the middle
  float baseline = top + height / 2.0f + font_size * 0.36f;
  vita2d_font_draw_text(labels, (int)(cx - width / 2.0f), (int)baseline, color, font_size, label);
}

// Width of a labelled pill for this label at this height
static int pill_width(vita2d_font *labels, const char *label, int size) {
  int font_size = (int)(size * 0.62f);
  if (font_size < 8) {
    font_size = 8;
  }
  int width = vita2d_font_text_width(labels, font_size, label) + size / 2 + 4;
  return width > size ? width : size;
}

static int draw_pill(vita2d_font *labels, const char *label, int x, float top, int size, unsigned int color) {
  int width = pill_width(labels, label, size);
  draw_outline_rect(x, top, width, size, color);
  draw_label(labels, label, x + width / 2.0f, top, size, color);
  return width;
}

static void draw_dpad(int x, float top, int size, int highlight) {
  unsigned int dim = RGBA8(0xDD, 0xDD, 0xDD, 0x60);
  unsigned int bright = RGBA8(0xFF, 0xFF, 0xFF, 0xFF);
  float arm = size / 3.0f;
  float cx = x + size / 2.0f;
  float cy = top + size / 2.0f;
  // The plus shape, then each arm as a triangle pointing outwards
  vita2d_draw_rectangle(x + arm, top + arm, arm, arm, highlight == BUTTON_GLYPH_DPAD ? bright : dim);
  struct { int glyph; float tx, ty, ax, ay, bx, by; } arms[4] = {
    {BUTTON_GLYPH_DPAD_UP, cx, top, x + arm, top + arm, x + 2 * arm, top + arm},
    {BUTTON_GLYPH_DPAD_DOWN, cx, top + size, x + arm, top + 2 * arm, x + 2 * arm, top + 2 * arm},
    {BUTTON_GLYPH_DPAD_LEFT, x, cy, x + arm, top + arm, x + arm, top + 2 * arm},
    {BUTTON_GLYPH_DPAD_RIGHT, x + size, cy, x + 2 * arm, top + arm, x + 2 * arm, top + 2 * arm},
  };
  for (int i = 0; i < 4; i++) {
    bool lit = highlight == BUTTON_GLYPH_DPAD || highlight == arms[i].glyph;
    fill_triangle(arms[i].tx, arms[i].ty, arms[i].ax, arms[i].ay, arms[i].bx, arms[i].by, lit ? bright : dim);
  }
}

// The label of each labelled (pill) button, or NULL
static const char *pill_label(int glyph) {
  switch (glyph) {
    case BUTTON_GLYPH_L: return "L";
    case BUTTON_GLYPH_R: return "R";
    case BUTTON_GLYPH_L2: return "L2";
    case BUTTON_GLYPH_R2: return "R2";
    case BUTTON_GLYPH_START: return "START";
    case BUTTON_GLYPH_SELECT: return "SELECT";
    default: return NULL;
  }
}

static int glyph_width(vita2d_font *labels, int glyph, int size) {
  const char *label = pill_label(glyph);
  if (label != NULL) {
    return pill_width(labels, label, size);
  }
  if (glyph == BUTTON_GLYPH_FRONT_TOUCH || glyph == BUTTON_GLYPH_REAR_TOUCH) {
    return (int)(size * 1.5f) + 1;
  }
  return size;
}

int hb_button_glyph_width(int glyph, int size) {
  return glyph_width(glyph_font, glyph, size);
}

static int draw_glyph(vita2d_font *labels, int glyph, int x, int y, int size) {
  // y is the text baseline; the symbol sits on it, a little below the cap height
  float top = y - size;
  float bottom = y - 1;
  float left = x;
  float right = x + size;
  float middle = x + size / 2.0f;
  float radius = size / 2.0f;
  float cy = top + radius;
  unsigned int grey = RGBA8(0xE8, 0xE8, 0xE8, 0xFF);

  const char *label = pill_label(glyph);
  if (label != NULL) {
    return draw_pill(labels, label, x, top, size, grey);
  }

  switch (glyph) {
    case BUTTON_GLYPH_DPAD:
    case BUTTON_GLYPH_DPAD_UP:
    case BUTTON_GLYPH_DPAD_DOWN:
    case BUTTON_GLYPH_DPAD_LEFT:
    case BUTTON_GLYPH_DPAD_RIGHT:
      draw_dpad(x, top, size, glyph);
      return size;
    case BUTTON_GLYPH_PS:
      draw_outline_circle(middle, cy, radius, grey);
      draw_label(labels, "PS", middle, top, size, grey);
      return size;
    case BUTTON_GLYPH_LEFT_STICK:
    case BUTTON_GLYPH_RIGHT_STICK:
      // An outer ring with the stick's cap in the middle
      draw_outline_circle(middle, cy, radius, grey);
      vita2d_draw_fill_circle(middle, cy, radius * 0.45f, grey);
      return size;
    case BUTTON_GLYPH_L3:
      draw_outline_circle(middle, cy, radius, grey);
      draw_label(labels, "L3", middle, top, size, grey);
      return size;
    case BUTTON_GLYPH_R3:
      draw_outline_circle(middle, cy, radius, grey);
      draw_label(labels, "R3", middle, top, size, grey);
      return size;
    case BUTTON_GLYPH_FRONT_TOUCH: {
      // A landscape screen with a fingertip on it
      float w = size * 1.5f;
      draw_outline_rect(x, top + size * 0.15f, w, size * 0.7f, grey);
      vita2d_draw_fill_circle(x + w * 0.6f, top + size * 0.5f, size * 0.14f, grey);
      return (int)w + 1;
    }
    case BUTTON_GLYPH_REAR_TOUCH: {
      // The same pad, split into the four corner zones the rear touch uses
      float w = size * 1.5f;
      float pad_top = top + size * 0.15f;
      float pad_h = size * 0.7f;
      draw_outline_rect(x, pad_top, w, pad_h, grey);
      vita2d_draw_line(x + w / 2, pad_top, x + w / 2, pad_top + pad_h, grey);
      vita2d_draw_line(x, pad_top + pad_h / 2, x + w, pad_top + pad_h / 2, grey);
      return (int)w + 1;
    }
    case BUTTON_GLYPH_TRIANGLE: {
      unsigned int green = RGBA8(0x40, 0xE0, 0xB0, 0xFF);
      draw_thick_line(middle, top, right, bottom, green);
      draw_thick_line(right, bottom, left, bottom, green);
      draw_thick_line(left, bottom, middle, top, green);
      break;
    }
    case BUTTON_GLYPH_SQUARE: {
      unsigned int pink = RGBA8(0xF0, 0x90, 0xD0, 0xFF);
      draw_thick_line(left, top, right, top, pink);
      draw_thick_line(right, top, right, bottom, pink);
      draw_thick_line(right, bottom, left, bottom, pink);
      draw_thick_line(left, bottom, left, top, pink);
      break;
    }
    case BUTTON_GLYPH_CROSS: {
      unsigned int blue = RGBA8(0x80, 0xB8, 0xF0, 0xFF);
      draw_thick_line(left, top, right, bottom, blue);
      draw_thick_line(right, top, left, bottom, blue);
      break;
    }
    case BUTTON_GLYPH_CIRCLE:
      draw_outline_circle(middle, cy, radius, RGBA8(0xFF, 0x70, 0x70, 0xFF));
      break;
  }
  return size;
}

int hb_draw_button_glyph(int glyph, int x, int y, int size) {
  return draw_glyph(glyph_font, glyph, x, y, size);
}

// Draws a run of text in one font (or only measures it, with draw false).
// Characters the app's font lacks come from the Vita's system font.
static int draw_run(const font_pair *fonts, bool draw, bool system, int x, int y, unsigned int color, int size,
                    const char *run) {
  if (!system) {
    if (draw) {
      vita2d_font_draw_text(fonts->text, x, y, color, size, run);
    }
    return vita2d_font_text_width(fonts->text, size, run);
  }
  vita2d_pgf *pgf = text_system_font();
  if (pgf == NULL) {
    return 0;
  }
  float scale = size / TEXT_SYSTEM_FONT_BASE_SIZE;
  if (draw) {
    vita2d_pgf_draw_text(pgf, x, y + SYSTEM_FONT_Y_OFFSET, color, scale, run);
  }
  return vita2d_pgf_text_width(pgf, scale, run);
}

// Draws (or, with draw false, only measures) text with its button symbols,
// in runs of whichever font has each character
static int rich_text(const font_pair *fonts, bool draw, int x, int y, unsigned int color, int size,
                     const char *text) {
  int glyph_size = size * HB_GLYPH_SIZE / HB_FONT_SIZE;
  int start_x = x;
  char run[WRAP_LINE_MAX * 4];
  size_t run_length = 0;
  bool run_system = false;
  const char *p = text;
  while (true) {
    int length = 0;
    int glyph = *p ? text_marker(p, &length) : -1;
    const char *next = p;
    unsigned int c = *p && glyph < 0 ? text_utf8_next(&next) : 0;
    bool system = c != 0 && !text_nerdfont_has(c);
    size_t bytes = (size_t)(next - p);
    // Draw what's gathered at the end, at a symbol, on a change of font, or when full
    if (*p == '\0' || glyph >= 0 || (run_length > 0 && system != run_system) || run_length + bytes >= sizeof(run)) {
      run[run_length] = '\0';
      if (run_length > 0) {
        x += draw_run(fonts, draw, run_system, x, y, color, size, run);
        run_length = 0;
      }
      if (*p == '\0') {
        break;
      }
      if (glyph >= 0) {
        x += draw ? draw_glyph(fonts->labels, glyph, x, y, glyph_size) : glyph_width(fonts->labels, glyph, glyph_size);
        p += length;
        continue;
      }
    }
    run_system = system;
    memcpy(run + run_length, p, bytes);
    run_length += bytes;
    p = next;
  }
  return x - start_x;
}

int hb_draw_rich_text(int x, int y, unsigned int color, int size, const char *text) {
  return rich_text(&ui_fonts, true, x, y, color, size, text);
}

int hb_rich_text_width(int size, const char *text) {
  return rich_text(&ui_fonts, false, 0, 0, 0, size, text);
}

int hb_draw_rich_text_aside(int x, int y, unsigned int color, int size, const char *text) {
  return rich_text(get_aside_fonts(), true, x, y, color, size, text);
}

int hb_rich_text_width_aside(int size, const char *text) {
  return rich_text(get_aside_fonts(), false, 0, 0, 0, size, text);
}

void hb_draw_text_hcentered(int x, int y, unsigned int color, const char *text) {
  int width = hb_rich_text_width(HB_FONT_SIZE, text);
  hb_draw_rich_text(x - width / 2, y, color, HB_FONT_SIZE, text);
}

static int measure_ui_text(const char *text, void *context) {
  (void)context;
  return hb_rich_text_width(HB_FONT_SIZE, text);
}

int hb_wrap_text(const char *text, int max_width, char lines[][WRAP_LINE_MAX], int max_lines) {
  return text_wrap(text, max_width, lines, max_lines, measure_ui_text, NULL);
}
