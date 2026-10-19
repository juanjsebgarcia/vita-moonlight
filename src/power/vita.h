/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015 Iwan Timmer, Sunguk Lee
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

#pragma once

#include <stdbool.h>
#include <stdint.h>

bool vitapower_init();
void vitapower_config(CONFIGURATION config);

void vitapower_start();
void vitapower_stop();

// The latest low battery warning while streaming. count changes with each
// warning, so a change means a new one to show.
typedef struct {
  unsigned int count;
  int percent;
  int minutes;      // the system's estimate of the time left, or -1 when it has none
  uint32_t age_us;  // how long ago it was given
} vitapower_battery_warning;

vitapower_battery_warning vitapower_battery_warning_latest();
