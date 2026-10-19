#include "ui_settings.h"

#include "guilib.h"
#include "ime.h"
#include "ui_keyboard.h"

#include "../config.h"
#include "../input/vita.h"
#include "../input/swap_shoulder_buttons.h"
#include "../video/vita.h"
#include "../debug.h"
#include "../stats.h"
#include "../connection.h"
#include "../bench.h"
#include "ui_connect.h"
#include "../input/touchabsolute.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <psp2/ctrl.h>
#include <psp2/rtc.h>
#include <psp2/touch.h>
#include <psp2/videodec.h>
#include <vita2d.h>
#include <Limelight.h>
#include "debug.h"
#include "../util.h"
#include "ui_recommended.h"
#include "../settings.h"
#include "../i18n/i18n.h"
extern char* strdup(const char*);

static unsigned int settings_special_codes[] = {0,
  // special
  INPUT_TYPE_DEF_NAME | INPUT_TYPE_SPECIAL,
  INPUT_SPECIAL_KEY_PAUSE | INPUT_TYPE_SPECIAL,
  INPUT_SPECIAL_KEY_KEYBOARD | INPUT_TYPE_SPECIAL,
  // gamepad
  INPUT_TYPE_DEF_NAME | INPUT_TYPE_GAMEPAD,
  SPECIAL_FLAG | INPUT_TYPE_GAMEPAD,
  LB_FLAG | INPUT_TYPE_GAMEPAD,
  RB_FLAG | INPUT_TYPE_GAMEPAD,
  LS_CLK_FLAG | INPUT_TYPE_GAMEPAD,
  RS_CLK_FLAG | INPUT_TYPE_GAMEPAD,
  LEFT_TRIGGER | INPUT_TYPE_ANALOG,
  RIGHT_TRIGGER | INPUT_TYPE_ANALOG,
  // mouse
  INPUT_TYPE_DEF_NAME | INPUT_TYPE_MOUSE,
  BUTTON_LEFT | INPUT_TYPE_MOUSE,
  BUTTON_RIGHT | INPUT_TYPE_MOUSE,
  BUTTON_MIDDLE | INPUT_TYPE_MOUSE,
  BUTTON_X1 | INPUT_TYPE_MOUSE,
  BUTTON_X2 | INPUT_TYPE_MOUSE,
  // keyboard
  INPUT_TYPE_DEF_NAME | INPUT_TYPE_KEYBOARD,
  27,    73,  77,  9,
  112,  113,  114,  115,  116,  117,  118,  119, 120,   121,   122,   123
};

// The name of each code above: a translated word, or a key name that's the
// same in every language (STR_NONE with the literal)
typedef struct {
  str_id id;
  const char *literal;
} special_name;

#define NAME(ID) {STR_##ID, NULL}
#define KEY(TEXT) {STR_NONE, TEXT}
static const special_name settings_special_names[] = {
  NAME(SPECIAL_NONE),
  // special
  NAME(SPECIAL_GROUP_SPECIAL),
  NAME(SPECIAL_PAUSE),
  NAME(SPECIAL_KEYBOARD),
  // gamepad
  NAME(SPECIAL_GROUP_GAMEPAD),
  NAME(SPECIAL_GUIDE),
  KEY("LB"), KEY("RB"), KEY("LS"), KEY("RS"), KEY("LT"), KEY("RT"),
  // mouse
  NAME(SPECIAL_GROUP_MOUSE),
  NAME(SPECIAL_MOUSE_LEFT),
  NAME(SPECIAL_MOUSE_RIGHT),
  NAME(SPECIAL_MOUSE_MIDDLE),
  NAME(SPECIAL_MOUSE_X1),
  NAME(SPECIAL_MOUSE_X2),
  // keyboard
  NAME(SPECIAL_GROUP_KEYBOARD),
  KEY("Esc"),
  KEY("I"), KEY("M"), KEY("Tab"),
  KEY("F1"), KEY("F2"), KEY("F3"), KEY("F4"), KEY("F5"), KEY("F6"), KEY("F7"), KEY("F8"), KEY("F9"),
  KEY("F10"), KEY("F11"), KEY("F12"),
};
#undef NAME
#undef KEY

/*
 * Deadzone
 */

