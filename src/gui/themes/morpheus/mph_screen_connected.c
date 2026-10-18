// S4, a connected PC's menu, in Morpheus (DESIGN.md §4 S4).
//
// The menu's rows, as ui_connect.c builds them, become:
//  - the header: a PC icon, the heading row as a kicker, the host name, and
//    the server line as chips (address:port, GPU, host version);
//  - with box art: an action column on the left (categories and action rows)
//    and the cover shelf on the right, which the CONNECTED_APPS row stands
//    for;
//  - as text: one list holding the categories, the actions and the apps.
// The order of the rows, and so the cursor's, is the menu's.

#include "mph_screens.h"

#include "../../text.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define HEADER_ICON_X 40
#define HEADER_ICON_Y 58
#define HEADER_TEXT_X 100
#define HEADER_KICKER_BASELINE 66
#define HEADER_TITLE_BASELINE 96
#define HEADER_TITLE_RIGHT 820
#define HEADER_CHIPS_Y 104
#define HEADER_LINE_BASELINE 118

#define CONTENT_TOP 140
#define COLUMN_X MPH_GUTTER
#define COLUMN_W 380
#define LIST_W 880

#define ACTION_H 48
#define CATEGORY_H 32
#define APP_GAP MPH_ROW_GAP

// The shelf (x 440 to 920)
#define SHELF_X 440
#define SHELF_RIGHT 920
#define SHELF_COUNTER_BASELINE 158
#define SHELF_CX 680
#define TILE_W 132
#define TILE_H 176
#define TILE_TOP 176
#define TILE_PITCH 148
#define TILE_SCALE 1.07f
#define SHELF_NAME_BASELINE 392
#define SHELF_NAME_MAX 460
// Tiles fade out past these distances from the middle instead of being clipped
#define FADE_START 1.2f
#define FADE_END 1.9f
// The carousel's ease towards the selection, per 60 Hz frame
#define CAROUSEL_RATE 0.25f

typedef enum { ITEM_HIDDEN, ITEM_CATEGORY, ITEM_ACTION, ITEM_APP, ITEM_SHELF } item_kind;

typedef struct {
  item_kind kind;
  bool live;  // the category of the app streaming now
  float top, h;
} item;

static item items[MPH_LIST_MAX];
static int item_count;
static const char *kicker, *server_line;
static const char *shelf_label;  // upper-cased CONNECTED_APPS, box art only
static const char *shelf_counter;
static int shelf_row = -1;
static float content_h;

static struct {
  mph_focus focus;
  mph_tween scroll;
  float position;  // the carousel, easing towards the selected cover
  unsigned int generation;
  bool carousel_known;
  float last_drawn;
  float seen;
  bool started;
} st = {.seen = -1};

static bool selectable(const menu_entry *e) {
  return !e->disabled && !e->separator;
}

// Sorts the rows into the header, the categories, actions, apps and shelf,
// and stacks the visible ones from 0
static void build(const menu_view *view) {
  const connected_view *c = view->screen->connected;
  bool covers = c != NULL && c->covers;
  bool running = c != NULL && c->running_app != NULL;
  int n = view->row_count < MPH_LIST_MAX ? view->row_count : MPH_LIST_MAX;
  kicker = server_line = NULL;
  shelf_label = shelf_counter = NULL;
  shelf_row = -1;
  item_count = n;

  bool seen_category = false, live_used = false;
  int headings = 0;
  float y = 0;
  item_kind previous = ITEM_HIDDEN;
  for (int i = 0; i < n; i++) {
    const menu_entry *e = &view->rows[i];
    item *it = &items[i];
    *it = (item){ITEM_HIDDEN, false, 0, 0};
    if (e->separator) {
      it->kind = ITEM_CATEGORY;
      it->h = CATEGORY_H;
      // While a game runs, the first category is "Streaming {app}"
      it->live = running && !live_used;
      live_used = true;
      seen_category = true;
    } else if (!seen_category && e->disabled) {
      // The heading and the server line; the empty spacer row is dropped
      if (headings == 0) {
        kicker = e->name;
      } else if (headings == 1) {
        server_line = e->name;
      }
      headings++;
      continue;
    } else if (covers && e->adjustable) {
      it->kind = ITEM_SHELF;
      shelf_row = i;
      shelf_label = e->name != NULL ? mph_upper(e->name) : NULL;
      shelf_counter = e->suffix;
      continue;
    } else if (e->kind == MENU_ROW_ACTION) {
      it->kind = ITEM_ACTION;
      it->h = ACTION_H;
    } else {
      it->kind = ITEM_APP;
      it->h = MPH_ROW_H;
    }
    if (it->kind == ITEM_APP && previous == ITEM_APP) {
      y += APP_GAP;
    }
    it->top = y;
    y += it->h;
    previous = it->kind;
  }
  content_h = y;
}

