#include "ui_ops.h"

#include <stddef.h>

static const ui_ops *current = NULL;

void ui_ops_set(const ui_ops *ops) {
  current = ops;
}

void ui_progress(const char *message) {
  if (current != NULL && current->progress != NULL) {
    current->progress(message);
  }
}

bool ui_cancel_requested(void) {
  return current != NULL && current->cancel_requested != NULL && current->cancel_requested();
}
