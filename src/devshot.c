#include "devshot.h"

#include "config.h"
#include "debug.h"
#include "i18n/i18n.h"
#include "stats.h"
#include "gui/guilib.h"
#include "gui/theme.h"
#include "input/vita.h"

#include <stdbool.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <string.h>
#include <vita2d.h>

// UI frames between checks for the request file, so it costs nothing to speak of
#define DEVSHOT_POLL_FRAMES 30
// vita2d's display buffers: 960x544, stride 960 pixels, 4 bytes each
#define DEVSHOT_BYTES (960 * 544 * 4)
#define DEVSHOT_PATH_MAX (sizeof(config.key_dir) + 32)

void devshot_after_swap(void) {
  static int frames = 0;
  if (!config.developer_mode || ++frames < DEVSHOT_POLL_FRAMES) {
    return;
  }
  frames = 0;

  // With nobody touching the Vita during remote checks, it turns the screen
  // off after a while, and the menus stop drawing (they wait for screen
  // refreshes that don't come). Tell it someone's active, as input would.
  sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);

  char request[DEVSHOT_PATH_MAX];
  snprintf(request, sizeof(request), "%sshot.req", config.key_dir);
  SceIoStat stat;
  if (sceIoGetstat(request, &stat) < 0) {
    return;
  }
  sceIoRemove(request);

  // After the swap, the current buffer is the frame just drawn, on screen
  const void *frame = vita2d_get_current_fb();
  char tmp[DEVSHOT_PATH_MAX];
  char path[DEVSHOT_PATH_MAX];
  snprintf(tmp, sizeof(tmp), "%sshot.raw.tmp", config.key_dir);
  snprintf(path, sizeof(path), "%sshot.raw", config.key_dir);
  FILE *f = fopen(tmp, "wb");
  if (f == NULL) {
    return;
  }
  bool written = fwrite(frame, 1, DEVSHOT_BYTES, f) == DEVSHOT_BYTES;
  fclose(f);
  // Moved into place whole, so the fetcher never reads a partial file
  sceIoRemove(path);
  if (written) {
    sceIoRename(tmp, path);
  }
}

// The stream's overlay drawing, in src/video/vita.c
void draw_fps(void);
void draw_indicators(void);
extern double_click_tracker dc_tracker;

#define DEVSHOT_HOLD_FRAMES 600

// The overlay's drawing time over the frames a request holds it, logged at
// the end (with save_debug_log on), so overlay changes can be compared
static struct {
  uint64_t sum, max;
  int frames;
} overlay_time;

static void devshot_time_overlay(uint64_t us) {
  overlay_time.sum += us;
  overlay_time.max = us > overlay_time.max ? us : overlay_time.max;
  overlay_time.frames++;
}

static void devshot_log_overlay(const char *what) {
  if (overlay_time.frames > 0) {
    vita_debug_log("[DEVSHOT] %s (%s): overlay %llu us avg, %llu us max over %d frames", what,
                   theme_active()->name, (unsigned long long)(overlay_time.sum / overlay_time.frames),
                   (unsigned long long)overlay_time.max, overlay_time.frames);
  }
  overlay_time.sum = overlay_time.max = 0;
  overlay_time.frames = 0;
}

