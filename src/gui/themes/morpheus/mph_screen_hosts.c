// Morpheus's host screens (DESIGN.md §4):
//   S1 the host list (ui.c ui_main_menu) as Home: the brand, or the current
//      connection, on the left; paired PCs and the ways to add one as cards
//      on the right, then Settings and Quit as rows.
//   S2 a paired PC's menu (ui.c ui_host_manage_menu) as PC options: the PC
//      on the left, its actions on a sheet at the right.
//   S3 the device search (ui_device.c) as Find a PC: a radar, the title and
//      the refresh hint on the left, the devices found as cards on the right.
//
// Everything shown comes from the view's rows and screen data; nothing is
// added. The cursor moves over the rows in list order, as display_menu()
// decides; these layouts only place them.

#include "mph_screens.h"

#include "../../text.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/*
 * State shared by the three screens. Only one is on show at a time, and a
 * new screen starts it afresh.
 */

static struct {
  bool started;
  float seen;
  mph_tween scroll;
  mph_focus ring, pill;
  mph_list_state sheet;
} st = {.seen = -1};

static void begin(const mph_enter *enter) {
  if (!mph_enter_is_new(enter, &st.seen)) {
    return;
  }
  st.started = false;
  mph_focus_hide(&st.ring);
  mph_focus_hide(&st.pill);
  mph_list_reset(&st.sheet);
}

// The scroll, eased towards target; snaps on a screen's first frame
static float scroll_to(float target) {
  if (!st.started) {
    mph_tween_snap(&st.scroll, target);
    st.started = true;
  }
  mph_tween_to(&st.scroll, target, MPH_M_SCROLL, MPH_EASE_OUT);
  return roundf(mph_tween_value(&st.scroll));
}

/*
 * Items: the rows of S1's right column and S3's list, placed top to bottom
 */

typedef enum { IT_SKIP, IT_CATEGORY, IT_CARD, IT_ROW, IT_SEPARATOR } item_kind;

typedef struct {
  item_kind kind;
  float y, h;
  int icon;  // mph_icon_id, or -1
} item;

static item items[MPH_LIST_MAX];

static int row_count(const menu_view *view) {
  return view->row_count < MPH_LIST_MAX ? view->row_count : MPH_LIST_MAX;
}

static float content_height(int n) {
  float bottom = 0;
  for (int i = 0; i < n; i++) {
    if (items[i].kind != IT_SKIP && items[i].y + items[i].h > bottom) {
      bottom = items[i].y + items[i].h;
    }
  }
  return bottom;
}

// The scroll that keeps the focused item in view, a margin from the edges
static float scroll_target(int n, int focused, float view_h, float margin, float current) {
  bool shown = focused >= 0 && focused < n && items[focused].kind != IT_SKIP;
  float top = shown ? items[focused].y : -1, bottom = shown ? top + items[focused].h : 0;
  return mph_scroll_keep(top, bottom, content_height(n), view_h, margin, current);
}

/*
 * S1: the host list as Home
 */

#define HOME_X 622
#define HOME_W 304
#define HOME_TOP 56
#define HOME_TOP_CONNECTED 58
#define HOME_CARD_H 56
#define HOME_CARD_GAP 8
#define HOME_CATEGORY_H 26
#define HOME_ROW_H 44
#define HOME_SEPARATOR_H 13
#define HOME_BUTTON_MAX 520

static bool main_connected(const menu_view *view) {
  return view->screen->main != NULL && view->screen->main->connected;
}

