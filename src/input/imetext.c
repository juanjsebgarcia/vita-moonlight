#include "imetext.h"

size_t ime_text_length(const uint16_t *text, size_t capacity) {
  size_t length = 0;
  while (length < capacity && text[length] != 0) {
    length++;
  }
  return length;
}

ime_text_edit ime_text_diff(const uint16_t *old_text, size_t old_length,
                            const uint16_t *new_text, size_t new_length) {
  size_t shorter = old_length < new_length ? old_length : new_length;
  size_t prefix = 0;
  while (prefix < shorter && old_text[prefix] == new_text[prefix]) {
    prefix++;
  }
  size_t suffix = 0;
  while (suffix < shorter - prefix &&
         old_text[old_length - 1 - suffix] == new_text[new_length - 1 - suffix]) {
    suffix++;
  }
  ime_text_edit edit = {
    .index = prefix,
    .deleted = old_length - prefix - suffix,
    .inserted_length = new_length - prefix - suffix,
  };
  return edit;
}

size_t ime_text_next(const uint16_t *text, size_t length, uint32_t *code_point) {
  if (length == 0) {
    return 0;
  }
  uint32_t unit = text[0];
  if (unit >= 0xD800 && unit <= 0xDBFF && length > 1 &&
      text[1] >= 0xDC00 && text[1] <= 0xDFFF) {
    *code_point = 0x10000 + ((unit - 0xD800) << 10) + (text[1] - 0xDC00);
    return 2;
  }
  *code_point = (unit >= 0xD800 && unit <= 0xDFFF) ? 0xFFFD : unit;
  return 1;
}

size_t ime_text_utf8(uint32_t c, char *out) {
  if (c < 0x80) {
    out[0] = (char)c;
    return 1;
  }
  if (c < 0x800) {
    out[0] = (char)(0xC0 | (c >> 6));
    out[1] = (char)(0x80 | (c & 0x3F));
    return 2;
  }
  if (c < 0x10000) {
    out[0] = (char)(0xE0 | (c >> 12));
    out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
    out[2] = (char)(0x80 | (c & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (c >> 18));
  out[1] = (char)(0x80 | ((c >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((c >> 6) & 0x3F));
  out[3] = (char)(0x80 | (c & 0x3F));
  return 4;
}
