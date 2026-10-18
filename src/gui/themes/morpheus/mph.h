#pragma once

// Morpheus primitives: everything the Morpheus screens draw with. Private to
// this folder. See README.md here for the conventions and the speed rules,
// and the design guide (DESIGN.md) for the tokens and the screen specs.
//
// Coordinates are device pixels on the 960x544 screen. Colours are vita2d's
// RGBA8 (0xAABBGGRR). Text is drawn at a baseline.

#include <stdbool.h>
#include <stdint.h>
#include <vita2d.h>

#include "../../ui_view.h"
#include "mph_icons.h"

/*
 * Tokens (DESIGN.md §2)
 */

#define MPH_C_FG            RGBA8(0xF2, 0xF4, 0xF8, 0xFF)
#define MPH_C_DIM           RGBA8(0xF2, 0xF4, 0xF8, 0xA3)
// .55, not the demo's .38: about #8A8F99 on the background, 5.6:1 (accessibility)
#define MPH_C_FAINT         RGBA8(0xF2, 0xF4, 0xF8, 0x8C)
#define MPH_C_ACCENT        RGBA8(0xA9, 0xC8, 0xFF, 0xFF)
#define MPH_C_LIVE          RGBA8(0xFF, 0xB8, 0x6B, 0xFF)
#define MPH_C_OK            RGBA8(0x7F, 0xE0, 0xA8, 0xFF)
#define MPH_C_BAD           RGBA8(0xFF, 0x8A, 0x80, 0xFF)
#define MPH_C_SURFACE       RGBA8(0x14, 0x17, 0x1F, 0xBD)
#define MPH_C_SURFACE2      RGBA8(0x28, 0x2E, 0x3C, 0xD1)
#define MPH_C_SOLID         RGBA8(0x12, 0x15, 0x1C, 0xFF)
#define MPH_C_SOLID2        RGBA8(0x1B, 0x20, 0x2B, 0xFF)
#define MPH_C_HL_BG         RGBA8(0xF2, 0xF4, 0xF8, 0xFF)
#define MPH_C_HL_FG         RGBA8(0x10, 0x13, 0x1A, 0xFF)
#define MPH_C_SCRIM         RGBA8(0x04, 0x06, 0x0A, 0x8C)
#define MPH_C_SHADOW        RGBA8(0x00, 0x00, 0x00, 0x66)
#define MPH_C_SHADOW_DIALOG RGBA8(0x00, 0x00, 0x00, 0x99)
#define MPH_C_SHADOW_SHEET  RGBA8(0x00, 0x00, 0x00, 0x73)
#define MPH_C_W05           RGBA8(0xFF, 0xFF, 0xFF, 0x0D)
#define MPH_C_W07           RGBA8(0xFF, 0xFF, 0xFF, 0x12)
#define MPH_C_W08           RGBA8(0xFF, 0xFF, 0xFF, 0x14)
#define MPH_C_W10           RGBA8(0xFF, 0xFF, 0xFF, 0x1A)
#define MPH_C_W12           RGBA8(0xFF, 0xFF, 0xFF, 0x1F)
#define MPH_C_W14           RGBA8(0xFF, 0xFF, 0xFF, 0x24)
#define MPH_C_W18           RGBA8(0xFF, 0xFF, 0xFF, 0x2E)
#define MPH_C_SW_ON_TRACK   RGBA8(0x7F, 0xE0, 0xA8, 0x59)
#define MPH_C_SW_TRACK_ON_PILL RGBA8(0x00, 0x00, 0x00, 0x2E)
#define MPH_C_OVERLAY_BG    RGBA8(0x00, 0x00, 0x00, 0x8C)
#define MPH_C_OVERLAY_TEXT  RGBA8(0xE9, 0xEC, 0xF2, 0xFF)
#define MPH_C_ZONE          RGBA8(0xA9, 0xC8, 0xFF, 0x38)
#define MPH_C_ZONE_CUR      RGBA8(0xA9, 0xC8, 0xFF, 0x73)
#define MPH_C_DEAD_STRIPE   RGBA8(0xFF, 0x8A, 0x80, 0x40)
#define MPH_C_ACTIVE_AREA   RGBA8(0x7F, 0xE0, 0xA8, 0x1A)
#define MPH_C_TILE_DIM      RGBA8(0x00, 0x00, 0x00, 0xA0)
// Face button symbols
#define MPH_C_TRIANGLE      RGBA8(0x40, 0xE0, 0xB0, 0xFF)
#define MPH_C_CIRCLE        RGBA8(0xFF, 0x70, 0x70, 0xFF)
#define MPH_C_CROSS         RGBA8(0x80, 0xB8, 0xF0, 0xFF)
#define MPH_C_SQUARE        RGBA8(0xF0, 0x90, 0xD0, 0xFF)

