#include "ui_boxart.h"

#include "guilib.h"

#include "../config.h"
#include "../connection.h"
#include "../debug.h"

#include "client.h"
#include "errors.h"

#include <math.h>
#include <png.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>

// Tile size on screen. Sunshine's covers are 3:4, usually 600x800.
#define TILE_W 150
#define TILE_H 200
#define TILE_GAP 18
#define TILE_SPACING (TILE_W + TILE_GAP)
// The selected tile is drawn this much larger
#define SELECTED_SCALE 1.12f
// Covers are shrunk once, when loaded, to the selected tile's size: about
// 150 KB of texture each instead of 1.9 MB for a full 600x800 cover
#define TEX_W 168
#define TEX_H 224
// Covers held in memory either side of the selection: the 7 that fit on
// screen, plus a couple more each side so scrolling shows art straight away
#define LOADED_EACH_SIDE 5
// Covers downloaded ahead to the memory card either side of the selection. Not
// the whole list, which on some hosts runs to hundreds of apps.
#define PREFETCH_EACH_SIDE 10
// Decoding needs the whole image in memory at 4 bytes a pixel, so larger
// images (8 MB and up) are shown as a plain card instead
#define MAX_SOURCE_PIXELS (2100 * 1000)
#define MAX_DOWNLOAD_BYTES (8 * 1024 * 1024)
#define FETCH_TIMEOUT_SECONDS 10
// At most this many textures are created per frame, to keep scrolling smooth
#define UPLOADS_PER_FRAME 2
// Where the row sits: the bottom of the screen, under the menu
#define ROW_CENTRE_Y 390
#define NAME_BASELINE_Y 530
#define LABEL_LINES 3
#define LABEL_LINE_HEIGHT 22
#define LABEL_LINE_MAX 64

// The loader runs below the menu's priority (higher numbers are lower on the
// Vita), with room on its stack for TLS and PNG decoding
#define LOADER_PRIORITY (0x10000100 + 16)
#define LOADER_STACK_SIZE (256 * 1024)

#define COLOUR_CARD RGBA8(0x2A, 0x2A, 0x2A, 0xFF)
#define COLOUR_LABEL RGBA8(0xDD, 0xDD, 0xDD, 0xFF)
#define COLOUR_DIM RGBA8(0x00, 0x00, 0x00, 0xA0)
#define COLOUR_FOCUS 0xff00ff00 // the menu's selection green
#define COLOUR_SELECTED RGBA8(0xAA, 0xAA, 0xAA, 0xFF)

enum {
  FETCH_UNKNOWN, // not looked for yet
  FETCH_CACHED,  // on the memory card
  FETCH_NONE,    // the host has none, or it failed; not retried until next time
  FETCH_STALE,   // the player asked for it again: download over the saved one
};

typedef struct {
  int id;
  char *name;

  // Shared with the loader thread, under lock
  int fetch;
  bool loaded;         // decoded (or tried); cleared when it leaves the window
  bool busy;           // the loader is working on it
  unsigned generation; // bumped to throw away work in progress
  uint32_t *pixels;    // shrunk cover waiting for the main thread to upload
  int pixels_w, pixels_h;

  // Main thread only
  vita2d_texture *texture;
  bool label_ready;
  int label_count;
  char label[LABEL_LINES][LABEL_LINE_MAX];
  int label_width[LABEL_LINES];
} cover_slot;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static cover_slot *slots;
static int slot_count;
static int selected;
static volatile bool stop_requested;
static SceUID loader_thread = -1;

static PSERVER_DATA host_server;
static char host_dir[512];
static float view_position;
static bool focused;
static bool locked;
static int running_id;
static bool refresh_requested;
static char counter_text[48];

// The selection is kept across menu rebuilds (after a stream, say) for the same host
static char remembered_host[256];
static int remembered_app;

static bool in_window(int index) {
  return abs(index - selected) <= LOADED_EACH_SIDE;
}

static bool cover_path(char *out, size_t len, int app_id) {
  int written = snprintf(out, len, "%s/%d.png", host_dir, app_id);
  return written > 0 && (size_t)written < len;
}

