// Morpheus: the frame, the batch, shapes and textures.
//
// Shapes are triangles with a colour per vertex, written straight into
// vita2d's frame pool and drawn in runs: one vita2d_draw_array() for every
// shape drawn in a row. Text, icons and covers are textured triangles, run
// by (texture, tint) the same way. A run ends when the next draw needs the
// other kind, another texture or another tint, so painter's order holds.

#include "mph.h"
#include "mph_internal.h"

#include <math.h>
#include <psp2/kernel/processmgr.h>
#include <string.h>

/*
 * Time
 */

static uint64_t time_origin, time_last;
static float time_now, time_step;

float mph_time(void) {
  return time_now;
}

float mph_dt(void) {
  return time_step;
}

static void sample_time(void) {
  uint64_t now = sceKernelGetProcessTimeWide();
  float step = (float)(now - time_last) / 1e6f;
  time_last = now;
  time_step = step < 0.0f ? 0.0f : step > 0.05f ? 0.05f : step;
  time_now = (float)(now - time_origin) / 1e6f;
}

/*
 * The batch
 *
 * Shapes go straight into pool chunks as one run. Textured quads (text,
 * icons, covers) collect in staging runs, one per (texture, tint), and are
 * copied into the pool when drawn. A shape flushes every textured run first,
 * and a textured quad flushes the shapes first, so text always lands on the
 * shapes drawn before it. Between two shapes, text and icons of the same
 * colour from anywhere on screen become one draw call.
 */

// Pool chunks the shapes are written into
#define CHUNK_BYTES (64 * 1024)

static uint8_t *chunk_end, *run_start, *cursor;
// Written to when the pool is full, and never drawn
static uint8_t dropped[CHUNK_BYTES] __attribute__((aligned(16)));
static bool run_dropped;

#define TEX_RUNS 8
#define TEX_RUN_VERTICES 1536  // 256 quads

typedef struct {
  const vita2d_texture *texture;
  uint32_t tint;
  int count;
  vita2d_texture_vertex v[TEX_RUN_VERTICES];
} tex_run;

static tex_run tex_runs[TEX_RUNS];
static int tex_run_count;  // open runs, in the order they were opened

static void flush_colour(void) {
  if (cursor > run_start && !run_dropped) {
    int count = (int)((cursor - run_start) / sizeof(vita2d_color_vertex));
    vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, (const vita2d_color_vertex *)run_start, count);
    mph_perf.draws++;
    mph_perf.vertices += count;
  }
  run_start = cursor;
}

static void flush_tex_run(tex_run *r) {
  if (r->count > 0) {
    size_t bytes = (size_t)r->count * sizeof(vita2d_texture_vertex);
    void *v = vita2d_pool_memalign(bytes, 16);
    if (v != NULL) {
      memcpy(v, r->v, bytes);
      vita2d_draw_array_textured(r->texture, SCE_GXM_PRIMITIVE_TRIANGLES, v, r->count, r->tint);
      mph_perf.draws++;
      mph_perf.vertices += r->count;
    }
  }
  r->count = 0;
}

static void flush_textured(void) {
  for (int i = 0; i < tex_run_count; i++) {
    flush_tex_run(&tex_runs[i]);
  }
  tex_run_count = 0;
}

void mph_flush(void) {
  flush_textured();
  flush_colour();
}

// Room for n colour vertices in the shapes' run
static vita2d_color_vertex *reserve_colour(int n) {
  if (tex_run_count > 0) {
    flush_textured();
  }
  size_t bytes = (size_t)n * sizeof(vita2d_color_vertex);
  if (bytes > CHUNK_BYTES) {
    return NULL;
  }
  if (cursor == NULL || cursor + bytes > chunk_end) {
    flush_colour();
    uint8_t *chunk = vita2d_pool_memalign(CHUNK_BYTES, 16);
    run_dropped = chunk == NULL;
    if (chunk == NULL) {
      chunk = dropped;
    }
    chunk_end = chunk + CHUNK_BYTES;
    cursor = run_start = chunk;
  }
  void *p = cursor;
  cursor += bytes;
  return p;
}