// The colour with its alpha multiplied by a (0 to 1)
static inline uint32_t mph_alpha(uint32_t c, float a) {
  float v = (float)(c >> 24) * a + 0.5f;
  uint32_t na = v <= 0.0f ? 0 : v >= 255.0f ? 255 : (uint32_t)v;
  return (c & 0x00FFFFFFu) | (na << 24);
}

// Between two colours, t from 0 to 1
static inline uint32_t mph_mix(uint32_t a, uint32_t b, float t) {
  if (a == b || t <= 0.0f) {
    return a;
  }
  if (t >= 1.0f) {
    return b;
  }
  uint32_t out = 0;
  for (int shift = 0; shift < 32; shift += 8) {
    float ca = (float)((a >> shift) & 0xFF), cb = (float)((b >> shift) & 0xFF);
    out |= ((uint32_t)(ca + (cb - ca) * t + 0.5f) & 0xFF) << shift;
  }
  return out;
}

// Layout
#define MPH_W 960
#define MPH_H 544
#define MPH_GUTTER 40
#define MPH_STATUSBAR_H 40
#define MPH_CONTENT_TOP 52
#define MPH_CONTENT_BOTTOM 500
#define MPH_LEGEND_BASELINE 526
#define MPH_ROW_H 46
#define MPH_ROW_GAP 2

// Radii
#define MPH_R_CARD 16
#define MPH_R_ROW 12
#define MPH_R_DIALOG 20
#define MPH_R_RING 20
#define MPH_R_TAB 11
#define MPH_R_CHIP 7
#define MPH_R_KEY 6
#define MPH_R_OVERLAY 10

// Motion, in seconds (DESIGN.md §2.4)
#define MPH_M_FOCUS 0.300f
#define MPH_M_FOCUS_FADE 0.150f
#define MPH_M_ENTER 0.260f
#define MPH_M_MODAL 0.240f
#define MPH_M_SCROLL 0.300f
#define MPH_M_TILE 0.250f
#define MPH_M_SWITCH 0.200f

/*
 * Frame, batching and the performance HUD (mph_draw.c, mph_perf.c)
 */

typedef struct {
  float x, y, w, h;
} mph_rect;

static inline mph_rect mph_rect_make(float x, float y, float w, float h) {
  return (mph_rect){x, y, w, h};
}
static inline mph_rect mph_rect_grow(mph_rect r, float d) {
  return (mph_rect){r.x - d, r.y - d, r.w + 2 * d, r.h + 2 * d};
}

// Loads the atlas, the background and the curves, and opens the fonts (whose
// sizes load lazily). Between frames, after vita2d_init().
void mph_init(void);
// Frees all of it. Between frames, with rendering finished.
void mph_fini(void);
// Start of a theme entry point's drawing, inside a vita2d frame: samples the
// time and resets the batch and the transform
void mph_frame_begin(void);
// End of it: flushes, and draws the developer HUD on screens that ask for it
void mph_frame_end(bool hud);
// Draws whatever geometry or text is pending. Call it before any raw
// vita2d_* draw (the primitives call it themselves when they need to).
void mph_flush(void);
// Seconds since mph_init(), and the last frame's step (clamped to 0..0.05)
float mph_time(void);
float mph_dt(void);

// A transform and opacity for everything drawn after it, until reset: screen
// enter slides and dialog scale-ins. Scale is about (cx, cy), then (dx, dy).
void mph_transform(float alpha, float scale, float cx, float cy, float dx, float dy);
void mph_transform_reset(void);

// Clips what's drawn to a rectangle (one stencil pass), until mph_clip_end()
void mph_clip(mph_rect r);
void mph_clip_end(void);

// Developer HUD: CPU time to build the frame, frame interval, draw calls,
// vertices, glyphs and pool use (averaged over a second)
void mph_perf_hud(void);
// Counters the primitives feed (mph_perf.c)
typedef struct {
  int draws, vertices, glyphs;
} mph_perf_counters;
extern mph_perf_counters mph_perf;

/*
 * Shapes: geometry into the batch (mph_draw.c). Antialiased by a 1 px alpha
 * fringe centred on each edge, so edges on whole pixels stay crisp.
 */