static mph_rect list_view(const menu_view *view) {
  bool covers = view->screen->connected != NULL && view->screen->connected->covers;
  return mph_rect_make(COLUMN_X, CONTENT_TOP, covers ? COLUMN_W : LIST_W, MPH_CONTENT_BOTTOM - CONTENT_TOP);
}

// The scroll that keeps the selected row in view (text mode; the box art
// column always fits)
static float scroll_target(mph_rect v, int focused, float current) {
  bool shown = focused >= 0 && focused < item_count && items[focused].kind != ITEM_HIDDEN &&
               items[focused].kind != ITEM_SHELF;
  float top = shown ? items[focused].top : -1, bottom = shown ? top + items[focused].h : 0;
  return mph_scroll_keep(top, bottom, content_h, v.h, MPH_ROW_H, current);
}

void mph_screen_connected_follow(menu_view *view) {
  build(view);
  view->scroll = (int)roundf(scroll_target(list_view(view), view->cursor_row, view->scroll));
}

/*
 * Header
 */

static void draw_header(const menu_view *view) {
  const connected_view *c = view->screen->connected;
  mph_icon(MPH_ICON_PC_44, HEADER_ICON_X, HEADER_ICON_Y, MPH_C_DIM);
  if (kicker != NULL) {
    mph_text_fit(MPH_T_META, HEADER_TEXT_X, HEADER_KICKER_BASELINE, HEADER_TITLE_RIGHT - HEADER_TEXT_X, MPH_C_DIM,
                 kicker);
  }
  if (c != NULL && c->host != NULL) {
    mph_text_fit(MPH_T_TITLE_S, HEADER_TEXT_X, HEADER_TITLE_BASELINE, HEADER_TITLE_RIGHT - HEADER_TEXT_X, MPH_C_FG,
                 c->host);
  }
  bool chips = c != NULL && c->address != NULL && c->address[0] && c->port != NULL && c->port[0] &&
               c->gpu != NULL && c->gpu[0] && c->gfe_version != NULL && c->gfe_version[0];
  if (chips) {
    static char address[96];
    snprintf(address, sizeof(address), "%s:%s", c->address, c->port);
    float x = HEADER_TEXT_X;
    x += mph_chip(x, HEADER_CHIPS_Y, address, MPH_C_DIM) + 8;
    x += mph_chip(x, HEADER_CHIPS_Y, c->gpu, MPH_C_DIM) + 8;
    mph_chip(x, HEADER_CHIPS_Y, c->gfe_version, MPH_C_DIM);
  } else if (server_line != NULL) {
    mph_text_fit(MPH_T_SMALL, HEADER_TEXT_X, HEADER_LINE_BASELINE, HEADER_TITLE_RIGHT - HEADER_TEXT_X, MPH_C_DIM,
                 server_line);
  }
}

/*
 * The action column, or the whole list in text mode
 */

static void draw_items(const menu_view *view, mph_rect v, float offset) {
  int focused = view->cursor_row;
  // Shapes: the streaming category's live dot
  for (int i = 0; i < item_count; i++) {
    const item *it = &items[i];
    float top = v.y + it->top - offset;
    if (it->kind == ITEM_CATEGORY && it->live && top + it->h >= v.y && top <= v.y + v.h) {
      mph_live_dot(v.x + 16 + 4, top + 20);
    }
  }
  // Text
  for (int i = 0; i < item_count; i++) {
    const item *it = &items[i];
    const menu_entry *e = &view->rows[i];
    float top = v.y + it->top - offset;
    if (it->kind == ITEM_HIDDEN || it->kind == ITEM_SHELF || top + it->h < v.y || top > v.y + v.h ||
        e->name == NULL) {
      continue;
    }
    bool on = i == focused && selectable(e);
    switch (it->kind) {
      case ITEM_CATEGORY: {
        float x = it->live ? v.x + 14 : v.x;
        mph_category(x, top, v.w - (x - v.x), mph_upper(e->name), it->live ? MPH_C_LIVE : MPH_C_FAINT);
        break;
      }
      case ITEM_ACTION: {
        uint32_t c = on ? MPH_C_HL_FG : e->disabled ? MPH_C_FAINT : e->danger ? MPH_C_BAD : MPH_C_FG;
        mph_text_fit(MPH_T_ROW_L, v.x + 16, top + 30, v.w - 32, c, e->name);
        break;
      }
      case ITEM_APP: {
        uint32_t c = on ? MPH_C_HL_FG : e->disabled ? MPH_C_FAINT : MPH_C_FG;
        mph_text_fit(MPH_T_ROW, v.x + 16, mph_baseline(MPH_T_ROW, top, it->h), v.w - 32, c, e->name);
        break;
      }
      default:
        break;
    }
  }
}