// Room for n textured vertices in the run for (texture, tint)
static vita2d_texture_vertex *reserve_textured(const vita2d_texture *texture, uint32_t tint, int n) {
  if (cursor > run_start) {
    flush_colour();
  }
  if (n > TEX_RUN_VERTICES) {
    return NULL;
  }
  tex_run *r = NULL;
  for (int i = 0; i < tex_run_count; i++) {
    if (tex_runs[i].texture == texture && tex_runs[i].tint == tint) {
      r = &tex_runs[i];
      break;
    }
  }
  if (r == NULL) {
    if (tex_run_count == TEX_RUNS) {
      flush_textured();
    }
    r = &tex_runs[tex_run_count++];
    r->texture = texture;
    r->tint = tint;
    r->count = 0;
  }
  if (r->count + n > TEX_RUN_VERTICES) {
    flush_tex_run(r);
  }
  vita2d_texture_vertex *v = &r->v[r->count];
  r->count += n;
  return v;
}

static void batch_reset(void) {
  chunk_end = run_start = cursor = NULL;
  run_dropped = false;
  tex_run_count = 0;
}

/*
 * The transform
 */

static float tf_alpha = 1.0f, tf_scale = 1.0f, tf_cx, tf_cy, tf_dx, tf_dy;
static bool tf_on;

void mph_transform(float alpha, float scale, float cx, float cy, float dx, float dy) {
  tf_alpha = alpha < 0.0f ? 0.0f : alpha > 1.0f ? 1.0f : alpha;
  tf_scale = scale;
  tf_cx = cx;
  tf_cy = cy;
  tf_dx = dx;
  tf_dy = dy;
  tf_on = scale != 1.0f || dx != 0.0f || dy != 0.0f;
}

void mph_transform_reset(void) {
  mph_transform(1.0f, 1.0f, 0, 0, 0, 0);
}

static inline float tx(float x) {
  return tf_on ? tf_cx + (x - tf_cx) * tf_scale + tf_dx : x;
}

static inline float ty(float y) {
  return tf_on ? tf_cy + (y - tf_cy) * tf_scale + tf_dy : y;
}

uint32_t mph_tint(uint32_t c) {
  return tf_alpha >= 1.0f ? c : mph_alpha(c, tf_alpha);
}

float mph_transform_scale(void) {
  return tf_scale;
}

static inline void cv(vita2d_color_vertex *v, float x, float y, uint32_t c) {
  v->x = tx(x);
  v->y = ty(y);
  v->z = 0.5f;
  v->color = c;
}

static inline void tv(vita2d_texture_vertex *v, float x, float y, float u, float w) {
  v->x = tx(x);
  v->y = ty(y);
  v->z = 0.5f;
  v->u = u;
  v->v = w;
}

void mph_quad_textured(const vita2d_texture *t, uint32_t tint, float x0, float y0, float x1, float y1, float u0,
                       float v0, float u1, float v1) {
  vita2d_texture_vertex *v = reserve_textured(t, mph_tint(tint), 6);
  if (v == NULL) {
    return;
  }
  tv(&v[0], x0, y0, u0, v0);
  tv(&v[1], x1, y0, u1, v0);
  tv(&v[2], x0, y1, u0, v1);
  tv(&v[3], x1, y0, u1, v0);
  tv(&v[4], x1, y1, u1, v1);
  tv(&v[5], x0, y1, u0, v1);
}

/*
 * Clipping
 */

void mph_clip(mph_rect r) {
  mph_flush();
  int x0 = (int)floorf(tx(r.x)), y0 = (int)floorf(ty(r.y));
  int x1 = (int)ceilf(tx(r.x + r.w)), y1 = (int)ceilf(ty(r.y + r.h));
  vita2d_set_clip_rectangle(x0 < 0 ? 0 : x0, y0 < 0 ? 0 : y0, x1 > MPH_W ? MPH_W : x1, y1 > MPH_H ? MPH_H : y1);
  vita2d_enable_clipping();
  mph_perf.draws += 2;
}

