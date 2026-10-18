#include "../i18n/i18n.h"
#include "guilib.h"
#include "text.h"
#include "theme.h"
#include "../devshot.h"

#include "../config.h"
#include "../platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <psp2/net/net.h>
#include <psp2/sysmodule.h>

#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/rtc.h>
#include <psp2/power.h>

#define BUTTON_DELAY 150 * 1000

static gui_loop_callback gui_global_loop_callback;

static int battery_percent;
static bool battery_charging;
static SceRtcTick battery_tick;

void ui_status_read(ui_status *status) {
  SceRtcTick current_tick;
  sceRtcGetCurrentTick(&current_tick);
  if (current_tick.tick - battery_tick.tick > 10 * 1000 * 1000) {
    battery_percent = scePowerGetBatteryLifePercent();
    battery_charging = scePowerIsBatteryCharging();

    battery_tick = current_tick;
  }

  SceDateTime time;
  sceRtcGetCurrentClockLocalTime(&time);
  status->hour = time.hour;
  status->minute = time.minute;
  status->battery_percent = battery_percent;
  status->battery_charging = battery_charging;
}

SceTouchData touch_data;
SceCtrlData ctrl_new_pad;

static SceRtcTick button_current_tick, button_until_tick;
bool was_button_pressed(short id) {
  sceRtcGetCurrentTick(&button_current_tick);

  if (ctrl_new_pad.buttons & id) {
    if (sceRtcCompareTick(&button_current_tick, &button_until_tick) > 0) {
      sceRtcTickAddMicroseconds(&button_until_tick, &button_current_tick, BUTTON_DELAY);
      return true;
    }
  }

  return false;
}

bool is_button_down(short id) {
  return ctrl_new_pad.buttons & id;
}

#define lerp(value, from_max, to_max) ((((value*10) * (to_max*10))/(from_max*10))/10)
bool is_rectangle_touched(const SceTouchData *touch, int lx, int ly, int rx, int ry) {
  for (int i = 0; i < touch->reportNum; i++) {
    int x = lerp(touch->report[i].x, 1919, WIDTH);
    int y = lerp(touch->report[i].y, 1087, HEIGHT);
    if (x < lx || x > rx || y < ly || y > ry) continue;
    return true;
  }

  return false;
}

void ui_start() {
  vita2d_start_drawing();
  vita2d_clear_screen();
}

void ui_end() {
  vita2d_end_drawing();
  vita2d_wait_rendering_done();
  vita2d_swap_buffers();
  devshot_after_swap();
}

int read_buttons() {
    SceCtrlData pad = {0};
    static int old;
    static int hold_times;
    int curr, btn;

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceCtrlPeekBufferPositive(0, &pad, 1);

    if (pad.ly < 0x10) {
        pad.buttons |= SCE_CTRL_UP;
    } else if (pad.ly > 0xef) {
        pad.buttons |= SCE_CTRL_DOWN;
    }
    curr = pad.buttons;
    btn = pad.buttons & ~old;
    if (curr && old == curr) {
        hold_times += 1;
        if (hold_times >= 10) {
            btn = curr;
            hold_times = 8;
            btn |= SCE_CTRL_HOLD;
        }
    } else {
        hold_times = 0;
        old = curr;
    }
    return btn;
}

static unsigned int intents_from_buttons(int buttons) {
  static const struct {
    unsigned int buttons;
    unsigned int intent;
  } MAP[] = {
    {SCE_CTRL_UP, UI_INTENT_UP},
    {SCE_CTRL_DOWN, UI_INTENT_DOWN},
    {SCE_CTRL_LEFT, UI_INTENT_LEFT},
    {SCE_CTRL_RIGHT, UI_INTENT_RIGHT},
    {SCE_CTRL_TRIANGLE, UI_INTENT_HELP},
    {SCE_CTRL_SQUARE, UI_INTENT_SECONDARY},
    // sceCtrlPeekBufferPositive reports the shoulder buttons on the L2/R2 bits
    // (the old LTRIGGER/RTRIGGER names), so accept either pair
    {SCE_CTRL_L1 | SCE_CTRL_L2, UI_INTENT_TAB_PREV},
    {SCE_CTRL_R1 | SCE_CTRL_R2, UI_INTENT_TAB_NEXT},
    {SCE_CTRL_START, UI_INTENT_START},
    {SCE_CTRL_SELECT, UI_INTENT_SELECT},
  };
  unsigned int intents = 0;
  for (size_t i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++) {
    if (buttons & MAP[i].buttons) {
      intents |= MAP[i].intent;
    }
  }
  // Confirm and cancel follow Swap X & O
  if (buttons & config.btn_confirm) {
    intents |= UI_INTENT_CONFIRM;
  }
  if (buttons & config.btn_cancel) {
    intents |= UI_INTENT_CANCEL;
  }
  if (buttons & SCE_CTRL_HOLD) {
    intents |= UI_INTENT_REPEAT;
  }
  return intents;
}