/*
 * The box art shelf
 */

static float enter_alpha, enter_dx;

// An opacity for what follows, on top of the screen's enter transition
static void set_alpha(float a) {
  mph_transform(enter_alpha * a, 1.0f, 0, 0, enter_dx, 0);
}

static float tile_alpha(float d) {
  d = fabsf(d);
  if (d <= FADE_START) {
    return 1.0f;
  }
  return d >= FADE_END ? 0.0f : (FADE_END - d) / (FADE_END - FADE_START);
}

static mph_rect tile_rect(int index) {
  float d = index - st.position;
  float closeness = 1.0f - fminf(fabsf(d), 1.0f);
  float s = 1.0f + (TILE_SCALE - 1.0f) * closeness;
  float cx = SHELF_CX + d * TILE_PITCH, cy = TILE_TOP + TILE_H / 2.0f;
  return mph_rect_make(cx - TILE_W * s / 2, cy - TILE_H * s / 2, TILE_W * s, TILE_H * s);
}

// A cover keeps its shape inside the tile
static mph_rect cover_rect(const vita2d_texture *t, mph_rect r) {
  float tw = vita2d_texture_get_width(t), th = vita2d_texture_get_height(t);
  if (tw <= 0 || th <= 0) {
    return r;
  }
  float s = fminf(r.w / tw, r.h / th);
  float w = tw * s, h = th * s;
  return mph_rect_make(r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, w, h);
}

// An app's name on a plain tile: up to three lines, the last with an ellipsis
static void draw_tile_name(mph_rect r, const char *name) {
  mph_lines lines;
  int n = mph_wrap(MPH_T_TILE, name, r.w - 20, &lines);
  int shown = n > 3 ? 3 : n;
  float cx = r.x + r.w / 2;
  float first = roundf(r.y + r.h / 2 - shown * 17 / 2.0f + 12);
  if (n <= 3) {
    mph_draw_lines(MPH_T_TILE, &lines, cx, first, 17, 0, n, MPH_C_FG, MPH_ALIGN_CENTER);
    return;
  }
  mph_draw_lines(MPH_T_TILE, &lines, cx, first, 17, 0, 2, MPH_C_FG, MPH_ALIGN_CENTER);
  char last[WRAP_LINE_MAX * 2];
  snprintf(last, sizeof(last), "%s %s", lines.line[2], lines.line[3]);
  float w = fminf(mph_text_w(MPH_T_TILE, last), r.w - 20);
  mph_text_fit(MPH_T_TILE, cx - w / 2, first + 2 * 17, r.w - 20, MPH_C_FG, last);
}

typedef struct {
  int index;
  mph_rect r;
  float alpha;
  const vita2d_texture *texture;
  const char *name;
  bool dim;
} tile;

static void draw_shelf(const menu_view *view, bool focused) {
  const boxart_view *b = view->decorated ? view->boxart : NULL;
  if (shelf_label != NULL) {
    set_alpha(1.0f);
    mph_category(SHELF_X, CONTENT_TOP, SHELF_RIGHT - SHELF_X - 120, shelf_label, MPH_C_FAINT);
  }
  if (shelf_counter != NULL) {
    mph_text_right(MPH_T_META, SHELF_RIGHT, SHELF_COUNTER_BASELINE, MPH_C_DIM, shelf_counter);
  }
  if (b == NULL || b->count <= 0) {
    return;
  }

  // The covers in reach of the middle: up to two either side
  tile tiles[5];
  int count = 0, selected = -1;
  int first = (int)floorf(st.position - FADE_END), last = (int)ceilf(st.position + FADE_END);
  first = first < 0 ? 0 : first;
  last = last >= b->count ? b->count - 1 : last;
  for (int i = first; i <= last && count < 5; i++) {
    float a = tile_alpha(i - st.position);
    if (a <= 0.0f) {
      continue;
    }
    // cover() returns the same struct each call: copy what's needed now
    const boxart_cover *cover = b->cover(i);
    tile *t = &tiles[count];
    t->index = i;
    t->r = tile_rect(i);
    t->alpha = a;
    t->texture = cover->texture;
    t->name = cover->name;
    t->dim = b->locked && cover->id != b->running_id;
    if (i == b->selected) {
      selected = count;
    }
    count++;
  }

  // Shapes: shadows, then the selected tile's outline and the plain tiles
  for (int i = 0; i < count; i++) {
    set_alpha(tiles[i].alpha);
    mph_shadow(tiles[i].r, MPH_R_ROW, 0, 10, 28, MPH_C_SHADOW);
  }
  set_alpha(1.0f);
  if (st.focus.kind == MPH_FOCUS_RING) {
    // Over the shadows, so they don't darken it
    mph_focus_draw(&st.focus, 0, 0);
  }
  for (int i = 0; i < count; i++) {
    set_alpha(tiles[i].alpha);
    if (i == selected && !focused) {
      mph_rrect_stroke(mph_rect_grow(tiles[i].r, 4), 14 + 1, 2, MPH_C_DIM);
    }
    mph_rrect_fill(tiles[i].r, MPH_R_ROW, MPH_C_SURFACE2);
  }
  // Covers, and names on the tiles without one
  for (int i = 0; i < count; i++) {
    set_alpha(tiles[i].alpha);
    if (tiles[i].texture != NULL) {
      mph_texture_rrect(tiles[i].texture, cover_rect(tiles[i].texture, tiles[i].r), MPH_R_ROW, 0xFFFFFFFF);
    } else if (tiles[i].name != NULL) {
      draw_tile_name(tiles[i].r, tiles[i].name);
    }
  }
  // Apps that can't be started while another streams
  for (int i = 0; i < count; i++) {
    if (tiles[i].dim) {
      set_alpha(tiles[i].alpha);
      mph_rrect_fill(tiles[i].r, MPH_R_ROW, MPH_C_TILE_DIM);
    }
  }
  set_alpha(1.0f);

  const boxart_cover *cover = b->cover(b->selected);
  if (cover != NULL && cover->name != NULL) {
    float w = fminf(mph_text_w(MPH_T_BTN, cover->name), SHELF_NAME_MAX);
    mph_text_fit(MPH_T_BTN, roundf(SHELF_CX - w / 2), SHELF_NAME_BASELINE, SHELF_NAME_MAX, MPH_C_FG, cover->name);
  }
}

