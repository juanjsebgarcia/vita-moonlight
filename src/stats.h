/*
 * This file is part of Moonlight Embedded.
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

#include <Limelight.h>
#include <stdbool.h>
#include <stdint.h>

// Values for config.stats_overlay
enum {
  STATS_OVERLAY_OFF = 0,
  STATS_OVERLAY_FPS = 1,
  STATS_OVERLAY_FULL = 2,
  STATS_OVERLAY_COUNT
};

bool vitastats_init(void);
// Clears all counters, at the start of each stream
void vitastats_reset(void);

// Called from the video decode thread for each decode unit
void vitastats_frame_received(const DECODE_UNIT *du, uint64_t submit_us);
void vitastats_frame_decoded(uint64_t decode_us);
void vitastats_frame_rendered(uint64_t render_us);
void vitastats_frame_dropped_by_pacer(void);

// Rolls the counters into the displayed figures. Call about once a second.
void vitastats_tick(void);

// Draws the overlay for the current config.stats_overlay mode. Call between
// vita2d_start_drawing() and vita2d_end_drawing().
void vitastats_draw(void);

// The in-stream shortcut (START + RIGHT): moves config.stats_overlay to the
// next mode and names it on screen for a moment. From the input thread.
void vitastats_next_mode(void);
// The note naming the mode just chosen, with its opacity as it fades, or NULL
// when there's none to show. From the video thread, on every frame.
const char *vitastats_mode_note(float *opacity);
// Whether the shortcut changed the mode since the last call, so the caller
// saves the config (the input thread shouldn't wait on the memory card)
bool vitastats_take_mode_change(void);