#define lerp(value, from_max, to_max) ((((value*10) * (to_max*10))/(from_max*10))/10)
// The active area and the fingers inside it, read from the rear touchpad
static void deadzone_update(menu_view *view) {
  static deadzone_view zone;
  zone.top = config.back_deadzone.top;
  zone.right = config.back_deadzone.right;
  zone.bottom = config.back_deadzone.bottom;
  zone.left = config.back_deadzone.left;
  zone.touch_count = 0;

  SceTouchData touch_data;
  sceTouchPeek(SCE_TOUCH_PORT_BACK, &touch_data, 1);

  for (int i = 0; i < touch_data.reportNum && zone.touch_count < DEADZONE_MAX_TOUCHES; i++) {
    int x = lerp(touch_data.report[i].x, 1919, 960);
    int y = lerp(touch_data.report[i].y, 1087, 544);
    if (x < config.back_deadzone.left || x > WIDTH - config.back_deadzone.right)
      continue;

    if (y < config.back_deadzone.top || y > HEIGHT - config.back_deadzone.bottom)
      continue;

    zone.touches[zone.touch_count].x = x;
    zone.touches[zone.touch_count].y = y;
    zone.touch_count++;
  }
  view->deadzone = &zone;
}

static int deadzone_loop(int cursor, void *context, const input_data *input) {
  menu_entry *menu = context;

  bool left = ui_held(input, UI_INTENT_LEFT);
  bool right = ui_held(input, UI_INTENT_RIGHT);

  int delta = left ? -15 : (right ? 15 : 0);
  switch (cursor) {
    case 0: config.back_deadzone.top += delta; break;
    case 1: config.back_deadzone.right += delta; break;
    case 2: config.back_deadzone.bottom += delta; break;
    case 3: config.back_deadzone.left += delta; break;
  }

  char current[256];

  int numbers[] = {
    config.back_deadzone.top,
    config.back_deadzone.right,
    config.back_deadzone.bottom,
    config.back_deadzone.left
  };
  for (int i = 0; i < 4; i++) {
    char number[16];
    snprintf(number, sizeof(number), "%d", numbers[i]);
    TR_FMT(current, sizeof(current), STR_UNIT_PIXELS, number);
    strcpy(menu[i].subname, current);
  }

  return 0;
}

static int deadzone_settings_menu() {
  sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);

  menu_entry menu[16];
  int idx = 0;
  // In the order deadzone_loop adjusts them (it used to label 1 and 3 the wrong way round)
  menu[idx++] = (menu_entry) { .name = (char *)tr(STR_DEADZONE_TOP), .disabled = false, .id = 0, .adjustable = true, .kind = MENU_ROW_CHOICE };
  menu[idx++] = (menu_entry) { .name = (char *)tr(STR_DEADZONE_RIGHT), .disabled = false, .id = 1, .adjustable = true, .kind = MENU_ROW_CHOICE };
  menu[idx++] = (menu_entry) { .name = (char *)tr(STR_DEADZONE_BOTTOM), .disabled = false, .id = 2, .adjustable = true, .kind = MENU_ROW_CHOICE };
  menu[idx++] = (menu_entry) { .name = (char *)tr(STR_DEADZONE_LEFT), .disabled = false, .id = 3, .adjustable = true, .kind = MENU_ROW_CHOICE };

  static const menu_screen screen = {
    .id = UI_SCREEN_DEADZONE,
    .update = deadzone_update,
  };
  return display_menu(menu, idx, &screen, &deadzone_loop, NULL, &menu);
}

/*
 * Special keys
 */

static void special_keys_name(int ord, char *text) {
  if (ord == 0) {
    strcpy(text, tr(STR_SPECIAL_NONE));
    return;
  }

  bool did_find = false;
  int i = 0;
  for (; i < sizeof(settings_special_codes) / sizeof(int); i++) {
    if (settings_special_codes[i] == ord) {
      did_find = true;
      break;
    }
  }

  if (did_find) {
    const special_name *name = &settings_special_names[i];
    strcpy(text, name->literal ? name->literal : tr(name->id));
  } else {
    sprintf(text, "%d", ord);
  }
}

