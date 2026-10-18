#pragma once

#include <stdbool.h>

// The pause menu's Send keys screen: key combinations the Vita can't type,
// such as Alt+Tab or Ctrl+Alt+Del, sent to the PC as one chord. Returns true
// once one was sent (the caller goes back to the stream), false on cancel.
bool ui_send_keys_menu(void);
