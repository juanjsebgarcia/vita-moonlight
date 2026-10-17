#include "i18n.h"

#include "../config.h"
#include "../debug.h"

#include "ini.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <psp2/apputil.h>
#include <psp2/system_param.h>
#include <psp2/io/dirent.h>

static const char *const ENGLISH[STR_COUNT] = {
#define S(id, english, note) english,
#include "strings.def"
#undef S
};

static const char *const KEYS[STR_COUNT] = {
#define S(id, english, note) #id,
#include "strings.def"
#undef S
};

// The current language's text for each ID, or NULL to use the English
static char *active[STR_COUNT];

#define MAX_LANGUAGES 32
static i18n_language languages[MAX_LANGUAGES];
static int language_count = 0;
static int current_language = 0;

// Packed with the app, and on the memory card for testing a translation
// without rebuilding (a file there replaces the packed one with the same tag)
#define PACKED_LANGUAGE_DIR "app0:lang"

const char *tr(str_id id) {
  if ((unsigned int)id >= STR_COUNT) {
    return "";
  }
  return active[id] ? active[id] : ENGLISH[id];
}

// Copies up to n bytes of text, stopping before a character that wouldn't
// fit whole. Returns the bytes copied.
static size_t copy_whole_characters(char *out, const char *text, size_t n) {
  size_t len = strlen(text);
  if (len <= n) {
    memcpy(out, text, len);
    return len;
  }
  // Back off to the start of a UTF-8 character
  while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) {
    n--;
  }
  memcpy(out, text, n);
  return n;
}

int tr_fmt(char *out, size_t size, str_id id, int argc, const char *const argv[]) {
  if (size == 0) {
    return 0;
  }
  const char *p = tr(id);
  size_t used = 0;
  size_t room = size - 1;
  while (*p && used < room) {
    if (p[0] == '{' && p[1] == '{') {
      out[used++] = '{';
      p += 2;
    } else if (p[0] == '{' && p[1] >= '0' && p[1] <= '9' && p[2] == '}') {
      int arg = p[1] - '0';
      const char *value = arg < argc && argv[arg] ? argv[arg] : "";
      used += copy_whole_characters(out + used, value, room - used);
      p += 3;
    } else {
      // Copy one whole character
      size_t n = 1;
      while ((p[n] & 0xC0) == 0x80) {
        n++;
      }
      if (used + n > room) {
        break;
      }
      memcpy(out + used, p, n);
      used += n;
      p += n;
    }
  }
  out[used] = '\0';
  return (int)used;
}

/*
 * Language files
 */

// Undoes the escapes a value can use: \n for a line break, \\ for a backslash
static char *unescape(const char *value) {
  char *out = malloc(strlen(value) + 1);
  if (out == NULL) {
    return NULL;
  }
  char *o = out;
  for (const char *p = value; *p; p++) {
    if (p[0] == '\\' && p[1] == 'n') {
      *o++ = '\n';
      p++;
    } else if (p[0] == '\\' && p[1] == '\\') {
      *o++ = '\\';
      p++;
    } else {
      *o++ = *p;
    }
  }
  *o = '\0';
  return out;
}

static int meta_handler(void *user, const char *section, const char *name, const char *value) {
  i18n_language *language = user;
  if (strcmp(section, "meta") != 0) {
    // The meta section comes first, so the rest isn't needed: stop reading
    return strcmp(section, "strings") == 0 ? 0 : 1;
  }
  if (strcmp(name, "tag") == 0) {
    snprintf(language->tag, sizeof(language->tag), "%s", value);
  } else if (strcmp(name, "name") == 0) {
    snprintf(language->name, sizeof(language->name), "%s", value);
  } else if (strcmp(name, "font") == 0) {
    snprintf(language->font, sizeof(language->font), "%s", value);
  }
  return 1;
}

static int find_language(const char *tag) {
  for (int i = 0; i < language_count; i++) {
    if (strcasecmp(languages[i].tag, tag) == 0) {
      return i;
    }
  }
  return -1;
}

