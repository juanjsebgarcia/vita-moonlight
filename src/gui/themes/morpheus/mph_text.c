// Morpheus text: the type ramp, drawn from our own glyph cache.
//
// vita2d's fonts draw one call per glyph, cache each glyph at the first size
// it's drawn (so one instance can't serve two sizes), rasterise while
// measuring, and drop glyphs silently when their atlas fills. So Morpheus
// rasterises with FreeType itself (vita2d links it already) into one atlas
// keyed by (style, character), and draws each string's glyphs as textured
// quads in the batch: a line of text in one colour is one draw call.
//
// Characters the fonts lack (Japanese, Korean, Chinese) are drawn with the
// system font for the UI language (text_system_font()), one at a time.

#include "mph.h"
#include "mph_internal.h"

#include "../../text.h"
#include "../../../i18n/i18n.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SIZES_H
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Faces and styles
 */

enum { FACE_MEDIUM, FACE_BOLD, FACE_EXTRABOLD, FACE_MONO, FACE_COUNT };

static const char *const FACE_FILES[FACE_COUNT] = {
  "app0:assets/morpheus/MPLUS1p-Medium-latin.ttf",
  "app0:assets/morpheus/MPLUS1p-Bold-latin.ttf",
  "app0:assets/morpheus/MPLUS1p-ExtraBold-latin.ttf",
  "app0:assets/morpheus/IBMPlexMono-Medium.ttf",
};

typedef struct {
  unsigned char face, size, line;
  float tracking;
} style_def;

static const style_def STYLES[MPH_T_COUNT] = {
  [MPH_T_HERO] = {FACE_EXTRABOLD, 44, 48, -0.44f},
  [MPH_T_TITLE] = {FACE_EXTRABOLD, 34, 37, -0.34f},
  [MPH_T_TITLE_S] = {FACE_EXTRABOLD, 28, 31, -0.28f},
  [MPH_T_DIALOG] = {FACE_EXTRABOLD, 24, 26, 0},
  [MPH_T_PIN] = {FACE_EXTRABOLD, 60, 66, 0},
  [MPH_T_ROW_L] = {FACE_BOLD, 18, 24, 0},
  [MPH_T_ROW] = {FACE_MEDIUM, 17, 25, 0},
  [MPH_T_BTN] = {FACE_BOLD, 16, 24, 0},
  [MPH_T_BODY] = {FACE_MEDIUM, 15, 23, 0},
  [MPH_T_BODY_B] = {FACE_BOLD, 15, 23, 0},
  [MPH_T_TILE] = {FACE_EXTRABOLD, 15, 17, 0},
  [MPH_T_SMALL] = {FACE_MEDIUM, 14, 22, 0},
  [MPH_T_META] = {FACE_MEDIUM, 13, 20, 0},
  [MPH_T_STRONG_S] = {FACE_BOLD, 13, 20, 0},
  [MPH_T_LABEL] = {FACE_EXTRABOLD, 12, 18, 1.68f},
  [MPH_T_CHIP] = {FACE_BOLD, 12, 18, 0},
  [MPH_T_TAG] = {FACE_EXTRABOLD, 12, 16, 0.22f},  // 11 in the demo; 12 for legibility
  [MPH_T_MONO] = {FACE_MONO, 13, 20, 0},
};

static FT_Library library;
static FT_Face faces[FACE_COUNT];
static void *face_data[FACE_COUNT];
static bool face_failed[FACE_COUNT];
static FT_Size sizes[MPH_T_COUNT];

int mph_type_size(mph_type t) {
  return STYLES[t].size;
}

int mph_type_line(mph_type t) {
  return STYLES[t].line;
}

float mph_baseline(mph_type t, float top, float h) {
  float k = STYLES[t].face == FACE_MONO ? 0.36f : 0.3775f;
  return roundf(top + h / 2.0f + k * STYLES[t].size);
}