void mph_clip_end(void) {
  mph_flush();
  vita2d_disable_clipping();
}

/*
 * Shapes
 */

// Unit quarter circles: QUARTER[s][k] is the angle k/s of a right angle
#define MAX_SEGS 16
static float quarter_cos[MAX_SEGS + 1][MAX_SEGS + 1], quarter_sin[MAX_SEGS + 1][MAX_SEGS + 1];
#define MAX_POINTS (4 * (MAX_SEGS + 1))
#define MAX_RINGS 5

static void init_quarters(void) {
  for (int s = 1; s <= MAX_SEGS; s++) {
    for (int k = 0; k <= s; k++) {
      float a = (float)k / s * (float)M_PI_2;
      quarter_cos[s][k] = cosf(a);
      quarter_sin[s][k] = sinf(a);
    }
  }
}

static int segments_for(float radius) {
  int s = (int)ceilf(radius / 2.5f);
  return s < 3 ? 3 : s > MAX_SEGS ? MAX_SEGS : s;
}

static inline uint32_t lerp_colour(uint32_t a, uint32_t b, float t) {
  if (a == b) {
    return a;
  }
  t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
  uint32_t out = 0;
  for (int shift = 0; shift < 32; shift += 8) {
    float ca = (float)((a >> shift) & 0xFF), cb = (float)((b >> shift) & 0xFF);
    out |= ((uint32_t)(ca + (cb - ca) * t + 0.5f) & 0xFF) << shift;
  }
  return out;
}

static inline uint32_t scale_alpha(uint32_t c, float a) {
  return a >= 1.0f ? c : mph_alpha(c, a);
}

// The outline of r grown by off (shrunk when negative), with each corner's
// radius grown with it: corners top left, top right, bottom right, bottom left
static int perimeter(mph_rect r, const float radius[4], float off, int segs, float *xs, float *ys) {
  float min_half = (r.w < r.h ? r.w : r.h) / 2.0f;
  if (off < -min_half + 0.01f) {
    off = -min_half + 0.01f;
  }
  float x0 = r.x - off, y0 = r.y - off, x1 = r.x + r.w + off, y1 = r.y + r.h + off;
  float limit = ((x1 - x0) < (y1 - y0) ? (x1 - x0) : (y1 - y0)) / 2.0f;
  // Corner centres and directions, going clockwise from the left edge
  const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
  int n = 0;
  for (int c = 0; c < 4; c++) {
    float re = radius[c] + off;
    re = re < 0.0f ? 0.0f : re > limit ? limit : re;
    float cx = sx[c] < 0 ? x0 + re : x1 - re;
    float cy = sy[c] < 0 ? y0 + re : y1 - re;
    for (int k = 0; k <= segs; k++) {
      // Top left runs from the left (k=0) to the top; each corner continues clockwise
      float co = quarter_cos[segs][k], si = quarter_sin[segs][k];
      float dx, dy;
      switch (c) {
        case 0: dx = -co; dy = -si; break;
        case 1: dx = si; dy = -co; break;
        case 2: dx = co; dy = si; break;
        default: dx = -si; dy = co; break;
      }
      xs[n] = cx + dx * re;
      ys[n] = cy + dy * re;
      n++;
    }
  }
  return n;
}

