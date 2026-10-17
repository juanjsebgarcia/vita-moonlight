#pragma once

#include <stdbool.h>

// What the services ask of whichever UI is running, for work that takes a
// while: showing how it's going, and letting the player stop it. The GUI
// registers its own (src/gui/ui.c); another UI would register another.
typedef struct ui_ops {
  // Shows a message while work runs, such as how long a PC has taken to wake
  void (*progress)(const char *message);
  // Whether the player has asked to stop the work
  bool (*cancel_requested)(void);
} ui_ops;

void ui_ops_set(const ui_ops *ops);

// Safe to call with no UI registered: they do nothing, and never cancel
void ui_progress(const char *message);
bool ui_cancel_requested(void);
