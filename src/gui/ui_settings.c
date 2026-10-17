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

// settings_special_names debe ser visible globalmente para sizeof
char *settings_special_names[] = {
  "None",
  // special
  "Special inputs",
  "Pause stream",
  "Open keyboard",
  // gamepad
  "Gamepad buttons",
  "Special (XBox button)",
  "LB", "RB", "LS", "RS", "LT", "RT",
  // mouse
  "Mouse buttons",
  "Left",
  "Right",
  "Middle(wheel)",
  "X1(4th)",
  "X2(5th)",
  // keyboard
  "Keyboard input codes",
  "Esc",
  "I", "M", "Tab",
  "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12"
};

#define MAX_RESOLUTION 9
static int RESOLUTIONS[MAX_RESOLUTION][2] = {
  {960, 540},   // 16:9, QHD
  {960, 544},   // VITA original
  {1024, 576},  // 16:9
  {1152, 648},  // 16:9
  {1280, 540},  // 21:9
  {1280, 720},  // 16:9, 720p HD
  {1366, 768},  // 16:9, WXGA
  {1600, 900},  // 16:9, 900p HD+
  //{1720, 720},  // 21:9
  {1920, 1080}, // 16:9, FHD
  //{1920, 1200}, // 16:10
};

int support_resolution_idx[MAX_RESOLUTION] = {-1};
char *support_resolutions[MAX_RESOLUTION] = {0};
int support_resolution_count = 0;

static bool settings_loop_setup = 1;

/*
 * Deadzone
 */

#define lerp(value, from_max, to_max) ((((value*10) * (to_max*10))/(from_max*10))/10)
static void deadzone_draw() {
  int vertical = (WIDTH - config.back_deadzone.left - config.back_deadzone.right) / 2 + config.back_deadzone.left,
      horizontal = (HEIGHT - config.back_deadzone.top - config.back_deadzone.bottom) / 2 + config.back_deadzone.top;

  vita2d_draw_rectangle(
      config.back_deadzone.left,
      config.back_deadzone.top,
      WIDTH - config.back_deadzone.right - config.back_deadzone.left,
      HEIGHT - config.back_deadzone.bottom - config.back_deadzone.top,
      0x3000ff00
      );

  vita2d_draw_line(vertical, config.back_deadzone.top, vertical, HEIGHT - config.back_deadzone.bottom, 0xffffffff);
  vita2d_draw_line(config.back_deadzone.left, horizontal, WIDTH - config.back_deadzone.right, horizontal, 0xffffffff);

  SceTouchData touch_data;
  sceTouchPeek(SCE_TOUCH_PORT_BACK, &touch_data, 1);

  for (int i = 0; i < touch_data.reportNum; i++) {
    int x = lerp(touch_data.report[i].x, 1919, 960);
    int y = lerp(touch_data.report[i].y, 1087, 544);
    if (x < config.back_deadzone.left || x > WIDTH - config.back_deadzone.right)
      continue;

    if (y < config.back_deadzone.top || y > HEIGHT - config.back_deadzone.bottom)
      continue;

    vita2d_draw_fill_circle(x, y, 30, 0xffffffff);
  }
}

static int deadzone_loop(int cursor, void *context, const input_data *input) {
  menu_entry *menu = context;

  bool left = input->buttons & SCE_CTRL_LEFT;
  bool right = input->buttons & SCE_CTRL_RIGHT;

  int delta = left ? -15 : (right ? 15 : 0);
  switch (cursor) {
    case 0: config.back_deadzone.top += delta; break;
    case 1: config.back_deadzone.right += delta; break;
    case 2: config.back_deadzone.bottom += delta; break;
    case 3: config.back_deadzone.left += delta; break;
  }

  settings_loop_setup = 0;
  char current[256];

  int numbers[] = {
    config.back_deadzone.top,
    config.back_deadzone.right,
    config.back_deadzone.bottom,
    config.back_deadzone.left
  };
  for (int i = 0; i < 4; i++) {
    sprintf(current, "%dpx", numbers[i]);
    strcpy(menu[i].subname, current);
  }

  return 0;
}

static int deadzone_settings_menu() {
  sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);

  menu_entry menu[16];
  int idx = 0;
  menu[idx++] = (menu_entry) { .name = "Top: ", .disabled = false, .id = 0, .suffix = ICON_LEFT_RIGHT_ARROWS };
  menu[idx++] = (menu_entry) { .name = "Left: ", .disabled = false, .id = 1, .suffix = ICON_LEFT_RIGHT_ARROWS };
  menu[idx++] = (menu_entry) { .name = "Bottom: ", .disabled = false, .id = 2, .suffix = ICON_LEFT_RIGHT_ARROWS };
  menu[idx++] = (menu_entry) { .name = "Right: ", .disabled = false, .id = 3, .suffix = ICON_LEFT_RIGHT_ARROWS };

  menu_geom geom = make_geom_centered(250, 120);
  geom.x = 50;
  geom.y = 50;
  geom.el = 25;
  return display_menu(menu, idx, &geom, &deadzone_loop, NULL, &deadzone_draw, &menu);
}

/*
 * Special keys
 */

static int special_keys_ord(char *text) {
  bool did_find = false;
  int i = 0;
  for (; i < sizeof(settings_special_codes) / sizeof(int); i++) {
    if (strcmp(settings_special_names[i], text) == 0) {
      did_find = true;
      break;
    }
  }

  if (did_find) {
    return settings_special_codes[i];
  } else {
    return 0;
  }
}

static void special_keys_name(int ord, char *text) {
  if (ord == 0) {
    strcpy(text, "None");
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
    strcpy(text, settings_special_names[i]);
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

  if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
    return 0;
  }
  if (id != SETTINGS_SELECT_SPECIAL_KEY_MANUAL) {
    *code = id;
    return 1;
  }
  char key_code_value[512];
  if (ime_dialog_number(key_code_value, "Enter key code:", "") == 0) {
      int key_code = atoi(key_code_value);
      if (key_code) {
        *code = key_code;
        return 1;
      } else {
        display_error("Incorrect key code entered: %s", key_code_value);
      }
  }

  return 0;
}

static int select_special_key_menu(int *code) {
  // TODO: sizeof(codes) / sizeof(int) ?
  menu_entry menu[64];
  int idx = 0;
  for (int i = 0; i < sizeof(settings_special_codes) / sizeof(int); i++) {
    unsigned int id = settings_special_codes[i];
    menu[idx++] = (menu_entry) {
      .id = id,
      .name = malloc(sizeof(char) * 256),
      .disabled = (id >= INPUT_TYPE_DEF_NAME)
    };
    special_keys_name(id, menu[idx-1].name);
  }

  menu[idx++] = (menu_entry) { .name = "", .disabled = true, .separator = true };
  menu[idx++] = (menu_entry) { .name = "Enter manually ...", .id = SETTINGS_SELECT_SPECIAL_KEY_MANUAL };

  int return_code = display_menu(menu, idx, NULL, &select_special_key_loop, NULL, NULL, code);
  for (int i = 0; i < sizeof(settings_special_codes) / sizeof(int); i++) {
    free(menu[i].name);
  }

  return return_code;
}

