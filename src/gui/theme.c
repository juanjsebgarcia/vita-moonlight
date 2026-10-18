#include "theme.h"

#include <stddef.h>

const struct theme *theme_current = &THEME_HOMEBREW;

void theme_init(void) {
  theme_current->init();
}