enum {
  SETTINGS_SELECT_SPECIAL_KEY_MANUAL = -1000
};

enum {
  SETTINGS_SPECIAL_KEYS_OFFSET,
  SETTINGS_SPECIAL_KEYS_SIZE
};

enum {
  SETTINGS_SPECIAL_KEYS_NW_VIEW = 3
};

static int select_special_key_loop(int id, void *context, const input_data *input) {
  int *code = context;

  if (!ui_pressed(input, UI_INTENT_CONFIRM)) {
    return 0;
  }
  if (id != SETTINGS_SELECT_SPECIAL_KEY_MANUAL) {
    *code = id;
    return 1;
  }
  char key_code_value[512];
  if (ime_dialog_number(key_code_value, tr(STR_SPECIAL_ENTER_CODE), "") == 0) {
      int key_code = atoi(key_code_value);
      if (key_code) {
        *code = key_code;
        return 1;
      } else {
        char message[256];
        TR_FMT(message, sizeof(message), STR_SPECIAL_BAD_CODE, key_code_value);
        display_error("%s", message);
      }
  }

  return 0;
}

static int select_special_key_menu(int *code) {
  // TODO: sizeof(codes) / sizeof(int) ?
  menu_entry menu[64];
  int idx = 0;
  for (int i = 0; i < (int)(sizeof(settings_special_codes) / sizeof(int)); i++) {
    unsigned int id = settings_special_codes[i];
    menu[idx++] = (menu_entry) {
      .id = id,
      .name = malloc(sizeof(char) * 256),
      .disabled = (id >= INPUT_TYPE_DEF_NAME),
      .kind = id >= INPUT_TYPE_DEF_NAME ? MENU_ROW_HEADER : MENU_ROW_PLAIN,
    };
    special_keys_name(id, menu[idx-1].name);
  }

  menu[idx++] = (menu_entry) { .name = "", .disabled = true, .separator = true };
  menu[idx++] = (menu_entry) { .name = (char *)tr(STR_SPECIAL_ENTER_MANUALLY), .id = SETTINGS_SELECT_SPECIAL_KEY_MANUAL, .kind = MENU_ROW_ACTION };

  menu_screen screen = {
    .id = UI_SCREEN_SPECIAL_KEY_LIST,
    .title = tr(STR_SET_SPECIAL_KEYS),
  };
  int return_code = display_menu(menu, idx, &screen, &select_special_key_loop, NULL, code);
  for (int i = 0; i < sizeof(settings_special_codes) / sizeof(int); i++) {
    free(menu[i].name);
  }

  return return_code;
}

static int special_keys_loop(int id, void *context, const input_data *input) {
  bool left = ui_held(input, UI_INTENT_LEFT);
  bool right = ui_held(input, UI_INTENT_RIGHT);
  int selected_ord = -1;
  int delta = left ? -15 : (right ? 15 : 0);

  switch (id) {
    case SETTINGS_SPECIAL_KEYS_OFFSET:
      config.special_keys.offset += delta;
      break;
    case SETTINGS_SPECIAL_KEYS_SIZE:
      config.special_keys.size += delta;
      break;
    default:
      if (!ui_pressed(input, UI_INTENT_CONFIRM)) {
        break;
      }
      select_special_key_menu(&selected_ord);
      if (selected_ord != -1) {
        switch (id) {
          case TOUCHSEC_SPECIAL_NW:
            config.special_keys.nw = selected_ord;
            break;
          case TOUCHSEC_SPECIAL_NE:
            config.special_keys.ne = selected_ord;
            break;
          case TOUCHSEC_SPECIAL_SW:
            config.special_keys.sw = selected_ord;
            break;
          case TOUCHSEC_SPECIAL_SE:
            config.special_keys.se = selected_ord;
            break;
        }
      }
      break;
  }

  menu_entry *menu = context;

  int idx = 0;
  sprintf(menu[idx++].subname, "%d", config.special_keys.offset);
  sprintf(menu[idx++].subname, "%d", config.special_keys.size);

  idx++;
  special_keys_name(config.special_keys.nw, menu[idx++].subname);
  special_keys_name(config.special_keys.ne, menu[idx++].subname);
  special_keys_name(config.special_keys.sw, menu[idx++].subname);
  special_keys_name(config.special_keys.se, menu[idx++].subname);

  return 0;
}

