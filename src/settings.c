#include "settings.h"

#include "config.h"
#include "connection.h"
#include "stats.h"
#include "util.h"
#include "video/vita.h"
#include "input/touchabsolute.h"
#include "input/swap_shoulder_buttons.h"
#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <psp2/videodec.h>
#include <Limelight.h>

extern char *strdup(const char *);

// Read by the input code, which swaps the shoulder buttons while it's set
bool swap_shoulder_buttons = false;

const str_id SETTINGS_TAB_NAMES[SETTINGS_TAB_COUNT] = {
  STR_TAB_STREAM, STR_TAB_SYSTEM, STR_TAB_CONTROLS, STR_TAB_DEVELOPER, STR_TAB_ABOUT,
};

bool settings_tab_shown(int tab) {
  return tab != SETTINGS_TAB_DEVELOPER || config.developer_mode;
}

bool settings_streaming(void) {
  int status = connection_get_status();
  return status == LI_CONNECTED || status == LI_MINIMIZED;
}

/*
 * Toggles on a config field
 */

#define TOGGLE(NAME, FIELD) \
  static bool NAME##_on(void) { return (FIELD) != 0; } \
  static bool NAME##_flip(int step) { (void)step; FIELD = !(FIELD); return true; }

TOGGLE(sops, config.sops)
TOGGLE(frame_inval, config.enable_ref_frame_invalidation)
TOGGLE(stream_optimize, config.stream.streamingRemotely)
TOGGLE(vblank_wait, config.enable_vita_vblank_wait)
TOGGLE(fill_screen, config.center_region_only)
TOGGLE(frame_pacer, config.enable_frame_pacer)
TOGGLE(decode_yuv, config.decode_yuv)
TOGGLE(local_audio, config.localaudio)
TOGGLE(debug_log, config.save_debug_log)
TOGGLE(powersave, config.disable_powersave)
TOGGLE(boost_clocks, config.boost_clocks)
TOGGLE(jp_layout, config.jp_layout)
TOGGLE(input_sync, config.input_sync_to_controller)
TOGGLE(motion, config.enable_motion_controls)
TOGGLE(sprint, config.enable_double_tap_sprint)
TOGGLE(psbutton, config.enable_psbutton_capture)
TOGGLE(touch_keys, config.enable_front_touchzones)
TOGGLE(direct_decode, config.direct_decode)

/*
 * Resolution and frame rate
 */

#define MAX_RESOLUTION 9
static const int RESOLUTIONS[MAX_RESOLUTION][2] = {
  {960, 540},   // 16:9, QHD
  {960, 544},   // VITA original
  {1024, 576},  // 16:9
  {1152, 648},  // 16:9
  {1280, 540},  // 21:9
  {1280, 720},  // 16:9, 720p HD
  {1366, 768},  // 16:9, WXGA
  {1600, 900},  // 16:9, 900p HD+
  {1920, 1080}, // 16:9, FHD
};

// The resolutions this Vita's decoder accepts, found the first time they're needed
static int supported_resolutions[MAX_RESOLUTION];
static int supported_resolution_count = 0;

static void find_supported_resolutions(void) {
  if (supported_resolution_count > 0 || vitavideo_initialized()) {
    return;
  }
  for (int i = 0; i < MAX_RESOLUTION; i++) {
    SceVideodecQueryInitInfoHwAvcdec dec;
    dec.size = sizeof(SceVideodecQueryInitInfoHwAvcdec);
    dec.horizontal = VITA_DECODER_RESOLUTION(RESOLUTIONS[i][0]);
    dec.vertical = VITA_DECODER_RESOLUTION(RESOLUTIONS[i][1]);
    dec.numOfRefFrames = 5;
    dec.numOfStreams = 1;
    int ret = sceVideodecInitLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC, &dec);
    sceVideodecTermLibrary(SCE_VIDEODEC_TYPE_HW_AVCDEC);
    if (ret >= 0) {
      supported_resolutions[supported_resolution_count++] = i;
    }
  }
}

// Moves from the option at current by step, stopping at either end. An
// option that isn't in the list moves to the last one.
static int step_clamped(int current, int count, int step) {
  if (current < 0) {
    return count - 1;
  }
  int next = current + step;
  return next < 0 ? 0 : (next >= count ? count - 1 : next);
}