static void scan_directory(const char *dir) {
  SceUID handle = sceIoDopen(dir);
  if (handle < 0) {
    return;
  }
  SceIoDirent entry;
  while (sceIoDread(handle, &entry) > 0) {
    size_t len = strlen(entry.d_name);
    if (len < 5 || strcasecmp(entry.d_name + len - 4, ".ini") != 0) {
      continue;
    }
    i18n_language language = {0};
    snprintf(language.font, sizeof(language.font), "latin");
    snprintf(language.path, sizeof(language.path), "%s/%s", dir, entry.d_name);
    ini_parse(language.path, meta_handler, &language);
    // A file without a tag, such as the template, isn't a language
    if (language.tag[0] == '\0' || strcasecmp(language.tag, "en") == 0) {
      continue;
    }
    if (language.name[0] == '\0') {
      snprintf(language.name, sizeof(language.name), "%s", language.tag);
    }
    int existing = find_language(language.tag);
    if (existing >= 0) {
      // The memory card's copy replaces the packed one
      languages[existing] = language;
    } else if (language_count < MAX_LANGUAGES) {
      languages[language_count++] = language;
    }
    vita_debug_log("[I18N] found %s (%s) in %s", language.tag, language.name, language.path);
  }
  sceIoDclose(handle);
}

static int strings_handler(void *user, const char *section, const char *name, const char *value) {
  (void)user;
  if (strcmp(section, "strings") != 0) {
    return 1;
  }
  for (int i = 0; i < STR_COUNT; i++) {
    if (strcmp(KEYS[i], name) == 0) {
      free(active[i]);
      active[i] = value[0] ? unescape(value) : NULL;
      return 1;
    }
  }
  vita_debug_log("[I18N] unknown string %s", name);
  return 1;
}

static void clear_active(void) {
  for (int i = 0; i < STR_COUNT; i++) {
    free(active[i]);
    active[i] = NULL;
  }
}

bool i18n_set(const char *tag) {
  int index = find_language(tag);
  if (index < 0) {
    return false;
  }
  clear_active();
  current_language = index;
  if (languages[index].path[0]) {
    int ret = ini_parse(languages[index].path, strings_handler, NULL);
    vita_debug_log("[I18N] loaded %s from %s (%d)", tag, languages[index].path, ret);
  }
  return true;
}

const char *i18n_system_tag(void) {
  // SceSystemParamLang, in order (psp2/system_param.h). Portugal's Portuguese
  // and British English share the Brazilian and American files unless a
  // language of their own is added.
  static const char *const TAGS[] = {
    "ja", "en", "fr", "es", "de", "it", "nl", "pt", "ru", "ko",
    "zh-Hant", "zh-Hans", "fi", "sv", "da", "no", "pl", "pt-BR", "en-GB", "tr",
  };
  int lang = 1;
  if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &lang) < 0 || lang < 0 ||
      lang >= (int)(sizeof(TAGS) / sizeof(TAGS[0]))) {
    return "en";
  }
  return TAGS[lang];
}

// The language for a tag: the same tag, else one with the same base
// language (pt finds pt-BR), else English
static const char *best_match(const char *tag) {
  if (find_language(tag) >= 0) {
    return tag;
  }
  size_t base = strcspn(tag, "-");
  for (int i = 0; i < language_count; i++) {
    if (strncasecmp(languages[i].tag, tag, base) == 0 &&
        (languages[i].tag[base] == '\0' || languages[i].tag[base] == '-')) {
      return languages[i].tag;
    }
  }
  return "en";
}

void i18n_init(const char *setting) {
  language_count = 0;
  i18n_language english = {.tag = "en", .name = "English", .font = "latin"};
  languages[language_count++] = english;
  scan_directory(PACKED_LANGUAGE_DIR);
  char card_dir[4200];
  snprintf(card_dir, sizeof(card_dir), "%s/lang", config.key_dir);
  scan_directory(card_dir);

  i18n_use(setting);
}

void i18n_use(const char *setting) {
  const char *wanted = setting == NULL || setting[0] == '\0' || strcmp(setting, "auto") == 0
                       ? i18n_system_tag() : setting;
  const char *tag = best_match(wanted);
  vita_debug_log("[I18N] setting %s, system %s, using %s", setting ? setting : "(none)", i18n_system_tag(), tag);
  i18n_set(tag);
}

bool i18n_follow_system(void) {
  if (config.language != NULL && config.language[0] != '\0' && strcmp(config.language, "auto") != 0) {
    return false;
  }
  const char *tag = best_match(i18n_system_tag());
  if (strcasecmp(tag, i18n_current()->tag) == 0) {
    return false;
  }
  vita_debug_log("[I18N] the system language changed: using %s", tag);
  return i18n_set(tag);
}

int i18n_language_count(void) {
  return language_count;
}

const i18n_language *i18n_language_at(int index) {
  return index >= 0 && index < language_count ? &languages[index] : NULL;
}

const i18n_language *i18n_current(void) {
  return &languages[current_language];
}
