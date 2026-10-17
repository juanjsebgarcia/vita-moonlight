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
#include "ui_connect.h"
#include "../check_host.h"
#include "../config.h"
#include "../connection.h"
#include "../device.h"
#include "../util.h"

#include <Limelight.h>

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
// The test stream: the PC's Desktop, which every Sunshine install has, for
// this long after it settles. Even a still Desktop sends ~100 packets a second
// once error correction is counted, enough to see about 1% loss.
#define TEST_APP "Desktop"
#define TEST_SETTLE_US 2000000
#define TEST_SECONDS 10
#define TEST_CONNECT_TIMEOUT_US 20000000
// Good also needs the test stream to lose under this share of packets, with
// the round trip varying by less than this. On a still Desktop the variation
// measured ±3.8 ms at a 74% signal and ±7.2 ms at 60%; only two readings, so
// treat the line as a reasonable one rather than a proven one.
#define GOOD_MAX_LOSS_PERCENT 1.0f
#define GOOD_MAX_JITTER_MS 6

typedef struct {
  const char *name;
  int width, height, fps, bitrate_kbps;
} preset;

// Good: by the access point, 720p at 20 Mbps was sharp and smooth with a
// 13.9 ms latency estimate, and higher bitrates only added latency. Weak: at a
// middling signal, 540p at 10 Mbps measured ~12.5 ms against ~24 ms for 720p
// at 20 Mbps, with as few freezes, and looks the same on the Vita's 960x544
// screen. (docs/DECODE.md, with the out-of-order packet fix in place.)
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
  strcpy(menu[ROW_GOOD].subname, percent >= 0 && good ? "(fits now)" : "");
  strcpy(menu[ROW_WEAK].subname, percent >= 0 && !good ? "(fits now)" : "");
}

static int start_choice;

static int start_loop(int result, void *context, const input_data *input) {
  start_choice = result;
  return 1;
}

typedef struct {
  int signal_avg, signal_min;
  float loss_percent;
  float jitter_ms;
  bool streamed;
} link_test;

// Auto-detect needs a PC to test against: the one Settings was opened from.
// Returns false, after telling the player what to do, when there isn't one.
static bool have_test_pc(void) {
  if (ui_connect_server_ready()) {
    return true;
  }
  bool paired_online = false;
  for (int i = 0; i < known_devices.count && i < MAX_HOSTS; i++) {
    paired_online |= known_devices.devices[i].paired && g_host_status[i].status == HOST_ONLINE;
  }
  if (paired_online) {
    display_error("Auto-detect tests the connection to your PC. Select your PC in the main menu, "
                  "open Settings from its menu, and run Auto-detect again.");
  } else {
    display_error("Auto-detect needs a paired PC that's switched on, to test the connection to "
                  "it. Turn your PC on, or choose Good or Weak Wi-Fi yourself.");
  }
  return false;
}

static bool measure_signal(link_test *result) {
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
      flash_message("Measuring the Wi-Fi signal, hold still...\n\nNow %d%%, %d s left", percent, left);
    } else {
      flash_message("Measuring the Wi-Fi signal, hold still...\n\nNo signal, %d s left", left);
    }
    sceKernelDelayThread(MEASURE_INTERVAL_US);
  }
  if (samples == 0) {
    return false;
  }
  result->signal_avg = total / samples;
  result->signal_min = lowest;
  return true;
}

static bool wait_connected(uint64_t timeout_us) {
  uint64_t start = LiGetMicroseconds();
  while (connection_get_status() != LI_CONNECTED) {
    if (LiGetMicroseconds() - start > timeout_us) {
      return false;
    }
    sceKernelDelayThread(50000);
  }
  return true;
}