static int main_layout(const menu_view *view) {
  int n = row_count(view);
  bool connected = main_connected(view);
  float y = 0;
  for (int i = 0; i < n; i++) {
    const menu_entry *e = &view->rows[i];
    bool named = e->name != NULL && e->name[0] != '\0';
    item *it = &items[i];
    *it = (item){.kind = IT_SKIP, .y = y, .icon = -1};
    if (e->id == MAIN_MENU_SETTINGS || e->id == MAIN_MENU_QUIT) {
      *it = (item){IT_ROW, y, HOME_ROW_H, e->id == MAIN_MENU_SETTINGS ? MPH_ICON_GEAR_18 : MPH_ICON_POWER_18};
      y += HOME_ROW_H;
    } else if (connected) {
      // The current connection is drawn on the left; only Settings and Quit
      // stay in the column
      continue;
    } else if (e->separator && named) {
      *it = (item){IT_CATEGORY, y, HOME_CATEGORY_H, -1};
      y += HOME_CATEGORY_H;
    } else if (e->separator) {
      *it = (item){IT_SEPARATOR, y, HOME_SEPARATOR_H, -1};
      y += HOME_SEPARATOR_H;
    } else if (e->disabled) {
      // The title row: it's the status bar's title
      continue;
    } else {
      int icon = e->id == MAIN_MENU_SEARCH ? MPH_ICON_SEARCH_30 : e->id == MAIN_MENU_CONNECT ? MPH_ICON_PLUS_30 : MPH_ICON_PC_30;
      *it = (item){IT_CARD, y, HOME_CARD_H, icon};
      y += HOME_CARD_H + HOME_CARD_GAP;
    }
  }
  return n;
}

static mph_rect main_view_rect(const menu_view *view) {
  float top = main_connected(view) ? HOME_TOP_CONNECTED : HOME_TOP;
  return mph_rect_make(HOME_X, top, HOME_W, MPH_CONTENT_BOTTOM - top);
}

// The row that resumes the current connection, or -1
static int main_resume_row(const menu_view *view) {
  for (int i = 0; i < view->row_count; i++) {
    if (view->rows[i].id == MAIN_MENU_CONNECT_RESUME) {
      return i;
    }
  }
  return -1;
}

static float main_target(const menu_view *view, int n) {
  mph_rect r = main_view_rect(view);
  return scroll_target(n, view->cursor_row, r.h, 32, view->scroll);
}

// The brand, when nothing is connected
static void draw_brand(const main_view *main) {
  mph_icon(MPH_ICON_MOON_64, 40, 72, MPH_C_FG);
  mph_text(MPH_T_HERO, 40, 186, MPH_C_FG, "Moonlight");
  if (main != NULL && main->version != NULL) {
    mph_text(MPH_T_BODY, 40, 214, MPH_C_DIM, main->version);
  }
}

// The confirm button's glyph, following Swap X & O
static int confirm_glyph(void) {
  int length;
  return text_marker("{confirm}", &length);
}

// The current connection: kicker, the PC's name and address, and the resume
// row as the primary button. Returns the button's rect.
static mph_rect draw_connection(const menu_view *view, int resume) {
  const main_view *main = view->screen->main;
  const char *kicker = NULL;
  for (int i = 0; i < view->row_count; i++) {
    const menu_entry *e = &view->rows[i];
    if (e->separator && e->name != NULL && e->name[0]) {
      kicker = e->name;
      break;
    }
  }
  bool fields = main->connection_label != NULL && main->connection_address != NULL;
  const char *caption = resume >= 0 ? view->rows[resume].name : "";
  int glyph = confirm_glyph();
  float glyph_w = glyph >= 0 ? mph_glyph_w(glyph, MPH_T_ROW) : 0;
  float text_x = 16 + (glyph >= 0 ? glyph_w + 10 : 2);
  float text_w = mph_text_w(MPH_T_BTN, caption);
  float w = text_x + text_w + 20;
  if (w > HOME_BUTTON_MAX) {
    text_w -= w - HOME_BUTTON_MAX;
    w = HOME_BUTTON_MAX;
  }
  float by = fields ? 191 : 104;
  mph_rect button = mph_rect_make(40, by, roundf(w), 44);

  // Shapes
  mph_live_dot(44, 75);
  if (resume >= 0) {
    mph_pill(button, MPH_C_HL_BG);
  }
  // Symbols and text
  if (resume >= 0 && glyph >= 0) {
    mph_glyph(glyph, MPH_T_ROW, button.x + 16, roundf(button.y + 22 + 0.365f * mph_type_size(MPH_T_ROW)), MPH_C_HL_FG);
  }
  if (kicker != NULL) {
    mph_text_tracked(MPH_T_STRONG_S, 56, 80, 2.08f, MPH_C_LIVE, mph_upper(kicker));
  }
  if (fields) {
    mph_text_fit(MPH_T_HERO, 40, 136, HOME_BUTTON_MAX, MPH_C_FG, main->connection_label);
    mph_text_fit(MPH_T_BODY, 40, 168, HOME_BUTTON_MAX, MPH_C_DIM, main->connection_address);
  }
  if (resume >= 0) {
    mph_text_fit(MPH_T_BTN, button.x + text_x, mph_baseline(MPH_T_BTN, button.y, button.h), text_w, MPH_C_HL_FG,
                 caption);
  }
  return button;
}

