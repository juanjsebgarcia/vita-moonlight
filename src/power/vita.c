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
#include <stdbool.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/power.h>
#include "../config.h"
#include "../debug.h"
#include "vita.h"

enum {
  ENABLE_ALL = 0,
  DISABLE_SUSPEND = 1,
};

enum {
  BOOST_ARM_CLOCK_MHZ = 444,
  BOOST_BUS_CLOCK_MHZ = 222,
  BOOST_GPU_CLOCK_MHZ = 222,
  BOOST_GPU_XBAR_CLOCK_MHZ = 166,
};

typedef struct {
  int arm;
  int bus;
  int gpu;
  int gpu_xbar;
} clock_state;

static int powermode = ENABLE_ALL;
static bool active_power_thread = false;
static bool clocks_boosted = false;
static clock_state saved_clocks = {0};

// The battery levels that warn while streaming, highest first. Each warns
// once, then again only after the battery has risen above it (charging).
static const int battery_thresholds[] = {15, 5};
#define BATTERY_THRESHOLD_COUNT (sizeof(battery_thresholds) / sizeof(battery_thresholds[0]))
static bool battery_warned[BATTERY_THRESHOLD_COUNT] = {false};
static volatile vitapower_battery_warning battery_warning = {0};

static void boost_clocks() {
  if (clocks_boosted) {
    return;
  }
  saved_clocks.arm = scePowerGetArmClockFrequency();
  saved_clocks.bus = scePowerGetBusClockFrequency();
  saved_clocks.gpu = scePowerGetGpuClockFrequency();
  saved_clocks.gpu_xbar = scePowerGetGpuXbarClockFrequency();

  scePowerSetArmClockFrequency(BOOST_ARM_CLOCK_MHZ);
  scePowerSetBusClockFrequency(BOOST_BUS_CLOCK_MHZ);
  scePowerSetGpuClockFrequency(BOOST_GPU_CLOCK_MHZ);
  scePowerSetGpuXbarClockFrequency(BOOST_GPU_XBAR_CLOCK_MHZ);
  clocks_boosted = true;

  vita_debug_log("Clocks boosted (arm/bus/gpu/xbar): %d/%d/%d/%d -> %d/%d/%d/%d MHz",
                 saved_clocks.arm, saved_clocks.bus, saved_clocks.gpu, saved_clocks.gpu_xbar,
                 scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
                 scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());
}

static void restore_clocks() {
  if (!clocks_boosted) {
    return;
  }
  scePowerSetArmClockFrequency(saved_clocks.arm);
  scePowerSetBusClockFrequency(saved_clocks.bus);
  scePowerSetGpuClockFrequency(saved_clocks.gpu);
  scePowerSetGpuXbarClockFrequency(saved_clocks.gpu_xbar);
  clocks_boosted = false;
}

// The system status bar is hidden while streaming, so say when the battery
// runs low. Called every 10 s during a stream.
static void check_battery() {
  // A PS TV has no battery
  if (config.model == SCE_KERNEL_MODEL_VITATV) {
    return;
  }
  int percent = scePowerGetBatteryLifePercent();
  if (percent < 0 || percent > 100) {
    return;
  }
  bool charging = scePowerIsBatteryCharging() || scePowerIsPowerOnline();
  int crossed = -1;
  for (int i = 0; i < BATTERY_THRESHOLD_COUNT; i++) {
    if (percent > battery_thresholds[i]) {
      battery_warned[i] = false;
    } else if (!battery_warned[i] && !charging) {
      // Below several at once (a stream started at 4%): one warning, for the lowest
      battery_warned[i] = true;
      crossed = i;
    }
  }
  if (crossed < 0) {
    return;
  }
  // Minutes left, when the system has an estimate
  int minutes = scePowerGetBatteryLifeTime();
  battery_warning.percent = percent;
  battery_warning.minutes = minutes > 0 && minutes < 24 * 60 ? minutes : -1;
  __sync_synchronize();
  battery_warning.count++;
  vita_debug_log("Battery low: %d%% (%d min left), warned at %d%%", percent, minutes,
                 battery_thresholds[crossed]);
}

vitapower_battery_warning vitapower_battery_warning_latest() {
  vitapower_battery_warning latest;
  latest.count = battery_warning.count;
  __sync_synchronize();
  latest.percent = battery_warning.percent;
  latest.minutes = battery_warning.minutes;
  return latest;
}

int vitapower_thread(SceSize args, void *argp) {
  while (1) {
    if (!active_power_thread) {
      sceKernelDelayThread(10 * 1000 * 1000);
      continue;
    }
    if (powermode & DISABLE_SUSPEND) {
      sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
      sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_OLED_OFF);
    }
    check_battery();
    sceKernelDelayThread(10 * 1000 * 1000);
  }

  return 0;
}

bool vitapower_init() {
  SceUID thid = sceKernelCreateThread("vitapower_thread", vitapower_thread, 0x10000100, 0x40000, 0, 0, NULL);
  if (thid >= 0)
    sceKernelStartThread(thid, 0, NULL);

  return true;
}

void vitapower_config(CONFIGURATION config) {
  powermode = ENABLE_ALL;

  if (config.disable_powersave) {
    powermode |= DISABLE_SUSPEND;
  }
}

void vitapower_start() {
  active_power_thread = true;
  // Read the global config so a change made in the settings menu applies to the next stream
  if (config.boost_clocks) {
    boost_clocks();
  } else {
    // Turned off from the pause menu during this stream
    restore_clocks();
  }
}

void vitapower_stop() {
  active_power_thread = false;
  restore_clocks();
}