void mph_rect_fill(mph_rect r, uint32_t c);  // no fringe
void mph_rect_vgradient(mph_rect r, uint32_t top, uint32_t bottom);
void mph_rrect_fill(mph_rect r, float radius, uint32_t c);
// Each corner its own radius: top left, top right, bottom right, bottom left
void mph_rrect_fill4(mph_rect r, float tl, float tr, float br, float bl, uint32_t c);
void mph_rrect_stroke(mph_rect r, float radius, float width, uint32_t c);  // centred on r's edge
void mph_rrect_vgradient(mph_rect r, float radius, uint32_t top, uint32_t bottom);
void mph_pill(mph_rect r, uint32_t c);  // radius h/2
// A soft shadow: r offset by (dx, dy), blurred over `blur` px (CSS box-shadow)
void mph_shadow(mph_rect r, float radius, float dx, float dy, float blur, uint32_t c);
void mph_circle_fill(float cx, float cy, float radius, uint32_t c);
void mph_circle_stroke(float cx, float cy, float radius, float width, uint32_t c);
// Angles in radians, clockwise from 3 o'clock (screen y points down)
void mph_arc_stroke(float cx, float cy, float radius, float width, float a0, float a1, uint32_t c);
void mph_sector_gradient(float cx, float cy, float radius, float a0, float a1, uint32_t c0, uint32_t c1);
void mph_line(float x0, float y0, float x1, float y1, float width, uint32_t c);
void mph_dashed_rect(mph_rect r, float width, float dash, float gap, uint32_t c);
void mph_diamond(float cx, float cy, float half, uint32_t c);

/*
 * Textures (mph_draw.c)
 */

void mph_bg_draw(void);  // the full-screen background gradient
void mph_scrim(void);    // the full-screen SCRIM over it
// Icons from the atlas at their native size, tinted. (x, y) is the top left.
void mph_icon(mph_icon_id id, float x, float y, uint32_t tint);
int mph_icon_w(mph_icon_id id);
int mph_icon_h(mph_icon_id id);
// A texture (box art) scaled into dst with rounded corners
void mph_texture_rrect(const vita2d_texture *t, mph_rect dst, float radius, uint32_t tint);
// 45° stripes over r
void mph_hatch(mph_rect r, uint32_t tint);

/*
 * Text (mph_text.c)
 */

typedef enum {
  MPH_T_HERO,      // ExtraBold 44
  MPH_T_TITLE,     // ExtraBold 34
  MPH_T_TITLE_S,   // ExtraBold 28
  MPH_T_DIALOG,    // ExtraBold 24
  MPH_T_PIN,       // ExtraBold 60
  MPH_T_ROW_L,     // Bold 18
  MPH_T_ROW,       // Medium 17
  MPH_T_BTN,       // Bold 16
  MPH_T_BODY,      // Medium 15
  MPH_T_BODY_B,    // Bold 15
  MPH_T_TILE,      // ExtraBold 15
  MPH_T_SMALL,     // Medium 14
  MPH_T_META,      // Medium 13
  MPH_T_STRONG_S,  // Bold 13
  MPH_T_LABEL,     // ExtraBold 12, tracked: pass mph_upper() text
  MPH_T_CHIP,      // Bold 12
  MPH_T_TAG,       // ExtraBold 11 (keycaps)
  MPH_T_MONO,      // IBM Plex Mono Medium 13
  MPH_T_COUNT
} mph_type;

// The style's size in px, and its line height
int mph_type_size(mph_type t);
int mph_type_line(mph_type t);
// The baseline that centres one line of t in a box (DESIGN.md §2.3)
float mph_baseline(mph_type t, float top, float h);

// Text may hold button markers ({confirm}, {triangle}, {l}...), drawn as
// symbols or keycaps, and characters the fonts lack (CJK), drawn with the
// system font for the UI language. Each returns the advance in px.
int mph_text(mph_type t, float x, float baseline, uint32_t c, const char *s);
int mph_text_w(mph_type t, const char *s);
// Ends in "…" when wider than max_w
int mph_text_fit(mph_type t, float x, float baseline, float max_w, uint32_t c, const char *s);
int mph_text_right(mph_type t, float right_x, float baseline, uint32_t c, const char *s);
int mph_text_center(mph_type t, float cx, float baseline, uint32_t c, const char *s);
// The same with extra letter spacing (kickers); no markers
int mph_text_tracked(mph_type t, float x, float baseline, float tracking, uint32_t c, const char *s);
int mph_text_tracked_w(mph_type t, float tracking, const char *s);