void mph_screen_main_draw(const menu_view *view, const mph_enter *enter) {
  begin(enter);
  int n = main_layout(view);
  mph_rect vr = main_view_rect(view);
  float offset = scroll_to(main_target(view, n));
  int focused = view->cursor_row;
  bool connected = main_connected(view);
  int resume = connected ? main_resume_row(view) : -1;

  mph_bg_draw();
  mph_statusbar(view->screen->title);
  mph_enter_apply(enter);

  // Left: the brand, or the current connection
  mph_rect button = {0};
  if (connected) {
    button = draw_connection(view, resume);
  } else {
    draw_brand(view->screen->main);
  }

  // The focus: a ring for cards and the resume button, a pill for rows
  if (focused >= 0 && focused == resume) {
    mph_focus_set(&st.ring, button, MPH_R_RING, MPH_FOCUS_RING, focused);
    mph_focus_hide(&st.pill);
  } else if (focused >= 0 && focused < n && items[focused].kind == IT_CARD) {
    mph_focus_set(&st.ring, mph_rect_make(vr.x, vr.y + items[focused].y, vr.w, items[focused].h), MPH_R_RING,
                  MPH_FOCUS_RING, focused);
    mph_focus_hide(&st.pill);
  } else if (focused >= 0 && focused < n && items[focused].kind == IT_ROW) {
    mph_focus_set(&st.pill, mph_rect_make(vr.x, vr.y + items[focused].y, vr.w, items[focused].h), MPH_R_ROW,
                  MPH_FOCUS_PILL, focused);
    mph_focus_hide(&st.ring);
  } else {
    mph_focus_hide(&st.ring);
    mph_focus_hide(&st.pill);
  }

  // The resume button's ring isn't in the scrolled column
  if (focused == resume && resume >= 0) {
    mph_focus_draw(&st.ring, 0, 0);
  }

  float content = content_height(n);
  bool clip = content > vr.h;
  if (clip) {
    // Wide enough for the focus ring around the cards
    mph_clip(mph_rect_make(vr.x - 12, vr.y - 10, vr.w + 22, vr.h + 16));
  }
  if (focused != resume) {
    mph_focus_draw(&st.ring, 0, -offset);
  }
  mph_focus_draw(&st.pill, 0, -offset);

  // Shapes, then icons, then text: a few draw calls for the whole column
  for (int pass = 0; pass < 3; pass++) {
    for (int i = 0; i < n; i++) {
      const item *it = &items[i];
      if (it->kind == IT_SKIP) {
        continue;
      }
      float y = vr.y + it->y - offset;
      if (y + it->h < vr.y - 10 || y > vr.y + vr.h + 6) {
        continue;
      }
      const menu_entry *e = &view->rows[i];
      bool on = i == focused;
      mph_rect r = mph_rect_make(vr.x, y, vr.w, it->h);
      switch (it->kind) {
        case IT_CATEGORY:
          if (pass == 2) {
            // 26 px here: the label's baseline 14 px down, clear of the
            // focus ring around the card below it
            mph_category(r.x, r.y - 10, r.w, mph_upper(e->name), MPH_C_FAINT);
          }
          break;
        case IT_SEPARATOR:
          if (pass == 0) {
            mph_separator(r.x, r.y, r.w);
          }
          break;
        case IT_CARD: {
          // Continue reads as the app, with its PC beneath
          const main_view *main = view->screen->main;
          bool resume_card = e->id == MAIN_MENU_CONTINUE && main != NULL && main->continue_title != NULL;
          const char *title = resume_card ? main->continue_title : e->name;
          const char *detail = resume_card ? main->continue_host : e->subname;
          bool sub = detail != NULL && detail[0] != '\0';
          if (pass == 0) {
            mph_surface(r, MPH_R_CARD);
            mph_status_mark(e->status, r.x + r.w - 22, r.y + 28, false);
          } else if (pass == 1) {
            mph_icon(it->icon, r.x + 14, r.y + 13, MPH_C_DIM);
          } else {
            float right = e->status != MENU_STATUS_NONE ? 36 : 16;
            mph_text_fit(MPH_T_BTN, r.x + 60, r.y + (sub ? 25 : 33), r.w - 60 - right, MPH_C_FG, title);
            if (sub) {
              mph_text_fit(MPH_T_META, r.x + 60, r.y + 43, r.w - 60 - right,
                           e->status == MENU_STATUS_CHANGED ? MPH_C_LIVE : MPH_C_DIM, detail);
            }
          }
          break;
        }
        case IT_ROW: {
          uint32_t c = on ? MPH_C_HL_FG : e->danger ? MPH_C_BAD : MPH_C_FG;
          if (pass == 1) {
            mph_icon(it->icon, r.x + 16, r.y + (r.h - mph_icon_h(it->icon)) / 2, on ? MPH_C_HL_FG : MPH_C_DIM);
          } else if (pass == 2) {
            mph_text_fit(MPH_T_ROW, r.x + 44, mph_baseline(MPH_T_ROW, r.y, r.h), r.w - 44 - 16, c, e->name);
          }
          break;
        }
        default:
          break;
      }
    }
  }
  if (clip) {
    mph_clip_end();
  }
  mph_scrollbar(mph_rect_make(vr.x + vr.w + 10, vr.y, 3, vr.h), content, vr.h, offset);
  mph_transform_reset();
  mph_menu_legend(view);
}