input_data read_input(void) {
  input_data input = {0};
  input.buttons = read_buttons();
  input.intents = intents_from_buttons(input.buttons);
  sceTouchPeek(SCE_TOUCH_PORT_FRONT, &input.touch, 1);
  return input;
}

bool ui_shoulders_held(void) {
  SceCtrlData pad = {0};
  sceCtrlPeekBufferPositive(0, &pad, 1);
  // The menus' sceCtrlPeekBufferPositive reports the shoulder buttons on the
  // L2/R2 bits (the old LTRIGGER/RTRIGGER names), so accept either pair
  return (pad.buttons & (SCE_CTRL_L1 | SCE_CTRL_L2)) && (pad.buttons & (SCE_CTRL_R1 | SCE_CTRL_R2));
}

static bool select_pending = false;
static int select_id = 0;

void display_menu_select(int id) {
  select_pending = true;
  select_id = id;
}

// The index of the cursor'th selectable row, or total_elements if there's none
static int row_of_cursor(const menu_entry menu[], int total_elements, int cursor) {
  int row = 0;
  for (int c = 0; row < total_elements; row++) {
    if (menu[row].disabled) {
      continue;
    }
    if (cursor == c) {
      break;
    }
    c++;
  }
  return row;
}

int display_menu(menu_entry menu[], int total_elements, const menu_screen *screen,
                 gui_loop_callback cb, gui_back_callback back_cb,
                 void *context) {
  ui_end();

  int cursor = 0;
  // The cursor counts selectable rows only
  if (select_pending) {
    select_pending = false;
    for (int i = 0, selectable = 0; i < total_elements; i++) {
      if (menu[i].disabled) {
        continue;
      }
      if (menu[i].id == select_id) {
        cursor = selectable;
        break;
      }
      selectable++;
    }
  }

  menu_view view = {
    .screen = screen,
    .rows = menu,
    .row_count = total_elements,
    .hint = {BUTTON_GLYPH_NONE, NULL},
  };

  int tick_number = 0;
  int exit_code = 0;

  while (true) {
    int active_elements = 0;
    for (int i = 0; i < total_elements; i++) {
        active_elements += menu[i].disabled ? 0 : 1;
    }

    ui_start();
    tick_number++;

    view.cursor = cursor;
    view.cursor_row = row_of_cursor(menu, total_elements, cursor);
    view.drawn_row = view.cursor_row;
    view.decorated = tick_number > 3;
    if (view.decorated && screen->update) {
      screen->update(&view);
    }
    theme_active()->draw_menu(&view);

    int real_cursor = view.cursor_row;

    // select item
    input_data input = read_input();
    if (ui_held(&input, UI_INTENT_DOWN)) {
      cursor += 1;
    }
    if (ui_held(&input, UI_INTENT_UP)) {
      cursor -= 1;
    }
    cursor = cursor < 0 ? 0 : cursor;
    cursor = cursor > active_elements - 1 ? active_elements - 1 : cursor;

    view.cursor = cursor;
    view.cursor_row = row_of_cursor(menu, total_elements, cursor);
    theme_active()->menu_follow(&view);

    if (cb) {
      exit_code = cb(menu[real_cursor].id, context, &input);
      if (exit_code) {
        goto error;
      }
    }

    if (gui_global_loop_callback) {
      gui_global_loop_callback(menu[real_cursor].id, context, &input);
    }

    // The intents were read before the callback ran, with the buttons the
    // press was made with: Swap X & O changed in Settings mustn't turn the X
    // that opened it into a cancel that closes this menu too
    if (ui_pressed(&input, UI_INTENT_CANCEL)) {
      if (!back_cb || back_cb(context) == 0) {
        exit_code = 1;
        goto error;
      }
    }

    ui_end();
  }

  return 0;

error:
  ui_end();
  return exit_code;
}

// An alert's buttons: index 0 is confirm and 1 cancel, which swap when X
// and O are swapped, then triangle and square
static alert_view make_alert(alert_kind kind, const char *message, char *captions[], int count) {
  const char *default_captions[ALERT_MAX_BUTTONS] = {tr(STR_BUTTON_OK), tr(STR_BUTTON_CANCEL), tr(STR_BUTTON_OPTIONS),
                                                     tr(STR_BUTTON_DELETE)};
  alert_view view = {
    .kind = kind,
    .message = message,
    .button_count = count < ALERT_MAX_BUTTONS ? count : ALERT_MAX_BUTTONS,
    .glyphs = {config.jp_layout ? BUTTON_GLYPH_CIRCLE : BUTTON_GLYPH_CROSS,
               config.jp_layout ? BUTTON_GLYPH_CROSS : BUTTON_GLYPH_CIRCLE,
               BUTTON_GLYPH_TRIANGLE, BUTTON_GLYPH_SQUARE},
  };
  for (int i = 0; i < ALERT_MAX_BUTTONS; i++) {
    view.captions[i] = captions && i < count && captions[i] ? captions[i] : default_captions[i];
  }
  return view;
}