static int special_keys_loop(int id, void *context, const input_data *input) {
  bool left = input->buttons & SCE_CTRL_LEFT;
  bool right = input->buttons & SCE_CTRL_RIGHT;
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
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
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

static void special_keys_draw() {
  int special_offset = config.special_keys.offset,
      special_size = config.special_keys.size;

  unsigned int color = 0xff006000;

  for (int i = TOUCHSEC_SPECIAL_NW; i <= TOUCHSEC_SPECIAL_SE; i++) {
    switch (i) {
      case TOUCHSEC_SPECIAL_SW:
        vita2d_draw_rectangle(
            special_offset,
            HEIGHT - special_size - special_offset,
            special_size,
            special_size,
            color);
        // fallthrough
      case TOUCHSEC_SPECIAL_SE:
        vita2d_draw_rectangle(
            WIDTH - special_size - special_offset,
            HEIGHT - special_size - special_offset,
            special_size,
            special_size,
            color);
        // fallthrough
      case TOUCHSEC_SPECIAL_NW:
        vita2d_draw_rectangle(
            special_offset,
            special_offset,
            special_size,
            special_size,
            color);
        // fallthrough
      case TOUCHSEC_SPECIAL_NE:
        vita2d_draw_rectangle(
            WIDTH - special_size - special_offset,
            special_offset,
            special_size,
            special_size,
            color);
    }
  }

}

static int special_keys_menu() {
  menu_entry menu[16];
  int idx = 0;

  menu[idx++] = (menu_entry) { .name = "Offset", .id = SETTINGS_SPECIAL_KEYS_OFFSET };
  menu[idx++] = (menu_entry) { .name = "Size", .id = SETTINGS_SPECIAL_KEYS_SIZE };
  menu[idx++] = (menu_entry) { .name = "Assignments", .disabled = true, .separator = false };
  menu[idx++] = (menu_entry) { .name = "Top left", .id = TOUCHSEC_SPECIAL_NW };
  menu[idx++] = (menu_entry) { .name = "Top right", .id = TOUCHSEC_SPECIAL_NE };
  menu[idx++] = (menu_entry) { .name = "Bottom left", .id = TOUCHSEC_SPECIAL_SW };
  menu[idx++] = (menu_entry) { .name = "Bottom right", .id = TOUCHSEC_SPECIAL_SE };

  return display_menu(menu, idx, NULL, &special_keys_loop, NULL, &special_keys_draw, &menu);
}



// --- Controller Type Selection ---
static const char* controller_type_names[] = {"Xbox", "PlayStation"};
static int controller_type_values[] = {1, 2}; // 1: Xbox, 2: PS
#define CONTROLLER_TYPE_COUNT 2

static int get_controller_type_index(int value) {
  for (int i = 0; i < CONTROLLER_TYPE_COUNT; ++i) {
    if (controller_type_values[i] == value) return i;
  }
  return 1; // Default to PlayStation if not found
}

/*
 * Main menu
 */

enum {
  SETTINGS_RESOLUTION = 100,
  SETTINGS_FPS,
  SETTINGS_BITRATE,
  SETTINGS_SOPS,
  SETTINGS_ENABLE_FRAME_INVAL,
  SETTINGS_ENABLE_STREAM_OPTIMIZE,
  SETTINGS_ENABLE_VITA_VBLANK_WAIT,
  SETTINGS_ENABLE_MOTION_CONTROLS, //Metalface
  SETTINGS_MOTION_CONTROLS_SCALAR_X,
  SETTINGS_MOTION_CONTROLS_SCALAR_Y,
  SETTINGS_ENABLE_DOUBLE_TAP_SPRINT, //Metalface
  SETTINGS_DOUBLE_TAP_SPRINT_STEP_TIME,
  SETTINGS_SAVE_DEBUG_LOG,
  SETTINGS_DISABLE_POWERSAVE,
  SETTINGS_JP_LAYOUT,
  SETTINGS_SHOW_FPS,
  SETTINGS_LOCAL_AUDIO,
  SETTINGS_ENABLE_FRAME_PACER,
  SETTINGS_CENTER_REGION_ONLY,
  SETTINGS_ENABLE_MAPPING,
  SETTINGS_BACK_DEADZONE,
  SETTINGS_SPECIAL_KEYS,
  SETTINGS_ENABLE_SPECIAL_KEYS,
  SETTINGS_ENABLE_PSBUTTON_CAPTURE,
  SETTINGS_CONTROLLER_TYPE,
  SETTINGS_SWAP_SHOULDER_BUTTONS, // NUEVO: Swap R1/L1 <-> R2/L2
  SETTINGS_MOUSE_ACCEL,
  SETTINGS_KEYBOARD_LAYOUT,
  SETTINGS_TOUCH_MODE_SELECT, // Nuevo: selección de modo táctil exclusivo
  SETTINGS_BOOST_CLOCKS,
  SETTINGS_DIRECT_DECODE,
  SETTINGS_INPUT_SYNC,
  SETTINGS_PACKET_SIZE,
  SETTINGS_BENCH_RUN,
  SETTINGS_BENCH_SECONDS,
  SETTINGS_HIDE_DEVELOPER,
  SETTINGS_DECODE_YUV
};

enum {
  SETTINGS_VIEW_RESOLUTION,
  SETTINGS_VIEW_FPS,
  SETTINGS_VIEW_BITRATE,
  SETTINGS_VIEW_SOPS,
  SETTINGS_VIEW_ENABLE_FRAME_INVAL,
  SETTINGS_VIEW_ENABLE_STREAM_OPTIMIZE,
  SETTINGS_VIEW_ENABLE_VITA_VBLANK_WAIT,
  SETTINGS_VIEW_ENABLE_MOTION_CONTROLS, //Metalface
  SETTINGS_VIEW_MOTION_CONTROLS_SCALAR_X,
  SETTINGS_VIEW_MOTION_CONTROLS_SCALAR_Y,
  SETTINGS_VIEW_ENABLE_DOUBLE_TAP_SPRINT, //Metalface
  SETTINGS_VIEW_DOUBLE_TAP_SPRINT_STEP_TIME,
  SETTINGS_VIEW_SAVE_DEBUG_LOG,
  SETTINGS_VIEW_DISABLE_POWERSAVE,
  SETTINGS_VIEW_JP_LAYOUT,
  SETTINGS_VIEW_SHOW_FPS,
  SETTINGS_VIEW_LOCAL_AUDIO,
  SETTINGS_VIEW_ENABLE_FRAME_PACER,
  SETTINGS_VIEW_CENTER_REGION_ONLY,
  SETTINGS_VIEW_ENABLE_MAPPING,
  SETTINGS_VIEW_BACK_DEADZONE,
  SETTINGS_VIEW_SPECIAL_KEYS,
  SETTINGS_VIEW_ENABLE_SPECIAL_KEYS,
  SETTINGS_VIEW_ENABLE_PSBUTTON_CAPTURE,
  SETTINGS_VIEW_CONTROLLER_TYPE,
  SETTINGS_VIEW_SWAP_SHOULDER_BUTTONS, // NUEVO: Swap R1/L1 <-> R2/L2
  SETTINGS_VIEW_MOUSE_ACCEL,
  SETTINGS_VIEW_KEYBOARD_LAYOUT,
  SETTINGS_VIEW_TOUCH_MODE_SELECT, // Vista para modo táctil exclusivo
  SETTINGS_VIEW_BOOST_CLOCKS,
  SETTINGS_VIEW_DIRECT_DECODE,
  SETTINGS_VIEW_INPUT_SYNC,
  SETTINGS_VIEW_PACKET_SIZE,
  SETTINGS_VIEW_BENCH_RUN,
  SETTINGS_VIEW_BENCH_SECONDS,
  SETTINGS_VIEW_HIDE_DEVELOPER,
  SETTINGS_VIEW_DECODE_YUV,

  SETTINGS_VIEW_MAX_COUNT,
};

static int SETTINGS_VIEW_IDX[SETTINGS_VIEW_MAX_COUNT];

// Writes a setting's value into its row. Row 0 is never a setting, so 0 means
// the setting has no row in this menu (hidden mid-stream, or not listed) and
// the value is skipped rather than written over the first row.
#define SET_ROW_VALUE(VIEW, VALUE) \
  do { \
    if (SETTINGS_VIEW_IDX[(VIEW)] > 0) { \
      strcpy(menu[SETTINGS_VIEW_IDX[(VIEW)]].subname, (VALUE)); \
    } \
  } while (0)
// Variable global para swap de botones
bool swap_shoulder_buttons = false;
// Variable global para modo táctil exclusivo
int touch_mode_select = 0;

// _countof only works for variable allocated on the stack, not from malloc (sizeof(i) will be incorrect).
#define _countof(i) (sizeof(i) / sizeof((i)[0]))
#define _move_idx_in_array(a, f, i) move_idx_in_array((a), _countof(a), (f), (i))
static int move_idx_in_array(char *array[], int count, char *find, int index_dist) {
  int i = 0;
  for (; i < count; i++) {
    if (strcmp(find, array[i]) == 0) {
      i += index_dist;
      break;
    }
  }

  if (i >= count) {
    return count - 1;
  } else if (i < 0) {
    return 0;
  } else {
    return i;
  }
}


// Returned by settings_loop to rebuild the menu, for example after developer
// options are shown or hidden
#define SETTINGS_REBUILD 0x42

// Number of rows in the settings menu, so the help can look up a row's title
static int settings_menu_count = 0;

// Plain-language help for each setting, shown with triangle. Written for the
// player, not the developer: what it does, and when to change it.
static const char *settings_help_text(int id) {
  switch (id) {
    case SETTINGS_RESOLUTION:
      return "The picture size your PC sends. The Vita's screen is 960x544, so 960x544 or 960x540 "
             "looks sharpest for the least Wi-Fi. Bigger sizes look about the same on the Vita but "
             "need more bandwidth. Can't be changed while a game is streaming.";
    case SETTINGS_FPS:
      return "Frames per second your PC sends. 60 is the smoothest. Try 30 if the stream stutters "
             "or your Wi-Fi is weak: it needs about half the data. 24 suits films.";
    case SETTINGS_BITRATE:
      return "How much video is sent each second, in kilobits (10000 = 10 Mbps). Higher looks "
             "sharper but needs a stronger Wi-Fi signal. The Vita's Wi-Fi is happiest at around "
             "5000 to 10000. Lower it if the picture goes blocky or the stream stutters.";
    case SETTINGS_SOPS:
      return "Lets GeForce Experience change your game's own graphics settings to suit the stream. "
             "Only does anything with NVIDIA GeForce Experience; Sunshine ignores it. Turn it off "
             "if your in-game settings keep changing by themselves.";
    case SETTINGS_ENABLE_FRAME_INVAL:
      return "Fixes glitches from lost video faster, without waiting for a whole new picture. "
             "Needs support from your PC. If you see corrupted video that doesn't clear up, "
             "turn this off.";
    case SETTINGS_ENABLE_STREAM_OPTIMIZE:
      return "Tunes the stream for playing over the internet, away from home. Leave it off on "
             "your home network, where it only adds overhead.";
    case SETTINGS_ENABLE_VITA_VBLANK_WAIT:
      return "V-Sync. Waits for the screen to finish a refresh before showing the next frame. "
             "Stops lines tearing across the picture, but can add up to a frame of input lag. "
             "Off feels snappier, on looks cleaner.";
    case SETTINGS_CENTER_REGION_ONLY:
      return "When the stream's shape doesn't match the Vita's screen, for example a 4:3 or "
             "ultrawide game, zoom in and crop the edges to fill the screen instead of showing "
             "black bars. You may lose some of the picture at the edges.";
    case SETTINGS_ENABLE_FRAME_PACER:
      return "Throws away extra frames when more arrive than your FPS setting. It can cause a "
             "small hitch every few seconds, so off is recommended.";
    case SETTINGS_PACKET_SIZE:
      return "How big each chunk of video sent over Wi-Fi is. 1392 sends fewer chunks per frame, "
             "which can be smoother on a good connection. If the video breaks up or won't start, "
             "go back to 1024.";
    case SETTINGS_DIRECT_DECODE:
      return "For troubleshooting only. Uses the older way of handling video, which stutters more "
             "on a busy Wi-Fi network. Leave it off unless you want to compare the two.";
    case SETTINGS_DECODE_YUV:
      return "Experimental. The hardware decoder outputs YUV instead of RGB, and the graphics "
             "chip converts the colours while drawing. It may decode a little faster; compare "
             "with the benchmark. If colours look wrong, turn it off.";
    case SETTINGS_BENCH_RUN:
      return "Streams the benchmark page (tools/bench/bench.html) from this computer and measures "
             "input lag, latency, frame rate and dropped frames, then saves a report to the "
             "memory card. The computer needs a Sunshine app that shows the page; see "
             "tools/bench/README.md. Only available from a connected computer's menu.";
    case SETTINGS_BENCH_SECONDS:
      return "How long the benchmark measures for. Longer runs smooth out Wi-Fi hiccups.";
    case SETTINGS_HIDE_DEVELOPER:
      return "Hides these developer options again. Hold L and R and press triangle in Settings "
             "to show them.";
    case SETTINGS_LOCAL_AUDIO:
      return "Also plays the game's sound on your PC's speakers. Off keeps the PC quiet and sends "
             "the sound only to the Vita.";
    case SETTINGS_DISABLE_POWERSAVE:
      return "Stops the Vita dimming its screen or going to sleep while you stream, even when you "
             "aren't pressing anything, such as during a cutscene.";
    case SETTINGS_BOOST_CLOCKS:
      return "Runs the Vita's processor and graphics at full speed while streaming, for smoother "
             "video and sound. Uses more battery. Turn it off to save battery if your streams "
             "already run smoothly.";
    case SETTINGS_JP_LAYOUT:
      return "Swaps X and O in Moonlight's own menus, so O confirms and X goes back, like on "
             "Japanese consoles. Doesn't change any buttons in your games.";
    case SETTINGS_SHOW_FPS:
      return "Shows stream information on screen. fps: just the frame rate. full: frame rate, "
             "bitrate, lost frames, delays and Wi-Fi signal, to help track down stutter or lag. "
             "Use left and right to change.";
    case SETTINGS_INPUT_SYNC:
      return "Reads your buttons in step with the Vita's controls, which saves battery without "
             "adding lag. If touch controls feel slower than before, turn it off.";
    case SETTINGS_ENABLE_MOTION_CONTROLS:
      return "Sends the Vita's motion sensor to your PC, for games with gyro aiming or motion "
             "controls. Needs a PC host that supports motion, such as Sunshine.";
    case SETTINGS_ENABLE_DOUBLE_TAP_SPRINT:
      return "Push the left stick fully forward twice, quickly, to hold sprint (a left stick "
             "click). Handy because the Vita's sticks can't be clicked. A running icon shows "
             "while it's active.";
    case SETTINGS_DOUBLE_TAP_SPRINT_STEP_TIME:
      return "How quickly the two pushes must come for double tap to sprint, in milliseconds. "
             "Raise it if sprint won't start, lower it if it starts by accident.";
    case SETTINGS_CONTROLLER_TYPE:
      return "Which controller your PC thinks is connected: PlayStation or Xbox. Games show "
             "button prompts to match.";
    case SETTINGS_SWAP_SHOULDER_BUTTONS:
      return "Off: L and R are L1 and R1, and the top corners of the rear touchpad are L2 and R2. "
             "On: L and R become L2 and R2, which suits shooters where you aim and fire with the "
             "triggers, and the rear corners become L1 and R1.";
    case SETTINGS_MOUSE_ACCEL:
      return "How fast the mouse cursor moves when you use the front screen as a trackpad. "
             "Higher moves the cursor further for the same swipe.";
    case SETTINGS_ENABLE_PSBUTTON_CAPTURE:
      return "Press the PS button once to send it to your PC, which opens things like Steam's "
             "overlay. Press it twice quickly to go to the Vita's home screen as usual.";
    case SETTINGS_BACK_DEADZONE:
      return "Strips around the edges of the rear touchpad that are ignored, so holding the Vita "
             "doesn't press L2, R2, L3 or R3 by accident.";
    case SETTINGS_ENABLE_SPECIAL_KEYS:
      return "Turns the corners of the front screen into buttons, such as top-left for the pause "
             "menu. Choose what each corner does in Touchscreen special keys.";
    case SETTINGS_SPECIAL_KEYS:
      return "Choose what each corner of the front screen does, such as opening the pause menu or "
             "pressing the PS button. Only works when Enable touchscreen special keys is on.";
    case SETTINGS_TOUCH_MODE_SELECT:
      return "How the front screen works in games. Off: a trackpad for the mouse. DS4 Touchpad: "
             "a PlayStation controller touchpad. Mouse Absolute: the cursor jumps to your finger. "
             "Tablet: full multi-touch (Sunshine).";
    case SETTINGS_KEYBOARD_LAYOUT:
      return "The language layout of the on-screen keyboard, which you open with START + LEFT "
             "while streaming.";
  }
  return NULL;
}

static void show_settings_help(const menu_entry *menu, int id) {
  const char *title = "";
  for (int i = 0; i < settings_menu_count; i++) {
    if (menu[i].id == id && !menu[i].disabled) {
      title = menu[i].name;
      break;
    }
  }

  char text[1024];
  // These two include a path on the memory card, which depends on where the config lives
  if (id == SETTINGS_SAVE_DEBUG_LOG) {
    snprintf(text, sizeof(text),
             "Saves a detailed log to %smoonlight.log for tracking down problems. It slows "
             "streaming down, so only turn it on when something's wrong, and turn it off again "
             "afterwards.", config.key_dir);
  } else if (id == SETTINGS_ENABLE_MAPPING) {
    snprintf(text, sizeof(text),
             "Use your own button layout from a file at %smappings/vita.conf instead of the "
             "default. There's an example file in the project's GitHub repository.",
             config.key_dir);
  } else {
    const char *help = settings_help_text(id);
    snprintf(text, sizeof(text), "%s", help ? help : "No help for this setting yet.");
  }
  display_help(title, text);
}

// Drawn under the settings box, so the help is discoverable without taking a row
static void settings_draw(void) {
  const char *hint = "Help";
  const int glyph_size = 14;
  const int gap = 6;
  int width = glyph_size + gap + vita2d_font_text_width(font, 18, hint);
  // The settings menu uses the default 600x400 box, centred on the 960x544 screen
  int x = 480 + 300 - width;
  int y = 272 + 200 + 26;
  x += draw_button_glyph(BUTTON_GLYPH_TRIANGLE, x, y, glyph_size) + gap;
  vita2d_font_draw_text(font, x, y, 0xffffffff, 18, hint);
}

static int settings_loop(int id, void *context, const input_data *input) {
  menu_entry *menu = context;

  if ((input->buttons & SCE_CTRL_TRIANGLE) && (input->buttons & SCE_CTRL_HOLD) == 0) {
    // read_buttons() only reports newly pressed buttons, so check L and R are held directly
    SceCtrlData pad = {0};
    sceCtrlPeekBufferPositive(0, &pad, 1);
    if ((pad.buttons & SCE_CTRL_L1) && (pad.buttons & SCE_CTRL_R1)) {
      config.developer_mode = !config.developer_mode;
      ui_settings_save_config();
      display_error(config.developer_mode
                    ? "Developer options are now shown at the end of Settings."
                    : "Developer options are hidden.");
      return SETTINGS_REBUILD;
    }
    show_settings_help(menu, id);
    return 0;
  }

  bool confirm = (input->buttons & config.btn_confirm) && (input->buttons & SCE_CTRL_HOLD) == 0;
  if (id == SETTINGS_BENCH_RUN && confirm) {
    ui_settings_save_config();
    vitabench_run(false);
    return SETTINGS_REBUILD;
  }
  if (id == SETTINGS_DECODE_YUV && confirm) {
    config.decode_yuv = !config.decode_yuv;
    SET_ROW_VALUE(SETTINGS_VIEW_DECODE_YUV, config.decode_yuv ? "yes" : "no");
    return 0;
  }
  if (id == SETTINGS_HIDE_DEVELOPER && confirm) {
    config.developer_mode = false;
    ui_settings_save_config();
    return SETTINGS_REBUILD;
  }
  if (id == SETTINGS_BENCH_SECONDS) {
    static const int durations[] = {30, 60, 120, 300};
    const int count = sizeof(durations) / sizeof(durations[0]);
    bool left_pressed = (input->buttons & SCE_CTRL_LEFT) && (input->buttons & SCE_CTRL_HOLD) == 0;
    bool right_pressed = (input->buttons & SCE_CTRL_RIGHT) && (input->buttons & SCE_CTRL_HOLD) == 0;
    if (left_pressed || right_pressed || confirm) {
      int current = 0;
      for (int i = 0; i < count; i++) {
        if (durations[i] == config.bench_seconds) {
          current = i;
        }
      }
      current = (current + (left_pressed ? count - 1 : 1)) % count;
      config.bench_seconds = durations[current];
      char value[32];
      snprintf(value, sizeof(value), "%d s", config.bench_seconds);
      SET_ROW_VALUE(SETTINGS_VIEW_BENCH_SECONDS, value);
    }
    return 0;
  }
  bool did_change = 0;
  bool left = (input->buttons & SCE_CTRL_LEFT) && (input->buttons & SCE_CTRL_HOLD) == 0;
  bool right = (input->buttons & SCE_CTRL_RIGHT) && (input->buttons & SCE_CTRL_HOLD) == 0;

  char current[256];
  int new_idx;

  if (id == SETTINGS_TOUCH_MODE_SELECT) {
    if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
      static const char* mode_names[] = {"Off", "DS4 Touchpad", "Mouse Absolute", "Tablet (Sunshine)"};
      SET_ROW_VALUE(SETTINGS_VIEW_TOUCH_MODE_SELECT, mode_names[config.touchscreen_mode]);
      return 0;
    }
    config.touchscreen_mode = (config.touchscreen_mode + 1) % 4;
    static const char* mode_names[] = {"Off", "DS4 Touchpad", "Mouse Absolute", "Tablet (Sunshine)"};
    SET_ROW_VALUE(SETTINGS_VIEW_TOUCH_MODE_SELECT, mode_names[config.touchscreen_mode]);
    // Solo activar touchabsolute_enable en modo Mouse Absolute
    touchabsolute_enable(config.touchscreen_mode == 2);
    did_change = 1;
    return 0;
  }
  switch (id) {
    case SETTINGS_SWAP_SHOULDER_BUTTONS: {
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      swap_shoulder_buttons = !swap_shoulder_buttons;
      config.swap_shoulder_buttons = swap_shoulder_buttons;
      did_change = 1;
      SET_ROW_VALUE(SETTINGS_VIEW_SWAP_SHOULDER_BUTTONS, swap_shoulder_buttons ? "yes" : "no");
      // Exclusividad: desactivar mapping si swap está activo
      if (swap_shoulder_buttons) {
        if (config.mapping) {
          free(config.mapping);
          config.mapping = NULL;
          SET_ROW_VALUE(SETTINGS_VIEW_ENABLE_MAPPING, "no");
        }
      }
      break;
    }
    case SETTINGS_ENABLE_MAPPING: {
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      if (config.mapping) {
        free(config.mapping);
        config.mapping = NULL;
        SET_ROW_VALUE(SETTINGS_VIEW_ENABLE_MAPPING, "no");
      } else {
        config.mapping = strdup("mappings/vita.conf");
        SET_ROW_VALUE(SETTINGS_VIEW_ENABLE_MAPPING, "yes");
        // Si se activa mapping, desactivar swap
        swap_shoulder_buttons = 0;
        config.swap_shoulder_buttons = 0;
        SET_ROW_VALUE(SETTINGS_VIEW_SWAP_SHOULDER_BUTTONS, "no");
      }
      break;
    }
    // Eliminados SETTINGS_ABSOLUTE_MOUSE y SETTINGS_TOUCHSCREEN_MODE: ahora todo es por touchscreen_mode
  }


  if (!vitavideo_initialized() && !support_resolution_count) {
    for (int i = 0; i < MAX_RESOLUTION; i++) {
      SceVideodecQueryInitInfoHwAvcdec dec;
      dec.size = sizeof(SceVideodecQueryInitInfoHwAvcdec);
      dec.horizontal = VITA_DECODER_RESOLUTION(RESOLUTIONS[i][0]);
      dec.vertical = VITA_DECODER_RESOLUTION(RESOLUTIONS[i][1]);
      dec.numOfRefFrames = 5;
      dec.numOfStreams = 1;
      int ret = sceVideodecInitLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC, &dec);
      sceVideodecTermLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC);
      if (ret < 0) {
        // unsupported resolution
        continue;
      }
      support_resolutions[support_resolution_count] = calloc(10, sizeof(char));
      snprintf(support_resolutions[support_resolution_count], 10, "%dx%d", RESOLUTIONS[i][0], RESOLUTIONS[i][1]);
      vita_debug_log("res: %d\n", support_resolution_count);
      vita_debug_log("%s\n", support_resolutions[support_resolution_count]);
      support_resolution_idx[support_resolution_count++] = i;
    }
  }

  switch (id) {
    case SETTINGS_CONTROLLER_TYPE: {
      int idx = get_controller_type_index(config.controller_type);
      if (!left && !right) {
        // Solo refresca el subname
        char options[64];
        int current_idx = get_controller_type_index(config.controller_type);
        snprintf(options, sizeof(options), "%s", controller_type_names[current_idx]);
        SET_ROW_VALUE(SETTINGS_VIEW_CONTROLLER_TYPE, options);
        break;
      }
      if (left) {
        idx = (idx - 1 + CONTROLLER_TYPE_COUNT) % CONTROLLER_TYPE_COUNT;
        config.controller_type = controller_type_values[idx];
        did_change = 1;
      } else if (right) {
        idx = (idx + 1) % CONTROLLER_TYPE_COUNT;
        config.controller_type = controller_type_values[idx];
        did_change = 1;
      }
      // Actualiza el subname
      char options[64];
      int current_idx = get_controller_type_index(config.controller_type);
      snprintf(options, sizeof(options), "%s", controller_type_names[current_idx]);
      SET_ROW_VALUE(SETTINGS_VIEW_CONTROLLER_TYPE, options);
      // Guarda la configuración inmediatamente
      ui_settings_save_config();
      break;
    }
    case SETTINGS_RESOLUTION:
      if (!left && !right) {
        break;
      }
      if (vitavideo_initialized()) {
        break;
      }
      //char *resolutions[] = {"960x540", "960x544", "1280x540", "1280x720", "1920x1080"};
      sprintf(current, "%dx%d", config.stream.width, config.stream.height);

      new_idx = move_idx_in_array(support_resolutions, support_resolution_count, current, left ? -1 : +1);
      config.stream.width = RESOLUTIONS[support_resolution_idx[new_idx]][0];
      config.stream.height = RESOLUTIONS[support_resolution_idx[new_idx]][1];

      did_change = 1;
      break;
    case SETTINGS_FPS:
      if (!left && !right) {
          break;
      }
      char *settings[] = {"24", "30", "40", "50", "60"};
      sprintf(current, "%d", config.stream.fps);
      new_idx = _move_idx_in_array(settings, current, left ? -1 : +1);

      switch (new_idx) {
        case 0: config.stream.fps = 24; break; // Movies
        case 1: config.stream.fps = 30; break;
        case 2: config.stream.fps = 40; break;
        case 3: config.stream.fps = 50; break; // PAL
        case 4: config.stream.fps = 60; break; // NTSC
      }

      did_change = 1;
      break;
    case SETTINGS_BITRATE:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      {
        char value[512];
        int ret;
        if ((ret = ime_dialog_number(value, "Enter bitrate: ", "")) == 0) {
          int bitrate = atoi(value);
          if (bitrate) {
            config.stream.bitrate = bitrate;
            did_change = 1;
          } else {
            display_error("Incorrect bitrate entered: %s", value);
          }
        }
      }
      break;
    case SETTINGS_SOPS:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.sops = !config.sops;
      break;
    case SETTINGS_ENABLE_FRAME_INVAL:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.enable_ref_frame_invalidation = !config.enable_ref_frame_invalidation;
      break;
    case SETTINGS_ENABLE_STREAM_OPTIMIZE:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.stream.streamingRemotely = config.stream.streamingRemotely ? 0 : 1;
      break;
    case SETTINGS_ENABLE_VITA_VBLANK_WAIT:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.enable_vita_vblank_wait = config.enable_vita_vblank_wait ? 0 : 1;
      break;
    //Metalface--
    case SETTINGS_ENABLE_MOTION_CONTROLS:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
          break;
      }
      did_change = 1;
      config.enable_motion_controls = config.enable_motion_controls ? 0 : 1;
      break;
    case SETTINGS_MOTION_CONTROLS_SCALAR_X:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
          break;
      }
      {
        char value[512];
        int ret;
        if ((ret = ime_dialog_number(value, "Enter Motion Sensitvity Scalar X", "")) == 0) {
          float scalar = atof(value);
          if (scalar) {
            config.motion_controls_scalar_x = scalar;
            did_change = 1;
          } else {
            display_error("Incorrect scalar entered: %s", value);
          }
        }
        break;
      }
    case SETTINGS_MOTION_CONTROLS_SCALAR_Y:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
          break;
      }
      {
        char value[512];
        int ret;
        if ((ret = ime_dialog_number(value, "Enter Motion Sensitvity Scalar Y", "")) == 0) {
          float scalar = atof(value);
          if (scalar) {
            config.motion_controls_scalar_y = scalar;
            did_change = 1;
          } else {
            display_error("Incorrect scalar entered: %s", value);
          }
        }
      }
      break;
    case SETTINGS_ENABLE_DOUBLE_TAP_SPRINT:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
          break;
      }
      did_change = 1;
      config.enable_double_tap_sprint = config.enable_double_tap_sprint ? 0 : 1;
      break;
    case SETTINGS_DOUBLE_TAP_SPRINT_STEP_TIME:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
          break;
      }
      {
        char value[512];
        int ret;
        if ((ret = ime_dialog_number(value, "Enter Sprint Step Time in Milliseconds", "")) == 0) {
          int stp = atoi(value);
          if (stp) {
            config.double_tap_sprint_step_time = stp;
            did_change = 1;
          } else {
            display_error("Incorrect step time entered: %s", value);
          }
        }
      }
      break;
    //Metalface--
    case SETTINGS_SAVE_DEBUG_LOG:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.save_debug_log = !config.save_debug_log;
      break;
    case SETTINGS_DISABLE_POWERSAVE:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.disable_powersave = !config.disable_powersave;
      break;
    case SETTINGS_BOOST_CLOCKS:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.boost_clocks = !config.boost_clocks;
      break;
    case SETTINGS_JP_LAYOUT:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.jp_layout = !config.jp_layout;
      break;
    case SETTINGS_SHOW_FPS:
      // Left/right or confirm cycles off -> fps -> full
      if (left) {
        config.stats_overlay = (config.stats_overlay + STATS_OVERLAY_COUNT - 1) % STATS_OVERLAY_COUNT;
      } else if (right || ((input->buttons & config.btn_confirm) && (input->buttons & SCE_CTRL_HOLD) == 0)) {
        config.stats_overlay = (config.stats_overlay + 1) % STATS_OVERLAY_COUNT;
      } else {
        break;
      }
      did_change = 1;
      break;
    case SETTINGS_LOCAL_AUDIO:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.localaudio = !config.localaudio;
      break;
    case SETTINGS_ENABLE_FRAME_PACER:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.enable_frame_pacer = !config.enable_frame_pacer;
      break;
    case SETTINGS_DIRECT_DECODE:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.direct_decode = !config.direct_decode;
      break;
    case SETTINGS_INPUT_SYNC:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.input_sync_to_controller = !config.input_sync_to_controller;
      break;
    case SETTINGS_PACKET_SIZE: {
      // 1024 is the safe default; 1392 is the largest that fits a 1500-byte MTU
      static const int packet_sizes[] = {1024, 1392};
      const int packet_size_count = sizeof(packet_sizes) / sizeof(packet_sizes[0]);
      int step = 0;
      if (left) {
        step = -1;
      } else if (right || ((input->buttons & config.btn_confirm) && (input->buttons & SCE_CTRL_HOLD) == 0)) {
        step = 1;
      } else {
        break;
      }
      int current_idx = -1;
      for (int i = 0; i < packet_size_count; i++) {
        if (packet_sizes[i] == config.stream.packetSize) {
          current_idx = i;
        }
      }
      // A custom value from the config file moves to the first preset
      int next_idx = current_idx < 0 ? 0 : (current_idx + step + packet_size_count) % packet_size_count;
      config.stream.packetSize = packet_sizes[next_idx];
      did_change = 1;
      break;
    }
    case SETTINGS_CENTER_REGION_ONLY:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      did_change = 1;
      config.center_region_only = !config.center_region_only;
      break;
    // ...eliminado duplicado, la lógica completa está arriba...
    case SETTINGS_BACK_DEADZONE:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      deadzone_settings_menu();
      did_change = 1;
      break;
    case SETTINGS_SPECIAL_KEYS:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      special_keys_menu();
      break;
    case SETTINGS_ENABLE_SPECIAL_KEYS:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }

      config.enable_front_touchzones = !config.enable_front_touchzones;
      did_change = 1;
      break;
    case SETTINGS_ENABLE_PSBUTTON_CAPTURE:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }

      config.enable_psbutton_capture = !config.enable_psbutton_capture;
      did_change = 1;
      break;
    case SETTINGS_MOUSE_ACCEL:
      left = input->buttons & SCE_CTRL_LEFT;
      right = input->buttons & SCE_CTRL_RIGHT;
      if (!left && !right) {
          break;
      }
      if (left) {
        config.mouse_acceleration -= 15;
        if (config.mouse_acceleration < 0) {
          config.mouse_acceleration = 0;
        }
      } else {
        config.mouse_acceleration += 15;
        if (config.mouse_acceleration > 300) {
          config.mouse_acceleration = 300;
        }
      }

      did_change = 1;
      break;
    case SETTINGS_KEYBOARD_LAYOUT:
      if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
        break;
      }
      keyboard_layout_menu();
      did_change = 1;
      break;
    // Eliminados duplicados de SETTINGS_ABSOLUTE_MOUSE y SETTINGS_TOUCHSCREEN_MODE

  }

  if (!did_change && !settings_loop_setup) {
    return 0;
  }
  settings_loop_setup = 0;