static bool file_exists(const char *path) {
  SceIoStat stat;
  return sceIoGetstat(path, &stat) >= 0 && stat.st_size > 0;
}

// Sunshine sends its own small grey cover (about 680 bytes) for apps that
// have no artwork set. Every such app would look the same, so anything this
// small counts as no artwork, and the app gets a card with its name instead.
#define NO_ARTWORK_MAX_BYTES 1024

// Downloads a cover to the memory card. Returns FETCH_CACHED or FETCH_NONE.
static int download_cover(int app_id, const char *path) {
  static const unsigned char png_signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  char *image = NULL;
  size_t size = 0;
  int ret = gs_app_boxart(host_server, app_id, FETCH_TIMEOUT_SECONDS, &image, &size);
  if (ret != GS_OK) {
    vita_debug_log("[BOXART] app %d: download failed (%d)", app_id, ret);
    return FETCH_NONE;
  }
  // Only keep real PNGs, so an error page is never saved as a cover
  if (size < sizeof(png_signature) || size > MAX_DOWNLOAD_BYTES ||
      memcmp(image, png_signature, sizeof(png_signature)) != 0) {
    vita_debug_log("[BOXART] app %d: not a usable PNG (%u bytes)", app_id, (unsigned)size);
    free(image);
    return FETCH_NONE;
  }
  if (size <= NO_ARTWORK_MAX_BYTES) {
    vita_debug_log("[BOXART] app %d: no artwork on the host (%u bytes), showing its name", app_id, (unsigned)size);
    free(image);
    return FETCH_NONE;
  }

  // Write to a temporary file first, so a power cut never leaves half a cover
  char temp_path[640];
  int temp_len = snprintf(temp_path, sizeof(temp_path), "%s.part", path);
  bool written = false;
  FILE *file = temp_len > 0 && (size_t)temp_len < sizeof(temp_path) ? fopen(temp_path, "wb") : NULL;
  if (file != NULL) {
    written = fwrite(image, 1, size, file) == size;
    written = fclose(file) == 0 && written;
  }
  free(image);
  sceIoRemove(path);
  if (!written || sceIoRename(temp_path, path) < 0) {
    vita_debug_log("[BOXART] app %d: couldn't save %s", app_id, path);
    sceIoRemove(temp_path);
    return FETCH_NONE;
  }
  vita_debug_log("[BOXART] app %d: saved %u bytes", app_id, (unsigned)size);
  return FETCH_CACHED;
}

// Box-filters an RGBA image down to dst_w x dst_h, weighting colour by alpha
// so transparent edges don't darken
static uint32_t *shrink(const uint8_t *src, int src_w, int src_h, int dst_w, int dst_h) {
  uint32_t *dst = malloc((size_t)dst_w * dst_h * sizeof(uint32_t));
  if (dst == NULL) {
    return NULL;
  }
  for (int y = 0; y < dst_h; y++) {
    int y0 = y * src_h / dst_h;
    int y1 = (y + 1) * src_h / dst_h;
    y1 = y1 > y0 ? y1 : y0 + 1;
    for (int x = 0; x < dst_w; x++) {
      int x0 = x * src_w / dst_w;
      int x1 = (x + 1) * src_w / dst_w;
      x1 = x1 > x0 ? x1 : x0 + 1;
      uint32_t r = 0, g = 0, b = 0, a = 0, n = 0;
      for (int sy = y0; sy < y1; sy++) {
        const uint8_t *p = src + ((size_t)sy * src_w + x0) * 4;
        for (int sx = x0; sx < x1; sx++, p += 4) {
          r += p[0] * p[3];
          g += p[1] * p[3];
          b += p[2] * p[3];
          a += p[3];
          n++;
        }
      }
      uint32_t pixel = 0;
      if (a > 0) {
        // Bytes in memory are R, G, B, A, which is vita2d's default texture format
        pixel = (r / a) | ((g / a) << 8) | ((b / a) << 16) | ((a / n) << 24);
      }
      dst[(size_t)y * dst_w + x] = pixel;
    }
  }
  return dst;
}

