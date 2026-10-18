#pragma once

// A theme draws every screen: the look (geometry, fonts, colours, text
// measurement, scrolling) of what the shared screen code describes in the
// view models of ui_view.h. The screens, their rows and what each button
// does are the same in every theme. See src/gui/themes/README.md.

#include "ui_view.h"

struct theme {
  const char *name;

  // Load fonts and textures, and set the clear colour. Called between
  // frames when the theme becomes the active one.
  void (*init)(void);
  // Free what init loaded. Called between frames, with rendering finished.
  void (*fini)(void);
  // The language changed: drop anything measured or cached for the old one.
  // Called between frames, after the shared system font was released.
  void (*language_changed)(void);

  // A list screen's frame, between ui_start() and ui_end(): the screen's
  // decorations (when view->decorated) and its rows, with view->cursor's
  // row selected and scrolled by view->scroll
  void (*draw_menu)(const menu_view *view);
  // After the cursor moves, before the next frame: update view->scroll so
  // the selected row comes into view. view->cursor and cursor_row are the
  // new selection, drawn_row the one just drawn.
  void (*menu_follow)(menu_view *view);

  // An alert or progress message's frame, between ui_start() and ui_end().
  // Dialogs draw alone: the screen is cleared behind them.
  void (*draw_alert)(const alert_view *view);
  // Lays out a help dialog once, before it opens, and returns how many
  // lines it can scroll (0 when everything fits)
  int (*help_prepare)(const char *title, const char *message);
  // A help dialog's frame, laid out by the last help_prepare()
  void (*draw_help)(const help_view *view);

  // Behind the system keyboard, which the system draws on top
  void (*draw_ime_backdrop)(void);

  // Over the stream, on the video thread, on every frame: keep them cheap
  void (*draw_stats)(const stats_view *view);
  void (*draw_stream_overlay)(const stream_overlay_view *view);
};

// The two themes. Each lives in its own folder and shares no drawing code.
extern const struct theme THEME_HOMEBREW;
extern const struct theme THEME_MORPHEUS;

// The theme drawing now. One pointer read, so it costs nothing on the
// video thread.
extern const struct theme *theme_current;
static inline const struct theme *theme_active(void) {
  return theme_current;
}

// Starts the default theme (Homebrew). Once, after vita2d_init().
void theme_init(void);
