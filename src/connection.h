/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015 Iwan Timmer
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
#include <stddef.h>
#include <Limelight.h>

//.DISCONNECTED <-.
//      |         |
//      v         |
//    READY       |
//      |         |
//      v         |
//    PAIRED   ---|
//     | ^        |
//     v |        |
//  CONNECTED  ---|
//     | ^        |
//     v |        |
//  MINIMISED  ---'

enum {
  LI_DISCONNECTED,
  LI_READY,
  LI_PAIRED,
  LI_CONNECTED,
  LI_MINIMIZED
};

extern CONNECTION_LISTENER_CALLBACKS connection_callbacks;

extern int connection_stage;

int connection_reset();
int connection_paired();
int connection_minimize();
int connection_resume();
int connection_terminate();

bool connection_is_ready();
bool connection_is_connected();
int connection_get_status();

// Forgets why the last stream ended, before another starts
void connection_clear_end_reason(void);
// If the last stream ended with an error, writes why to out, in words with a
// next step, and forgets it. False after a normal end, or once taken. Call it
// from the UI thread once the stream has torn down.
bool connection_take_end_reason(char *out, size_t len);
// Why a stream failed to start at stage with code, in words with a next step
void connection_describe_start_failure(int stage, int code, char *out, size_t len);