// Decodes a saved cover and shrinks it to fit TEX_W x TEX_H, keeping its shape
static bool decode_cover(const char *path, uint32_t **pixels, int *width, int *height) {
  // A cover cached before small ones counted as no artwork: drop it like a bad one
  SceIoStat stat;
  if (sceIoGetstat(path, &stat) >= 0 && stat.st_size <= NO_ARTWORK_MAX_BYTES) {
    return false;
  }
  png_image image;
  memset(&image, 0, sizeof(image));
  image.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_file(&image, path)) {
    return false;
  }
  if (image.width == 0 || image.height == 0 ||
      (uint64_t)image.width * image.height > MAX_SOURCE_PIXELS) {
    vita_debug_log("[BOXART] %s is %ux%u, too large to show", path, image.width, image.height);
    png_image_free(&image);
    return false;
  }
  image.format = PNG_FORMAT_RGBA;
  uint8_t *full = malloc(PNG_IMAGE_SIZE(image));
  if (full == NULL) {
    png_image_free(&image);
    return false;
  }
  // finish_read releases the image's own state whether or not it succeeds
  if (!png_image_finish_read(&image, NULL, full, 0, NULL)) {
    free(full);
    return false;
  }

  int src_w = image.width;
  int src_h = image.height;
  float scale = fminf((float)TEX_W / src_w, (float)TEX_H / src_h);
  scale = scale < 1.0f ? scale : 1.0f;
  int dst_w = (int)(src_w * scale + 0.5f);
  int dst_h = (int)(src_h * scale + 0.5f);
  dst_w = dst_w > 0 ? dst_w : 1;
  dst_h = dst_h > 0 ? dst_h : 1;
  *pixels = shrink(full, src_w, src_h, dst_w, dst_h);
  free(full);
  *width = dst_w;
  *height = dst_h;
  return *pixels != NULL;
}

// Chooses the next cover to work on, nearest the selection first: one to load
// into memory, else one to download ahead. Call with the lock held.
static int pick_job(bool *load) {
  for (int d = 0; d <= LOADED_EACH_SIDE; d++) {
    int candidates[2] = {selected - d, selected + d};
    for (int c = 0; c < (d == 0 ? 1 : 2); c++) {
      int i = candidates[c];
      if (i >= 0 && i < slot_count && !slots[i].loaded && !slots[i].busy && slots[i].fetch != FETCH_NONE) {
        *load = true;
        return i;
      }
    }
  }
  for (int d = 0; d <= PREFETCH_EACH_SIDE; d++) {
    int candidates[2] = {selected - d, selected + d};
    for (int c = 0; c < (d == 0 ? 1 : 2); c++) {
      int i = candidates[c];
      if (i >= 0 && i < slot_count && !slots[i].busy &&
          (slots[i].fetch == FETCH_UNKNOWN || slots[i].fetch == FETCH_STALE)) {
        *load = false;
        return i;
      }
    }
  }
  return -1;
}

static void run_job(int index, bool load) {
  pthread_mutex_lock(&lock);
  int app_id = slots[index].id;
  int fetch = slots[index].fetch;
  unsigned generation = slots[index].generation;
  pthread_mutex_unlock(&lock);

  char path[600];
  if (!cover_path(path, sizeof(path), app_id)) {
    fetch = FETCH_NONE;
  } else if (fetch == FETCH_UNKNOWN && file_exists(path)) {
    fetch = FETCH_CACHED;
  } else if (fetch == FETCH_UNKNOWN || fetch == FETCH_STALE) {
    fetch = download_cover(app_id, path);
  }

  uint32_t *pixels = NULL;
  int width = 0, height = 0;
  if (load && fetch == FETCH_CACHED && !decode_cover(path, &pixels, &width, &height)) {
    // A damaged file: remove it so the next visit downloads it again
    vita_debug_log("[BOXART] app %d: couldn't decode %s", app_id, path);
    sceIoRemove(path);
    fetch = FETCH_NONE;
  }

  pthread_mutex_lock(&lock);
  cover_slot *slot = &slots[index];
  slot->busy = false;
  // A refresh while this ran makes the result stale; the slot is redone
  if (slot->generation == generation) {
    slot->fetch = fetch;
    if (load && in_window(index)) {
      slot->loaded = true;
      free(slot->pixels);
      slot->pixels = pixels;
      slot->pixels_w = width;
      slot->pixels_h = height;
      pixels = NULL;
    }
  }
  pthread_mutex_unlock(&lock);
  free(pixels);
}