// A rounded rect as concentric outlines at offsets offs[] (inner first) with
// alpha factors alphas[], filled inside the first when fill. Colour runs
// from top to bottom over r.
static void rrect_rings(mph_rect r, const float radius[4], const float *offs, const float *alphas, int rings, bool fill,
                        uint32_t top, uint32_t bottom) {
  top = mph_tint(top);
  bottom = mph_tint(bottom);
  if (((top | bottom) >> 24) == 0 || r.w <= 0 || r.h <= 0) {
    return;
  }
  float rmax = radius[0];
  for (int c = 1; c < 4; c++) {
    rmax = radius[c] > rmax ? radius[c] : rmax;
  }
  int segs = segments_for(rmax + offs[rings - 1]);
  // Wide soft edges (shadows) hide their facets: fewer segments do
  if (offs[rings - 1] > 2.0f && segs > 8) {
    segs = 8;
  }
  int points = 4 * (segs + 1);
  int count = (fill ? points * 3 : 0) + (rings - 1) * points * 6;
  vita2d_color_vertex *v = reserve_colour(count);
  if (v == NULL) {
    return;
  }
  static float xs[MAX_RINGS][MAX_POINTS], ys[MAX_RINGS][MAX_POINTS];
  static uint32_t cs[MAX_RINGS][MAX_POINTS];
  bool gradient = top != bottom;
  for (int i = 0; i < rings; i++) {
    perimeter(r, radius, offs[i], segs, xs[i], ys[i]);
    for (int p = 0; p < points; p++) {
      uint32_t c = gradient ? lerp_colour(top, bottom, (ys[i][p] - r.y) / r.h) : top;
      cs[i][p] = scale_alpha(c, alphas[i]);
    }
  }
  if (fill) {
    float cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    uint32_t cc = scale_alpha(gradient ? lerp_colour(top, bottom, 0.5f) : top, alphas[0]);
    for (int p = 0; p < points; p++) {
      int q = p + 1 == points ? 0 : p + 1;
      cv(v++, cx, cy, cc);
      cv(v++, xs[0][p], ys[0][p], cs[0][p]);
      cv(v++, xs[0][q], ys[0][q], cs[0][q]);
    }
  }
  for (int i = 0; i + 1 < rings; i++) {
    for (int p = 0; p < points; p++) {
      int q = p + 1 == points ? 0 : p + 1;
      cv(v++, xs[i][p], ys[i][p], cs[i][p]);
      cv(v++, xs[i + 1][p], ys[i + 1][p], cs[i + 1][p]);
      cv(v++, xs[i][q], ys[i][q], cs[i][q]);
      cv(v++, xs[i][q], ys[i][q], cs[i][q]);
      cv(v++, xs[i + 1][p], ys[i + 1][p], cs[i + 1][p]);
      cv(v++, xs[i + 1][q], ys[i + 1][q], cs[i + 1][q]);
    }
  }
}

void mph_rect_vgradient(mph_rect r, uint32_t top, uint32_t bottom) {
  top = mph_tint(top);
  bottom = mph_tint(bottom);
  if (((top | bottom) >> 24) == 0 || r.w <= 0 || r.h <= 0) {
    return;
  }
  vita2d_color_vertex *v = reserve_colour(6);
  if (v == NULL) {
    return;
  }
  float x0 = roundf(r.x), y0 = roundf(r.y), x1 = roundf(r.x + r.w), y1 = roundf(r.y + r.h);
  cv(&v[0], x0, y0, top);
  cv(&v[1], x1, y0, top);
  cv(&v[2], x0, y1, bottom);
  cv(&v[3], x1, y0, top);
  cv(&v[4], x1, y1, bottom);
  cv(&v[5], x0, y1, bottom);
}

void mph_rect_fill(mph_rect r, uint32_t c) {
  mph_rect_vgradient(r, c, c);
}

static const float FILL_OFFS[2] = {-0.5f, 0.5f};
static const float FILL_ALPHAS[2] = {1.0f, 0.0f};

void mph_rrect_fill4(mph_rect r, float tl, float tr, float br, float bl, uint32_t c) {
  const float radius[4] = {tl, tr, br, bl};
  rrect_rings(r, radius, FILL_OFFS, FILL_ALPHAS, 2, true, c, c);
}

void mph_rrect_fill(mph_rect r, float radius, uint32_t c) {
  if (radius <= 0.0f) {
    mph_rect_fill(r, c);
    return;
  }
  mph_rrect_fill4(r, radius, radius, radius, radius, c);
}

