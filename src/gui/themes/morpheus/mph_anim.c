// Morpheus motion: the demo's easing curves as lookup tables, tweens that
// retarget like CSS transitions, and framerate-independent followers.

#include "mph.h"
#include "mph_internal.h"

#include <math.h>

#define LUT_SIZE 64

static float luts[MPH_EASE_COUNT][LUT_SIZE + 1];

static float bezier(float a, float b, float t) {
  // A cubic Bezier from 0 to 1 with control points a and b
  float u = 1.0f - t;
  return 3.0f * u * u * t * a + 3.0f * u * t * t * b + t * t * t;
}

static float bezier_slope(float a, float b, float t) {
  float u = 1.0f - t;
  return 3.0f * u * u * a + 6.0f * u * t * (b - a) + 3.0f * t * t * (1.0f - b);
}

// cubic-bezier(x1, y1, x2, y2) sampled at even x
static void make_lut(float *lut, float x1, float y1, float x2, float y2) {
  for (int i = 0; i <= LUT_SIZE; i++) {
    float x = (float)i / LUT_SIZE;
    float t = x;
    for (int step = 0; step < 8; step++) {
      float slope = bezier_slope(x1, x2, t);
      if (fabsf(slope) < 1e-5f) {
        break;
      }
      t -= (bezier(x1, x2, t) - x) / slope;
      t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    }
    lut[i] = bezier(y1, y2, t);
  }
}

void mph_anim_init(void) {
  make_lut(luts[MPH_EASE_FOCUS], 0.22f, 1.2f, 0.36f, 1.0f);
  make_lut(luts[MPH_EASE_OUT], 0.2f, 0.8f, 0.2f, 1.0f);
  make_lut(luts[MPH_EASE_CSS], 0.25f, 0.1f, 0.25f, 1.0f);
  for (int i = 0; i <= LUT_SIZE; i++) {
    luts[MPH_EASE_LINEAR][i] = (float)i / LUT_SIZE;
  }
}

float mph_ease_eval(mph_ease e, float t) {
  if (t <= 0.0f) {
    return 0.0f;
  }
  if (t >= 1.0f) {
    return 1.0f;
  }
  float f = t * LUT_SIZE;
  int i = (int)f;
  float frac = f - i;
  return luts[e][i] + (luts[e][i + 1] - luts[e][i]) * frac;
}

void mph_tween_snap(mph_tween *tw, float v) {
  *tw = (mph_tween){v, v, mph_time(), 0.0f, MPH_EASE_LINEAR};
}

float mph_tween_value(const mph_tween *tw) {
  if (tw->dur <= 0.0f) {
    return tw->to;
  }
  float k = mph_ease_eval(tw->e, (mph_time() - tw->t0) / tw->dur);
  return tw->from + (tw->to - tw->from) * k;
}

bool mph_tween_done(const mph_tween *tw) {
  return tw->dur <= 0.0f || mph_time() - tw->t0 >= tw->dur;
}

void mph_tween_to(mph_tween *tw, float to, float dur, mph_ease e) {
  if (to == tw->to) {
    return;
  }
  float now = mph_tween_value(tw);
  *tw = (mph_tween){now, to, mph_time(), dur, e};
}

float mph_follow(float cur, float target, float rate_per_60fps) {
  float k = 1.0f - powf(1.0f - rate_per_60fps, mph_dt() * 60.0f);
  return cur + (target - cur) * k;
}

void mph_spring_step(mph_spring *s, float target, float omega) {
  // Critically damped: x'' = omega^2 (target - x) - 2 omega x', in small steps
  float dt = mph_dt();
  int steps = (int)ceilf(dt / 0.008f);
  float h = steps > 0 ? dt / steps : 0.0f;
  for (int i = 0; i < steps; i++) {
    float a = omega * omega * (target - s->x) - 2.0f * omega * s->v;
    s->v += a * h;
    s->x += s->v * h;
  }
}