#define MENU_REPLACE(ID, MESSAGE) SET_ROW_VALUE((ID), (MESSAGE))

  sprintf(current, "%dx%d", config.stream.width, config.stream.height);
  MENU_REPLACE(SETTINGS_VIEW_RESOLUTION, current);

  sprintf(current, "%d", config.stream.fps);
  MENU_REPLACE(SETTINGS_VIEW_FPS, current);

  sprintf(current, "%d", config.stream.bitrate);
  MENU_REPLACE(SETTINGS_VIEW_BITRATE, current);

  sprintf(current, "%s", config.sops ? "yes" : "no");
  MENU_REPLACE(SETTINGS_VIEW_SOPS, current);

  sprintf(current, "%s", config.enable_ref_frame_invalidation ? "yes" : "no");
  MENU_REPLACE(SETTINGS_VIEW_ENABLE_FRAME_INVAL, current);

  sprintf(current, "%s", config.stream.streamingRemotely ? "yes" : "no");
  MENU_REPLACE(SETTINGS_VIEW_ENABLE_STREAM_OPTIMIZE, current);

  sprintf(current, "%s", config.enable_vita_vblank_wait ? "yes" : "no");
  MENU_REPLACE(SETTINGS_VIEW_ENABLE_VITA_VBLANK_WAIT, current);

  //Metalface
  sprintf(current, "%s", config.enable_motion_controls ? "yes" : "no");
  MENU_REPLACE(SETTINGS_VIEW_ENABLE_MOTION_CONTROLS, current);

  sprintf(current, "%f", config.motion_controls_scalar_x);
  MENU_REPLACE(SETTINGS_VIEW_MOTION_CONTROLS_SCALAR_X, current);

  sprintf(current, "%f", config.motion_controls_scalar_y);
  MENU_REPLACE(SETTINGS_VIEW_MOTION_CONTROLS_SCALAR_Y, current);

  sprintf(current, "%s", config.enable_double_tap_sprint ? "yes" : "no");
  MENU_REPLACE(SETTINGS_VIEW_ENABLE_DOUBLE_TAP_SPRINT, current);

  sprintf(current, "%u", config.double_tap_sprint_step_time);
  MENU_REPLACE(SETTINGS_VIEW_DOUBLE_TAP_SPRINT_STEP_TIME, current);
  //Metalface

  sprintf(current, "%s", config.disable_powersave ? "yes" : "no");
  MENU_REPLACE(SETTINGS_VIEW_DISABLE_POWERSAVE, current);

  sprintf(current, "%s", config.boost_clocks ? "yes" : "no");
  MENU_REPLACE(SETTINGS_VIEW_BOOST_CLOCKS, current);

  sprintf(current, "%s", config.jp_layout ? "yes" : "no");
  MENU_REPLACE(SETTINGS_VIEW_JP_LAYOUT, current);

  static const char *stats_overlay_names[STATS_OVERLAY_COUNT] = {"off", "fps", "full"};
  sprintf(current, "%s", stats_overlay_names[config.stats_overlay]);
  MENU_REPLACE(SETTINGS_VIEW_SHOW_FPS, current);

  sprintf(current, "%s", config.localaudio ? "yes" : "no");
  MENU_REPLACE(SETTINGS_VIEW_LOCAL_AUDIO, current);

  sprintf(current, "%s", config.enable_frame_pacer ? "yes" : "no");
  MENU_REPLACE(SETTINGS_VIEW_ENABLE_FRAME_PACER, current);

  sprintf(current, "%s", config.direct_decode ? "yes" : "no");
  MENU_REPLACE(SETTINGS_VIEW_DIRECT_DECODE, current);

  sprintf(current, "%s", config.decode_yuv ? "yes" : "no");
  MENU_REPLACE(SETTINGS_VIEW_DECODE_YUV, current);

  sprintf(current, "%s", config.input_sync_to_controller ? "yes" : "no");
  MENU_REPLACE(SETTINGS_VIEW_INPUT_SYNC, current);

  sprintf(current, "%d bytes", config.stream.packetSize);
  MENU_REPLACE(SETTINGS_VIEW_PACKET_SIZE, current);

  sprintf(current, "%s", config.center_region_only ? "yes" : "no");
  MENU_REPLACE(SETTINGS_VIEW_CENTER_REGION_ONLY, current);

  sprintf(current, "%s", config.save_debug_log ? "yes" : "no");
  MENU_REPLACE(SETTINGS_VIEW_SAVE_DEBUG_LOG, current);

  sprintf(current, "%s", config.enable_psbutton_capture ? "yes" : "no");
  MENU_REPLACE(SETTINGS_VIEW_ENABLE_PSBUTTON_CAPTURE, current);

  sprintf(current, "%s", config.enable_front_touchzones ? "yes" : "no");
  MENU_REPLACE(SETTINGS_VIEW_ENABLE_SPECIAL_KEYS, current);

  sprintf(current, "%s", config.mapping != 0 ? "yes" : "no");
  MENU_REPLACE(SETTINGS_VIEW_ENABLE_MAPPING, current);

  sprintf(current, "%dpx,%dpx,%dpx,%dpx",
          config.back_deadzone.top,
          config.back_deadzone.right,
          config.back_deadzone.bottom,
          config.back_deadzone.left);
  MENU_REPLACE(SETTINGS_VIEW_BACK_DEADZONE, current);

  sprintf(current, "%d", config.mouse_acceleration);
  MENU_REPLACE(SETTINGS_VIEW_MOUSE_ACCEL, current);

  // Eliminar subnames antiguos de modos táctiles
  static const char* mode_names[] = {"Off", "DS4 Touchpad", "Mouse Absolute", "Tablet (Sunshine)"};
  SET_ROW_VALUE(SETTINGS_VIEW_TOUCH_MODE_SELECT, mode_names[config.touchscreen_mode]);
  // No actualizar subnames de absolute_mouse ni touchscreen_mode boolean
  // ...resto de actualizaciones de subname...
  return 0;
}

