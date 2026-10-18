#include "theme.h"

#include <stddef.h>
#include <vita2d.h>

const struct theme *theme_current = &THEME_HOMEBREW;
static const struct theme *theme_pending = NULL;

void theme_init(void) {
  theme_current->init();
}

bool theme_offers_new(void) {
  return theme_current == &THEME_HOMEBREW;
}

void theme_request_new(void) {
  theme_pending = &THEME_MORPHEUS;
}

bool theme_apply_pending(void) {
  if (theme_pending == NULL || theme_pending == theme_current) {
    theme_pending = NULL;
    return false;
  }
  // The last frame may still use the old theme's fonts and textures
  vita2d_wait_rendering_done();
  theme_current->fini();
  theme_current = theme_pending;
  theme_pending = NULL;
  theme_current->init();
  return true;
}
