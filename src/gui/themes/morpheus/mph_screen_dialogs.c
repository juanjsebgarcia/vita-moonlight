// Morpheus dialogs (DESIGN.md S11): alerts, errors, confirmations, flash
// messages (with the pairing PIN as tiles) and help; and the IME backdrop
// (S13). Every dialog draws alone over the background and the scrim, as the
// app's dialogs clear the screen behind them.

#include "mph_screens.h"

#include "../../../config.h"

#include <math.h>
#include <string.h>

#define ALERT_W 500
#define ALERT_W_WIDE 640
#define ALERT_PAD 26
#define ALERT_LINE 24
#define ALERT_MIN_H 160
#define ALERT_MAX_H 504
#define BUTTON_H 40
#define BUTTON_GAP 10
#define BUTTON_INSET 22
#define FLASH_LINE 26
#define FLASH_MIN_W 360
#define PIN_TILES_W (4 * 86 + 3 * 12)
#define PIN_BLOCK_H (14 + 104 + 14)

static void frame_end(void) {
  mph_frame_end(config.developer_mode);
}

// The modal enter transition plays when a dialog opens: a new message, or
// the first frame after a pause in drawing it
static float modal_t0;
static const void *modal_key;
static float modal_last;

static void modal_begin(const void *key, mph_rect frame) {
  float now = mph_time();
  if (key != modal_key || now - modal_last > 0.25f) {
    modal_t0 = now;
  }
  modal_key = key;
  modal_last = now;
  float k = mph_ease_eval(MPH_EASE_OUT, (now - modal_t0) / MPH_M_MODAL);
  if (k < 1.0f) {
    mph_transform(k, 0.965f + 0.035f * k, frame.x + frame.w / 2, frame.y + frame.h / 2, 0, 0);
  }
}

// Right-aligned from `right`: index 0 (confirm) rightmost and primary. The
// buttons aren't focusable: each is pressed with its own button, as today.
static void draw_buttons(float right, float top, int count, const int *glyphs, const char *const *captions) {
  float x = right;
  for (int i = 0; i < count; i++) {
    int style = (i == 0 ? MPH_BTN_PRIMARY : MPH_BTN_SECONDARY) | MPH_BTN_SMALL;
    x -= mph_button_w(glyphs[i], captions[i], style);
    mph_button(x, top, glyphs[i], captions[i], style, false);
    x -= BUTTON_GAP;
  }
}

// The pairing PIN's line: the view's PIN, or else a line of exactly four
// digits (every translation of the pairing message ends in one)
static bool is_pin_line(const char *line, const char *pin) {
  if (pin != NULL) {
    return strcmp(line, pin) == 0;
  }
  for (int i = 0; i < 4; i++) {
    if (line[i] < '0' || line[i] > '9') {
      return false;
    }
  }
  return line[4] == '\0';
}

// Flash messages: one frame before blocking work, so nothing animates.
// Centred lines in a frame as wide as the widest, without buttons.
static void draw_flash(const alert_view *view) {
  mph_lines lines;
  int count = mph_wrap(MPH_T_ROW, view->message, ALERT_W_WIDE - 2 * ALERT_PAD, &lines);
  float widest = 0, height = 2 * ALERT_PAD;
  for (int i = 0; i < count; i++) {
    if (is_pin_line(lines.line[i], view->pin)) {
      height += PIN_BLOCK_H;
      widest = widest > PIN_TILES_W ? widest : PIN_TILES_W;
      continue;
    }
    float w = mph_text_w(MPH_T_ROW, lines.line[i]);
    widest = w > widest ? w : widest;
    height += FLASH_LINE;
  }
  float w = widest + 2 * ALERT_PAD;
  w = w < FLASH_MIN_W ? FLASH_MIN_W : w > ALERT_W_WIDE ? ALERT_W_WIDE : w;
  height = height > ALERT_MAX_H ? ALERT_MAX_H : height;
  mph_rect frame = mph_rect_make(roundf((MPH_W - w) / 2), roundf((MPH_H - height) / 2), roundf(w), roundf(height));
  mph_dialog_frame(frame);

  // Shapes first (the PIN tiles), then all the text in one run. Lines that
  // don't fit (a message taller than the screen) are left out.
  float bottom = frame.y + frame.h - ALERT_PAD + 1;
  float cx = frame.x + frame.w / 2;
  for (int pass = 0; pass < 2; pass++) {
    float y = frame.y + ALERT_PAD;
    for (int i = 0; i < count; i++) {
      bool pin = is_pin_line(lines.line[i], view->pin);
      float line_h = pin ? PIN_BLOCK_H : FLASH_LINE;
      if (y + line_h > bottom) {
        break;
      }
      if (pin && pass == 0) {
        mph_pin_tiles(cx, y + 14, lines.line[i]);
      } else if (!pin && pass == 1) {
        mph_text_center(MPH_T_ROW, cx, y + 19, MPH_C_FG, lines.line[i]);
      }
      y += line_h;
    }
  }
}

