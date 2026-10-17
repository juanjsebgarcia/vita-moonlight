/*
 * This file is part of Moonlight Embedded.
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

// Settings > Apply recommended settings: stream settings for good or weak
// Wi-Fi, picked by hand or by measuring the signal where the player sits.
// The values come from benchmarks in docs/DECODE.md.

#include "ui_recommended.h"
#include "guilib.h"
#include "../config.h"
#include "../util.h"

#include <stdbool.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <string.h>

// At or above this the signal counts as good. The stats overlay draws the
// reading in yellow below the same line.
#define GOOD_WIFI_PERCENT 60
// Auto-detect also wants the signal never to dip below this while measuring
#define GOOD_WIFI_MIN_PERCENT 50
#define MEASURE_SECONDS 5
#define MEASURE_INTERVAL_US 250000

typedef struct {
  const char *name;
  int width, height, fps, bitrate_kbps;
} preset;

// Good: by the access point, 20 Mbps was sharp with almost no loss, and more
// only added latency. Weak is provisional until measured at a weaker signal:
// 540p looks the same as 720p on the Vita's screen, so every bit goes into
// the picture, at half the bitrate.
static const preset PRESETS[] = {
  [RECOMMENDED_GOOD] = {"Good Wi-Fi", 1280, 720, 60, 20000},
  [RECOMMENDED_WEAK] = {"Weak Wi-Fi", 960, 540, 60, 10000},
};

enum {
  ROW_NOW,
  ROW_AUTO,
  ROW_GOOD,
  ROW_WEAK,
};

static int chosen;

static void describe(int which, char *out, size_t size) {
  const preset *p = &PRESETS[which];
  snprintf(out, size, "%dx%d, %d fps, %d Mbps", p->width, p->height, p->fps, p->bitrate_kbps / 1000);
}

static void apply(int which) {
  const preset *p = &PRESETS[which];
  config.stream.width = p->width;
  config.stream.height = p->height;
  config.stream.fps = p->fps;
  config.stream.bitrate = p->bitrate_kbps;
  // The lowest-latency choices, whatever the signal
  config.enable_frame_pacer = false;
  config.enable_vita_vblank_wait = false;
  config.boost_clocks = true;
}

static void show_applied(int which) {
  char what[96];
  describe(which, what, sizeof(what));
  display_error("Applied %s: %s, frame pacer off, V-Sync off, clock boost on.", PRESETS[which].name, what);
}

// Shows the live reading, and marks the choice that fits it
static void refresh(menu_entry *menu) {
  int percent = vita_wifi_percent();
  if (percent < 0) {
    snprintf(menu[ROW_NOW].subname, sizeof(menu[ROW_NOW].subname), "Not connected to Wi-Fi");
  } else {
    snprintf(menu[ROW_NOW].subname, sizeof(menu[ROW_NOW].subname), "Your Wi-Fi now: %d%%", percent);
  }
  bool good = percent >= GOOD_WIFI_PERCENT;
  strcpy(menu[ROW_GOOD].subname, percent >= 0 && good ? "fits now" : "");
  strcpy(menu[ROW_WEAK].subname, percent >= 0 && !good ? "fits now" : "");
}

static int start_choice;

static int start_loop(int result, void *context, const input_data *input) {
  start_choice = result;
  return 1;
}

// Returns RECOMMENDED_NONE if the player cancels
static int auto_detect(void) {
  char *captions[] = {"Start", "Cancel"};
  start_choice = 1;
  display_alert("Take the Vita to where you'll play, and hold it as you normally would. "
                "Then press Start to measure the Wi-Fi signal for 5 seconds.",
                captions, 2, start_loop, NULL);
  if (start_choice != 0) {
    return RECOMMENDED_NONE;
  }

  int samples = 0, total = 0, lowest = 100;
  int count = MEASURE_SECONDS * 1000000 / MEASURE_INTERVAL_US;
  for (int i = 0; i < count; i++) {
    int percent = vita_wifi_percent();
    if (percent >= 0) {
      samples++;
      total += percent;
      lowest = percent < lowest ? percent : lowest;
    }
    int left = (count - i) * MEASURE_INTERVAL_US / 1000000 + 1;
    if (percent >= 0) {
      flash_message("Measuring Wi-Fi, hold still...\n\nNow %d%%, %d s left", percent, left);
    } else {
      flash_message("Measuring Wi-Fi, hold still...\n\nNo signal, %d s left", left);
    }
    sceKernelDelayThread(MEASURE_INTERVAL_US);
  }
  if (samples == 0) {
    display_error("Couldn't read the Wi-Fi signal. Check the Vita is connected to Wi-Fi.");
    return RECOMMENDED_NONE;
  }

  int average = total / samples;
  int which = average >= GOOD_WIFI_PERCENT && lowest >= GOOD_WIFI_MIN_PERCENT ? RECOMMENDED_GOOD : RECOMMENDED_WEAK;
  char what[96];
  describe(which, what, sizeof(what));
  char message[512];
  snprintf(message, sizeof(message), "Here the signal averaged %d%% (lowest %d%%). Recommended: %s (%s).%s\n\nApply it?",
           average, lowest, PRESETS[which].name, what,
           vita_bluetooth_enabled() ? " Bluetooth is on, which can make it worse." : "");
  if (!display_confirm(message)) {
    return RECOMMENDED_NONE;
  }
  apply(which);
  show_applied(which);
  return which;
}

static const char *help_text(int row) {
  switch (row) {
    case ROW_AUTO:
      return "Measures the Wi-Fi signal where you'll play, then recommends Good or Weak Wi-Fi "
             "settings. Do it from your usual spot, holding the Vita as you normally would.";
    case ROW_GOOD:
      return "For a strong signal, such as in the same room as your router: 1280x720 at 60 fps "
             "and 20 Mbps. A sharp picture with almost no lost frames. Higher bitrates only add "
             "delay. Also turns the frame pacer and V-Sync off and clock boost on.";
    case ROW_WEAK:
      return "For a weaker signal, such as further from your router or through walls: 960x540 at "
             "60 fps and 10 Mbps. It looks almost the same on the Vita's screen and needs half the "
             "bandwidth. Also turns the frame pacer and V-Sync off and clock boost on.";
    default:
      return NULL;
  }
}

static int menu_loop(int id, void *context, const input_data *input) {
  menu_entry *menu = context;

  // Keep the live reading current, about once a second
  static int ticks = 0;
  if (++ticks >= 60) {
    ticks = 0;
    refresh(menu);
  }

  bool pressed_once = (input->buttons & SCE_CTRL_HOLD) == 0;
  if ((input->buttons & SCE_CTRL_TRIANGLE) && pressed_once) {
    const char *help = help_text(id);
    if (help) {
      display_help(menu[id].name, help);
    }
    return 0;
  }
  if ((input->buttons & config.btn_confirm) == 0 || !pressed_once) {
    return 0;
  }
  if (id == ROW_AUTO) {
    chosen = auto_detect();
    return chosen == RECOMMENDED_NONE ? 0 : 1;
  }
  if (id == ROW_GOOD || id == ROW_WEAK) {
    chosen = id == ROW_GOOD ? RECOMMENDED_GOOD : RECOMMENDED_WEAK;
    apply(chosen);
    show_applied(chosen);
    return 1;
  }
  return 0;
}

int ui_recommended_settings_menu(void) {
  menu_entry menu[4];
  memset(menu, 0, sizeof(menu));
  menu[ROW_NOW] = (menu_entry) { .name = "", .disabled = true };
  menu[ROW_AUTO] = (menu_entry) { .name = "Auto-detect: test where you'll play", .id = ROW_AUTO };
  menu[ROW_GOOD] = (menu_entry) { .name = "Good Wi-Fi: near the router", .id = ROW_GOOD };
  menu[ROW_WEAK] = (menu_entry) { .name = "Weak Wi-Fi: further away or through walls", .id = ROW_WEAK };
  refresh(menu);

  chosen = RECOMMENDED_NONE;
  // Four rows of 24 px, plus margins
  menu_geom geom = make_geom_centered(600, 5 * 24);
  display_menu(menu, 4, &geom, menu_loop, NULL, NULL, menu);
  return chosen;
}
