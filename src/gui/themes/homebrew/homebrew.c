#include "homebrew.h"

static void homebrew_init(void) {
  vita2d_set_clear_color(0xff000000);
  hb_text_init();
}

static void homebrew_fini(void) {
  hb_boxart_fini();
  hb_text_fini();
}

static void homebrew_language_changed(void) {
  // Nothing measured is kept across a change: app names on the covers are
  // the same in every language
}

static void homebrew_draw_ime_backdrop(void) {
  // Black, the clear colour
}

const struct theme THEME_HOMEBREW = {
  .name = "Homebrew",
  .init = homebrew_init,
  .fini = homebrew_fini,
  .language_changed = homebrew_language_changed,
  .draw_menu = hb_draw_menu,
  .menu_follow = hb_menu_follow,
  .draw_alert = hb_draw_alert,
  .help_prepare = hb_help_prepare,
  .draw_help = hb_draw_help,
  .draw_ime_backdrop = homebrew_draw_ime_backdrop,
  .draw_stats = hb_draw_stats,
  .draw_stream_overlay = hb_draw_stream_overlay,
};