void mph_rrect_vgradient(mph_rect r, float radius, uint32_t top, uint32_t bottom) {
  const float radii[4] = {radius, radius, radius, radius};
  rrect_rings(r, radii, FILL_OFFS, FILL_ALPHAS, 2, true, top, bottom);
}

void mph_pill(mph_rect r, uint32_t c) {
  mph_rrect_fill(r, r.h / 2.0f, c);
}

void mph_rrect_stroke(mph_rect r, float radius, float width, uint32_t c) {
  const float radii[4] = {radius, radius, radius, radius};
  float h = width / 2.0f;
  if (width >= 1.0f) {
    const float offs[4] = {-h - 0.5f, -h + 0.5f, h - 0.5f, h + 0.5f};
    const float alphas[4] = {0, 1, 1, 0};
    rrect_rings(r, radii, offs, alphas, 4, false, c, c);
  } else {
    const float offs[3] = {-0.5f - h, 0.0f, 0.5f + h};
    const float alphas[3] = {0, width, 0};
    rrect_rings(r, radii, offs, alphas, 3, false, c, c);
  }
}

void mph_shadow(mph_rect r, float radius, float dx, float dy, float blur, uint32_t c) {
  r.x += dx;
  r.y += dy;
  const float radii[4] = {radius, radius, radius, radius};
  if (blur < 2.0f) {
    rrect_rings(r, radii, FILL_OFFS, FILL_ALPHAS, 2, true, c, c);
    return;
  }
  // A Gaussian with sigma = blur / 2 (CSS), sampled at -2..2 sigma
  float b = blur;
  const float offs[5] = {-b, -b / 2, 0, b / 2, b};
  const float alphas[5] = {1.0f, 0.84f, 0.5f, 0.16f, 0.0f};
  rrect_rings(r, radii, offs, alphas, 5, true, c, c);
}

void mph_circle_fill(float cx, float cy, float radius, uint32_t c) {
  mph_rrect_fill(mph_rect_make(cx - radius, cy - radius, 2 * radius, 2 * radius), radius, c);
}

void mph_circle_stroke(float cx, float cy, float radius, float width, uint32_t c) {
  mph_rrect_stroke(mph_rect_make(cx - radius, cy - radius, 2 * radius, 2 * radius), radius, width, c);
}

void mph_arc_stroke(float cx, float cy, float radius, float width, float a0, float a1, uint32_t c) {
  c = mph_tint(c);
  if ((c >> 24) == 0) {
    return;
  }
  int steps = (int)ceilf(fabsf(a1 - a0) * radius / 3.0f);
  steps = steps < 4 ? 4 : steps > 64 ? 64 : steps;
  float h = width / 2.0f;
  const float radii[4] = {radius - h - 0.5f, radius - h + 0.5f, radius + h - 0.5f, radius + h + 0.5f};
  uint32_t clear = c & 0x00FFFFFFu;
  const uint32_t colours[4] = {clear, c, c, clear};
  vita2d_color_vertex *v = reserve_colour(steps * 3 * 6);
  if (v == NULL) {
    return;
  }
  float pc = cosf(a0), ps = sinf(a0);
  for (int i = 1; i <= steps; i++) {
    float a = a0 + (a1 - a0) * i / steps;
    float nc = cosf(a), ns = sinf(a);
    for (int b = 0; b < 3; b++) {
      float r0 = radii[b], r1 = radii[b + 1];
      cv(v++, cx + pc * r0, cy + ps * r0, colours[b]);
      cv(v++, cx + pc * r1, cy + ps * r1, colours[b + 1]);
      cv(v++, cx + nc * r0, cy + ns * r0, colours[b]);
      cv(v++, cx + nc * r0, cy + ns * r0, colours[b]);
      cv(v++, cx + pc * r1, cy + ps * r1, colours[b + 1]);
      cv(v++, cx + nc * r1, cy + ns * r1, colours[b + 1]);
    }
    pc = nc;
    ps = ns;
  }
}

