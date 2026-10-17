// Turning the system keyboard's edits into text to send. Kept free of the
// Vita SDK so a host-side test can build it.
#pragma once

#include <stddef.h>
#include <stdint.h>

// What changed between two versions of the keyboard's text: `deleted` units
// were removed at `index`, and `inserted_length` units from new_text + index
// took their place
typedef struct {
  size_t index;
  size_t deleted;
  size_t inserted_length;
} ime_text_edit;

// The length of a NUL-terminated UTF-16 text, at most `capacity` units
size_t ime_text_length(const uint16_t *text, size_t capacity);

// Compares the text before an update with the text after it
ime_text_edit ime_text_diff(const uint16_t *old_text, size_t old_length,
                            const uint16_t *new_text, size_t new_length);

// Reads one code point from UTF-16 text, joining a surrogate pair, and
// returns how many units it used (0 at the end). A lone surrogate reads as
// U+FFFD.
size_t ime_text_next(const uint16_t *text, size_t length, uint32_t *code_point);

// Encodes a code point as UTF-8 into out (room for 4 bytes); returns its length
size_t ime_text_utf8(uint32_t code_point, char *out);
