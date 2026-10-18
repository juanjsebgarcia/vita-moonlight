#pragma once

// Morpheus dialogs (DESIGN.md S11) and the IME backdrop (S13):
// mph_screen_dialogs.c. The theme's entry points in morpheus.c call these.

#include "../../ui_view.h"

void mph_screen_alert_draw(const alert_view *view);
int mph_screen_help_prepare(const char *title, const char *message);
void mph_screen_help_draw(const help_view *view);
void mph_screen_ime_backdrop_draw(void);
