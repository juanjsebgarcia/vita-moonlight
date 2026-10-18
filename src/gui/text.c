#include "text.h"

#include "font_coverage.h"
#include "ui_view.h"
#include "../config.h"
#include "../i18n/i18n.h"

#include <stdio.h>
#include <string.h>

unsigned int text_utf8_next(const char **p) {
  const unsigned char *s = (const unsigned char *)*p;
  unsigned int c = s[0];
  int extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
  if (extra < 0) {
    (*p)++;
    return 0xFFFD;
  }
  c = extra == 0 ? c : c & (0x3F >> extra);
  for (int i = 1; i <= extra; i++) {
    if ((s[i] & 0xC0) != 0x80) {
      (*p)++;
      return 0xFFFD;
    }
    c = (c << 6) | (s[i] & 0x3F);
  }
  *p += extra + 1;
  return c;
}

bool text_nerdfont_has(unsigned int c) {
  if (c >= 0xE000 && c <= 0xF8FF) {
    return true;  // the Nerd Font icons, which only the app's font has
  }
  int low = 0, high = (int)(sizeof(FONT_COVERAGE) / sizeof(FONT_COVERAGE[0])) - 1;
  while (low <= high) {
    int mid = (low + high) / 2;
    if (c < FONT_COVERAGE[mid][0]) {
      high = mid - 1;
    } else if (c > FONT_COVERAGE[mid][1]) {
      low = mid + 1;
    } else {
      return true;
    }
  }
  return false;
}

/*
 * The system font, loaded the first time one of its characters is needed,
 * and again after the language changes.
 */

static vita2d_pgf *system_font = NULL;
static bool system_font_failed = false;

static int is_hangul(unsigned int c) {
  return (c >= 0x1100 && c <= 0x11FF) || (c >= 0x3130 && c <= 0x318F) || (c >= 0xAC00 && c <= 0xD7AF);
}

static int is_han(unsigned int c) {
  return (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) ||
         (c >= 0x3000 && c <= 0x303F) || (c >= 0xFF00 && c <= 0xFFEF);
}

vita2d_pgf *text_system_font(void) {
  if (system_font != NULL || system_font_failed) {
    return system_font;
  }
  // Han characters come from the Chinese font for a Chinese UI, else from
  // the default (Japanese) one, which also has kana and Latin
  bool chinese = strncmp(i18n_current()->font, "cjk-zh", 6) == 0;
  vita2d_system_pgf_config configs[3];
  int count = 0;
  configs[count++] = (vita2d_system_pgf_config){SCE_FONT_LANGUAGE_KOREAN, is_hangul};
  if (chinese) {
    configs[count++] = (vita2d_system_pgf_config){SCE_FONT_LANGUAGE_CHINESE, is_han};
  }
  configs[count++] = (vita2d_system_pgf_config){SCE_FONT_LANGUAGE_DEFAULT, NULL};
  system_font = vita2d_load_system_pgf(count, configs);
  if (system_font == NULL) {
    system_font = vita2d_load_default_pgf();
  }
  system_font_failed = system_font == NULL;
  return system_font;
}

void text_language_changed(void) {
  // Only between frames: the last frame may still use its glyphs
  if (system_font != NULL) {
    vita2d_wait_rendering_done();
    vita2d_free_pgf(system_font);
    system_font = NULL;
  }
  system_font_failed = false;
}

/*
 * Button markers: {confirm}, {triangle}, {start} and so on in a string
 * stand for the button, so translations can name buttons without spelling
 * them out. Anything else in braces is text.
 */

static const struct {
  const char *name;
  int glyph;
} TEXT_GLYPHS[] = {
  {"cross", BUTTON_GLYPH_CROSS},
  {"circle", BUTTON_GLYPH_CIRCLE},
  {"triangle", BUTTON_GLYPH_TRIANGLE},
  {"square", BUTTON_GLYPH_SQUARE},
  {"dpad", BUTTON_GLYPH_DPAD},
  {"up", BUTTON_GLYPH_DPAD_UP},
  {"down", BUTTON_GLYPH_DPAD_DOWN},
  {"left", BUTTON_GLYPH_DPAD_LEFT},
  {"right", BUTTON_GLYPH_DPAD_RIGHT},
  {"l", BUTTON_GLYPH_L},
  {"r", BUTTON_GLYPH_R},
  {"l2", BUTTON_GLYPH_L2},
  {"r2", BUTTON_GLYPH_R2},
  {"start", BUTTON_GLYPH_START},
  {"select", BUTTON_GLYPH_SELECT},
  {"ps", BUTTON_GLYPH_PS},
  {"left_stick", BUTTON_GLYPH_LEFT_STICK},
  {"right_stick", BUTTON_GLYPH_RIGHT_STICK},
  {"l3", BUTTON_GLYPH_L3},
  {"r3", BUTTON_GLYPH_R3},
  {"front_touch", BUTTON_GLYPH_FRONT_TOUCH},
  {"rear_touch", BUTTON_GLYPH_REAR_TOUCH},
};

