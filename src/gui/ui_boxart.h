#pragma once

#include <stdbool.h>

#include "client.h"

// A PC's apps as a row of box art covers, drawn under the host menu. The covers
// are downloaded on a background thread, kept on the memory card under
// <key_dir>/boxart/<host>/<appid>.png, and only those near the selection are
// held in memory, shrunk to tile size.

// Starts the row for these apps, copying the list. running_app_id is the app
// streaming now, or 0; while one runs the row shows it and nothing else can be
// chosen. Closes any row that was open.
void ui_boxart_open(PSERVER_DATA server, const char *host, PAPP_LIST apps, int running_app_id);
// Draws the row. Use as the menu's draw callback: it also frees and uploads
// textures, which is only safe before anything is drawn in the frame.
void ui_boxart_draw(void);
// Whether the menu's cursor is on the row, which highlights the selection
void ui_boxart_set_focus(bool focused);
// Left and right move along the row, L and R jump five, triangle downloads
// the selected cover again
void ui_boxart_input(int buttons);
// The selected app's id, or 0 if there is none
int ui_boxart_selected_app(void);
// "3 of 12" and arrows, for the menu row that stands for the covers; kept up to date
char *ui_boxart_counter(void);
// Stops the download thread, leaving loaded covers in memory. Safe mid-frame.
void ui_boxart_stop_loading(void);
// Stops the download thread and frees everything. Only call when no frame
// that drew the covers is still being rendered (after ui_end or flash_message).
void ui_boxart_close(void);
