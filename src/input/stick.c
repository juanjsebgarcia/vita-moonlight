#include "stick.h"

#include <math.h>

// read_analog() gives the stick's travel as up to +-32640 (raw 0 and 255),
// which is full travel here, and the shaped stick is sent as up to +-32767
#define STICK_IN_MAX 32640.0f
#define STICK_MAX 32767.0f

void stick_shape(short *x, short *y, int deadzone_percent, int curve) {
  // Off and linear: send the stick exactly as before
  if (deadzone_percent <= 0 && curve == STICK_CURVE_LINEAR) {
    return;
  }

  float fx = *x / STICK_IN_MAX;
  float fy = *y / STICK_IN_MAX;
  float magnitude = sqrtf(fx * fx + fy * fy);
  float deadzone = deadzone_percent / 100.0f;
  if (magnitude <= deadzone) {
    *x = 0;
    *y = 0;
    return;
  }

  // Rescale what's past the dead zone to the whole range, so the stick still
  // reaches full travel. Past full travel (the corners of the Vita's square
  // range), it's left alone rather than pulled in to a circle. A full push
  // is sent as full scale, whatever the settings, which double tap to sprint
  // relies on.
  float travel = magnitude > 1.0f ? 1.0f : magnitude;
  float shaped = (travel - deadzone) / (1.0f - deadzone);
  if (curve == STICK_CURVE_PRECISE) {
    shaped *= shaped;
  }
  float scale = shaped / travel;

  float sx = fx * scale * STICK_MAX;
  float sy = fy * scale * STICK_MAX;
  *x = (short)(sx > STICK_MAX ? STICK_MAX : (sx < -STICK_MAX ? -STICK_MAX : sx));
  *y = (short)(sy > STICK_MAX ? STICK_MAX : (sy < -STICK_MAX ? -STICK_MAX : sy));
}
