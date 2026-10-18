#include "../i18n/i18n.h"
#include "ui_keyboard.h"
#include "../input/keyboardkeys.h"
#include "ui_settings.h"
#include "guilib.h"
#include "../config.h"
#include <psp2/kernel/clib.h>

static const str_id keyboard_layout_names[] = {
    STR_KEYBOARD_EN_US,
    STR_KEYBOARD_ES_ES,
    STR_KEYBOARD_ES_LATAM,
};

static int keyboard_layout_loop(int id, void *context, const input_data *input) {
    if (ui_pressed(input, UI_INTENT_CONFIRM)) {
        // Selecciona el layout
        keyboardsystem_set_layout((KeyboardLayout)id);
        config.keyboard_layout = id;
        sceClibPrintf("[Moonlight] Layout de teclado seleccionado: %s (id=%d)\n", tr(keyboard_layout_names[id]), id);
        ui_settings_save_config();
        return 1; // salir del menú
    }
    return 0;
}

int keyboard_layout_menu(void) {
    // Sincroniza el layout global con la config antes de mostrar el menú
    keyboardsystem_set_layout((KeyboardLayout)config.keyboard_layout);
    menu_entry menu[KB_LAYOUT_COUNT] = {0}; // Inicializa todo a cero
    for (int i = 0; i < KB_LAYOUT_COUNT; ++i) {
        menu[i].name = (char*)tr(keyboard_layout_names[i]);
        menu[i].id = i;
        menu[i].disabled = false;
        menu[i].checked = i == config.keyboard_layout;
        menu[i].kind = MENU_ROW_CHECK;
    }
    menu_screen screen = {.id = UI_SCREEN_KEYBOARD_LAYOUT, .title = tr(STR_SET_KEYBOARD_LAYOUT)};
    int sel = display_menu(menu, KB_LAYOUT_COUNT, &screen, &keyboard_layout_loop, NULL, menu);
    // El layout ya se aplica en el loop, aquí solo refrescamos el menú principal si es necesario
    (void)sel; // Evita warning de variable no usada
    return 0;
}