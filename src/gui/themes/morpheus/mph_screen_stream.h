#pragma once

// Morpheus stream overlays (DESIGN.md S12): mph_screen_stream.c. Drawn by the
// video thread on every decoded frame.

#include "../../ui_view.h"

void mph_screen_stats_draw(const stats_view *view);
void mph_screen_stream_overlay_draw(const stream_overlay_view *view);