static int settings_back(void *context) {
  ui_settings_save_config();
  update_layout();
  return 0;
}

// --- OPCIÓN DE LAYOUT DE TECLADO EN UI SETTINGS ---
#include "../input/keyboardkeys.h"
#include "ui_keyboard.h"

// --- HOTKEYS MENU (UI) ---
// Eliminado: hotkeys_menu y referencias, ya que los atajos ahora son fijos

int ui_settings_menu() {
  // Must hold every MENU_ENTRY, MENU_CATEGORY and MENU_MESSAGE below (40 at most today). Each
  // entry is about 1 KB of the main thread stack, so do not oversize it
  menu_entry menu[44];
  int idx;
rebuild:
  idx = 0;
  // Rows are re-numbered on every build; 0 means the setting has no row
  memset(SETTINGS_VIEW_IDX, 0, sizeof(SETTINGS_VIEW_IDX));
  // Mid-stream, only list settings that take effect on resume. The rest (such as
  // resolution, bitrate or controller type) are agreed with the PC when a stream
  // starts, so they come back once it ends.
  int stream_status = connection_get_status();
  bool streaming = stream_status == LI_CONNECTED || stream_status == LI_MINIMIZED;
#define MENU_CATEGORY(NAME) \
  do { \
    menu[idx] = (menu_entry) { .name = (NAME), .disabled = true, .separator = true }; \
    idx++; \
  } while (0)
#define MENU_ENTRY(ID, TAG, NAME, SUFFIX) \
  do { \
    menu[idx] = (menu_entry) { .name = (NAME), .id = (ID), .suffix = (SUFFIX) }; \
    SETTINGS_VIEW_IDX[(TAG)] = idx; \
    idx++; \
  } while(0)
#define MENU_ENTRY_BETWEEN_STREAMS(ID, TAG, NAME, SUFFIX) \
  do { \
    if (!streaming) { \
      MENU_ENTRY((ID), (TAG), (NAME), (SUFFIX)); \
    } \
  } while (0)
#define MENU_MESSAGE(MESSAGE) \
  do { \
    menu[idx] = (menu_entry) { .name = "", .disabled = true, .subname = (MESSAGE) }; \
    idx++; \
  } while(0)

  if (streaming) {
    MENU_MESSAGE("Settings that need a new stream return once this one ends.");
  }
  MENU_CATEGORY("Stream");
  MENU_ENTRY_BETWEEN_STREAMS(SETTINGS_RESOLUTION, SETTINGS_VIEW_RESOLUTION, "Resolution", ICON_LEFT_RIGHT_ARROWS);
  MENU_ENTRY_BETWEEN_STREAMS(SETTINGS_FPS, SETTINGS_VIEW_FPS, "FPS", ICON_LEFT_RIGHT_ARROWS);
  MENU_ENTRY_BETWEEN_STREAMS(SETTINGS_BITRATE, SETTINGS_VIEW_BITRATE, "Bitrate", "");
  MENU_ENTRY_BETWEEN_STREAMS(SETTINGS_SOPS, SETTINGS_VIEW_SOPS, "Change graphical game settings for performance", "");
  MENU_ENTRY_BETWEEN_STREAMS(SETTINGS_ENABLE_FRAME_INVAL, SETTINGS_VIEW_ENABLE_FRAME_INVAL, "Enable reference frame invalidation", "");
  MENU_ENTRY_BETWEEN_STREAMS(SETTINGS_ENABLE_STREAM_OPTIMIZE, SETTINGS_VIEW_ENABLE_STREAM_OPTIMIZE, "Enable stream optimization", "");
  MENU_ENTRY(SETTINGS_ENABLE_VITA_VBLANK_WAIT, SETTINGS_VIEW_ENABLE_VITA_VBLANK_WAIT, "Enable VITA vblank", "");
  MENU_ENTRY_BETWEEN_STREAMS(SETTINGS_CENTER_REGION_ONLY, SETTINGS_VIEW_CENTER_REGION_ONLY, "Fill screen (crop edges)", "");


  MENU_ENTRY(SETTINGS_ENABLE_FRAME_PACER, SETTINGS_VIEW_ENABLE_FRAME_PACER, "Enable frame pacer", "");
  MENU_ENTRY_BETWEEN_STREAMS(SETTINGS_PACKET_SIZE, SETTINGS_VIEW_PACKET_SIZE, "Video packet size", ICON_LEFT_RIGHT_ARROWS);
  MENU_ENTRY_BETWEEN_STREAMS(SETTINGS_LOCAL_AUDIO, SETTINGS_VIEW_LOCAL_AUDIO, "Enable local audio", "");

  MENU_CATEGORY("System");
  MENU_ENTRY(SETTINGS_SAVE_DEBUG_LOG, SETTINGS_VIEW_SAVE_DEBUG_LOG, "Enable debug log", "");
  MENU_ENTRY(SETTINGS_DISABLE_POWERSAVE, SETTINGS_VIEW_DISABLE_POWERSAVE, "Disable power save", "");
  MENU_ENTRY(SETTINGS_BOOST_CLOCKS, SETTINGS_VIEW_BOOST_CLOCKS, "Boost CPU/GPU clocks while streaming", "");
  MENU_ENTRY(SETTINGS_JP_LAYOUT, SETTINGS_VIEW_JP_LAYOUT, "Swap X & O for Moonlight", "");
  MENU_ENTRY(SETTINGS_SHOW_FPS, SETTINGS_VIEW_SHOW_FPS, "Stats overlay", ICON_LEFT_RIGHT_ARROWS);

  MENU_CATEGORY("Input");

  MENU_ENTRY(SETTINGS_INPUT_SYNC, SETTINGS_VIEW_INPUT_SYNC, "Sync input to controller sampling", "");
  MENU_ENTRY_BETWEEN_STREAMS(SETTINGS_ENABLE_MOTION_CONTROLS, SETTINGS_VIEW_ENABLE_MOTION_CONTROLS, "Enable Gyroscope reporting", "");
  MENU_ENTRY(SETTINGS_ENABLE_DOUBLE_TAP_SPRINT, SETTINGS_VIEW_ENABLE_DOUBLE_TAP_SPRINT, "Enable double tap to sprint", "");
  MENU_ENTRY(SETTINGS_DOUBLE_TAP_SPRINT_STEP_TIME, SETTINGS_VIEW_DOUBLE_TAP_SPRINT_STEP_TIME, "Sprint double tap time", "");
  MENU_ENTRY_BETWEEN_STREAMS(SETTINGS_CONTROLLER_TYPE, SETTINGS_VIEW_CONTROLLER_TYPE, "Controller type", ICON_LEFT_RIGHT_ARROWS);
  MENU_ENTRY(SETTINGS_SWAP_SHOULDER_BUTTONS, SETTINGS_VIEW_SWAP_SHOULDER_BUTTONS, "Swap R1/L1 <-> R2/L2", "");
  MENU_ENTRY(SETTINGS_MOUSE_ACCEL, SETTINGS_VIEW_MOUSE_ACCEL, "Mouse acceleration", ICON_LEFT_RIGHT_ARROWS);
  MENU_ENTRY(SETTINGS_ENABLE_MAPPING, SETTINGS_VIEW_ENABLE_MAPPING, "Enable mapping file", "");
  MENU_ENTRY(SETTINGS_ENABLE_PSBUTTON_CAPTURE, SETTINGS_VIEW_ENABLE_PSBUTTON_CAPTURE, "Enable PS button capture", "");
  MENU_ENTRY(SETTINGS_BACK_DEADZONE, SETTINGS_VIEW_BACK_DEADZONE, "Back touchscreen deadzone", "");
  MENU_ENTRY(SETTINGS_ENABLE_SPECIAL_KEYS, SETTINGS_VIEW_ENABLE_SPECIAL_KEYS, "Enable touchscreen special keys", "");
  MENU_ENTRY(SETTINGS_SPECIAL_KEYS, SETTINGS_VIEW_SPECIAL_KEYS, "Touchscreen special keys", "");
  // NUEVO: Opción para usar la pantalla táctil como touchpad DS4
  MENU_ENTRY(SETTINGS_TOUCH_MODE_SELECT, SETTINGS_VIEW_TOUCH_MODE_SELECT, "Touchscreen mode", "");
  MENU_CATEGORY("Keyboard");
  MENU_ENTRY(SETTINGS_KEYBOARD_LAYOUT, SETTINGS_VIEW_KEYBOARD_LAYOUT, "Keyboard layout", "");

  // Shown after holding L and R and pressing triangle
  char bench_target[256] = "";
  if (config.developer_mode) {
    MENU_CATEGORY("Developer");
    if (streaming) {
      MENU_MESSAGE("The benchmark can't run during a stream.");
    } else if (!ui_connect_server_ready()) {
      MENU_MESSAGE("To benchmark, open Settings from a connected computer's menu.");
    } else {
      char app_name[128];
      if (ui_connect_find_bench_app(config.bench_app, app_name, sizeof(app_name)) >= 0) {
        snprintf(bench_target, sizeof(bench_target), "%s on %s", app_name, ui_connect_server_name());
      } else {
        snprintf(bench_target, sizeof(bench_target), "no benchmark app on %s", ui_connect_server_name());
      }
      MENU_ENTRY(SETTINGS_BENCH_RUN, SETTINGS_VIEW_BENCH_RUN, "Run benchmark", "");
      MENU_ENTRY(SETTINGS_BENCH_SECONDS, SETTINGS_VIEW_BENCH_SECONDS, "Benchmark duration", ICON_LEFT_RIGHT_ARROWS);
    }
    MENU_ENTRY_BETWEEN_STREAMS(SETTINGS_DIRECT_DECODE, SETTINGS_VIEW_DIRECT_DECODE, "Decode on network thread (legacy)", "");
    MENU_ENTRY_BETWEEN_STREAMS(SETTINGS_DECODE_YUV, SETTINGS_VIEW_DECODE_YUV, "Decode to YUV (experimental)", "");
    MENU_ENTRY(SETTINGS_HIDE_DEVELOPER, SETTINGS_VIEW_HIDE_DEVELOPER, "Hide developer options", "");
  }

  // Inicializar el subname de todas las opciones antes de mostrar el menú
  char current[256];
  // Resolution
  sprintf(current, "%dx%d", config.stream.width, config.stream.height);
  SET_ROW_VALUE(SETTINGS_VIEW_RESOLUTION, current);
  // FPS
  sprintf(current, "%d", config.stream.fps);
  SET_ROW_VALUE(SETTINGS_VIEW_FPS, current);
  // Bitrate
  sprintf(current, "%d", config.stream.bitrate);
  SET_ROW_VALUE(SETTINGS_VIEW_BITRATE, current);
  // Controller type
  int current_idx = get_controller_type_index(config.controller_type);
  snprintf(current, sizeof(current), "%s", controller_type_names[current_idx]);
  SET_ROW_VALUE(SETTINGS_VIEW_CONTROLLER_TYPE, current);
  // Swap shoulder buttons
  swap_shoulder_buttons = config.swap_shoulder_buttons;
  SET_ROW_VALUE(SETTINGS_VIEW_SWAP_SHOULDER_BUTTONS, swap_shoulder_buttons ? "yes" : "no");
  // Touchscreen mode
  static const char* mode_names[] = {"Off", "DS4 Touchpad", "Mouse Absolute", "Tablet (Sunshine)"};
  SET_ROW_VALUE(SETTINGS_VIEW_TOUCH_MODE_SELECT, mode_names[config.touchscreen_mode]);
  // Puedes agregar aquí más inicializaciones si quieres que otras opciones también muestren su valor actual al abrir el menú

  settings_loop_setup = 1;
  assert(idx <= (int)(sizeof(menu) / sizeof(menu[0])));

  if (bench_target[0]) {
    SET_ROW_VALUE(SETTINGS_VIEW_BENCH_RUN, bench_target);
    char seconds[32];
    snprintf(seconds, sizeof(seconds), "%d s", config.bench_seconds);
    SET_ROW_VALUE(SETTINGS_VIEW_BENCH_SECONDS, seconds);
  }

  settings_menu_count = idx;
  int ret = display_menu(menu, idx, NULL, &settings_loop, &settings_back, &settings_draw, &menu);
  if (ret == SETTINGS_REBUILD) {
    settings_loop_setup = 1;
    goto rebuild;
  }
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
  vita_debug_log("  stream.bitrate = %d", config.stream.bitrate);
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