// The stream overlays over black with made-up numbers, for the states a
// stream is needed for: "overlay full" (every stats line and the hint) or
// "overlay poor" (weak Wi-Fi, the poor network advice and badges, sprint,
// and the hint fading out)
static void devshot_sample_overlay(const char *variant) {
  static const char sample[][80] = {
    "Latency ~38 ms (max 52)",
    "960x544@60  14.8 / 15.0 Mbps",
    "FPS rx 60.0  drawn 59.9  pacer drop 0",
    "Lost 0  IDR 1  FEC rec 3 fail 0  OOS 0",
    "Net 4.1/9.8 ms  Queue 0.4/1.2 ms (0)",
    "Decode 2.6/4.0 ms  Render 1.1/1.9 ms",
    "Host 6.9 ms  RTT 3 +/- 1 ms",
    "Wi-Fi 72% (-58 dBm) ch 36  packet 1024 B",
    "Clocks cpu 444 bus 222 gpu 222 MHz",
    "Overlay 0.12 ms CPU per frame",
  };
  bool poor = strcmp(variant, "poor") == 0;
  int wifi = poor ? 41 : 72;
  stats_view stats = {
    .full = !poor,
    .fps = 59.9f,
    .target_fps = 60,
    .wifi_percent = wifi,
    .wifi_weak = poor,
    .lines = &sample[0][0],
    .line_stride = sizeof(sample[0]),
    .line_count = sizeof(sample) / sizeof(sample[0]),
    .wifi_line = 7,
    .version = 1,
  };
  stream_overlay_view overlay = {.hint = tr(STR_STREAM_HINT), .hint_opacity = 1.0f};
  char advice[160];
  TR_FMT(advice, sizeof(advice), STR_STREAM_POOR_BITRATE, "10");
  for (int i = 0; i < DEVSHOT_HOLD_FRAMES; i++) {
    if (poor) {
      // The hint shows for 5 s and fades over the last one, as in a stream
      overlay.hint_opacity = i < 240 ? 1.0f : i < 300 ? (300 - i) / 60.0f : 0.0f;
      overlay.hint = i < 300 ? tr(STR_STREAM_HINT) : NULL;
      overlay.poor_network = true;
      overlay.network_advice = advice;
      overlay.network_advice_opacity = 1.0f;
      overlay.poor_network_alpha = (uint8_t)(i * 4 % 512 < 256 ? i * 4 % 256 : 255 - i * 4 % 256);
      overlay.sprinting = true;
    }
    ui_start();
    uint64_t start = sceKernelGetProcessTimeWide();
    theme_active()->draw_stats(&stats);
    theme_active()->draw_stream_overlay(&overlay);
    devshot_time_overlay(sceKernelGetProcessTimeWide() - start);
    ui_end();
  }
  devshot_log_overlay(poor ? "overlay poor" : "overlay full");
}

void devshot_open_requested_screen(void) {
  if (!config.developer_mode) {
    return;
  }
  char request[DEVSHOT_PATH_MAX];
  snprintf(request, sizeof(request), "%sscreen.req", config.key_dir);
  FILE *f = fopen(request, "rb");
  if (f == NULL) {
    return;
  }
  char line[1024] = "";
  size_t length = fread(line, 1, sizeof(line) - 1, f);
  fclose(f);
  sceIoRemove(request);
  line[length] = '\0';

  // "\n" written in the file is a line break; the real one ends the request
  char text[1024];
  size_t out = 0;
  for (const char *p = line; *p && *p != '\n' && *p != '\r' && out < sizeof(text) - 1; p++) {
    if (p[0] == '\\' && p[1] == 'n') {
      text[out++] = '\n';
      p++;
    } else {
      text[out++] = *p;
    }
  }
  text[out] = '\0';
  char *kind = text;
  // Checking the new theme: switch to it before opening the screen
  if (strncmp(kind, "morpheus", 8) == 0 && (kind[8] == ' ' || kind[8] == '\0')) {
    theme_request_new();
    theme_apply_pending();
    kind += kind[8] == ' ' ? 9 : 8;
  }
  char *arg = strchr(kind, ' ');
  if (arg != NULL) {
    *arg++ = '\0';
  } else {
    arg = text + out;
  }

  if (strcmp(kind, "flash") == 0) {
    for (int i = 0; i < DEVSHOT_HOLD_FRAMES; i++) {
      flash_message("%s", arg);
    }
  } else if (strcmp(kind, "error") == 0) {
    display_error("%s", arg);
  } else if (strcmp(kind, "confirm") == 0) {
    display_confirm(arg);
  } else if (strcmp(kind, "help") == 0) {
    char *body = strchr(arg, '|');
    if (body != NULL) {
      *body++ = '\0';
    }
    display_help(arg, body ? body : "");
  } else if (strcmp(kind, "overlay") == 0 && arg[0] == '\0') {
    // In memory only: the saved settings are left alone
    int stats_overlay = config.stats_overlay;
    config.stats_overlay = STATS_OVERLAY_FPS;
    dc_tracker.currently_sprinting = true;
    for (int i = 0; i < DEVSHOT_HOLD_FRAMES; i++) {
      ui_start();
      uint64_t start = sceKernelGetProcessTimeWide();
      draw_fps();
      draw_indicators();
      devshot_time_overlay(sceKernelGetProcessTimeWide() - start);
      ui_end();
    }
    dc_tracker.currently_sprinting = false;
    config.stats_overlay = stats_overlay;
    devshot_log_overlay(kind);
  } else if (strcmp(kind, "overlay") == 0) {
    devshot_sample_overlay(arg);
  }
}