// Wrapped lines, cached: wrap each frame, it costs a lookup. The lines stay
// valid until 31 other wraps have been made.
#define MPH_WRAP_MAX 48
typedef struct {
  int count;
  const char *line[MPH_WRAP_MAX];
} mph_lines;
int mph_wrap(mph_type t, const char *s, float max_w, mph_lines *out);
enum { MPH_ALIGN_LEFT, MPH_ALIGN_CENTER, MPH_ALIGN_RIGHT };
// Draws lines first..first+max-1; x is the left edge, centre or right edge
void mph_draw_lines(mph_type t, const mph_lines *l, float x, float first_baseline, float line_h, int first, int max,
                    uint32_t c, int align);
// Upper-cased (Latin, Greek, Cyrillic; Turkish i to İ), cached
const char *mph_upper(const char *s);
// Between frames: drop what was measured or cached for the old language
void mph_text_language_changed(void);

// Button symbols as drawn in text of style t: the width, and drawing one with
// its left edge at x on the baseline. Face buttons are a disc and a coloured
// stroke; labelled buttons are keycaps.
int mph_glyph_w(int glyph, mph_type t);
int mph_glyph(int glyph, mph_type t, float x, float baseline, uint32_t c);

/*
 * Animation (mph_anim.c)
 */

typedef enum { MPH_EASE_FOCUS, MPH_EASE_OUT, MPH_EASE_CSS, MPH_EASE_LINEAR, MPH_EASE_COUNT } mph_ease;
float mph_ease_eval(mph_ease e, float t);  // t clamped to 0..1

typedef struct {
  float from, to, t0, dur;
  mph_ease e;
} mph_tween;
void mph_tween_snap(mph_tween *tw, float v);
// Retargets from the current value; nothing happens if `to` is the target already
void mph_tween_to(mph_tween *tw, float to, float dur, mph_ease e);
float mph_tween_value(const mph_tween *tw);
bool mph_tween_done(const mph_tween *tw);
// Exponential approach, framerate independent: rate is the share per 60 Hz frame
float mph_follow(float cur, float target, float rate_per_60fps);
typedef struct {
  float x, v;
} mph_spring;
void mph_spring_step(mph_spring *s, float target, float omega);  // critically damped

// A screen's enter transition: a fade and an 18 px slide over MPH_M_ENTER.
// morpheus.c fills it for the menu screen on show and hands it to the screen.
typedef struct {
  float t0;      // when the screen came on: a new value means a new screen
  bool animate;  // whether it plays (not on a return to the screen before)
} mph_enter;
// Its progress, 0 to 1 (1 when it doesn't play or has finished)
float mph_enter_k(const mph_enter *e);
// Applies it to what's drawn next while it plays, until mph_transform_reset()
void mph_enter_apply(const mph_enter *e);
// Whether this is a new screen for state that last saw *seen (start it
// afresh); stores the screen in *seen. Start *seen at -1.
bool mph_enter_is_new(const mph_enter *e, float *seen);

/*
 * Chrome (mph_chrome.c)
 */

// Title at the left (ellipsis at x 600), battery and clock at the right
void mph_statusbar(const char *title);
typedef struct {
  int glyph;            // BUTTON_GLYPH_*, or BUTTON_GLYPH_NONE
  const char *caption;  // rich text
} mph_hint;
// Right-aligned, ending at x 936 on the legend baseline
void mph_legend(const mph_hint *hints, int n);
// The menu's button hint as the legend, when update has given one
void mph_menu_legend(const menu_view *view);
// A category label, 32 px tall: pass mph_upper() text
void mph_category(float x, float y, float w, const char *upper_text, uint32_t c);
void mph_separator(float x, float y, float w);  // 13 px tall
// Drawn only when content_h > view_h
void mph_scrollbar(mph_rect track, float content_h, float view_h, float offset);

/*
 * Rows, lists and focus (mph_widgets.c)
 */

typedef enum {
  MPH_ROW_PLAIN,
  MPH_ROW_VALUE,
  MPH_ROW_CHOICE,
  MPH_ROW_TOGGLE,
  MPH_ROW_ACTION,
  MPH_ROW_INFO,      // a heading or information: label and value DIM
  MPH_ROW_CHECK,
  MPH_ROW_NOTE,
  MPH_ROW_CATEGORY,
  MPH_ROW_SEPARATOR,
} mph_row_kind;

