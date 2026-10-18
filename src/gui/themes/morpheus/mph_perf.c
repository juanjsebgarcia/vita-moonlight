// Morpheus developer HUD: what a frame costs, averaged over about a second.
// Shown in developer mode, in the bottom left corner.

#include "mph.h"
#include "mph_internal.h"

#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <vita2d.h>

mph_perf_counters mph_perf;

// vita2d_init()'s frame pool
#define POOL_BYTES (1024 * 1024)

static uint64_t frame_start, last_start;

// Sums over the current window, and the last window's averages
static struct {
  int frames;
  uint64_t cpu, interval, cpu_max;
  int draws, vertices, glyphs, pool, pool_max;
} sum;
static char line[96];

void mph_perf_begin(void) {
  frame_start = sceKernelGetProcessTimeWide();
  mph_perf = (mph_perf_counters){0};
}

void mph_perf_end(bool hud) {
  uint64_t now = sceKernelGetProcessTimeWide();
  uint64_t cpu = now - frame_start;
  uint64_t interval = last_start ? frame_start - last_start : 0;
  last_start = frame_start;
  int pool = POOL_BYTES - (int)vita2d_pool_free_space();

  sum.frames++;
  sum.cpu += cpu;
  sum.cpu_max = cpu > sum.cpu_max ? cpu : sum.cpu_max;
  sum.interval += interval;
  sum.draws += mph_perf.draws;
  sum.vertices += mph_perf.vertices;
  sum.glyphs += mph_perf.glyphs;
  sum.pool += pool;
  sum.pool_max = pool > sum.pool_max ? pool : sum.pool_max;
  if (sum.interval >= 1000000 || line[0] == '\0') {
    int n = sum.frames;
    snprintf(line, sizeof(line), "cpu %llu/%lluus  frame %.1fms  draws %d  verts %d  glyphs %d  pool %dK/%dK",
             (unsigned long long)(sum.cpu / n), (unsigned long long)sum.cpu_max, sum.interval / (n * 1000.0),
             sum.draws / n, sum.vertices / n, sum.glyphs / n, sum.pool / n / 1024, sum.pool_max / 1024);
    sum = (typeof(sum)){0};
  }
  if (hud) {
    mph_perf_hud();
  }
}

void mph_perf_hud(void) {
  // Drawn after the frame was measured, so it costs nothing in its own numbers
  int w = mph_text_w(MPH_T_MONO, line);
  mph_rrect_fill(mph_rect_make(6, 518, w + 16, 22), 6, MPH_C_OVERLAY_BG);
  mph_text(MPH_T_MONO, 14, mph_baseline(MPH_T_MONO, 518, 22), MPH_C_OK, line);
}