static int loader_main(SceSize args, void *argp) {
  while (!stop_requested) {
    // Keep the network to the stream while one is playing
    if (connection_get_status() == LI_CONNECTED) {
      sceKernelDelayThread(200 * 1000);
      continue;
    }

    bool load = false;
    pthread_mutex_lock(&lock);
    int index = pick_job(&load);
    if (index >= 0) {
      slots[index].busy = true;
    }
    pthread_mutex_unlock(&lock);

    if (index < 0) {
      sceKernelDelayThread(50 * 1000);
      continue;
    }
    run_job(index, load);
  }
  return 0;
}

static void update_counter(void) {
  snprintf(counter_text, sizeof(counter_text), "%d of %d " ICON_LEFT_RIGHT_ARROWS, selected + 1, slot_count);
}

void ui_boxart_stop_loading(void) {
  if (loader_thread < 0) {
    return;
  }
  stop_requested = true;
  // Waits for any download in progress, which gives up after FETCH_TIMEOUT_SECONDS
  sceKernelWaitThreadEnd(loader_thread, NULL, NULL);
  sceKernelDeleteThread(loader_thread);
  loader_thread = -1;
}

void ui_boxart_close(void) {
  ui_boxart_stop_loading();
  for (int i = 0; i < slot_count; i++) {
    if (slots[i].texture != NULL) {
      vita2d_free_texture(slots[i].texture);
    }
    free(slots[i].pixels);
    free(slots[i].name);
  }
  free(slots);
  slots = NULL;
  slot_count = 0;
}

void ui_boxart_open(PSERVER_DATA server, const char *host, PAPP_LIST apps, int running_app_id) {
  ui_boxart_close();

  int count = 0;
  for (PAPP_LIST app = apps; app != NULL; app = app->next) {
    count++;
  }
  slots = calloc(count > 0 ? count : 1, sizeof(cover_slot));
  if (slots == NULL) {
    return;
  }
  for (PAPP_LIST app = apps; app != NULL; app = app->next) {
    cover_slot *slot = &slots[slot_count++];
    slot->id = app->id;
    slot->name = strdup(app->name ? app->name : "");
    if (slot->name == NULL) {
      // Out of memory: the labels would dereference it, so show no covers
      ui_boxart_close();
      return;
    }
  }

  if (host == NULL || host[0] == '\0') {
    host = "unknown";
  }
  if (strcmp(host, remembered_host) != 0) {
    snprintf(remembered_host, sizeof(remembered_host), "%s", host);
    remembered_app = 0;
  }
  running_id = running_app_id;
  locked = running_app_id != 0;
  // Start on the running app, else where the player was last time
  int start_id = locked ? running_app_id : remembered_app;
  selected = 0;
  for (int i = 0; i < slot_count; i++) {
    if (slots[i].id == start_id) {
      selected = i;
    }
  }
  view_position = selected;
  focused = false;
  refresh_requested = false;
  update_counter();

  host_server = server;
  char boxart_dir[512];
  int dir_len = snprintf(boxart_dir, sizeof(boxart_dir), "%s/boxart", config.key_dir);
  int host_len = snprintf(host_dir, sizeof(host_dir), "%s/%s", boxart_dir, host);
  if (dir_len <= 0 || (size_t)dir_len >= sizeof(boxart_dir) ||
      host_len <= 0 || (size_t)host_len >= sizeof(host_dir)) {
    // Too long to be a real path; show the names on plain cards
    vita_debug_log("[BOXART] cover folder path too long, not loading covers");
    return;
  }
  // Either may exist already, which is fine
  sceIoMkdir(boxart_dir, 0777);
  sceIoMkdir(host_dir, 0777);

  stop_requested = false;
  loader_thread = sceKernelCreateThread("boxart_loader", loader_main, LOADER_PRIORITY, LOADER_STACK_SIZE, 0, 0, NULL);
  if (loader_thread >= 0) {
    sceKernelStartThread(loader_thread, 0, NULL);
  } else {
    vita_debug_log("[BOXART] couldn't start the loader thread: 0x%08X", loader_thread);
  }
}