static void resolution_value(char *out, size_t size) {
  snprintf(out, size, "%dx%d", config.stream.width, config.stream.height);
}

static bool resolution_change(int step) {
  // The decoder is set up while a stream runs, so it can't be queried then
  if (vitavideo_initialized()) {
    return false;
  }
  find_supported_resolutions();
  if (supported_resolution_count == 0) {
    return false;
  }
  int current = -1;
  for (int i = 0; i < supported_resolution_count; i++) {
    const int *res = RESOLUTIONS[supported_resolutions[i]];
    if (res[0] == config.stream.width && res[1] == config.stream.height) {
      current = i;
    }
  }
  const int *res = RESOLUTIONS[supported_resolutions[step_clamped(current, supported_resolution_count, step)]];
  config.stream.width = res[0];
  config.stream.height = res[1];
  return true;
}

static const int FRAME_RATES[] = {24, 30, 40, 50, 60};
#define FRAME_RATE_COUNT ((int)(sizeof(FRAME_RATES) / sizeof(FRAME_RATES[0])))

static void fps_value(char *out, size_t size) {
  snprintf(out, size, "%d", config.stream.fps);
}

static bool fps_change(int step) {
  int current = -1;
  for (int i = 0; i < FRAME_RATE_COUNT; i++) {
    if (FRAME_RATES[i] == config.stream.fps) {
      current = i;
    }
  }
  config.stream.fps = FRAME_RATES[step_clamped(current, FRAME_RATE_COUNT, step)];
  return true;
}

/*
 * Bitrate
 */

// The Bitrate row's steps, in kbps, then Custom, which enables the Custom
// bitrate row for any value in kbps, as the setting has always been entered.
// A saved value that isn't a step opens on Custom unchanged, so upgrading
// never alters anyone's bitrate.
static const int BITRATE_STEPS_KBPS[] = {5000, 10000, 15000, 20000, 25000, 30000};
#define BITRATE_STEP_COUNT ((int)(sizeof(BITRATE_STEPS_KBPS) / sizeof(BITRATE_STEPS_KBPS[0])))
static bool bitrate_custom = false;

static int bitrate_step_index(int kbps) {
  for (int i = 0; i < BITRATE_STEP_COUNT; i++) {
    if (BITRATE_STEPS_KBPS[i] == kbps) {
      return i;
    }
  }
  return -1;
}

void settings_sync_bitrate(void) {
  bitrate_custom = bitrate_step_index(config.stream.bitrate) < 0;
}

static void bitrate_value(char *out, size_t size) {
  int kbps = config.stream.bitrate;
  char number[16];
  if (bitrate_custom) {
    snprintf(out, size, "%s", tr(STR_SET_VALUE_CUSTOM));
    return;
  } else if (kbps % 1000 == 0) {
    snprintf(number, sizeof(number), "%d", kbps / 1000);
  } else {
    snprintf(number, sizeof(number), "%.1f", kbps / 1000.0f);
  }
  TR_FMT(out, size, STR_UNIT_MBPS, number);
}

static bool bitrate_change(int step) {
  // The steps, then Custom as the last position
  int position = bitrate_custom ? BITRATE_STEP_COUNT : bitrate_step_index(config.stream.bitrate);
  if (position < 0) {
    position = BITRATE_STEP_COUNT;
  }
  position = (position + (step < 0 ? BITRATE_STEP_COUNT : 1)) % (BITRATE_STEP_COUNT + 1);
  bitrate_custom = position == BITRATE_STEP_COUNT;
  if (!bitrate_custom) {
    config.stream.bitrate = BITRATE_STEPS_KBPS[position];
  }
  return true;
}

static bool bitrate_is_custom(void) {
  return bitrate_custom;
}

static void custom_bitrate_value(char *out, size_t size) {
  char number[16];
  snprintf(number, sizeof(number), "%d", config.stream.bitrate);
  TR_FMT(out, size, STR_UNIT_KBPS, number);
}

/*
 * Other choices
 */

static const int PACKET_SIZES[] = {1024, 1392};
#define PACKET_SIZE_COUNT ((int)(sizeof(PACKET_SIZES) / sizeof(PACKET_SIZES[0])))

