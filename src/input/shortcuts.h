// shortcuts.h
#pragma once
#include <psp2/ctrl.h>
#include <stdbool.h>

// Devuelve true si se ejecutó un acceso directo y se debe limpiar el input
typedef struct SceCtrlData SceCtrlData;
bool process_physical_shortcuts(const SceCtrlData* pad, const SceCtrlData* pad_old);

// START + RIGHT cycles the stats overlay. Call on each fresh controller
// sample, before the buttons are read for the PC: from the moment the pair
// is pressed until both are let go, it takes START and RIGHT out of pad, so
// the PC doesn't see them held.
void process_stats_shortcut(SceCtrlData* pad);
