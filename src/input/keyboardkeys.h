#ifndef KEYBOARDKEYS_H
#define KEYBOARDKEYS_H

#include <wchar.h>

// Estructura para el mapeo carácter → VK + shift
struct CharVKMap {
    wchar_t ch;
    int vk;
    int needs_shift;
};

// The key presses for each character on a US keyboard, and how many there are
const struct CharVKMap* get_char_vk_dict(int* out_count);

#endif // KEYBOARDKEYS_H