static void packet_size_value(char *out, size_t size) {
  char number[16];
  snprintf(number, sizeof(number), "%d", config.stream.packetSize);
  TR_FMT(out, size, STR_UNIT_BYTES, number);
}

static bool packet_size_change(int step) {
  // 1024 is the safe default; 1392 is the largest that fits a 1500-byte MTU
  int current = -1;
  for (int i = 0; i < PACKET_SIZE_COUNT; i++) {
    if (PACKET_SIZES[i] == config.stream.packetSize) {
      current = i;
    }
  }
  // A custom value from the config file moves to the first preset
  int next = current < 0 ? 0 : (current + step + PACKET_SIZE_COUNT) % PACKET_SIZE_COUNT;
  config.stream.packetSize = PACKET_SIZES[next];
  return true;
}

static void stats_overlay_value(char *out, size_t size) {
  static const str_id names[STATS_OVERLAY_COUNT] = {STR_STATS_OFF, STR_STATS_FPS, STR_STATS_FULL};
  snprintf(out, size, "%s", tr(names[config.stats_overlay]));
}

static bool stats_overlay_change(int step) {
  config.stats_overlay = (config.stats_overlay + STATS_OVERLAY_COUNT + step) % STATS_OVERLAY_COUNT;
  return true;
}

static void app_list_value(char *out, size_t size) {
  snprintf(out, size, "%s", tr(config.app_list_style == APP_LIST_TEXT ? STR_APP_LIST_TEXT : STR_APP_LIST_BOXART));
}

static bool app_list_change(int step) {
  config.app_list_style = (config.app_list_style + APP_LIST_STYLE_COUNT + step) % APP_LIST_STYLE_COUNT;
  return true;
}

// Product names, the same in every language
static const char *const CONTROLLER_TYPE_NAMES[] = {"Xbox", "PlayStation"};
static const int CONTROLLER_TYPE_VALUES[] = {1, 2};
#define CONTROLLER_TYPE_COUNT 2

static int controller_type_index(void) {
  for (int i = 0; i < CONTROLLER_TYPE_COUNT; i++) {
    if (CONTROLLER_TYPE_VALUES[i] == config.controller_type) {
      return i;
    }
  }
  return 1; // PlayStation if not found
}

static void controller_type_value(char *out, size_t size) {
  snprintf(out, size, "%s", CONTROLLER_TYPE_NAMES[controller_type_index()]);
}

static bool controller_type_change(int step) {
  int index = (controller_type_index() + CONTROLLER_TYPE_COUNT + step) % CONTROLLER_TYPE_COUNT;
  config.controller_type = CONTROLLER_TYPE_VALUES[index];
  return true;
}

static void mouse_accel_value(char *out, size_t size) {
  snprintf(out, size, "%d", config.mouse_acceleration);
}

static bool mouse_accel_change(int step) {
  int value = config.mouse_acceleration + step * 15;
  config.mouse_acceleration = value < 0 ? 0 : (value > 300 ? 300 : value);
  return true;
}

static const str_id TOUCH_MODE_NAMES[] = {STR_TOUCH_OFF, STR_TOUCH_DS4, STR_TOUCH_MOUSE_ABSOLUTE, STR_TOUCH_TABLET};
#define TOUCH_MODE_COUNT 4

static void touch_mode_value(char *out, size_t size) {
  int mode = config.touchscreen_mode;
  snprintf(out, size, "%s", mode >= 0 && mode < TOUCH_MODE_COUNT ? tr(TOUCH_MODE_NAMES[mode]) : "");
}

static bool touch_mode_change(int step) {
  (void)step;
  config.touchscreen_mode = (config.touchscreen_mode + 1) % TOUCH_MODE_COUNT;
  // Absolute touch only runs in Mouse Absolute mode
  touchabsolute_enable(config.touchscreen_mode == 2);
  return true;
}

/*
 * Shoulder buttons and the mapping file, which can't both be on
 */

static bool swap_shoulders_on(void) {
  return config.swap_shoulder_buttons;
}

static bool swap_shoulders_flip(int step) {
  (void)step;
  config.swap_shoulder_buttons = !config.swap_shoulder_buttons;
  swap_shoulder_buttons = config.swap_shoulder_buttons;
  if (config.swap_shoulder_buttons && config.mapping) {
    free(config.mapping);
    config.mapping = NULL;
  }
  return true;
}