void mph_sector_gradient(float cx, float cy, float radius, float a0, float a1, uint32_t c0, uint32_t c1) {
  c0 = mph_tint(c0);
  c1 = mph_tint(c1);
  int steps = (int)ceilf(fabsf(a1 - a0) * radius / 4.0f);
  steps = steps < 4 ? 4 : steps > 64 ? 64 : steps;
  vita2d_color_vertex *v = reserve_colour(steps * 3);
  if (v == NULL) {
    return;
  }
  float pc = cosf(a0), ps = sinf(a0);
  uint32_t pcol = c0;
  for (int i = 1; i <= steps; i++) {
    float t = (float)i / steps;
    float a = a0 + (a1 - a0) * t;
    float nc = cosf(a), ns = sinf(a);
    uint32_t ncol = lerp_colour(c0, c1, t);
    // The centre takes each slice's own colour, so the sweep fades by angle
    cv(v++, cx, cy, lerp_colour(c0, c1, t - 0.5f / steps));
    cv(v++, cx + pc * radius, cy + ps * radius, pcol);
    cv(v++, cx + nc * radius, cy + ns * radius, ncol);
    pc = nc;
    ps = ns;
    pcol = ncol;
  }
}

void mph_line(float x0, float y0, float x1, float y1, float width, uint32_t c) {
  c = mph_tint(c);
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);
  if (len <= 0.0f || (c >> 24) == 0) {
    return;
  }
  float nx = -dy / len, ny = dx / len;
  float h = width / 2.0f;
  float core = width >= 1.0f ? 1.0f : width;
  float o[4] = {-h - 0.5f, -h + 0.5f, h - 0.5f, h + 0.5f};
  if (width < 1.0f) {
    o[1] = o[2] = 0.0f;
  }
  uint32_t clear = c & 0x00FFFFFFu;
  uint32_t solid = scale_alpha(c, core);
  const uint32_t colours[4] = {clear, solid, solid, clear};
  vita2d_color_vertex *v = reserve_colour(18);
  if (v == NULL) {
    return;
  }
  for (int b = 0; b < 3; b++) {
    cv(v++, x0 + nx * o[b], y0 + ny * o[b], colours[b]);
    cv(v++, x1 + nx * o[b], y1 + ny * o[b], colours[b]);
    cv(v++, x0 + nx * o[b + 1], y0 + ny * o[b + 1], colours[b + 1]);
    cv(v++, x1 + nx * o[b], y1 + ny * o[b], colours[b]);
    cv(v++, x1 + nx * o[b + 1], y1 + ny * o[b + 1], colours[b + 1]);
    cv(v++, x0 + nx * o[b + 1], y0 + ny * o[b + 1], colours[b + 1]);
  }
}

static void dashed_line(float x0, float y0, float x1, float y1, float width, float dash, float gap, uint32_t c) {
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);
  if (len <= 0.0f) {
    return;
  }
  dx /= len;
  dy /= len;
  for (float s = 0; s < len; s += dash + gap) {
    float e = s + dash > len ? len : s + dash;
    mph_line(x0 + dx * s, y0 + dy * s, x0 + dx * e, y0 + dy * e, width, c);
  }
}

void mph_dashed_rect(mph_rect r, float width, float dash, float gap, uint32_t c) {
  float x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
  dashed_line(x0, y0, x1, y0, width, dash, gap, c);
  dashed_line(x1, y0, x1, y1, width, dash, gap, c);
  dashed_line(x1, y1, x0, y1, width, dash, gap, c);
  dashed_line(x0, y1, x0, y0, width, dash, gap, c);
}