// The corners' size and place, and which one's row is selected
static void special_keys_update(menu_view *view) {
  static special_keys_view keys;
  keys.offset = config.special_keys.offset;
  keys.size = config.special_keys.size;
  keys.focused = -1;
  if (view->cursor_row < view->row_count) {
    switch (view->rows[view->cursor_row].id) {
      case TOUCHSEC_SPECIAL_NW: keys.focused = 0; break;
      case TOUCHSEC_SPECIAL_NE: keys.focused = 1; break;
      case TOUCHSEC_SPECIAL_SW: keys.focused = 2; break;
      case TOUCHSEC_SPECIAL_SE: keys.focused = 3; break;
    }
  }
  view->special_keys = &keys;
}

static int special_keys_menu() {
  menu_entry menu[16];
  int idx = 0;

  // Offset and size respond to left and right, though they aren't marked adjustable
  menu[idx++] = (menu_entry) { .name = (char *)tr(STR_SPECIAL_OFFSET), .id = SETTINGS_SPECIAL_KEYS_OFFSET, .kind = MENU_ROW_CHOICE };
  menu[idx++] = (menu_entry) { .name = (char *)tr(STR_SPECIAL_SIZE), .id = SETTINGS_SPECIAL_KEYS_SIZE, .kind = MENU_ROW_CHOICE };
  menu[idx++] = (menu_entry) { .name = (char *)tr(STR_SPECIAL_ASSIGNMENTS), .disabled = true, .separator = false, .kind = MENU_ROW_HEADER };
  menu[idx++] = (menu_entry) { .name = (char *)tr(STR_SPECIAL_TOP_LEFT), .id = TOUCHSEC_SPECIAL_NW, .kind = MENU_ROW_ACTION };
  menu[idx++] = (menu_entry) { .name = (char *)tr(STR_SPECIAL_TOP_RIGHT), .id = TOUCHSEC_SPECIAL_NE, .kind = MENU_ROW_ACTION };
  menu[idx++] = (menu_entry) { .name = (char *)tr(STR_SPECIAL_BOTTOM_LEFT), .id = TOUCHSEC_SPECIAL_SW, .kind = MENU_ROW_ACTION };
  menu[idx++] = (menu_entry) { .name = (char *)tr(STR_SPECIAL_BOTTOM_RIGHT), .id = TOUCHSEC_SPECIAL_SE, .kind = MENU_ROW_ACTION };

  static const menu_screen screen = {
    .id = UI_SCREEN_SPECIAL_KEYS,
    .update = special_keys_update,
  };
  return display_menu(menu, idx, &screen, &special_keys_loop, NULL, &menu);
}



// --- Controller Type Selection ---
/*
 * The Settings screen, listing the settings described in src/settings.c
 */

static int settings_back(void *context);

// Returned by settings_loop to rebuild the rows, for example after another
// tab is chosen or developer options are shown or hidden
#define SETTINGS_REBUILD 0x42

static int settings_tab = SETTINGS_TAB_STREAM;

// Must hold the most rows any tab lists (about 18 today). Each is about 1 KB
// of the main thread stack, so don't oversize it.
#define SETTINGS_MAX_ROWS 32

// The rows on screen, which use their setting's index in SETTINGS as their id
static menu_entry *settings_rows = NULL;
static int settings_row_count = 0;

static void move_tab(int step) {
  do {
    settings_tab = (settings_tab + step + SETTINGS_TAB_COUNT) % SETTINGS_TAB_COUNT;
  } while (!settings_tab_shown(settings_tab));
}

// What kind of row a setting makes
static menu_row_kind row_kind(const setting_def *setting) {
  switch (setting->kind) {
    case SETTING_TOGGLE: return setting->is_on != NULL ? MENU_ROW_TOGGLE : MENU_ROW_CYCLE;
    case SETTING_CHOICE: return MENU_ROW_CHOICE;
    case SETTING_ACTION: return MENU_ROW_ACTION;
    case SETTING_INFO: return MENU_ROW_INFO;
    case SETTING_NOTE: return MENU_ROW_NOTE;
    case SETTING_HEADER: return MENU_ROW_HEADER;
  }
  return MENU_ROW_PLAIN;
}