static bool mapping_on(void) {
  return config.mapping != NULL;
}

static bool mapping_flip(int step) {
  (void)step;
  if (config.mapping) {
    free(config.mapping);
    config.mapping = NULL;
  } else {
    config.mapping = strdup("mappings/vita.conf");
    config.swap_shoulder_buttons = false;
    swap_shoulder_buttons = false;
  }
  return true;
}

static void sprint_time_value(char *out, size_t size) {
  snprintf(out, size, "%u", config.double_tap_sprint_step_time);
}

static void deadzone_value(char *out, size_t size) {
  int edges[4] = {config.back_deadzone.top, config.back_deadzone.right, config.back_deadzone.bottom,
                  config.back_deadzone.left};
  size_t used = 0;
  out[0] = '\0';
  for (int i = 0; i < 4 && used < size; i++) {
    char number[16];
    snprintf(number, sizeof(number), "%d", edges[i]);
    if (i > 0) {
      used += snprintf(out + used, size - used, ",");
    }
    if (used < size) {
      used += TR_FMT(out + used, size - used, STR_UNIT_PIXELS, number);
    }
  }
}

/*
 * Help with a path in it, which depends on where the config lives
 */

static void debug_log_help(char *out, size_t size) {
  TR_FMT(out, size, STR_SET_SAVE_DEBUG_LOG_HELP, config.key_dir);
}

static void mapping_help(char *out, size_t size) {
  TR_FMT(out, size, STR_SET_ENABLE_MAPPING_HELP, config.key_dir);
}

/*
 * The benchmark, against the computer Settings was opened from
 */

static bool bench_not_ready(void) {
  return !settings_streaming() && !session_server_ready();
}

static bool bench_ready(void) {
  return !settings_streaming() && session_server_ready();
}

static void bench_target_value(char *out, size_t size) {
  char app_name[128];
  if (session_find_bench_app(config.bench_app, app_name, sizeof(app_name)) >= 0) {
    TR_FMT(out, size, STR_SET_BENCH_TARGET, app_name, session_server_name());
  } else {
    TR_FMT(out, size, STR_SET_BENCH_NO_APP, session_server_name());
  }
}

static const int BENCH_DURATIONS[] = {30, 60, 120, 300};
#define BENCH_DURATION_COUNT ((int)(sizeof(BENCH_DURATIONS) / sizeof(BENCH_DURATIONS[0])))

static void bench_seconds_value(char *out, size_t size) {
  char number[16];
  snprintf(number, sizeof(number), "%d", config.bench_seconds);
  TR_FMT(out, size, STR_UNIT_SECONDS, number);
}

static bool bench_seconds_change(int step) {
  int current = 0;
  for (int i = 0; i < BENCH_DURATION_COUNT; i++) {
    if (BENCH_DURATIONS[i] == config.bench_seconds) {
      current = i;
    }
  }
  config.bench_seconds = BENCH_DURATIONS[(current + BENCH_DURATION_COUNT + step) % BENCH_DURATION_COUNT];
  return true;
}

/*
 * Language: Automatic (the system's), then every language found
 */

// The position of the language setting: 0 for Automatic, then each language
static int language_position(void) {
  if (config.language == NULL || strcmp(config.language, "auto") == 0) {
    return 0;
  }
  for (int i = 0; i < i18n_language_count(); i++) {
    if (strcasecmp(i18n_language_at(i)->tag, config.language) == 0) {
      return i + 1;
    }
  }
  return 0;
}

static void language_value(char *out, size_t size) {
  if (language_position() == 0) {
    TR_FMT(out, size, STR_SET_LANGUAGE_AUTO, i18n_current()->name);
  } else {
    snprintf(out, size, "%s", i18n_current()->name);
  }
}

static bool language_change(int step) {
  int count = i18n_language_count() + 1;
  int position = (language_position() + count + step) % count;
  // Tags live as long as the app, in i18n's list
  config.language = position == 0 ? "auto" : (char *)i18n_language_at(position - 1)->tag;
  i18n_use(config.language);
  return true;
}