static void run_alert(alert_kind kind, char *message, char *button_captions[], int buttons_count,
                      gui_loop_callback cb, void *context) {
  alert_view view = make_alert(kind, message, button_captions, buttons_count);

  while (true) {
    ui_start();

    theme_active()->draw_alert(&view);

    input_data input = read_input();

    int result = -1;

    if (input.intents & UI_INTENT_REPEAT) {
      ui_end();
      continue;
    }

    if (ui_pressed(&input, UI_INTENT_CONFIRM)) {
      result = 0;
    } else if (ui_pressed(&input, UI_INTENT_CANCEL)) {
      result = 1;
    } else if (ui_pressed(&input, UI_INTENT_HELP)) {
      result = 2;
    } else if (ui_pressed(&input, UI_INTENT_SECONDARY)) {
      result = 3;
    }

    if (cb && result != -1 && result < buttons_count) {
      switch(cb(result, context, &input)) {
        case 1:
          return;
      }
    } else if (result == 0) {
      return;
    }

    ui_end();
  }
}

void display_alert(char *message, char *button_captions[], int buttons_count,
                   gui_loop_callback cb, void *context) {
  run_alert(ALERT_MESSAGE, message, button_captions, buttons_count, cb, context);
}

void display_help(const char *title, const char *message) {
  // Laid out once up front; the text doesn't change while the dialog is open
  help_view view = {
    .title = title,
    .message = message,
    .offset = 0,
    .max_offset = theme_active()->help_prepare(title, message),
    .close_glyph = config.jp_layout ? BUTTON_GLYPH_CIRCLE : BUTTON_GLYPH_CROSS,
    .close_caption = tr(STR_BUTTON_CLOSE),
  };

  while (true) {
    ui_start();

    theme_active()->draw_help(&view);

    // Up and down (or the left stick) scroll, repeating while held
    input_data input = read_input();
    if (ui_held(&input, UI_INTENT_UP) && view.offset > 0) {
      view.offset--;
    } else if (ui_held(&input, UI_INTENT_DOWN) && view.offset < view.max_offset) {
      view.offset++;
    }

    // Ignore the triangle press that opened this until it is released
    if (ui_pressed(&input, UI_INTENT_CONFIRM | UI_INTENT_CANCEL | UI_INTENT_HELP)) {
      return;
    }

    ui_end();
  }
}

void display_error(char *format, ...) {
  char buf[0x1000];

  va_list opt;
  va_start(opt, format);
  vsnprintf(buf, sizeof(buf), format, opt);
  run_alert(ALERT_ERROR, buf, NULL, 1, NULL, NULL);
  va_end(opt);
}

void guilib_language_changed(void) {
  text_language_changed();
  theme_active()->language_changed();
}

static void show_progress(const char *message, const char *pin) {
  ui_end();

  alert_view view = make_alert(ALERT_PROGRESS, message, NULL, 0);
  view.pin = pin;
  ui_start();
  theme_active()->draw_alert(&view);
  ui_end();
}

void flash_message(char *format, ...) {
  char buf[0x1000];

  va_list opt;
  va_start(opt, format);
  vsnprintf(buf, sizeof(buf), format, opt);
  va_end(opt);

  show_progress(buf, NULL);
}

void flash_message_pin(const char *pin, const char *message) {
  show_progress(message, pin);
}

void guilib_init(gui_loop_callback global_loop_cb) {
  vita2d_init();
  // For the system font, which draws what the themes' fonts can't
  sceSysmoduleLoadModule(SCE_SYSMODULE_PGF);
  theme_init();

  gui_global_loop_callback = global_loop_cb;
}

int display_confirm(const char* message) {
    // The buttons' symbols are drawn beside them, following Swap X & O
    char* buttons[] = {(char *)tr(STR_BUTTON_YES), (char *)tr(STR_BUTTON_NO)};
    alert_view view = make_alert(ALERT_CONFIRM, message, buttons, 2);
    while (1) {
        ui_start();
        theme_active()->draw_alert(&view);
        input_data input = read_input();
        if (ui_pressed(&input, UI_INTENT_CONFIRM)) {
            ui_end();
            return 1; // X
        } else if (ui_pressed(&input, UI_INTENT_CANCEL)) {
            ui_end();
            return 0; // O
        }
        ui_end();
    }
}
