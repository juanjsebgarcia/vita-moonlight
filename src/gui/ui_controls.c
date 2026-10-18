// Controls & shortcuts: a read-only list of what every button, touch and
// shortcut does, built from the live config so it shows the player's own
// setup (Swap X & O, Swap R1/L1 <-> R2/L2, a PS TV, the touchscreen mode,
// the corner buttons and a mapping file).
//
// What it describes lives in src/input/shortcuts.c (START + L + R, START +
// LEFT), src/input/vita.c (vitainput_config's button map, process_triggers,
// handle_psbutton, the touch modes and the double tap to sprint) and
// src/gui/ui.c (global_loop's resume corner, the host list's buttons).
// Change a row here when the binding it describes changes.

#include "ui_controls.h"

#include "guilib.h"
#include "theme.h"
#include "ui_settings.h"

#include "../config.h"
#include "../i18n/i18n.h"

#include <stdio.h>
#include <string.h>

#include <psp2/kernel/sysmem.h>

// Rows at most, with room to spare: about 34 with every option on. Static,
// as each row is about 1 KB.
#define CONTROLS_ROWS_MAX 48

static menu_entry rows[CONTROLS_ROWS_MAX];
static int count;

static void heading(const char *name) {
  if (count < CONTROLS_ROWS_MAX) {
    rows[count++] = (menu_entry){.name = (char *)name, .disabled = true, .separator = true, .kind = MENU_ROW_HEADER};
  }
}

static void note(const char *text) {
  if (count < CONTROLS_ROWS_MAX) {
    rows[count++] = (menu_entry){.name = (char *)text, .disabled = true, .kind = MENU_ROW_NOTE};
  }
}

// One binding: the controls on the left, what they do on the right. Rows
// can be selected (doing nothing), because display_menu scrolls only by
// moving its cursor.
static void binding(const char *controls, const char *action) {
  if (count < CONTROLS_ROWS_MAX) {
    rows[count] = (menu_entry){.id = count, .name = (char *)controls, .kind = MENU_ROW_PLAIN};
    snprintf(rows[count].subname, sizeof(rows[count].subname), "%s", action);
    count++;
  }
}

static void corner(unsigned int code, str_id where) {
  if (code == 0) {
    return;
  }
  char name[256];
  ui_settings_special_key_name(code, name);
  binding(tr(where), name);
}