static void version_value(char *out, size_t size) {
  snprintf(out, size, "v%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
}

/*
 * The table, in the order the rows are listed
 */

const setting_def SETTINGS[] = {
  // Above the tabs, on every one
  {.tab = SETTINGS_TAB_ALL, .kind = SETTING_ACTION, .flags = SETTING_WARNING,
   .label = STR_SET_BLUETOOTH_WARNING, .listed = vita_bluetooth_enabled,
   .action = SETTING_ACTION_SHOW_HELP,
   .help = STR_SET_BLUETOOTH_WARNING_HELP},
  {.tab = SETTINGS_TAB_ALL, .kind = SETTING_NOTE,
   .label = STR_SET_NOTE_NEW_STREAM, .listed = settings_streaming},

  // Stream
  // Listed mid-stream too: it can restart the stream to apply its settings
  {.tab = SETTINGS_TAB_STREAM, .kind = SETTING_ACTION, .label = STR_SET_RECOMMENDED,
   .action = SETTING_ACTION_RECOMMENDED,
   .help = STR_SET_RECOMMENDED_HELP},
  {.tab = SETTINGS_TAB_STREAM, .kind = SETTING_CHOICE, .flags = SETTING_NEEDS_NEW_STREAM,
   .label = STR_SET_RESOLUTION, .value = resolution_value, .change = resolution_change,
   .help = STR_SET_RESOLUTION_HELP},
  {.tab = SETTINGS_TAB_STREAM, .kind = SETTING_CHOICE, .flags = SETTING_NEEDS_NEW_STREAM,
   .label = STR_SET_FPS, .value = fps_value, .change = fps_change,
   .help = STR_SET_FPS_HELP},
  {.tab = SETTINGS_TAB_STREAM, .kind = SETTING_CHOICE, .flags = SETTING_NEEDS_NEW_STREAM | SETTING_CONFIRM_STEPS,
   .label = STR_SET_BITRATE, .value = bitrate_value, .change = bitrate_change,
   .help = STR_SET_BITRATE_HELP},
  {.tab = SETTINGS_TAB_STREAM, .kind = SETTING_ACTION, .flags = SETTING_NEEDS_NEW_STREAM,
   .label = STR_SET_BITRATE_CUSTOM, .available = bitrate_is_custom, .value = custom_bitrate_value,
   .action = SETTING_ACTION_CUSTOM_BITRATE,
   .help = STR_SET_BITRATE_CUSTOM_HELP},
  {.tab = SETTINGS_TAB_STREAM, .kind = SETTING_TOGGLE, .flags = SETTING_NEEDS_NEW_STREAM,
   .label = STR_SET_SOPS, .is_on = sops_on, .change = sops_flip,
   .help = STR_SET_SOPS_HELP},
  {.tab = SETTINGS_TAB_STREAM, .kind = SETTING_TOGGLE, .flags = SETTING_NEEDS_NEW_STREAM,
   .label = STR_SET_ENABLE_FRAME_INVAL, .is_on = frame_inval_on, .change = frame_inval_flip,
   .help = STR_SET_ENABLE_FRAME_INVAL_HELP},
  {.tab = SETTINGS_TAB_STREAM, .kind = SETTING_TOGGLE, .flags = SETTING_NEEDS_NEW_STREAM,
   .label = STR_SET_ENABLE_STREAM_OPTIMIZE, .is_on = stream_optimize_on, .change = stream_optimize_flip,
   .help = STR_SET_ENABLE_STREAM_OPTIMIZE_HELP},
  {.tab = SETTINGS_TAB_STREAM, .kind = SETTING_TOGGLE,
   .label = STR_SET_ENABLE_VITA_VBLANK_WAIT, .is_on = vblank_wait_on, .change = vblank_wait_flip,
   .help = STR_SET_ENABLE_VITA_VBLANK_WAIT_HELP},
  {.tab = SETTINGS_TAB_STREAM, .kind = SETTING_TOGGLE, .flags = SETTING_NEEDS_NEW_STREAM,
   .label = STR_SET_CENTER_REGION_ONLY, .is_on = fill_screen_on, .change = fill_screen_flip,
   .help = STR_SET_CENTER_REGION_ONLY_HELP},
  {.tab = SETTINGS_TAB_STREAM, .kind = SETTING_TOGGLE,
   .label = STR_SET_ENABLE_FRAME_PACER, .is_on = frame_pacer_on, .change = frame_pacer_flip,
   .help = STR_SET_ENABLE_FRAME_PACER_HELP},
  {.tab = SETTINGS_TAB_STREAM, .kind = SETTING_CHOICE, .flags = SETTING_NEEDS_NEW_STREAM | SETTING_CONFIRM_STEPS,
   .label = STR_SET_PACKET_SIZE, .value = packet_size_value, .change = packet_size_change,
   .help = STR_SET_PACKET_SIZE_HELP},
  {.tab = SETTINGS_TAB_STREAM, .kind = SETTING_TOGGLE, .flags = SETTING_NEEDS_NEW_STREAM,
   .label = STR_SET_DECODE_YUV, .is_on = decode_yuv_on, .change = decode_yuv_flip,
   .help = STR_SET_DECODE_YUV_HELP},
  {.tab = SETTINGS_TAB_STREAM, .kind = SETTING_TOGGLE, .flags = SETTING_NEEDS_NEW_STREAM,
   .label = STR_SET_LOCAL_AUDIO, .is_on = local_audio_on, .change = local_audio_flip,
   .help = STR_SET_LOCAL_AUDIO_HELP},

  // System
  // First on its tab: changing it rebuilds the rows, which puts the cursor on
  // the first one, so it stays on this row
  {.tab = SETTINGS_TAB_SYSTEM, .kind = SETTING_CHOICE, .flags = SETTING_REBUILDS,
   .label = STR_SET_LANGUAGE, .value = language_value, .change = language_change,
   .help = STR_SET_LANGUAGE_HELP},
  {.tab = SETTINGS_TAB_SYSTEM, .kind = SETTING_TOGGLE,
   .label = STR_SET_SAVE_DEBUG_LOG, .is_on = debug_log_on, .change = debug_log_flip, .help_with = debug_log_help},
  {.tab = SETTINGS_TAB_SYSTEM, .kind = SETTING_TOGGLE,
   .label = STR_SET_DISABLE_POWERSAVE, .is_on = powersave_on, .change = powersave_flip,
   .help = STR_SET_DISABLE_POWERSAVE_HELP},
  {.tab = SETTINGS_TAB_SYSTEM, .kind = SETTING_TOGGLE,
   .label = STR_SET_BOOST_CLOCKS, .is_on = boost_clocks_on, .change = boost_clocks_flip,
   .help = STR_SET_BOOST_CLOCKS_HELP},
  {.tab = SETTINGS_TAB_SYSTEM, .kind = SETTING_TOGGLE,
   .label = STR_SET_JP_LAYOUT, .is_on = jp_layout_on, .change = jp_layout_flip,
   .help = STR_SET_JP_LAYOUT_HELP},
  {.tab = SETTINGS_TAB_SYSTEM, .kind = SETTING_CHOICE, .flags = SETTING_CONFIRM_STEPS,
   .label = STR_SET_SHOW_FPS, .value = stats_overlay_value, .change = stats_overlay_change,
   .help = STR_SET_SHOW_FPS_HELP},
  {.tab = SETTINGS_TAB_SYSTEM, .kind = SETTING_CHOICE, .flags = SETTING_CONFIRM_STEPS,
   .label = STR_SET_APP_LIST_STYLE, .value = app_list_value, .change = app_list_change,
   .help = STR_SET_APP_LIST_STYLE_HELP},

  // Controls
  {.tab = SETTINGS_TAB_CONTROLS, .kind = SETTING_TOGGLE,
   .label = STR_SET_INPUT_SYNC, .is_on = input_sync_on, .change = input_sync_flip,
   .help = STR_SET_INPUT_SYNC_HELP},
  {.tab = SETTINGS_TAB_CONTROLS, .kind = SETTING_TOGGLE, .flags = SETTING_NEEDS_NEW_STREAM,
   .label = STR_SET_ENABLE_MOTION_CONTROLS, .is_on = motion_on, .change = motion_flip,
   .help = STR_SET_ENABLE_MOTION_CONTROLS_HELP},
  {.tab = SETTINGS_TAB_CONTROLS, .kind = SETTING_TOGGLE,
   .label = STR_SET_ENABLE_DOUBLE_TAP_SPRINT, .is_on = sprint_on, .change = sprint_flip,
   .help = STR_SET_ENABLE_DOUBLE_TAP_SPRINT_HELP},
  {.tab = SETTINGS_TAB_CONTROLS, .kind = SETTING_ACTION,
   .label = STR_SET_DOUBLE_TAP_SPRINT_STEP_TIME, .value = sprint_time_value, .action = SETTING_ACTION_SPRINT_TIME,
   .help = STR_SET_DOUBLE_TAP_SPRINT_STEP_TIME_HELP},
  {.tab = SETTINGS_TAB_CONTROLS, .kind = SETTING_CHOICE, .flags = SETTING_NEEDS_NEW_STREAM | SETTING_SAVE_AT_ONCE,
   .label = STR_SET_CONTROLLER_TYPE, .value = controller_type_value, .change = controller_type_change,
   .help = STR_SET_CONTROLLER_TYPE_HELP},
  {.tab = SETTINGS_TAB_CONTROLS, .kind = SETTING_TOGGLE,
   .label = STR_SET_SWAP_SHOULDER_BUTTONS, .is_on = swap_shoulders_on, .change = swap_shoulders_flip,
   .help = STR_SET_SWAP_SHOULDER_BUTTONS_HELP},
  {.tab = SETTINGS_TAB_CONTROLS, .kind = SETTING_CHOICE, .flags = SETTING_HELD_REPEATS,
   .label = STR_SET_MOUSE_ACCEL, .value = mouse_accel_value, .change = mouse_accel_change,
   .help = STR_SET_MOUSE_ACCEL_HELP},
  {.tab = SETTINGS_TAB_CONTROLS, .kind = SETTING_TOGGLE,
   .label = STR_SET_ENABLE_MAPPING, .is_on = mapping_on, .change = mapping_flip, .help_with = mapping_help},
  {.tab = SETTINGS_TAB_CONTROLS, .kind = SETTING_TOGGLE,
   .label = STR_SET_ENABLE_PSBUTTON_CAPTURE, .is_on = psbutton_on, .change = psbutton_flip,
   .help = STR_SET_ENABLE_PSBUTTON_CAPTURE_HELP},
  {.tab = SETTINGS_TAB_CONTROLS, .kind = SETTING_ACTION,
   .label = STR_SET_BACK_DEADZONE, .value = deadzone_value, .action = SETTING_ACTION_BACK_DEADZONE,
   .help = STR_SET_BACK_DEADZONE_HELP},
  {.tab = SETTINGS_TAB_CONTROLS, .kind = SETTING_TOGGLE,
   .label = STR_SET_ENABLE_SPECIAL_KEYS, .is_on = touch_keys_on, .change = touch_keys_flip,
   .help = STR_SET_ENABLE_SPECIAL_KEYS_HELP},
  {.tab = SETTINGS_TAB_CONTROLS, .kind = SETTING_ACTION,
   .label = STR_SET_SPECIAL_KEYS, .action = SETTING_ACTION_SPECIAL_KEYS,
   .help = STR_SET_SPECIAL_KEYS_HELP},
  // Confirm cycles the modes, so it's a toggle with four positions
  {.tab = SETTINGS_TAB_CONTROLS, .kind = SETTING_TOGGLE,
   .label = STR_SET_TOUCH_MODE_SELECT, .value = touch_mode_value, .change = touch_mode_change,
   .help = STR_SET_TOUCH_MODE_SELECT_HELP},
  {.tab = SETTINGS_TAB_CONTROLS, .kind = SETTING_HEADER, .label = STR_SET_HEADER_KEYBOARD},
  {.tab = SETTINGS_TAB_CONTROLS, .kind = SETTING_ACTION,
   .label = STR_SET_KEYBOARD_LAYOUT, .action = SETTING_ACTION_KEYBOARD_LAYOUT,
   .help = STR_SET_KEYBOARD_LAYOUT_HELP},

  // Developer, shown after holding L and R and pressing triangle
  {.tab = SETTINGS_TAB_DEVELOPER, .kind = SETTING_NOTE,
   .label = STR_SET_NOTE_BENCH_STREAMING, .listed = settings_streaming},
  {.tab = SETTINGS_TAB_DEVELOPER, .kind = SETTING_NOTE,
   .label = STR_SET_NOTE_BENCH_CONNECT, .listed = bench_not_ready},
  {.tab = SETTINGS_TAB_DEVELOPER, .kind = SETTING_ACTION,
   .label = STR_SET_BENCH_RUN, .listed = bench_ready, .value = bench_target_value,
   .action = SETTING_ACTION_RUN_BENCHMARK,
   .help = STR_SET_BENCH_RUN_HELP},
  {.tab = SETTINGS_TAB_DEVELOPER, .kind = SETTING_CHOICE, .flags = SETTING_CONFIRM_STEPS,
   .label = STR_SET_BENCH_SECONDS, .listed = bench_ready, .value = bench_seconds_value,
   .change = bench_seconds_change,
   .help = STR_SET_BENCH_SECONDS_HELP},
  {.tab = SETTINGS_TAB_DEVELOPER, .kind = SETTING_TOGGLE, .flags = SETTING_NEEDS_NEW_STREAM,
   .label = STR_SET_DIRECT_DECODE, .is_on = direct_decode_on, .change = direct_decode_flip,
   .help = STR_SET_DIRECT_DECODE_HELP},
  {.tab = SETTINGS_TAB_DEVELOPER, .kind = SETTING_ACTION,
   .label = STR_SET_HIDE_DEVELOPER, .action = SETTING_ACTION_HIDE_DEVELOPER,
   .help = STR_SET_HIDE_DEVELOPER_HELP},

  // About: selectable lines, so the tab can scroll and show help
  {.tab = SETTINGS_TAB_ABOUT, .kind = SETTING_INFO, .label = STR_ABOUT_APP, .value = version_value,
   .help = STR_ABOUT_HELP},
  {.tab = SETTINGS_TAB_ABOUT, .kind = SETTING_INFO, .label = STR_ABOUT_PROJECT,
   .fixed_value = "github.com/xyzz/vita-moonlight", .help = STR_ABOUT_HELP},
  {.tab = SETTINGS_TAB_ABOUT, .kind = SETTING_INFO, .label = STR_ABOUT_MAINTAINERS,
   .fixed_value = "AorsiniYT, d3m3vilurr, xyzz", .help = STR_ABOUT_HELP},
  {.tab = SETTINGS_TAB_ABOUT, .kind = SETTING_INFO, .label = STR_ABOUT_BUILT_ON,
   .fixed_value = "moonlight-stream.org", .help = STR_ABOUT_HELP},
  {.tab = SETTINGS_TAB_ABOUT, .kind = SETTING_INFO, .label = STR_ABOUT_HOST,
   .fixed_value = "Sunshine: github.com/LizardByte/Sunshine", .help = STR_ABOUT_HELP},
  {.tab = SETTINGS_TAB_ABOUT, .kind = SETTING_INFO, .label = STR_ABOUT_LICENCE,
   .fixed_value = "GNU GPL version 3", .help = STR_ABOUT_HELP},
};

const int SETTINGS_COUNT = (int)(sizeof(SETTINGS) / sizeof(SETTINGS[0]));

bool setting_listed(const setting_def *setting, int tab) {
  if (setting->tab != SETTINGS_TAB_ALL && setting->tab != tab) {
    return false;
  }
  if ((setting->flags & SETTING_NEEDS_NEW_STREAM) && settings_streaming()) {
    return false;
  }
  return setting->listed == NULL || setting->listed();
}

void setting_value(const setting_def *setting, char *out, size_t size) {
  out[0] = '\0';
  if (setting->value) {
    setting->value(out, size);
  } else if (setting->is_on) {
    snprintf(out, size, "%s", tr(setting->is_on() ? STR_COMMON_YES : STR_COMMON_NO));
  } else if (setting->fixed_value) {
    snprintf(out, size, "%s", setting->fixed_value);
  }
}

void setting_help(const setting_def *setting, char *out, size_t size) {
  if (setting->help_with) {
    setting->help_with(out, size);
  } else {
    snprintf(out, size, "%s", tr(setting->help ? setting->help : STR_SET_NO_HELP));
  }
}