// Fills in every row's value and whether it can be selected, from the settings
static void refresh_rows(void) {
  for (int i = 0; i < settings_row_count; i++) {
    menu_entry *row = &settings_rows[i];
    const setting_def *setting = &SETTINGS[row->id];
    setting_value(setting, row->subname, sizeof(row->subname));
    row->on = setting->is_on != NULL && setting->is_on();
    bool selectable = setting->kind != SETTING_NOTE && setting->kind != SETTING_HEADER;
    row->disabled = !selectable || (setting->available != NULL && !setting->available());
  }
}

static void show_settings_help(const setting_def *setting) {
  char text[1024];
  setting_help(setting, text, sizeof(text));
  display_help(tr(setting->label), text);
}

// Asks for a whole number, returning false if the player cancels
static bool ask_number(const char *title, int current, int *result) {
  char value[512];
  char initial[16];
  snprintf(initial, sizeof(initial), "%d", current);
  if (ime_dialog_number(value, title, initial) != 0) {
    return false;
  }
  *result = atoi(value);
  return true;
}

// Carries out an action row. Returns 0, SETTINGS_REBUILD, or
// SETTINGS_RESTART_STREAM.
static int run_action(const setting_def *setting) {
  int number;
  switch (setting->action) {
    case SETTING_ACTION_RECOMMENDED: {
      // Rebuilt whatever happened: the values may have changed, and
      // auto-detect may have streamed, as a benchmark run does
      int bitrate = config.stream.bitrate;
      bool bitrate_auto = config.bitrate_auto;
      ui_recommended_settings_menu();
      if (config.stream.bitrate != bitrate || config.bitrate_auto != bitrate_auto) {
        // A preset was applied: show it on its step rather than as Custom
        settings_sync_bitrate();
      }
      if (ui_recommended_take_restart()) {
        // Save and leave, so whoever opened Settings restarts the stream
        settings_back(NULL);
        return SETTINGS_RESTART_STREAM;
      }
      return SETTINGS_REBUILD;
    }
    case SETTING_ACTION_CUSTOM_BITRATE:
      if (ask_number(tr(STR_SET_BITRATE_PROMPT), config.stream.bitrate, &number)) {
        if (number >= BITRATE_MIN_KBPS && number <= BITRATE_MAX_KBPS) {
          config.stream.bitrate = number;
        } else {
          char low[16], high[16], message[256];
          snprintf(low, sizeof(low), "%d", BITRATE_MIN_KBPS);
          snprintf(high, sizeof(high), "%d", BITRATE_MAX_KBPS);
          TR_FMT(message, sizeof(message), STR_SET_BITRATE_RANGE, low, high);
          display_error("%s", message);
        }
      }
      return 0;
    case SETTING_ACTION_SPRINT_TIME: {
      char value[512];
      if (ime_dialog_number(value, tr(STR_SET_SPRINT_PROMPT), "") == 0) {
        int step_time = atoi(value);
        if (step_time) {
          config.double_tap_sprint_step_time = step_time;
        } else {
          char message[256];
          TR_FMT(message, sizeof(message), STR_SET_SPRINT_BAD, value);
          display_error("%s", message);
        }
      }
      return 0;
    }
    case SETTING_ACTION_BACK_DEADZONE:
      deadzone_settings_menu();
      return 0;
    case SETTING_ACTION_SPECIAL_KEYS:
      special_keys_menu();
      return 0;
    case SETTING_ACTION_KEYBOARD_LAYOUT:
      keyboard_layout_menu();
      return 0;
    case SETTING_ACTION_RUN_BENCHMARK:
      ui_settings_save_config();
      vitabench_run(false);
      return SETTINGS_REBUILD;
    case SETTING_ACTION_HIDE_DEVELOPER:
      config.developer_mode = false;
      ui_settings_save_config();
      return SETTINGS_REBUILD;
    case SETTING_ACTION_SHOW_HELP:
      show_settings_help(setting);
      return 0;
    case SETTING_ACTION_NONE:
      break;
  }
  return 0;
}

