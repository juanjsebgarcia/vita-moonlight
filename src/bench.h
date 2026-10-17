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

// Benchmark against tools/bench/bench.html streamed from the host. See
// tools/bench/README.md for the page and its layout contract.

#define BENCH_DEFAULT_SECONDS 60
#define BENCH_MIN_SECONDS 10
#define BENCH_MAX_SECONDS 600

bool vitabench_init(void);

// True while a benchmark is measuring; cheap enough to check on every frame
bool vitabench_active(void);

// Called from the video decode thread with a just-decoded frame. hw_start_us
// is when sceAvcdecDecode was called, after the frame was assembled. pixels
// is the texture the frame was decoded into (RGBA at 4 bytes per pixel, or
// the Y plane at 1 when decoding to YUV), and region is the part of it that
// is shown on screen, in texture pixels.
void vitabench_frame_decoded(const DECODE_UNIT *du, uint64_t submit_us, uint64_t hw_start_us, uint64_t decoded_us,
                             const uint8_t *pixels, int stride_bytes, int bytes_per_pixel,
                             float region_x1, float region_y1, float region_x2, float region_y2);
// Called when a decoded frame has been drawn and swapped onto the screen
void vitabench_frame_rendered(uint64_t rendered_us);

// Runs a whole benchmark from the GUI thread: connects to the host, starts the
// benchmark app, measures for config.bench_seconds and writes a report to
// <key_dir>reports/. When automated, nothing waits for a button press, and
// the caller is expected to exit afterwards. Returns 0 on success.
int vitabench_run(bool automated);