/*
 * S2: a paired PC's menu as PC options
 */

#define SHEET_X 560
#define SHEET_PAD 24
#define SHEET_TOP 104

static mph_row sheet_rows[MPH_LIST_MAX];

// The actions: the rows after the separator. Returns their count and where
// they start in the view's rows.
static int host_actions(const menu_view *view, int *first) {
  int n = row_count(view);
  int start = 0;
  for (int i = 0; i < n; i++) {
    if (view->rows[i].separator) {
      start = i + 1;
      break;
    }
  }
  int count = 0;
  for (int i = start; i < n; i++) {
    sheet_rows[count++] = mph_row_from_entry(&view->rows[i]);
  }
  *first = start;
  return count;
}

static mph_rect sheet_rect(void) {
  return mph_rect_make(SHEET_X + SHEET_PAD, SHEET_TOP, MPH_W - SHEET_X - 2 * SHEET_PAD, MPH_CONTENT_BOTTOM - SHEET_TOP);
}

// The address line: the heading row that carries the status as its value
static const char *host_address_line(const menu_view *view, int first) {
  for (int i = 0; i < first && i < view->row_count; i++) {
    const menu_entry *e = &view->rows[i];
    if (e->disabled && !e->separator && e->subname[0] && e->name != NULL && e->name[0]) {
      return e->name;
    }
  }
  return NULL;
}