// Around the rows: the tabs, the help hint (so the help is discoverable
// without taking a row), and the selected row's help for themes that show it
static void settings_update(menu_view *view) {
  static ui_tab tabs[SETTINGS_TAB_COUNT];
  static tabs_view tab_bar;
  tab_bar.count = 0;
  for (int tab = 0; tab < SETTINGS_TAB_COUNT; tab++) {
    if (!settings_tab_shown(tab)) {
      continue;
    }
    tabs[tab_bar.count++] = (ui_tab){tr(SETTINGS_TAB_NAMES[tab]), tab == settings_tab};
  }
  tab_bar.tabs = tabs;
  view->tabs = &tab_bar;
  view->hint = (ui_hint){BUTTON_GLYPH_TRIANGLE, tr(STR_HINT_HELP)};

  // Written again only when the selection moves to another setting, or the
  // language changes
  static char help[1024];
  static const setting_def *helped = NULL;
  static const i18n_language *helped_language = NULL;
  if (view->cursor_row < view->row_count) {
    const setting_def *setting = &SETTINGS[view->rows[view->cursor_row].id];
    if (setting != helped || i18n_current() != helped_language) {
      setting_help(setting, help, sizeof(help));
      helped = setting;
      helped_language = i18n_current();
    }
    view->help = help;
  } else {
    view->help = NULL;
  }
}

static int settings_loop(int id, void *context, const input_data *input) {
  const setting_def *setting = &SETTINGS[id];

  if (ui_pressed(input, UI_INTENT_HELP)) {
    if (ui_shoulders_held()) {
      config.developer_mode = !config.developer_mode;
      ui_settings_save_config();
      display_error("%s", tr(config.developer_mode ? STR_SET_DEVELOPER_SHOWN : STR_SET_DEVELOPER_HIDDEN));
      settings_tab = config.developer_mode ? SETTINGS_TAB_DEVELOPER : SETTINGS_TAB_STREAM;
      return SETTINGS_REBUILD;
    }
    show_settings_help(setting);
    return 0;
  }

  // L and R switch tabs, after triangle so the developer combination (L + R +
  // triangle) wins when R and triangle land in the same frame. Pressing both
  // shoulders together moves back and forth and lands on the same tab.
  bool left_tab = ui_pressed(input, UI_INTENT_TAB_PREV);
  bool right_tab = ui_pressed(input, UI_INTENT_TAB_NEXT);
  if (left_tab != right_tab) {
    move_tab(left_tab ? -1 : 1);
    return SETTINGS_REBUILD;
  }

  bool confirm = ui_pressed(input, UI_INTENT_CONFIRM);
  bool changed = false;
  switch (setting->kind) {
    case SETTING_TOGGLE:
      changed = confirm && setting->change(1);
      break;
    case SETTING_CHOICE: {
      bool repeats = (setting->flags & SETTING_HELD_REPEATS) != 0;
      bool left = repeats ? ui_held(input, UI_INTENT_LEFT) : ui_pressed(input, UI_INTENT_LEFT);
      bool right = repeats ? ui_held(input, UI_INTENT_RIGHT) : ui_pressed(input, UI_INTENT_RIGHT);
      right = right || (confirm && (setting->flags & SETTING_CONFIRM_STEPS));
      if (left != right) {
        changed = setting->change(left ? -1 : 1);
      }
      break;
    }
    case SETTING_ACTION:
      if (confirm) {
        int ret = run_action(setting);
        if (ret) {
          return ret;
        }
        changed = true;
      }
      break;
    case SETTING_INFO:
    case SETTING_NOTE:
    case SETTING_HEADER:
      break;
  }

  if (changed) {
    if (setting->flags & SETTING_SAVE_AT_ONCE) {
      ui_settings_save_config();
    }
    if (setting->flags & SETTING_REBUILDS) {
      // Every row changes, but the cursor stays on this one
      display_menu_select(id);
      return SETTINGS_REBUILD;
    }
    refresh_rows();
  }
  return 0;
}

static int settings_back(void *context) {
  ui_settings_save_config();
  update_layout();
  return 0;
}

