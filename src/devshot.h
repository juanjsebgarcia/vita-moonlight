#pragma once

// Developer screenshots of the menus, for checking layouts remotely. With
// developer mode on, the UI checks about twice a second for a request file,
// <key_dir>shot.req, and when it finds one, saves the frame it just drew to
// <key_dir>shot.raw (960x544, 4 bytes per pixel, R G B A, stride 960) and
// deletes the request. tools/bench/shot.sh does the asking and fetching.
// It also keeps the screen from turning off, as input would, so the menus
// keep drawing while nobody's touching the Vita.
// Call after each UI frame is swapped onto the screen.
void devshot_after_swap(void);

// Developer mode only: opens the screen named in <key_dir>screen.req, once,
// at launch, and deletes the request, so remote checks can capture screens
// that buttons reach only with a PC or a stream. The file holds one line,
// "<kind> <text>", with \n in the text for a line break:
//   flash <text>          a progress message, held for 10 s
//   error <text>          an alert with an OK button
//   confirm <text>        an alert with Yes and No
//   help <title>|<text>   the help dialog
//   overlay               the stream's overlays (stats in fps mode, the start
//                         hint, the sprint icon) over black, for 10 s
// A script uploads the file over FTP, then launches the app.
// Call after the UI is set up, before the main menu.
void devshot_open_requested_screen(void);