void mph_diamond(float cx, float cy, float half, uint32_t c) {
  c = mph_tint(c);
  if ((c >> 24) == 0) {
    return;
  }
  // A 1 px fringe across each edge moves each corner by sqrt(2)/2
  float in = half - 0.7071f, out = half + 0.7071f;
  uint32_t clear = c & 0x00FFFFFFu;
  const float dx[4] = {-1, 0, 1, 0}, dy[4] = {0, -1, 0, 1};
  vita2d_color_vertex *v = reserve_colour(4 * 3 + 4 * 6);
  if (v == NULL) {
    return;
  }
  for (int i = 0; i < 4; i++) {
    int j = (i + 1) % 4;
    cv(v++, cx, cy, c);
    cv(v++, cx + dx[i] * in, cy + dy[i] * in, c);
    cv(v++, cx + dx[j] * in, cy + dy[j] * in, c);
  }
  for (int i = 0; i < 4; i++) {
    int j = (i + 1) % 4;
    cv(v++, cx + dx[i] * in, cy + dy[i] * in, c);
    cv(v++, cx + dx[i] * out, cy + dy[i] * out, clear);
    cv(v++, cx + dx[j] * in, cy + dy[j] * in, c);
    cv(v++, cx + dx[j] * in, cy + dy[j] * in, c);
    cv(v++, cx + dx[i] * out, cy + dy[i] * out, clear);
    cv(v++, cx + dx[j] * out, cy + dy[j] * out, clear);
  }
}

/*
 * Textures
 */

static vita2d_texture *bg_texture, *icon_texture, *hatch_texture;

#define BG_W 480
#define BG_H 272