int ui_settings_menu() {
  menu_entry menu[SETTINGS_MAX_ROWS];
  settings_rows = menu;
  // A fresh visit opens on the first tab, and on Custom bitrate when the
  // saved value isn't a step; rebuilds stay as they are
  settings_tab = SETTINGS_TAB_STREAM;
  settings_sync_bitrate();

  int ret;
  // Settings opens from a menu's callback, before that menu's frame is
  // finished, and the frame may still use system-font glyphs that changing
  // the fonts would free. So under Automatic, a new system language waits
  // for that frame to finish; a fresh one then takes its place, for
  // display_menu() to finish as it would have
  if (i18n_system_language_changed()) {
    ui_end();
    i18n_follow_system();
    guilib_language_changed();
    ui_start();
  }
  const i18n_language *language = i18n_current();
  do {
    if (i18n_current() != language) {
      // The menu has finished drawing, so the fonts can change now
      language = i18n_current();
      guilib_language_changed();
    }
    if (!settings_tab_shown(settings_tab)) {
      settings_tab = SETTINGS_TAB_STREAM;
    }
    // The rows for this tab, in the table's order
    settings_row_count = 0;
    for (int i = 0; i < SETTINGS_COUNT; i++) {
      const setting_def *setting = &SETTINGS[i];
      if (!setting_listed(setting, settings_tab)) {
        continue;
      }
      assert(settings_row_count < SETTINGS_MAX_ROWS);
      menu[settings_row_count++] = (menu_entry) {
        .id = i,
        .name = (char *)tr(setting->label),
        .separator = setting->kind == SETTING_HEADER,
        .adjustable = setting->kind == SETTING_CHOICE,
        .status = (setting->flags & SETTING_WARNING) ? MENU_STATUS_WARNING : MENU_STATUS_NONE,
        .kind = row_kind(setting),
      };
    }
    refresh_rows();
    static const menu_screen screen = {
      .id = UI_SCREEN_SETTINGS,
      .update = settings_update,
    };
    ret = display_menu(menu, settings_row_count, &screen, &settings_loop, &settings_back, NULL);
  } while (ret == SETTINGS_REBUILD);
  settings_rows = NULL;
  settings_row_count = 0;
  return ret;
}

void ui_settings_save_config() {
  vita_debug_log("[DEBUG] Guardando configuración:");
  vita_debug_log("  touchscreen_mode = %d", config.touchscreen_mode);
  vita_debug_log("  swap_shoulder_buttons = %d", config.swap_shoulder_buttons);
  vita_debug_log("  controller_type = %d", config.controller_type);
  vita_debug_log("  enable_mapping = %s", config.mapping ? "yes" : "no");
  vita_debug_log("  stats_overlay = %d", config.stats_overlay);
  vita_debug_log("  enable_motion_controls = %d", config.enable_motion_controls);
  vita_debug_log("  double_tap_sprint = %d", config.enable_double_tap_sprint);
  vita_debug_log("  double_tap_sprint_step_time = %u", config.double_tap_sprint_step_time);
  vita_debug_log("  mouse_acceleration = %d", config.mouse_acceleration);
  vita_debug_log("  stream.width = %d", config.stream.width);
  vita_debug_log("  stream.height = %d", config.stream.height);
  vita_debug_log("  stream.fps = %d", config.stream.fps);
  vita_debug_log("  stream.bitrate = %d%s", config.stream.bitrate, config.bitrate_auto ? " (auto)" : "");
  vita_debug_log("  save_debug_log = %d", config.save_debug_log);
  vita_debug_log("  disable_powersave = %d", config.disable_powersave);
  vita_debug_log("  boost_clocks = %d", config.boost_clocks);
  vita_debug_log("  jp_layout = %d", config.jp_layout);
  vita_debug_log("  localaudio = %d", config.localaudio);
  vita_debug_log("  enable_frame_pacer = %d", config.enable_frame_pacer);
  vita_debug_log("  center_region_only = %d", config.center_region_only);
  vita_debug_log("  enable_ref_frame_invalidation = %d", config.enable_ref_frame_invalidation);
  vita_debug_log("  stream.streamingRemotely = %d", config.stream.streamingRemotely);
  vita_debug_log("  enable_vita_vblank_wait = %d", config.enable_vita_vblank_wait);
  config_save(config_path, &config);
  vita_debug_log("[DEBUG] Configuración guardada en %s", config_path);
}