/*
 * The screen
 */

void mph_screen_connected_draw(const menu_view *view, const mph_enter *enter) {
  build(view);
  const boxart_view *b = view->decorated ? view->boxart : NULL;
  mph_rect v = list_view(view);
  float target = scroll_target(v, view->cursor_row, view->scroll);

  // A new screen, or this one back from a stream or a dialog (a pause in
  // drawing it), starts afresh
  float now = mph_time();
  bool fresh = mph_enter_is_new(enter, &st.seen);
  if (!st.started || fresh || now - st.last_drawn > 0.2f) {
    mph_focus_hide(&st.focus);
    mph_tween_snap(&st.scroll, target);
    st.carousel_known = false;
    st.started = true;
  }
  st.last_drawn = now;
  mph_tween_to(&st.scroll, target, MPH_M_SCROLL, MPH_EASE_OUT);
  float offset = roundf(mph_tween_value(&st.scroll));

  if (b != NULL && b->count > 0) {
    if (!st.carousel_known || st.generation != b->generation) {
      st.position = b->selected;
      st.generation = b->generation;
      st.carousel_known = true;
    }
    st.position = mph_follow(st.position, b->selected, CAROUSEL_RATE);
    if (fabsf(st.position - b->selected) < 0.01f) {
      st.position = b->selected;
    }
  }

  mph_bg_draw();
  mph_statusbar(view->screen->title);

  float k = mph_enter_k(enter);
  enter_alpha = k;
  enter_dx = 18.0f * (1.0f - k);
  set_alpha(1.0f);

  // The highlight: the pill on a row, or the ring on the selected cover
  int focused = view->cursor_row;
  bool shelf_focused = focused == shelf_row && shelf_row >= 0 && selectable(&view->rows[focused]);
  if (shelf_focused && b != NULL && b->count > 0) {
    mph_focus_set(&st.focus, tile_rect(b->selected), MPH_R_RING, MPH_FOCUS_RING, focused);
  } else if (!shelf_focused && focused >= 0 && focused < item_count && items[focused].kind != ITEM_HIDDEN &&
             items[focused].kind != ITEM_CATEGORY && selectable(&view->rows[focused])) {
    mph_focus_set(&st.focus, mph_rect_make(v.x, v.y + items[focused].top, v.w, items[focused].h), MPH_R_ROW,
                  MPH_FOCUS_PILL, focused);
  } else {
    mph_focus_hide(&st.focus);
  }

  bool clip = content_h > v.h;
  if (clip) {
    mph_clip(v);
  }
  // The pill scrolls with the rows; the ring is drawn with the shelf
  if (st.focus.kind == MPH_FOCUS_PILL) {
    mph_focus_draw(&st.focus, 0, -offset);
  }
  draw_items(view, v, offset);
  if (clip) {
    mph_clip_end();
    mph_scrollbar(mph_rect_make(v.x + v.w + 4, v.y, 3, v.h), content_h, v.h, offset);
  }

  draw_header(view);
  if (shelf_row >= 0) {
    draw_shelf(view, shelf_focused);
  } else if (st.focus.kind == MPH_FOCUS_RING) {
    mph_focus_draw(&st.focus, 0, 0);
  }

  mph_transform_reset();
  mph_menu_legend(view);
}