void mph_screen_host_draw(const menu_view *view, const mph_enter *enter) {
  begin(enter);
  const host_view *host = view->screen->host;
  int first;
  int n = host_actions(view, &first);
  int focused = view->cursor_row - first;
  mph_rect sr = sheet_rect();
  float target = mph_list_target(sr, sheet_rows, n, focused, view->scroll);

  mph_bg_draw();
  mph_enter_apply(enter);
  mph_sheet_frame(SHEET_X);

  // The spotlight: shapes, then the icon, then text
  menu_status status = host != NULL ? host->status : MENU_STATUS_NONE;
  bool mark = status == MENU_STATUS_ONLINE || status == MENU_STATUS_OFFLINE || status == MENU_STATUS_CHANGED;
  mph_status_mark(status, 44, 209, false);
  mph_icon(MPH_ICON_PC_64, 40, 70, MPH_C_DIM);
  if (host != NULL) {
    mph_text_fit(MPH_T_TITLE, 40, 182, 480, MPH_C_FG, host->label);
    uint32_t c = status == MENU_STATUS_ONLINE ? MPH_C_OK : status == MENU_STATUS_CHANGED ? MPH_C_LIVE : MPH_C_DIM;
    mph_text_fit(MPH_T_BODY, mark ? 56 : 40, 214, mark ? 464 : 480, c, host->status_text);
  }
  const char *address = host_address_line(view, first);
  if (address != NULL) {
    mph_text_fit(MPH_T_SMALL, 40, 242, 480, MPH_C_DIM, address);
  }

  // The sheet: its title (the menu's heading row) and the actions
  if (view->row_count > 0 && view->rows[0].disabled && view->rows[0].name != NULL) {
    mph_text_fit(MPH_T_DIALOG, sr.x, 80, sr.w, MPH_C_FG, view->rows[0].name);
  }
  mph_list(sr, sheet_rows, n, focused, target, &st.sheet, -1);
  mph_transform_reset();

  mph_statusbar(view->screen->title);
  mph_menu_legend(view);
}

/*
 * S3: the device search as Find a PC
 */

#define FIND_LEFT_W 330
#define FIND_X 410
#define FIND_W 510
#define FIND_TOP 70
#define FIND_CARD_H 62
#define FIND_GAP 6
#define RADAR_CX 115
#define RADAR_CY 153

static int search_layout(const menu_view *view) {
  int n = row_count(view);
  float y = 0;
  for (int i = 0; i < n; i++) {
    const menu_entry *e = &view->rows[i];
    if (e->disabled || e->separator) {
      // The refresh hint and the heading are on the left; the separator goes
      items[i] = (item){IT_SKIP, y, 0, -1};
      continue;
    }
    items[i] = (item){IT_CARD, y, FIND_CARD_H, -1};
    y += FIND_CARD_H + FIND_GAP;
  }
  return n;
}

static mph_rect search_view_rect(void) {
  return mph_rect_make(FIND_X, FIND_TOP, FIND_W, MPH_CONTENT_BOTTOM - FIND_TOP);
}

static void draw_radar(bool searching) {
  mph_circle_stroke(RADAR_CX, RADAR_CY, 74, 1, RGBA8(0xA9, 0xC8, 0xFF, 0x0F));
  mph_circle_stroke(RADAR_CX, RADAR_CY, 53, 1, RGBA8(0xA9, 0xC8, 0xFF, 0x14));
  if (searching) {
    float a = fmodf(mph_time(), 2.4f) / 2.4f * 2.0f * (float)M_PI;
    mph_sector_gradient(RADAR_CX, RADAR_CY, 74, a - (float)M_PI_2, a, RGBA8(0xA9, 0xC8, 0xFF, 0x00),
                        RGBA8(0xA9, 0xC8, 0xFF, 0x59));
  }
  mph_circle_fill(RADAR_CX, RADAR_CY, 32, RGBA8(0xA9, 0xC8, 0xFF, 0x24));
}

