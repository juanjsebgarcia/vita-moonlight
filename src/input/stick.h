#pragma once

// Shapes an analog stick before it's sent: a round dead zone around the
// centre, then a response curve. Both sticks, and any pad attached to a PS TV,
// go through it.

// config.stick_deadzone, in percent: off, so the sticks feel as they always have
#define STICK_DEADZONE_DEFAULT 0
#define STICK_DEADZONE_MAX 50

// Values for config.stick_curve
enum {
  STICK_CURVE_LINEAR = 0,   // the stick as it is
  STICK_CURVE_PRECISE = 1,  // squared: finer control near the centre
  STICK_CURVE_COUNT
};

// Shapes one stick's x and y, each from -32768 to 32767, in place.
// deadzone_percent is the dead zone's radius as a percentage of full travel.
void stick_shape(short *x, short *y, int deadzone_percent, int curve);