// Streams the PC's Desktop at the Good settings and measures packet loss
// (from common-c's error-correction counters) and jitter (the round trip's
// variation). Returns false with a message in err if it couldn't.
static bool test_stream(link_test *result, char *err, size_t err_len) {
  int app_id = ui_connect_find_app(TEST_APP);
  if (app_id < 0) {
    snprintf(err, err_len, "Your PC has no \"%s\" app in Sunshine, which Auto-detect streams "
             "to test the connection.", TEST_APP);
    return false;
  }
  // Test at the Good settings, then put the player's own back
  STREAM_CONFIGURATION saved = config.stream;
  const preset *good = &PRESETS[RECOMMENDED_GOOD];
  config.stream.width = good->width;
  config.stream.height = good->height;
  config.stream.fps = good->fps;
  config.stream.bitrate = good->bitrate_kbps;

  bool ok = false;
  if (ui_connect_start_app_by_id(app_id, err, err_len) != 0) {
    goto done;
  }
  result->streamed = true;
  if (!wait_connected(TEST_CONNECT_TIMEOUT_US)) {
    snprintf(err, err_len, "The test stream didn't start.");
    goto done;
  }
  sceKernelDelayThread(TEST_SETTLE_US);

  RTP_VIDEO_STATS before = {0}, after = {0};
  const RTP_VIDEO_STATS *rtp = LiGetRTPVideoStats();
  if (rtp != NULL) {
    before = *rtp;
  }
  double variance_sum = 0;
  int variance_samples = 0;
  for (int i = 0; i < TEST_SECONDS * 2; i++) {
    sceKernelDelayThread(500000);
    if (connection_get_status() != LI_CONNECTED) {
      snprintf(err, err_len, "The test stream ended early.");
      goto done;
    }
    uint32_t rtt = 0, variance = 0;
    if (LiGetEstimatedRttInfo(&rtt, &variance)) {
      variance_sum += variance;
      variance_samples++;
    }
  }
  rtp = LiGetRTPVideoStats();
  if (rtp != NULL) {
    after = *rtp;
  }
  uint32_t packets = (after.packetCountVideo - before.packetCountVideo) + (after.packetCountFec - before.packetCountFec);
  uint32_t lost = (after.packetCountFecRecovered - before.packetCountFecRecovered) +
                  (after.packetCountFecFailed - before.packetCountFecFailed);
  if (packets == 0) {
    snprintf(err, err_len, "No video arrived during the test stream.");
    goto done;
  }
  result->loss_percent = 100.0f * lost / packets;
  result->jitter_ms = variance_samples ? variance_sum / variance_samples : 0;
  ok = true;

done:
  if (result->streamed) {
    connection_terminate();
    ui_connect_quit_app();
    ui_connect_return_to_server();
  }
  config.stream = saved;
  return ok;
}

// Returns RECOMMENDED_NONE if the player cancels or the test can't run
static int auto_detect(void) {
  if (!have_test_pc()) {
    return RECOMMENDED_NONE;
  }
  char *captions[] = {"Start", "Cancel"};
  start_choice = 1;
  char intro[512];
  snprintf(intro, sizeof(intro),
           "Take the Vita to where you'll play, and hold it as you normally would. Then press "
           "Start. Auto-detect measures the Wi-Fi signal for %d seconds, then streams %s's "
           "desktop for about %d seconds to test the connection.",
           MEASURE_SECONDS, ui_connect_server_name(), TEST_SECONDS);
  display_alert(intro, captions, 2, start_loop, NULL);
  if (start_choice != 0) {
    return RECOMMENDED_NONE;
  }

  link_test result = {0};
  if (!measure_signal(&result)) {
    display_error("Couldn't read the Wi-Fi signal. Check the Vita is connected to Wi-Fi.");
    return RECOMMENDED_NONE;
  }
  flash_message("Testing the connection to %s...", ui_connect_server_name());
  char err[256] = "";
  if (!test_stream(&result, err, sizeof(err))) {
    display_error("Auto-detect couldn't finish the connection test: %s", err);
    return RECOMMENDED_NONE;
  }

  bool signal_ok = result.signal_avg >= GOOD_WIFI_PERCENT && result.signal_min >= GOOD_WIFI_MIN_PERCENT;
  bool loss_ok = result.loss_percent < GOOD_MAX_LOSS_PERCENT;
  bool jitter_ok = result.jitter_ms < GOOD_MAX_JITTER_MS;
  int which = signal_ok && loss_ok && jitter_ok ? RECOMMENDED_GOOD : RECOMMENDED_WEAK;
  const char *reason = which == RECOMMENDED_GOOD ? ""
                     : !signal_ok ? " The signal is on the weak side."
                     : !loss_ok ? " Packets went missing during the test."
                                : " The connection's timing was uneven.";

  char what[96];
  describe(which, what, sizeof(what));
  char message[768];
  snprintf(message, sizeof(message),
           "Signal %d%% (lowest %d%%), %.1f%% of packets lost, jitter %.0f ms.%s%s\n\n"
           "Recommended: %s (%s). Apply it?",
           result.signal_avg, result.signal_min, result.loss_percent, result.jitter_ms, reason,
           vita_bluetooth_enabled() ? " Bluetooth is on, which can make it worse." : "",
           PRESETS[which].name, what);
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
      return "Measures the Wi-Fi signal where you'll play, then briefly streams your PC's desktop "
             "to check for lost packets and uneven timing, and recommends Good or Weak Wi-Fi "
             "settings. Do it from your usual spot, holding the Vita as you normally would. Open "
             "Settings from your PC's menu first, so it knows which PC to test.";
    case ROW_GOOD:
      return "For a strong signal, such as in the same room as your router: 1280x720 at 60 fps "
             "and 20 Mbps. A sharp, smooth picture. Higher bitrates only add delay. Also turns "
             "the frame pacer and V-Sync off and clock boost on.";
    case ROW_WEAK:
      return "For a weaker signal, such as further from your router or through walls: 960x540 at "
             "60 fps and 10 Mbps. It looks the same on the Vita's screen, needs half the "
             "bandwidth, and on weaker Wi-Fi has about half the delay of the Good settings. Also "
             "turns the frame pacer and V-Sync off and clock boost on.";
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