static FT_Face load_face(int f) {
  if (faces[f] != NULL || face_failed[f] || library == NULL) {
    return faces[f];
  }
  face_failed[f] = true;
  FILE *file = fopen(FACE_FILES[f], "rb");
  if (file == NULL) {
    return NULL;
  }
  fseek(file, 0, SEEK_END);
  long length = ftell(file);
  fseek(file, 0, SEEK_SET);
  void *data = length > 0 ? malloc(length) : NULL;
  if (data == NULL || fread(data, 1, length, file) != (size_t)length) {
    free(data);
    fclose(file);
    return NULL;
  }
  fclose(file);
  if (FT_New_Memory_Face(library, data, length, 0, &faces[f]) != 0) {
    free(data);
    faces[f] = NULL;
    return NULL;
  }
  face_data[f] = data;
  face_failed[f] = false;
  return faces[f];
}

// The style's face at its size, made current
static FT_Face style_face(mph_type t) {
  FT_Face face = load_face(STYLES[t].face);
  if (face == NULL) {
    return NULL;
  }
  if (sizes[t] == NULL) {
    if (FT_New_Size(face, &sizes[t]) != 0) {
      sizes[t] = NULL;
      return NULL;
    }
    FT_Activate_Size(sizes[t]);
    FT_Set_Pixel_Sizes(face, 0, STYLES[t].size);
  } else {
    FT_Activate_Size(sizes[t]);
  }
  return face;
}

/*
 * The glyph cache and its atlas
 */

#define ATLAS_W 1024
#define ATLAS_H 512
#define CACHE_SIZE 2048  // a power of two

typedef struct {
  uint32_t code;      // 0: empty
  uint8_t type;
  uint8_t system;     // not in the font: drawn with the system font
  uint8_t w, h;       // in the atlas
  uint16_t x, y;
  int16_t left, top;  // bitmap offset from the pen, top above the baseline
  float advance;
} glyph;

static glyph cache[CACHE_SIZE];
static int cache_used;
static vita2d_texture *atlas;
static int shelf_x, shelf_y, shelf_h;
static bool atlas_full;

static void cache_clear(void) {
  memset(cache, 0, sizeof(cache));
  cache_used = 0;
  shelf_x = shelf_y = 1;
  shelf_h = 0;
  atlas_full = false;
}

// Room for a w x h bitmap in the atlas, with a pixel between glyphs
static bool atlas_place(int w, int h, int *x, int *y) {
  if (shelf_x + w + 1 > ATLAS_W) {
    shelf_x = 1;
    shelf_y += shelf_h + 1;
    shelf_h = 0;
  }
  if (shelf_y + h + 1 > ATLAS_H || w + 2 > ATLAS_W) {
    atlas_full = true;
    return false;
  }
  *x = shelf_x;
  *y = shelf_y;
  shelf_x += w + 1;
  shelf_h = h > shelf_h ? h : shelf_h;
  return true;
}

// Coverage is darkened a little, as browsers on the reference renders do:
// FreeType's linear coverage reads thinner on the Vita's screen
#define COVERAGE_GAMMA 1.45f
static uint8_t coverage[256];

static void make_coverage(void) {
  for (int i = 0; i < 256; i++) {
    coverage[i] = (uint8_t)(255.0f * powf(i / 255.0f, 1.0f / COVERAGE_GAMMA) + 0.5f);
  }
}

static float system_scale(mph_type t) {
  return STYLES[t].size / TEXT_SYSTEM_FONT_BASE_SIZE;
}