// The background: radial-gradient(90% 120% at 100% 0%, #1b2233 0%, #0e1118 62%),
// at half resolution with an ordered dither, drawn at 2x with linear filtering
static vita2d_texture *make_background(void) {
  vita2d_texture *t = vita2d_create_empty_texture_format(BG_W, BG_H, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
  if (t == NULL) {
    return NULL;
  }
  static const float bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  const float c0[3] = {0x1B, 0x22, 0x33}, c1[3] = {0x0E, 0x11, 0x18};
  uint32_t *data = vita2d_texture_get_datap(t);
  unsigned int stride = vita2d_texture_get_stride(t) / 4;
  for (int y = 0; y < BG_H; y++) {
    for (int x = 0; x < BG_W; x++) {
      float px = (x + 0.5f) * 2.0f, py = (y + 0.5f) * 2.0f;
      float ex = (px - 960.0f) / 864.0f, ey = py / 652.8f;
      float t01 = sqrtf(ex * ex + ey * ey) / 0.62f;
      t01 = t01 > 1.0f ? 1.0f : t01;
      float d = (bayer[y & 3][x & 3] + 0.5f) / 16.0f - 0.5f;
      uint32_t ch[3];
      for (int i = 0; i < 3; i++) {
        float v = c0[i] + (c1[i] - c0[i]) * t01 + d;
        ch[i] = v < 0 ? 0 : v > 255 ? 255 : (uint32_t)(v + 0.5f);
      }
      data[y * stride + x] = RGBA8(ch[0], ch[1], ch[2], 0xFF);
    }
  }
  vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
  return t;
}

// 16x16 diagonal stripes, white where (x + y) mod 16 < 8, repeating
static vita2d_texture *make_hatch(void) {
  vita2d_texture *t = vita2d_create_empty_texture_format(16, 16, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
  if (t == NULL) {
    return NULL;
  }
  uint32_t *data = vita2d_texture_get_datap(t);
  unsigned int stride = vita2d_texture_get_stride(t) / 4;
  for (int y = 0; y < 16; y++) {
    for (int x = 0; x < 16; x++) {
      data[y * stride + x] = ((x + y) & 15) < 8 ? 0xFFFFFFFFu : 0x00FFFFFFu;
    }
  }
  vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
  sceGxmTextureSetUAddrMode(&t->gxm_tex, SCE_GXM_TEXTURE_ADDR_REPEAT);
  sceGxmTextureSetVAddrMode(&t->gxm_tex, SCE_GXM_TEXTURE_ADDR_REPEAT);
  return t;
}

void mph_bg_draw(void) {
  if (bg_texture == NULL) {
    mph_rect_fill(mph_rect_make(0, 0, MPH_W, MPH_H), RGBA8(0x0E, 0x11, 0x18, 0xFF));
    return;
  }
  mph_quad_textured(bg_texture, 0xFFFFFFFF, 0, 0, MPH_W, MPH_H, 0, 0, 1, 1);
}

void mph_scrim(void) {
  mph_rect_fill(mph_rect_make(0, 0, MPH_W, MPH_H), MPH_C_SCRIM);
}

int mph_icon_w(mph_icon_id id) {
  return MPH_ICON_RECTS[id].w;
}

int mph_icon_h(mph_icon_id id) {
  return MPH_ICON_RECTS[id].h;
}

void mph_icon(mph_icon_id id, float x, float y, uint32_t tint) {
  if (icon_texture == NULL || (unsigned)id >= MPH_ICON_COUNT) {
    return;
  }
  const mph_icon_rect *r = &MPH_ICON_RECTS[id];
  const float inv = 1.0f / 256.0f;
  x = roundf(x);
  y = roundf(y);
  mph_quad_textured(icon_texture, tint, x, y, x + r->w, y + r->h, r->x * inv, r->y * inv, (r->x + r->w) * inv,
                    (r->y + r->h) * inv);
}

void mph_texture_rrect(const vita2d_texture *t, mph_rect dst, float radius, uint32_t tint) {
  if (t == NULL) {
    return;
  }
  int segs = segments_for(radius);
  int points = 4 * (segs + 1);
  vita2d_texture_vertex *v = reserve_textured(t, mph_tint(tint), points * 3);
  if (v == NULL) {
    return;
  }
  float xs[MAX_POINTS], ys[MAX_POINTS];
  const float radii[4] = {radius, radius, radius, radius};
  perimeter(dst, radii, 0.0f, segs, xs, ys);
  float cx = dst.x + dst.w / 2, cy = dst.y + dst.h / 2;
  for (int p = 0; p < points; p++) {
    int q = p + 1 == points ? 0 : p + 1;
    tv(v++, cx, cy, 0.5f, 0.5f);
    tv(v++, xs[p], ys[p], (xs[p] - dst.x) / dst.w, (ys[p] - dst.y) / dst.h);
    tv(v++, xs[q], ys[q], (xs[q] - dst.x) / dst.w, (ys[q] - dst.y) / dst.h);
  }
  // Softens the stair-steps of the corners
  mph_rrect_stroke(dst, radius, 1.0f, RGBA8(0, 0, 0, 0x40));
}

void mph_hatch(mph_rect r, uint32_t tint) {
  if (hatch_texture == NULL || r.w <= 0 || r.h <= 0) {
    return;
  }
  // Anchored to the screen, so neighbouring strips line up
  mph_quad_textured(hatch_texture, tint, r.x, r.y, r.x + r.w, r.y + r.h, r.x / 16.0f, r.y / 16.0f,
                    (r.x + r.w) / 16.0f, (r.y + r.h) / 16.0f);
}

/*
 * Frame
 */

void mph_init(void) {
  time_origin = time_last = sceKernelGetProcessTimeWide();
  init_quarters();
  mph_anim_init();
  mph_text_init();
  bg_texture = make_background();
  hatch_texture = make_hatch();
  icon_texture = vita2d_load_PNG_file("app0:assets/morpheus/icons.png");
  if (icon_texture != NULL) {
    vita2d_texture_set_filters(icon_texture, SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
  }
  batch_reset();
}

static void free_texture(vita2d_texture **t) {
  if (*t != NULL) {
    vita2d_free_texture(*t);
    *t = NULL;
  }
}

void mph_fini(void) {
  vita2d_wait_rendering_done();
  mph_text_fini();
  free_texture(&bg_texture);
  free_texture(&hatch_texture);
  free_texture(&icon_texture);
  batch_reset();
}

void mph_frame_begin(void) {
  sample_time();
  batch_reset();
  mph_transform_reset();
  mph_text_frame_begin();
  mph_perf_begin();
}

void mph_frame_end(bool hud) {
  mph_transform_reset();
  mph_flush();
  mph_perf_end(hud);
  mph_flush();
}
