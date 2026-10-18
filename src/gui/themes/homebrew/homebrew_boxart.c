#include "homebrew.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Tile size on screen. Sunshine's covers are 3:4, usually 600x800.
#define TILE_W 150
#define TILE_H 200
#define TILE_GAP 18
#define TILE_SPACING (TILE_W + TILE_GAP)
// The selected tile is drawn this much larger
#define SELECTED_SCALE 1.12f
// Where the row sits: the bottom of the screen, under the menu
#define ROW_CENTRE_Y 390
#define NAME_BASELINE_Y 530
#define LABEL_LINES 3
#define LABEL_LINE_HEIGHT 22
#define LABEL_LINE_MAX 64

#define COLOUR_CARD RGBA8(0x2A, 0x2A, 0x2A, 0xFF)
#define COLOUR_LABEL RGBA8(0xDD, 0xDD, 0xDD, 0xFF)
#define COLOUR_DIM RGBA8(0x00, 0x00, 0x00, 0xA0)
#define COLOUR_FOCUS 0xff00ff00 // the menu's selection green
#define COLOUR_SELECTED RGBA8(0xAA, 0xAA, 0xAA, 0xFF)

// An app's name on a plain card, wrapped the first time the card is drawn
typedef struct {
  bool ready;
  int count;
  char lines[LABEL_LINES][LABEL_LINE_MAX];
  int widths[LABEL_LINES];
} card_label;

static card_label *labels;
static int label_count;
static unsigned int label_generation;

void hb_boxart_fini(void) {
  free(labels);
  labels = NULL;
  label_count = 0;
}

static card_label *label_for(const boxart_view *view, int index, const char *name) {
  if (labels == NULL || label_generation != view->generation || label_count != view->count) {
    free(labels);
    labels = calloc(view->count > 0 ? view->count : 1, sizeof(card_label));
    label_count = labels != NULL ? view->count : 0;
    label_generation = view->generation;
  }
  if (labels == NULL || index >= label_count) {
    return NULL;
  }
  card_label *label = &labels[index];
  if (!label->ready) {
    char lines[LABEL_LINES + 1][WRAP_LINE_MAX];
    int count = hb_wrap_text(name, TILE_W - 16, lines, LABEL_LINES + 1);
    if (count > LABEL_LINES) {
      // Longer names end in an ellipsis on the last line
      count = LABEL_LINES;
      size_t len = strlen(lines[count - 1]);
      snprintf(lines[count - 1] + (len > WRAP_LINE_MAX - 4 ? WRAP_LINE_MAX - 4 : len), 4, "...");
    }
    label->count = count;
    for (int i = 0; i < count; i++) {
      snprintf(label->lines[i], LABEL_LINE_MAX, "%s", lines[i]);
      label->widths[i] = hb_rich_text_width(18, label->lines[i]);
    }
    label->ready = true;
  }
  return label;
}

static void draw_outline(float x, float y, float w, float h, int thickness, unsigned int colour) {
  vita2d_draw_rectangle(x - thickness, y - thickness, w + thickness * 2, thickness, colour);
  vita2d_draw_rectangle(x - thickness, y + h, w + thickness * 2, thickness, colour);
  vita2d_draw_rectangle(x - thickness, y, thickness, h, colour);
  vita2d_draw_rectangle(x + w, y, thickness, h, colour);
}

static void draw_tile(const boxart_view *view, int index) {
  const boxart_cover *cover = view->cover(index);
  float distance = index - view->position;
  float cx = WIDTH / 2.0f + distance * TILE_SPACING;
  if (cx < -TILE_W || cx > WIDTH + TILE_W) {
    return;
  }
  // Grows smoothly as it slides into the middle
  float closeness = 1.0f - fminf(fabsf(distance), 1.0f);
  float scale = 1.0f + (SELECTED_SCALE - 1.0f) * closeness;
  float w = TILE_W * scale;
  float h = TILE_H * scale;
  float x = cx - w / 2.0f;
  float y = ROW_CENTRE_Y - h / 2.0f;

  vita2d_draw_rectangle(x, y, w, h, COLOUR_CARD);
  if (cover->texture != NULL) {
    float tw = vita2d_texture_get_width(cover->texture);
    float th = vita2d_texture_get_height(cover->texture);
    float s = fminf(w / tw, h / th);
    vita2d_draw_texture_scale(cover->texture, x + (w - tw * s) / 2.0f, y + (h - th * s) / 2.0f, s, s);
  } else {
    // The app's name on a plain card, while its cover loads or if it has none
    card_label *label = label_for(view, index, cover->name);
    if (label != NULL) {
      int top = ROW_CENTRE_Y - label->count * LABEL_LINE_HEIGHT / 2 + 16;
      for (int i = 0; i < label->count; i++) {
        hb_draw_rich_text((int)(cx - label->widths[i] / 2.0f), top + i * LABEL_LINE_HEIGHT,
                          COLOUR_LABEL, 18, label->lines[i]);
      }
    }
  }

  if (view->locked && cover->id != view->running_id) {
    // Another app is streaming, so these can't be started
    vita2d_draw_rectangle(x, y, w, h, COLOUR_DIM);
  }
  if (index == view->selected) {
    draw_outline(x, y, w, h, 3, view->focused ? COLOUR_FOCUS : COLOUR_SELECTED);
  }
}

void hb_draw_boxart(const boxart_view *view, ui_hint hint, ui_hint extra_hint) {
  if (view->count == 0) {
    return;
  }
  for (int i = 0; i < view->count; i++) {
    if (i != view->selected) {
      draw_tile(view, i);
    }
  }
  // Last, so its outline sits on top of its neighbours
  draw_tile(view, view->selected);

  const char *name = view->cover(view->selected)->name;
  int name_width = hb_rich_text_width(18, name);
  hb_draw_rich_text(WIDTH / 2 - name_width / 2, NAME_BASELINE_Y, 0xffffffff, 18, name);

  if (hint.text != NULL) {
    int hint_width = hb_rich_text_width(18, hint.text);
    int x = WIDTH - 20 - hint_width - 14 - 6;
    x += hb_draw_button_glyph(hint.glyph, x, NAME_BASELINE_Y, 14) + 6;
    hb_draw_rich_text(x, NAME_BASELINE_Y, COLOUR_LABEL, 18, hint.text);
  }
  // The second hint on the left, balancing the first
  if (extra_hint.text != NULL) {
    int x = 20;
    x += hb_draw_button_glyph(extra_hint.glyph, x, NAME_BASELINE_Y, 14) + 6;
    hb_draw_rich_text(x, NAME_BASELINE_Y, COLOUR_LABEL, 18, extra_hint.text);
  }
}
