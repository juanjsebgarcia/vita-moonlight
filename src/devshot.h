#pragma once

// Developer screenshots of the menus, for checking layouts remotely. With
// developer mode on, the UI checks about twice a second for a request file,
// <key_dir>shot.req, and when it finds one, saves the frame it just drew to
// <key_dir>shot.raw (960x544, 4 bytes per pixel, R G B A, stride 960) and
// deletes the request. tools/bench/shot.sh does the asking and fetching.
// Call after each UI frame is swapped onto the screen.
void devshot_after_swap(void);