static void build(void) {
  count = 0;
  bool ps_tv = config.model == SCE_KERNEL_MODEL_VITATV;
  bool swap = config.swap_shoulder_buttons;

  heading(tr(STR_CTL_HEAD_SHORTCUTS));
  binding("{start} + {l} + {r}", tr(STR_CTL_PAUSE));
  binding("{start} + {left}", tr(STR_CTL_KEYBOARD));
  if (config.enable_psbutton_capture) {
    binding("{ps}", tr(STR_CTL_PS_TO_PC));
    binding(tr(STR_CTL_PS_TWICE), tr(STR_CTL_VITA_HOME));
  } else {
    binding("{ps}", tr(STR_CTL_VITA_HOME));
  }
  if (config.enable_double_tap_sprint) {
    binding(tr(STR_CTL_SPRINT_GESTURE), tr(STR_CTL_SPRINT));
  }

  heading(tr(STR_CTL_HEAD_BUTTONS));
  if (config.mapping) {
    note(tr(STR_CTL_MAPPING_NOTE));
  }
  binding("{dpad} {cross} {circle} {triangle} {square}", tr(STR_CTL_SAME_ON_PC));
  binding("{start} {select} {left_stick} {right_stick}", tr(STR_CTL_SAME_ON_PC));
  // process_triggers: with the swap, L and R send the triggers, and the
  // triggers (the rear touchpad's upper half, or a PS TV pad's L2 and R2)
  // send L1 and R1. L3 and R3 never swap.
  binding("{l} {r}", tr(swap ? STR_CTL_L2_R2 : STR_CTL_L1_R1));
  if (ps_tv) {
    binding("{l2} {r2}", tr(swap ? STR_CTL_L1_R1 : STR_CTL_L2_R2_ANALOG));
    binding("{l3} {r3}", tr(STR_CTL_L3_R3));
  } else {
    binding(tr(STR_CTL_REAR_UPPER), tr(swap ? STR_CTL_L1_R1 : STR_CTL_L2_R2));
    binding(tr(STR_CTL_REAR_LOWER), tr(STR_CTL_L3_R3));
  }

  // A PS TV has no touchscreen
  if (!ps_tv) {
    heading(tr(STR_CTL_HEAD_TOUCH));
    static const str_id modes[] = {STR_TOUCH_OFF, STR_TOUCH_DS4, STR_TOUCH_MOUSE_ABSOLUTE, STR_TOUCH_TABLET};
    int mode = config.touchscreen_mode;
    mode = mode >= 0 && mode < (int)(sizeof(modes) / sizeof(modes[0])) ? mode : 0;
    binding(tr(STR_SET_TOUCH_MODE_SELECT), tr(modes[mode]));
    switch (mode) {
      case 1:
        binding(tr(STR_CTL_TOUCH_HOLD), tr(STR_CTL_DS4_TOUCHPAD));
        break;
      case 2:
        binding(tr(STR_CTL_TOUCH_HOLD), tr(STR_CTL_POINT_AND_HOLD));
        binding(tr(STR_CTL_TOUCH_TWO_TAP), tr(STR_CTL_RIGHT_CLICK));
        binding(tr(STR_CTL_TOUCH_TWO_DRAG), tr(STR_CTL_SCROLL));
        break;
      case 3:
        binding(tr(STR_CTL_TOUCH_HOLD), tr(STR_CTL_TABLET));
        break;
      default:
        binding(tr(STR_CTL_TOUCH_DRAG), tr(STR_CTL_MOVE_MOUSE));
        binding(tr(STR_CTL_TOUCH_TAP), tr(STR_CTL_LEFT_CLICK));
        binding(tr(STR_CTL_TOUCH_TWO_TAP), tr(STR_CTL_RIGHT_CLICK));
        binding(tr(STR_CTL_TOUCH_TWO_DRAG), tr(STR_CTL_SCROLL));
        break;
    }
    // The corner buttons work in every mode, before it sees the touch
    if (config.enable_front_touchzones) {
      corner(config.special_keys.nw, STR_CTL_CORNER_TOP_LEFT);
      corner(config.special_keys.ne, STR_CTL_CORNER_TOP_RIGHT);
      corner(config.special_keys.sw, STR_CTL_CORNER_BOTTOM_LEFT);
      corner(config.special_keys.se, STR_CTL_CORNER_BOTTOM_RIGHT);
    }
  }

  heading(tr(STR_CTL_HEAD_MENUS));
  binding("{confirm}", tr(STR_CTL_SELECT));
  binding("{cancel}", tr(STR_CTL_BACK));
  binding("{triangle}", tr(STR_HINT_HELP));
  binding(tr(STR_CTL_TRIANGLE_PC_LIST), tr(STR_CTL_SEARCH_AGAIN));
  binding("{l} {r}", tr(STR_CTL_SETTINGS_TABS));
  if (theme_offers_new()) {
    binding(tr(STR_CTL_LR_PC_LIST), tr(STR_CTL_NEW_THEME));
  }
  if (!ps_tv) {
    binding(tr(STR_CTL_CORNER_TOP_LEFT), tr(STR_CTL_RESUME_PAUSED));
  }
}

static int controls_loop(int id, void *context, const input_data *input) {
  return 0;
}

void ui_controls_menu(void) {
  build();
  menu_screen screen = {
    .id = UI_SCREEN_CONTROLS,
    .title = tr(STR_CTL_TITLE),
  };
  display_menu(rows, count, &screen, &controls_loop, NULL, NULL);
}