void ui_boxart_set_focus(bool is_focused) {
  focused = is_focused;
}

void ui_boxart_input(int buttons) {
  if (slot_count == 0 || locked) {
    return;
  }
  int next = selected;
  // Held directions repeat, so the whole row can be scrolled quickly
  if (buttons & SCE_CTRL_LEFT) {
    next--;
  } else if (buttons & SCE_CTRL_RIGHT) {
    next++;
  } else if (buttons & SCE_CTRL_LTRIGGER) {
    next -= 5;
  } else if (buttons & SCE_CTRL_RTRIGGER) {
    next += 5;
  }
  next = next < 0 ? 0 : next;
  next = next >= slot_count ? slot_count - 1 : next;
  if (next != selected) {
    pthread_mutex_lock(&lock);
    selected = next;
    pthread_mutex_unlock(&lock);
    remembered_app = slots[selected].id;
    update_counter();
  }
  if ((buttons & SCE_CTRL_TRIANGLE) && (buttons & SCE_CTRL_HOLD) == 0) {
    // Handled at the start of the next frame, as this one may have drawn the old cover
    refresh_requested = true;
  }
}

int ui_boxart_selected_app(void) {
  if (slot_count == 0 || locked) {
    return 0;
  }
  return slots[selected].id;
}

char *ui_boxart_counter(void) {
  return counter_text;
}