static void rasterise(glyph *g, mph_type t) {
  FT_Face face = style_face(t);
  FT_UInt index = face != NULL ? FT_Get_Char_Index(face, g->code) : 0;
  if (index == 0) {
    // The system font draws it; measured once, here
    g->system = 1;
    vita2d_pgf *pgf = text_system_font();
    char utf8[5] = {0};
    unsigned int c = g->code;
    if (c < 0x80) {
      utf8[0] = (char)c;
    } else if (c < 0x800) {
      utf8[0] = (char)(0xC0 | (c >> 6));
      utf8[1] = (char)(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      utf8[0] = (char)(0xE0 | (c >> 12));
      utf8[1] = (char)(0x80 | ((c >> 6) & 0x3F));
      utf8[2] = (char)(0x80 | (c & 0x3F));
    } else {
      utf8[0] = (char)(0xF0 | (c >> 18));
      utf8[1] = (char)(0x80 | ((c >> 12) & 0x3F));
      utf8[2] = (char)(0x80 | ((c >> 6) & 0x3F));
      utf8[3] = (char)(0x80 | (c & 0x3F));
    }
    g->advance = pgf != NULL ? (float)vita2d_pgf_text_width(pgf, system_scale(t), utf8) : 0.0f;
    return;
  }
  // Light hinting: crisp horizontal stems, unhinted widths (as browsers lay out)
  if (FT_Load_Glyph(face, index, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT) != 0) {
    return;
  }
  FT_GlyphSlot slot = face->glyph;
  g->advance = slot->linearHoriAdvance / 65536.0f;
  g->left = (int16_t)slot->bitmap_left;
  g->top = (int16_t)slot->bitmap_top;
  const FT_Bitmap *bm = &slot->bitmap;
  int w = (int)bm->width, h = (int)bm->rows;
  int x, y;
  if (w == 0 || h == 0 || w > 255 || h > 255 || atlas == NULL || !atlas_place(w, h, &x, &y)) {
    return;
  }
  uint8_t *data = vita2d_texture_get_datap(atlas);
  unsigned int stride = vita2d_texture_get_stride(atlas);
  for (int row = 0; row < h; row++) {
    const uint8_t *src = bm->buffer + row * bm->pitch;
    uint8_t *dst = data + (y + row) * stride + x;
    if (bm->pixel_mode == FT_PIXEL_MODE_MONO) {
      for (int i = 0; i < w; i++) {
        dst[i] = (src[i >> 3] & (0x80 >> (i & 7))) ? 0xFF : 0;
      }
    } else {
      for (int i = 0; i < w; i++) {
        dst[i] = coverage[src[i]];
      }
    }
  }
  g->x = (uint16_t)x;
  g->y = (uint16_t)y;
  g->w = (uint8_t)w;
  g->h = (uint8_t)h;
}

static const glyph *get_glyph(mph_type t, uint32_t code) {
  static glyph none;
  uint32_t h = (code * 2654435761u) ^ ((uint32_t)t * 0x9E3779B9u);
  for (int probe = 0; probe < CACHE_SIZE; probe++) {
    glyph *g = &cache[(h + probe) & (CACHE_SIZE - 1)];
    if (g->code == code && g->type == t) {
      return g;
    }
    if (g->code == 0) {
      if (cache_used >= CACHE_SIZE * 3 / 4) {
        atlas_full = true;  // cleared at the next frame
        break;
      }
      cache_used++;
      g->code = code;
      g->type = (uint8_t)t;
      rasterise(g, t);
      return g;
    }
  }
  return &none;
}

/*
 * Button symbols
 */

static const char *keycap_label(int glyph) {
  switch (glyph) {
    case BUTTON_GLYPH_L: return "L";
    case BUTTON_GLYPH_R: return "R";
    case BUTTON_GLYPH_L2: return "L2";
    case BUTTON_GLYPH_R2: return "R2";
    case BUTTON_GLYPH_START: return "START";
    case BUTTON_GLYPH_SELECT: return "SELECT";
    case BUTTON_GLYPH_PS: return "PS";
    case BUTTON_GLYPH_L3: return "L3";
    case BUTTON_GLYPH_R3: return "R3";
    default: return NULL;
  }
}

// Icon sizes: text up to 15 px uses 18 px symbols, up to 21 px 22, larger 28
static int size_step(mph_type t) {
  int s = STYLES[t].size;
  return s <= 15 ? 0 : s < 22 ? 1 : 2;
}

// The icon for a symbol drawn in one colour, or -1
static int glyph_icon(int glyph, mph_type t) {
  int step = size_step(t);
  static const mph_icon_id dpad[5][3] = {
    {MPH_ICON_DPAD_ALL_18, MPH_ICON_DPAD_ALL_22, MPH_ICON_DPAD_ALL_28},
    {MPH_ICON_DPAD_UP_18, MPH_ICON_DPAD_UP_22, MPH_ICON_DPAD_UP_28},
    {MPH_ICON_DPAD_DOWN_18, MPH_ICON_DPAD_DOWN_22, MPH_ICON_DPAD_DOWN_28},
    {MPH_ICON_DPAD_LEFT_18, MPH_ICON_DPAD_LEFT_22, MPH_ICON_DPAD_LEFT_28},
    {MPH_ICON_DPAD_RIGHT_18, MPH_ICON_DPAD_RIGHT_22, MPH_ICON_DPAD_RIGHT_28},
  };
  static const mph_icon_id stick[3] = {MPH_ICON_STICK_18, MPH_ICON_STICK_22, MPH_ICON_STICK_28};
  static const mph_icon_id front[3] = {MPH_ICON_FRONT_TOUCH_18, MPH_ICON_FRONT_TOUCH_22, MPH_ICON_FRONT_TOUCH_28};
  static const mph_icon_id rear[3] = {MPH_ICON_REAR_TOUCH_18, MPH_ICON_REAR_TOUCH_22, MPH_ICON_REAR_TOUCH_28};
  switch (glyph) {
    case BUTTON_GLYPH_DPAD: return dpad[0][step];
    case BUTTON_GLYPH_DPAD_UP: return dpad[1][step];
    case BUTTON_GLYPH_DPAD_DOWN: return dpad[2][step];
    case BUTTON_GLYPH_DPAD_LEFT: return dpad[3][step];
    case BUTTON_GLYPH_DPAD_RIGHT: return dpad[4][step];
    case BUTTON_GLYPH_LEFT_STICK:
    case BUTTON_GLYPH_RIGHT_STICK: return stick[step];
    case BUTTON_GLYPH_FRONT_TOUCH: return front[step];
    case BUTTON_GLYPH_REAR_TOUCH: return rear[step];
    default: return -1;
  }
}

// A face button's coloured stroke, or -1
static int face_icon(int glyph, mph_type t, uint32_t *colour) {
  int step = size_step(t);
  static const mph_icon_id icons[4][3] = {
    {MPH_ICON_CROSS_18, MPH_ICON_CROSS_22, MPH_ICON_CROSS_28},
    {MPH_ICON_CIRCLE_18, MPH_ICON_CIRCLE_22, MPH_ICON_CIRCLE_28},
    {MPH_ICON_TRIANGLE_18, MPH_ICON_TRIANGLE_22, MPH_ICON_TRIANGLE_28},
    {MPH_ICON_SQUARE_18, MPH_ICON_SQUARE_22, MPH_ICON_SQUARE_28},
  };
  static const uint32_t colours[4] = {MPH_C_CROSS, MPH_C_CIRCLE, MPH_C_TRIANGLE, MPH_C_SQUARE};
  if (glyph < BUTTON_GLYPH_CROSS || glyph > BUTTON_GLYPH_SQUARE) {
    return -1;
  }
  *colour = colours[glyph - BUTTON_GLYPH_CROSS];
  return icons[glyph - BUTTON_GLYPH_CROSS][step];
}

static const mph_icon_id DISCS[3] = {MPH_ICON_DISC_18, MPH_ICON_DISC_22, MPH_ICON_DISC_28};

int mph_glyph_w(int glyph, mph_type t) {
  const char *label = keycap_label(glyph);
  if (label != NULL) {
    return (int)mph_keycap_w(label);
  }
  uint32_t colour;
  int icon = face_icon(glyph, t, &colour);
  if (icon < 0) {
    icon = glyph_icon(glyph, t);
  }
  return icon >= 0 ? mph_icon_w(icon) : 0;
}

int mph_glyph(int glyph, mph_type t, float x, float baseline, uint32_t c) {
  // Centred on the cap height band
  float centre = baseline - 0.365f * STYLES[t].size;
  uint32_t faint = (c & 0x00FFFFFFu) | ((uint32_t)(0x1A * ((c >> 24) / 255.0f)) << 24);
  const char *label = keycap_label(glyph);
  if (label != NULL) {
    float w = mph_keycap_w(label);
    float top = roundf(centre - 10.0f);
    mph_rrect_fill(mph_rect_make(x, top, w, 20), MPH_R_KEY, faint);
    mph_text_center(MPH_T_TAG, x + w / 2.0f, mph_baseline(MPH_T_TAG, top, 20), c | 0xFF000000u, label);
    return (int)w;
  }
  uint32_t colour;
  int icon = face_icon(glyph, t, &colour);
  if (icon >= 0) {
    float top = roundf(centre - mph_icon_h(icon) / 2.0f);
    mph_icon(DISCS[size_step(t)], x, top, faint);
    mph_icon(icon, x, top, mph_alpha(colour, (c >> 24) / 255.0f));
    return mph_icon_w(icon);
  }
  icon = glyph_icon(glyph, t);
  if (icon < 0) {
    return 0;
  }
  mph_icon(icon, x, roundf(centre - mph_icon_h(icon) / 2.0f), c);
  return mph_icon_w(icon);
}

/*
 * Laying out and drawing
 */

// Measures (and draws, with draw set) up to len bytes of s, with markers
// unless plain. With limit >= 0, stops before the first character that would
// end past it and stores where in *fit.
static float layout(mph_type t, const char *s, int len, float tracking, bool plain, float limit, int *fit,
                    bool draw, float x, float baseline, uint32_t c) {
  if (s == NULL) {
    return 0;
  }
  float pen = 0;
  float base = roundf(baseline);
  const char *p = s, *end = s + len;
  uint32_t tint = draw ? c : 0;
  vita2d_pgf *pgf = NULL;
  while (p < end && *p) {
    int marker_length = 0;
    int marker = plain ? -1 : text_marker(p, &marker_length);
    float advance;
    const char *next = p;
    if (marker >= 0) {
      advance = (float)mph_glyph_w(marker, t);
      next = p + marker_length;
      if (limit >= 0 && pen + advance > limit) {
        break;
      }
      if (draw) {
        mph_glyph(marker, t, roundf(x + pen), base, c);
      }
    } else {
      unsigned int code = text_utf8_next(&next);
      if (code == '\n' || code == '\r') {
        p = next;
        continue;
      }
      const glyph *g = get_glyph(t, code);
      advance = g->advance + tracking;
      if (limit >= 0 && pen + advance - tracking > limit) {
        break;
      }
      if (draw && g->system) {
        if (pgf == NULL) {
          pgf = text_system_font();
        }
        if (pgf != NULL) {
          char one[8];
          int n = (int)(next - p) < 7 ? (int)(next - p) : 7;
          memcpy(one, p, n);
          one[n] = '\0';
          // Through the transform like the atlas glyphs: they slide and scale together
          float px = roundf(x + pen), py = base;
          mph_transform_point(&px, &py);
          mph_flush();
          vita2d_pgf_draw_text(pgf, (int)roundf(px), (int)roundf(py), mph_tint(tint),
                               system_scale(t) * mph_transform_scale(), one);
          mph_perf.draws++;
          mph_perf.glyphs++;
        }
      } else if (draw && g->w > 0) {
        const float inv_w = 1.0f / ATLAS_W, inv_h = 1.0f / ATLAS_H;
        float gx = roundf(x + pen) + g->left, gy = base - g->top;
        mph_quad_textured(atlas, tint, gx, gy, gx + g->w, gy + g->h, g->x * inv_w, g->y * inv_h,
                          (g->x + g->w) * inv_w, (g->y + g->h) * inv_h);
        mph_perf.glyphs++;
      }
    }
    pen += advance;
    p = next;
  }
  if (fit != NULL) {
    *fit = (int)(p - s);
  }
  // Tracking after the last character doesn't count towards the width
  if (tracking != 0 && pen > 0) {
    pen -= tracking;
  }
  return pen;
}

static int whole(float w) {
  return (int)ceilf(w - 0.01f);
}

int mph_text(mph_type t, float x, float baseline, uint32_t c, const char *s) {
  return whole(layout(t, s, s ? (int)strlen(s) : 0, STYLES[t].tracking, false, -1, NULL, true, x, baseline, c));
}

int mph_text_w(mph_type t, const char *s) {
  return whole(layout(t, s, s ? (int)strlen(s) : 0, STYLES[t].tracking, false, -1, NULL, false, 0, 0, 0));
}

int mph_text_tracked(mph_type t, float x, float baseline, float tracking, uint32_t c, const char *s) {
  return whole(layout(t, s, s ? (int)strlen(s) : 0, tracking, true, -1, NULL, true, x, baseline, c));
}

int mph_text_tracked_w(mph_type t, float tracking, const char *s) {
  return whole(layout(t, s, s ? (int)strlen(s) : 0, tracking, true, -1, NULL, false, 0, 0, 0));
}

int mph_text_right(mph_type t, float right_x, float baseline, uint32_t c, const char *s) {
  int w = mph_text_w(t, s);
  mph_text(t, right_x - w, baseline, c, s);
  return w;
}

int mph_text_center(mph_type t, float cx, float baseline, uint32_t c, const char *s) {
  int w = mph_text_w(t, s);
  mph_text(t, roundf(cx - w / 2.0f), baseline, c, s);
  return w;
}

#define ELLIPSIS "\xe2\x80\xa6"

int mph_text_fit(mph_type t, float x, float baseline, float max_w, uint32_t c, const char *s) {
  if (s == NULL) {
    return 0;
  }
  int len = (int)strlen(s);
  float tracking = STYLES[t].tracking;
  float w = layout(t, s, len, tracking, false, -1, NULL, false, 0, 0, 0);
  if (w <= max_w + 0.01f) {
    return whole(layout(t, s, len, tracking, false, -1, NULL, true, x, baseline, c));
  }
  float dots = layout(t, ELLIPSIS, 3, tracking, true, -1, NULL, false, 0, 0, 0);
  if (dots > max_w + 0.01f) {
    // Not even the ellipsis fits
    return 0;
  }
  // A limit below 0 would mean none
  float room = max_w - dots - tracking;
  int fit = 0;
  layout(t, s, len, tracking, false, room > 0 ? room : 0, &fit, false, 0, 0, 0);
  // Not after a space
  while (fit > 0 && s[fit - 1] == ' ') {
    fit--;
  }
  float drawn = layout(t, s, fit, tracking, false, -1, NULL, true, x, baseline, c);
  drawn += fit > 0 ? tracking : 0;
  drawn += layout(t, ELLIPSIS, 3, tracking, true, -1, NULL, true, x + drawn, baseline, c);
  return whole(drawn);
}

/*
 * Wrapping, cached
 */

#define WRAP_CACHE 32
#define WRAP_POOL 2048

typedef struct {
  bool used;
  uint8_t type;
  int max_w, length;
  uint32_t hash;
  mph_lines lines;
  char pool[WRAP_POOL];
} wrap_entry;

static wrap_entry wraps[WRAP_CACHE];
static int wrap_next;

static uint32_t fnv1a(const char *s, int *length) {
  uint32_t h = 2166136261u;
  const char *p = s;
  for (; *p; p++) {
    h = (h ^ (uint8_t)*p) * 16777619u;
  }
  *length = (int)(p - s);
  return h;
}

static int measure_for_wrap(const char *s, void *context) {
  return mph_text_w((mph_type)(intptr_t)context, s);
}

int mph_wrap(mph_type t, const char *s, float max_w, mph_lines *out) {
  out->count = 0;
  if (s == NULL || *s == '\0') {
    return 0;
  }
  int length;
  uint32_t hash = fnv1a(s, &length);
  int width = (int)max_w;
  for (int i = 0; i < WRAP_CACHE; i++) {
    wrap_entry *e = &wraps[i];
    if (e->used && e->type == t && e->max_w == width && e->hash == hash && e->length == length) {
      *out = e->lines;
      return out->count;
    }
  }
  static char lines[MPH_WRAP_MAX][WRAP_LINE_MAX];
  int count = text_wrap(s, width, lines, MPH_WRAP_MAX, measure_for_wrap, (void *)(intptr_t)t);
  wrap_entry *e = &wraps[wrap_next];
  wrap_next = (wrap_next + 1) % WRAP_CACHE;
  *e = (wrap_entry){.used = true, .type = (uint8_t)t, .max_w = width, .length = length, .hash = hash};
  int used = 0;
  for (int i = 0; i < count; i++) {
    int n = (int)strlen(lines[i]) + 1;
    if (used + n > WRAP_POOL) {
      break;
    }
    memcpy(e->pool + used, lines[i], n);
    e->lines.line[e->lines.count++] = e->pool + used;
    used += n;
  }
  *out = e->lines;
  return out->count;
}

void mph_draw_lines(mph_type t, const mph_lines *l, float x, float first_baseline, float line_h, int first, int max,
                    uint32_t c, int align) {
  for (int i = 0; i < max && first + i < l->count; i++) {
    const char *s = l->line[first + i];
    float y = first_baseline + i * line_h;
    if (align == MPH_ALIGN_CENTER) {
      mph_text_center(t, x, y, c, s);
    } else if (align == MPH_ALIGN_RIGHT) {
      mph_text_right(t, x, y, c, s);
    } else {
      mph_text(t, x, y, c, s);
    }
  }
}

/*
 * Upper case
 */

static unsigned int upper(unsigned int c, bool turkish) {
  if (c >= 'a' && c <= 'z') {
    return turkish && c == 'i' ? 0x130 : c - 32;
  }
  if (c < 0x80) {
    return c;
  }
  if ((c >= 0xE0 && c <= 0xFE && c != 0xF7)) {
    return c - 32;
  }
  if (c == 0xFF) {
    return 0x178;
  }
  if (c == 0x131) {
    return 'I';
  }
  if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) {
    return c & ~1u;
  }
  if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) {
    return (c & 1u) ? c : c - 1;
  }
  if (c >= 0x3B1 && c <= 0x3C9) {
    return c == 0x3C2 ? 0x3A3 : c - 32;
  }
  switch (c) {
    case 0x3AC: return 0x386;
    case 0x3AD: return 0x388;
    case 0x3AE: return 0x389;
    case 0x3AF: return 0x38A;
    case 0x3CC: return 0x38C;
    case 0x3CD: return 0x38E;
    case 0x3CE: return 0x38F;
  }
  if (c >= 0x430 && c <= 0x44F) {
    return c - 32;
  }
  if (c >= 0x450 && c <= 0x45F) {
    return c - 80;
  }
  if ((c >= 0x460 && c <= 0x481) || (c >= 0x48A && c <= 0x4BF)) {
    return c & ~1u;
  }
  return c;
}

