#pragma once

enum {
  RECOMMENDED_NONE = -1,
  RECOMMENDED_GOOD = 0,
  RECOMMENDED_WEAK = 1,
};

// Opens the "Apply recommended settings" screen. Returns the preset applied,
// or RECOMMENDED_NONE if the player backed out.
int ui_recommended_settings_menu(void);