// Frees covers that left the window and uploads ones the loader finished. Runs
// at the start of a frame, when the GPU has finished with the last one.
static void update_textures(void) {
  pthread_mutex_lock(&lock);
  if (refresh_requested) {
    refresh_requested = false;
    cover_slot *slot = &slots[selected];
    slot->generation++;
    slot->fetch = FETCH_STALE;
    slot->loaded = false;
    free(slot->pixels);
    slot->pixels = NULL;
    if (slot->texture != NULL) {
      vita2d_free_texture(slot->texture);
      slot->texture = NULL;
    }
  }

  int uploads = 0;
  for (int i = 0; i < slot_count; i++) {
    cover_slot *slot = &slots[i];
    if (!in_window(i)) {
      if (slot->texture != NULL) {
        vita2d_free_texture(slot->texture);
        slot->texture = NULL;
      }
      free(slot->pixels);
      slot->pixels = NULL;
      slot->loaded = false;
      continue;
    }
    if (slot->pixels == NULL || uploads >= UPLOADS_PER_FRAME) {
      continue;
    }
    uploads++;
    vita2d_texture *texture = vita2d_create_empty_texture(slot->pixels_w, slot->pixels_h);
    if (texture != NULL) {
      unsigned int stride = vita2d_texture_get_stride(texture);
      uint8_t *data = vita2d_texture_get_datap(texture);
      for (int y = 0; y < slot->pixels_h; y++) {
        memcpy(data + (size_t)y * stride, slot->pixels + (size_t)y * slot->pixels_w,
               slot->pixels_w * sizeof(uint32_t));
      }
      // Tiles are drawn a little smaller than the texture
      vita2d_texture_set_filters(texture, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
      if (slot->texture != NULL) {
        vita2d_free_texture(slot->texture);
      }
      slot->texture = texture;
    }
    free(slot->pixels);
    slot->pixels = NULL;
  }
  pthread_mutex_unlock(&lock);
}

static void prepare_label(cover_slot *slot) {
  char lines[LABEL_LINES + 1][WRAP_LINE_MAX];
  int count = wrap_text(slot->name, TILE_W - 16, lines, LABEL_LINES + 1);
  if (count > LABEL_LINES) {
    // Longer names end in an ellipsis on the last line
    count = LABEL_LINES;
    size_t len = strlen(lines[count - 1]);
    snprintf(lines[count - 1] + (len > WRAP_LINE_MAX - 4 ? WRAP_LINE_MAX - 4 : len), 4, "...");
  }
  slot->label_count = count;
  for (int i = 0; i < count; i++) {
    snprintf(slot->label[i], LABEL_LINE_MAX, "%s", lines[i]);
    slot->label_width[i] = vita2d_font_text_width(font, 18, slot->label[i]);
  }
  slot->label_ready = true;
}

static void draw_outline(float x, float y, float w, float h, int thickness, unsigned int colour) {
  vita2d_draw_rectangle(x - thickness, y - thickness, w + thickness * 2, thickness, colour);
  vita2d_draw_rectangle(x - thickness, y + h, w + thickness * 2, thickness, colour);
  vita2d_draw_rectangle(x - thickness, y, thickness, h, colour);
  vita2d_draw_rectangle(x + w, y, thickness, h, colour);
}

static void draw_tile(int index) {
  cover_slot *slot = &slots[index];
  float distance = index - view_position;
  float cx = WIDTH / 2.0f + distance * TILE_SPACING;
  if (cx < -TILE_W || cx > WIDTH + TILE_W) {
    return;
  }
  // Grows smoothly as it slides into the middle
  float closeness = 1.0f - fminf(fabsf(distance), 1.0f);
  float scale = 1.0f + (SELECTED_SCALE - 1.0f) * closeness;
  float w = TILE_W * scale;
  float h = TILE_H * scale;
  float x = cx - w / 2.0f;
  float y = ROW_CENTRE_Y - h / 2.0f;

  vita2d_draw_rectangle(x, y, w, h, COLOUR_CARD);
  if (slot->texture != NULL) {
    float tw = vita2d_texture_get_width(slot->texture);
    float th = vita2d_texture_get_height(slot->texture);
    float s = fminf(w / tw, h / th);
    vita2d_draw_texture_scale(slot->texture, x + (w - tw * s) / 2.0f, y + (h - th * s) / 2.0f, s, s);
  } else {
    // The app's name on a plain card, while its cover loads or if it has none
    if (!slot->label_ready) {
      prepare_label(slot);
    }
    int top = ROW_CENTRE_Y - slot->label_count * LABEL_LINE_HEIGHT / 2 + 16;
    for (int i = 0; i < slot->label_count; i++) {
      vita2d_font_draw_text(font, (int)(cx - slot->label_width[i] / 2.0f), top + i * LABEL_LINE_HEIGHT,
                            COLOUR_LABEL, 18, slot->label[i]);
    }
  }

  if (locked && slot->id != running_id) {
    // Another app is streaming, so these can't be started
    vita2d_draw_rectangle(x, y, w, h, COLOUR_DIM);
  }
  if (index == selected) {
    draw_outline(x, y, w, h, 3, focused ? COLOUR_FOCUS : COLOUR_SELECTED);
  }
}

void ui_boxart_draw(void) {
  if (slots == NULL || slot_count == 0) {
    return;
  }
  update_textures();

  // Ease towards the selection rather than jumping
  view_position += (selected - view_position) * 0.25f;
  if (fabsf(selected - view_position) < 0.01f) {
    view_position = selected;
  }

  for (int i = 0; i < slot_count; i++) {
    if (i != selected) {
      draw_tile(i);
    }
  }
  // Last, so its outline sits on top of its neighbours
  draw_tile(selected);

  const char *name = slots[selected].name;
  int name_width = vita2d_font_text_width(font, 18, name);
  vita2d_font_draw_text(font, WIDTH / 2 - name_width / 2, NAME_BASELINE_Y, 0xffffffff, 18, name);

  if (focused && !locked) {
    const char *hint = "Refresh cover";
    int hint_width = vita2d_font_text_width(font, 18, hint);
    int x = WIDTH - 20 - hint_width - 14 - 6;
    x += draw_button_glyph(BUTTON_GLYPH_TRIANGLE, x, NAME_BASELINE_Y, 14) + 6;
    vita2d_font_draw_text(font, x, NAME_BASELINE_Y, COLOUR_LABEL, 18, hint);
  }
}
