#pragma once

// Text handling every theme shares: reading UTF-8, the button markers in
// translated strings, the Vita's system font for characters a theme's font
// lacks, and where lines may break. How wide text is, and so where it
// wraps, is each theme's own business: text_wrap() asks the theme.

#include <stdbool.h>
#include <vita2d.h>

// Longest line text_wrap() produces, in bytes
#define WRAP_LINE_MAX 256

// Decodes the UTF-8 character at *p and moves past it. Anything malformed
// reads as U+FFFD, one byte at a time.
unsigned int text_utf8_next(const char **p);

// The button symbol (BUTTON_GLYPH_*) for a marker such as {confirm},
// {triangle} or {start} starting at text (at its "{"), with its length in
// bytes, or -1 if it isn't one. Confirm and cancel follow Swap X & O.
int text_marker(const char *text, int *length);

// Whether assets/nerdfont.ttf has the character (from font_coverage.h,
// made by tools/gen_font_coverage.py), for themes that use that font
bool text_nerdfont_has(unsigned int c);

// The Vita's system font, for what a theme's font can't draw: Japanese,
// Korean, Chinese (in the forms the UI language uses), Turkish ğ and İ.
// Loaded the first time it's asked for, and NULL if it can't be loaded.
vita2d_pgf *text_system_font(void);
// The system font's size at scale 1
#define TEXT_SYSTEM_FONT_BASE_SIZE 20.0f
// Between frames, after the language changes: frees the system font, so
// the next text_system_font() loads the right one for the new language
void text_language_changed(void);

// Splits text into lines no wider than max_width, breaking at spaces and
// newlines, and between any two characters of languages written without
// spaces. measure gives a line's width in the theme's font. Returns the
// number of lines written to lines.
typedef int (*text_measure)(const char *text, void *context);
int text_wrap(const char *text, int max_width, char lines[][WRAP_LINE_MAX], int max_lines,
              text_measure measure, void *context);