void mph_screen_alert_draw(const alert_view *view) {
  mph_frame_begin();
  mph_bg_draw();
  mph_scrim();
  if (view->kind == ALERT_PROGRESS) {
    draw_flash(view);
    frame_end();
    return;
  }

  // 500 wide, or 640 when the message runs past nine lines
  float w = ALERT_W;
  mph_lines lines;
  int count = mph_wrap(MPH_T_ROW, view->message, w - 2 * ALERT_PAD, &lines);
  if (count > 9) {
    w = ALERT_W_WIDE;
    count = mph_wrap(MPH_T_ROW, view->message, w - 2 * ALERT_PAD, &lines);
  }
  int buttons = view->button_count;
  float button_area = buttons > 0 ? 20 + BUTTON_H : 0;
  float h = ALERT_PAD + count * ALERT_LINE + button_area + BUTTON_INSET;
  h = h < ALERT_MIN_H ? ALERT_MIN_H : h > ALERT_MAX_H ? ALERT_MAX_H : h;
  mph_rect frame = mph_rect_make((MPH_W - w) / 2, roundf((MPH_H - h) / 2), w, roundf(h));
  modal_begin(view->message, frame);
  mph_dialog_frame(frame);
  if (buttons > 0) {
    draw_buttons(frame.x + frame.w - BUTTON_INSET, frame.y + frame.h - BUTTON_INSET - BUTTON_H, buttons, view->glyphs,
                 view->captions);
  }
  // The message is the dialog's primary text (the app's messages have no title)
  // A message shorter than the smallest frame is centred in the room above
  // the buttons, as Homebrew centres a single line
  float room = frame.h - ALERT_PAD - button_area - BUTTON_INSET;
  int fit = (int)(room / ALERT_LINE);
  float top = frame.y + ALERT_PAD;
  if (count < fit) {
    top += roundf((room - count * ALERT_LINE) / 2);
  }
  mph_draw_lines(MPH_T_ROW, &lines, frame.x + ALERT_PAD, top + 18, ALERT_LINE, 0, fit, MPH_C_FG, MPH_ALIGN_LEFT);
  frame_end();
}

/*
 * Help: a titled, scrolling text in a 600x360 frame
 */

#define HELP_X 180
#define HELP_Y 92
#define HELP_W 600
#define HELP_H 360
#define HELP_LINE 23
#define HELP_FIRST 86
#define HELP_WRAP (HELP_W - 2 * ALERT_PAD - 14)
// Lines between the first baseline and the button area
#define HELP_VISIBLE 9

int mph_screen_help_prepare(const char *title, const char *message) {
  (void)title;
  mph_lines lines;
  int count = mph_wrap(MPH_T_BODY, message, HELP_WRAP, &lines);
  return count > HELP_VISIBLE ? count - HELP_VISIBLE : 0;
}

void mph_screen_help_draw(const help_view *view) {
  mph_frame_begin();
  mph_bg_draw();
  mph_scrim();
  mph_rect frame = mph_rect_make(HELP_X, HELP_Y, HELP_W, HELP_H);
  modal_begin(view->message, frame);
  mph_dialog_frame(frame);
  mph_lines lines;
  int count = mph_wrap(MPH_T_BODY, view->message, HELP_WRAP, &lines);
  float body_h = HELP_VISIBLE * HELP_LINE;
  mph_scrollbar(mph_rect_make(frame.x + frame.w - 16, frame.y + HELP_FIRST - 16, 3, body_h), count * HELP_LINE,
                body_h, view->offset * HELP_LINE);
  draw_buttons(frame.x + frame.w - BUTTON_INSET, frame.y + frame.h - BUTTON_INSET - BUTTON_H, 1, &view->close_glyph,
               (const char *const *)&view->close_caption);
  mph_text_fit(MPH_T_DIALOG, frame.x + ALERT_PAD, frame.y + 50, frame.w - 2 * ALERT_PAD, MPH_C_FG, view->title);
  mph_draw_lines(MPH_T_BODY, &lines, frame.x + ALERT_PAD, frame.y + HELP_FIRST, HELP_LINE, view->offset, HELP_VISIBLE,
                 MPH_C_DIM, MPH_ALIGN_LEFT);
  frame_end();
}

/*
 * IME backdrop (S13): the system keyboard draws over it
 */

void mph_screen_ime_backdrop_draw(void) {
  mph_frame_begin();
  mph_bg_draw();
  mph_scrim();
  mph_frame_end(false);
}