typedef struct {
  mph_row_kind kind;
  const char *label, *value, *suffix;
  bool on;  // TOGGLE
  bool checked, disabled, danger;
  menu_status status;
  int icon;  // a leading icon (mph_icon_id), or -1
  uint32_t value_colour;  // the value's colour when not focused, or 0 for the default
} mph_row;

// The row for a menu entry (DESIGN.md §3.6). The label of a category is
// upper-cased here (cached).
mph_row mph_row_from_entry(const menu_entry *e);
// rows[i] for entries[0..n), n capped at MPH_LIST_MAX; returns the count
int mph_rows_from_entries(mph_row *rows, const menu_entry *entries, int n);
bool mph_row_selectable(const mph_row *r);
// w is the row's width (a note wraps to a second line when it must)
float mph_row_height(const mph_row *r, float w);
// switch_t: the switch knob's position, 0 off to 1 on
void mph_row_draw(mph_rect r, const mph_row *row, bool focused, float switch_t);

typedef enum { MPH_FOCUS_PILL, MPH_FOCUS_RING } mph_focus_kind;
typedef struct {
  mph_rect from, to;
  float t0, appear;
  float radius;
  int key;
  mph_focus_kind kind;
  bool shown;
} mph_focus;
// Moves the highlight to target. A new key glides there (MPH_M_FOCUS); the
// same key with a new rect snaps; the first one fades in.
void mph_focus_set(mph_focus *f, mph_rect target, float radius, mph_focus_kind k, int key);
void mph_focus_hide(mph_focus *f);
mph_rect mph_focus_rect(const mph_focus *f);
// Pill: SH_CARD and an HL_BG rounded rect. Ring: a 3 px stroke 5 px outside.
// (dx, dy) shifts it (a list's scroll).
void mph_focus_draw(const mph_focus *f, float dx, float dy);

// A list's state across frames: its eased scroll and the switches' knobs
#define MPH_LIST_MAX 128
typedef struct {
  mph_tween scroll;
  mph_focus focus;
  mph_tween knob[MPH_LIST_MAX];
  bool knob_on[MPH_LIST_MAX];
  bool started;
} mph_list_state;
// Forget everything: the next mph_list() snaps to its targets
void mph_list_reset(mph_list_state *s);
// The scroll that keeps top..bottom (content coordinates) margin px inside a
// view view_h tall, moving as little as it can from current, clamped to the
// content. top < 0: nothing to keep in view, only the clamp.
float mph_scroll_keep(float top, float bottom, float content_h, float view_h, float margin, float current);
// The scroll offset that keeps the focused row in view, from the current one
float mph_list_target(mph_rect view, const mph_row *rows, int n, int focused, float current);
// Draws the rows in view (clipped), the focus pill under the focused one,
// and the scroll bar at track_x (< 0 for none). Eases to scroll_target.
void mph_list(mph_rect view, const mph_row *rows, int n, int focused, float scroll_target, mph_list_state *s,
              float track_x);

/*
 * Controls and containers (mph_widgets.c)
 */

enum { MPH_BTN_PRIMARY, MPH_BTN_SECONDARY, MPH_BTN_DANGER };
// A button 44 px tall (40 with MPH_BTN_SMALL ored in, for dialogs): glyph
// (or BUTTON_GLYPH_NONE) and caption. Returns its width.
#define MPH_BTN_SMALL 0x10
float mph_button_w(int glyph, const char *caption, int style);
float mph_button(float x, float y, int glyph, const char *caption, int style, bool focused);
float mph_chip(float x, float y, const char *text, uint32_t text_colour);  // h22
float mph_keycap_w(const char *label);
float mph_keycap(float x, float y, const char *label);  // h20, top left at (x, y)
void mph_card(mph_rect r);                              // SURFACE r16
// Box art or a name tile; focus 0 none, 1 selected, 2 focused
void mph_tile(const vita2d_texture *t, const char *name, mph_rect r, float scale, int focus, bool dim);
// SH_DIALOG + SOLID r20
void mph_dialog_frame(mph_rect r);
void mph_sheet_frame(float x);  // SH_SHEET + SOLID from x to the right edge
void mph_pin_tiles(float cx, float y, const char *digits);  // 4 tiles 86x104
void mph_meter10(float x, float y, float w, int filled);
void mph_live_dot(float cx, float cy);
void mph_spinner(float cx, float cy);
// A switch 40x24 at (x, y): t is the knob, 0 off to 1 on
void mph_switch(float x, float y, float t, bool focused, bool disabled);
