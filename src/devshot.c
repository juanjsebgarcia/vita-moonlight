#include "devshot.h"

#include "config.h"
#include "stats.h"
#include "gui/guilib.h"
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
  char *arg = strchr(text, ' ');
  if (arg != NULL) {
    *arg++ = '\0';
  } else {
    arg = text + out;
  }

  if (strcmp(text, "flash") == 0) {
    for (int i = 0; i < DEVSHOT_HOLD_FRAMES; i++) {
      flash_message("%s", arg);
    }
  } else if (strcmp(text, "error") == 0) {
    display_error("%s", arg);
  } else if (strcmp(text, "confirm") == 0) {
    display_confirm(arg);
  } else if (strcmp(text, "help") == 0) {
    char *body = strchr(arg, '|');
    if (body != NULL) {
      *body++ = '\0';
    }
    display_help(arg, body ? body : "");
  } else if (strcmp(text, "overlay") == 0) {
    // In memory only: the saved settings are left alone
    int stats_overlay = config.stats_overlay;
    config.stats_overlay = STATS_OVERLAY_FPS;
    dc_tracker.currently_sprinting = true;
    for (int i = 0; i < DEVSHOT_HOLD_FRAMES; i++) {
      ui_start();
      draw_fps();
      draw_indicators();
      ui_end();
    }
    dc_tracker.currently_sprinting = false;
    config.stats_overlay = stats_overlay;
  }
}
