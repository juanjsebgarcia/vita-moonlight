#include "devshot.h"

#include "config.h"

#include <stdbool.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdio.h>
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