static int put_utf8(char *out, unsigned int c) {
  if (c < 0x80) {
    out[0] = (char)c;
    return 1;
  }
  if (c < 0x800) {
    out[0] = (char)(0xC0 | (c >> 6));
    out[1] = (char)(0x80 | (c & 0x3F));
    return 2;
  }
  if (c < 0x10000) {
    out[0] = (char)(0xE0 | (c >> 12));
    out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
    out[2] = (char)(0x80 | (c & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (c >> 18));
  out[1] = (char)(0x80 | ((c >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((c >> 6) & 0x3F));
  out[3] = (char)(0x80 | (c & 0x3F));
  return 4;
}

#define UPPER_CACHE 32
#define UPPER_MAX 256

static struct {
  uint32_t hash;
  int length;
  char text[UPPER_MAX];
} uppers[UPPER_CACHE];
static int upper_next;

const char *mph_upper(const char *s) {
  if (s == NULL) {
    return "";
  }
  int length;
  uint32_t hash = fnv1a(s, &length);
  for (int i = 0; i < UPPER_CACHE; i++) {
    if (uppers[i].length == length && uppers[i].hash == hash && uppers[i].text[0]) {
      return uppers[i].text;
    }
  }
  bool turkish = strncmp(i18n_current()->tag, "tr", 2) == 0;
  int slot = upper_next;
  upper_next = (upper_next + 1) % UPPER_CACHE;
  char *out = uppers[slot].text;
  int n = 0;
  const char *p = s;
  while (*p && n < UPPER_MAX - 5) {
    n += put_utf8(out + n, upper(text_utf8_next(&p), turkish));
  }
  out[n] = '\0';
  uppers[slot].hash = hash;
  uppers[slot].length = length;
  return out;
}

/*
 * Life cycle
 */

static void warm_up(void) {
  // MONO and TAG too: the stream overlay draws them on the video thread, where
  // loading a face and rasterising on the first frame showed as a 64 ms hitch
  static const mph_type warm[] = {MPH_T_ROW,    MPH_T_BTN,  MPH_T_SMALL, MPH_T_LABEL,
                                  MPH_T_BODY_B, MPH_T_BODY, MPH_T_MONO,  MPH_T_TAG};
  for (size_t i = 0; i < sizeof(warm) / sizeof(warm[0]); i++) {
    for (unsigned int c = 0x20; c < 0x7F; c++) {
      get_glyph(warm[i], c);
    }
  }
}

void mph_text_init(void) {
  if (FT_Init_FreeType(&library) != 0) {
    library = NULL;
  }
  atlas = vita2d_create_empty_texture_format(ATLAS_W, ATLAS_H, SCE_GXM_TEXTURE_FORMAT_U8_R111);
  if (atlas != NULL) {
    memset(vita2d_texture_get_datap(atlas), 0, vita2d_texture_get_stride(atlas) * ATLAS_H);
    vita2d_texture_set_filters(atlas, SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
  }
  make_coverage();
  cache_clear();
  warm_up();
}

void mph_text_fini(void) {
  for (int t = 0; t < MPH_T_COUNT; t++) {
    sizes[t] = NULL;  // freed with their faces
  }
  for (int f = 0; f < FACE_COUNT; f++) {
    if (faces[f] != NULL) {
      FT_Done_Face(faces[f]);
      faces[f] = NULL;
    }
    free(face_data[f]);
    face_data[f] = NULL;
    face_failed[f] = false;
  }
  if (library != NULL) {
    FT_Done_FreeType(library);
    library = NULL;
  }
  if (atlas != NULL) {
    vita2d_free_texture(atlas);
    atlas = NULL;
  }
  memset(wraps, 0, sizeof(wraps));
  memset(uppers, 0, sizeof(uppers));
}

// A theme call isn't always a new GPU frame: the video thread draws the
// stats and then the stream overlay in one vita2d frame, and the GPU reads
// the atlas only once that frame is submitted. So a full atlas starts over
// only in a new vita2d frame, which the pool tells: vita2d_start_drawing()
// empties it, and within a frame it only fills. The app waits for the GPU
// before starting a frame (ui_end(), and the video thread before drawing),
// so by then nothing still reads the old glyphs.
static unsigned int pool_free_at_end;

void mph_text_frame_begin(void) {
  if (atlas_full && vita2d_pool_free_space() > pool_free_at_end) {
    cache_clear();
  }
}

void mph_text_frame_end(void) {
  pool_free_at_end = vita2d_pool_free_space();
}

void mph_text_language_changed(void) {
  // System font widths belong to the old language's font
  cache_clear();
  memset(wraps, 0, sizeof(wraps));
  memset(uppers, 0, sizeof(uppers));
}