void mph_screen_search_draw(const menu_view *view, const mph_enter *enter) {
  begin(enter);
  int n = search_layout(view);
  mph_rect vr = search_view_rect();
  float offset = scroll_to(scroll_target(n, view->cursor_row, vr.h, MPH_ROW_H, view->scroll));
  int focused = view->cursor_row;
  bool searching = view->screen->search != NULL && view->screen->search->searching;

  // The refresh hint (a row that's only a value) and the heading
  const char *note = NULL, *title = view->screen->title;
  for (int i = 0; i < n; i++) {
    const menu_entry *e = &view->rows[i];
    if (e->separator && e->name != NULL && e->name[0]) {
      title = e->name;
    } else if (e->disabled && !e->separator && note == NULL) {
      note = e->name != NULL && e->name[0] ? e->name : e->subname;
    }
  }

  mph_bg_draw();
  mph_statusbar(view->screen->title);
  mph_enter_apply(enter);

  // Left: radar, title, the hint
  draw_radar(searching);
  float base = 272;
  if (title != NULL) {
    mph_lines lines;
    int count = mph_wrap(MPH_T_TITLE_S, title, FIND_LEFT_W, &lines);
    count = count > 3 ? 3 : count;
    mph_draw_lines(MPH_T_TITLE_S, &lines, 40, base, 31, 0, count, MPH_C_FG, MPH_ALIGN_LEFT);
    base += (count > 0 ? count - 1 : 0) * 31 + 36;
  }
  if (note != NULL && note[0]) {
    mph_lines lines;
    int count = mph_wrap(MPH_T_SMALL, note, FIND_LEFT_W, &lines);
    int fit = (int)((MPH_CONTENT_BOTTOM - base) / 22) + 1;
    mph_draw_lines(MPH_T_SMALL, &lines, 40, base, 22, 0, count < fit ? count : fit, MPH_C_DIM, MPH_ALIGN_LEFT);
  }

  // Right: the devices, then Return
  if (focused >= 0 && focused < n && items[focused].kind == IT_CARD) {
    mph_focus_set(&st.pill, mph_rect_make(vr.x, vr.y + items[focused].y, vr.w, items[focused].h), MPH_R_ROW,
                  MPH_FOCUS_PILL, focused);
  } else {
    mph_focus_hide(&st.pill);
  }
  float content = content_height(n);
  bool clip = content > vr.h;
  if (clip) {
    mph_clip(mph_rect_make(vr.x - 8, vr.y, vr.w + 16, vr.h + 8));
  }
  for (int pass = 0; pass < 3; pass++) {
    if (pass == 1) {
      // Over the cards, under the text
      mph_focus_draw(&st.pill, 0, -offset);
    }
    for (int i = 0; i < n; i++) {
      const item *it = &items[i];
      if (it->kind != IT_CARD) {
        continue;
      }
      float y = vr.y + it->y - offset;
      if (y + it->h < vr.y || y > vr.y + vr.h) {
        continue;
      }
      const menu_entry *e = &view->rows[i];
      bool on = i == focused;
      bool device = e->suffix != NULL && e->suffix[0];
      mph_rect r = mph_rect_make(vr.x, y, vr.w, it->h);
      if (pass == 0) {
        mph_surface(r, MPH_R_ROW);
      } else if (pass == 1) {
        if (!device) {
          mph_icon(MPH_ICON_CHEVRON_14, r.x + r.w - 16 - 14, r.y + (r.h - 14) / 2, on ? MPH_C_HL_FG : MPH_C_FAINT);
        }
      } else if (device) {
        mph_text_fit(MPH_T_ROW, r.x + 16, r.y + 27, r.w - 32, on ? MPH_C_HL_FG : MPH_C_FG, e->name);
        mph_text_fit(MPH_T_META, r.x + 16, r.y + 46, r.w - 32, on ? MPH_C_HL_FG : MPH_C_DIM, e->suffix);
      } else {
        mph_text_fit(MPH_T_ROW, r.x + 16, r.y + 37, r.w - 32 - 24, on ? MPH_C_HL_FG : MPH_C_FG, e->name);
      }
    }
  }
  if (clip) {
    mph_clip_end();
  }
  mph_scrollbar(mph_rect_make(vr.x + vr.w + 10, vr.y, 3, vr.h), content, vr.h, offset);
  mph_transform_reset();
  mph_menu_legend(view);
}

/*
 * menu_follow for the three screens
 */

void mph_screen_main_follow(menu_view *view) {
  int n = main_layout(view);
  view->scroll = (int)roundf(main_target(view, n));
}

void mph_screen_host_follow(menu_view *view) {
  int first;
  int n = host_actions(view, &first);
  view->scroll = (int)roundf(mph_list_target(sheet_rect(), sheet_rows, n, view->cursor_row - first, view->scroll));
}

void mph_screen_search_follow(menu_view *view) {
  int n = search_layout(view);
  view->scroll = (int)roundf(scroll_target(n, view->cursor_row, search_view_rect().h, MPH_ROW_H, view->scroll));
}