int text_marker(const char *text, int *length) {
  if (text[0] != '{') {
    return -1;
  }
  const char *end = strchr(text, '}');
  if (end == NULL || end - text > 16) {
    return -1;
  }
  int name_length = (int)(end - text - 1);
  *length = name_length + 2;
  if (name_length == 7 && strncmp(text + 1, "confirm", 7) == 0) {
    return config.jp_layout ? BUTTON_GLYPH_CIRCLE : BUTTON_GLYPH_CROSS;
  }
  if (name_length == 6 && strncmp(text + 1, "cancel", 6) == 0) {
    return config.jp_layout ? BUTTON_GLYPH_CROSS : BUTTON_GLYPH_CIRCLE;
  }
  for (size_t i = 0; i < sizeof(TEXT_GLYPHS) / sizeof(TEXT_GLYPHS[0]); i++) {
    if ((int)strlen(TEXT_GLYPHS[i].name) == name_length && strncmp(text + 1, TEXT_GLYPHS[i].name, name_length) == 0) {
      return TEXT_GLYPHS[i].glyph;
    }
  }
  return -1;
}

// Japanese and Chinese have no spaces between words, so a line can break
// between any two of their characters (Korean uses spaces)
static bool breaks_anywhere(unsigned int c) {
  return (c >= 0x3040 && c <= 0x30FF) || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) ||
         (c >= 0xF900 && c <= 0xFAFF) || (c >= 0x3000 && c <= 0x303F) || (c >= 0xFF00 && c <= 0xFFEF);
}

int text_wrap(const char *text, int max_width, char lines[][WRAP_LINE_MAX], int max_lines,
              text_measure measure, void *context) {
  int count = 0;
  char line[WRAP_LINE_MAX] = "";
  const char *p = text;
  bool space_before = false;

  while (*p && count < max_lines) {
    if (*p == '\n') {
      snprintf(lines[count++], WRAP_LINE_MAX, "%s", line);
      line[0] = '\0';
      space_before = false;
      p++;
      continue;
    }
    if (*p == ' ') {
      space_before = true;
      p++;
      continue;
    }

    // The next word runs to the next space or newline, or is a single
    // character of a language written without spaces
    const char *end = p;
    const char *after_first = p;
    if (breaks_anywhere(text_utf8_next(&after_first))) {
      end = after_first;
    } else {
      while (*end && *end != ' ' && *end != '\n') {
        const char *next = end;
        unsigned int c = text_utf8_next(&next);
        if (end != p && breaks_anywhere(c)) {
          break;
        }
        end = next;
      }
    }
    int word_len = end - p;
    if (word_len >= WRAP_LINE_MAX) {
      word_len = WRAP_LINE_MAX - 1;
    }

    char candidate[WRAP_LINE_MAX];
    snprintf(candidate, sizeof(candidate), "%s%s%.*s", line, line[0] && space_before ? " " : "", word_len, p);
    if (measure(candidate, context) <= max_width) {
      snprintf(line, sizeof(line), "%s", candidate);
      p = end;
      space_before = false;
      continue;
    }

    if (line[0]) {
      // The word doesn't fit after what is already on the line, so start a new one
      snprintf(lines[count++], WRAP_LINE_MAX, "%s", line);
      line[0] = '\0';
      space_before = false;
      continue;
    }

    // A single word wider than the line: split it, on a whole character,
    // where it overflows
    const char *fit_end = p;
    text_utf8_next(&fit_end);
    while (fit_end < p + word_len) {
      const char *next = fit_end;
      text_utf8_next(&next);
      snprintf(candidate, sizeof(candidate), "%.*s", (int)(next - p), p);
      if (measure(candidate, context) > max_width) {
        break;
      }
      fit_end = next;
    }
    snprintf(lines[count++], WRAP_LINE_MAX, "%.*s", (int)(fit_end - p), p);
    p = fit_end;
    space_before = false;
  }

  if (line[0] && count < max_lines) {
    snprintf(lines[count++], WRAP_LINE_MAX, "%s", line);
  }
  return count;
}
