#pragma once

#include <stdbool.h>
#include <stddef.h>

// Translations. Every piece of text the UI shows has an ID, with its English
// in strings.def, which is the source of truth. A language is a file,
// lang/<tag>.ini, that gives some or all of the IDs in that language; any it
// leaves out stay in English. Adding a language needs no code: just the file.
//
// This is data, with no drawing, so every UI shows the same text.

typedef enum {
#define S(id, english, note) STR_##id,
#include "strings.def"
#undef S
  STR_COUNT
} str_id;

// The text for id in the current language, never NULL
const char *tr(str_id id);

// Writes id's text to out with {0} to {9} replaced by the arguments, which
// are all strings, so a translation can put them in any order ({{ writes a
// brace). Returns the length written. Truncates on a character boundary.
int tr_fmt(char *out, size_t size, str_id id, int argc, const char *const argv[]);
#define TR_FMT(out, size, id, ...) \
  tr_fmt((out), (size), (id), \
         (int)(sizeof((const char *[]){__VA_ARGS__}) / sizeof(const char *)), \
         (const char *[]){__VA_ARGS__})

typedef struct {
  char tag[16];          // such as "pt-BR"
  char name[64];         // in the language itself, such as "Português (Brasil)"
  char font[24];         // which fonts it needs, such as "latin" or "cjk-ja"
  char path[512];        // its file, or empty for English, which is built in
} i18n_language;

// Finds the language files and picks the language: setting is a tag, or
// "auto" (or NULL) for the system language
void i18n_init(const char *setting);

// Switches to the language a setting asks for, as i18n_init does
void i18n_use(const char *setting);

// Under Automatic, switches to the system language if it changed since the
// app started (the Vita can change it while the app waits in the
// background). Returns true if it switched: rebuild what shows text.
bool i18n_follow_system(void);

// Switches language. Returns false, staying as it was, if there's no such
// language. Whoever shows text must rebuild it afterwards.
bool i18n_set(const char *tag);

// The languages found: English first, then each file, by tag
int i18n_language_count(void);
const i18n_language *i18n_language_at(int index);
const i18n_language *i18n_current(void);

// The tag for the Vita's system language, such as "ja" or "en"
const char *i18n_system_tag(void);
