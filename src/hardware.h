#pragma once

#include <stdbool.h>

// What this machine is and has, read once at boot by vita_hardware_init()

// Reads the hardware, after the config is loaded and the log is open
void vita_hardware_init(void);

// A real PS TV: no touchscreens, no battery, an Ethernet port. Also true
// on a handheld with force_ps_tv set in moonlight.conf, for testing.
bool vita_is_ps_tv(void);

// The system runs as a PS TV: DualShock pads on ports 1 to 4. A real PS TV,
// or a handheld with a plugin like MiniVitaTV, which still has its touchscreens
// and battery.
bool vita_ps_tv_mode(void);

// The machine has a battery, so battery life is worth saving
bool vita_has_battery(void);
