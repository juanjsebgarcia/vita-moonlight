// keyboardsystem.c
// Lógica de apertura de teclado IME sin cuadro de texto para PS Vita
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/libime.h>
#include <psp2/sysmodule.h>
#include <psp2/types.h>
#include <psp2/kernel/clib.h>
#include <string.h>
#include "Limelight.h" // Asegúrate de que la ruta sea correcta según tu proyecto
#include "input/keyboardkeys.h"
#include "config.h"
#include "session.h"
#include "../src/debug.h"

#define WORK_BUFFER_SIZE (SCE_IME_WORK_BUFFER_SIZE)

static uint8_t work_buffer[WORK_BUFFER_SIZE];
// static SceWChar16 input_text_dummy[1];   // Eliminado: no se usa
static SceWChar16 output_text[4];      // Buffer para recibir el texto (aunque no lo mostraremos)
#include "input/keyboard.h"
#include <stddef.h>

// Estado interno del overlay de teclado virtual
static bool _keyboard_overlay_open = false;

// Exponer el estado para otros módulos
bool keyboardsystem_is_open(void) {
    return _keyboard_overlay_open;
}

static int find_vk_for_char(wchar_t ch, int* vk, int* needs_shift) {
    int count = 0;
    const struct CharVKMap* dict = get_char_vk_dict(&count);
    for (int i = 0; i < count; ++i) {
        if (dict[i].ch == ch) {
            *vk = dict[i].vk;
            *needs_shift = dict[i].needs_shift;
            return 1;
        }
    }
    return 0;
}

// The character just typed: the first in the buffer that isn't a placeholder,
// joining a UTF-16 surrogate pair (emoji and the like) into one code point
static uint32_t typed_character(void) {
    for (int i = 0; i < 4; ++i) {
        uint32_t c = output_text[i];
        if (c == 0 || c == 1) {
            continue;
        }
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < 4 && output_text[i + 1] >= 0xDC00 && output_text[i + 1] <= 0xDFFF) {
            return 0x10000 + ((c - 0xD800) << 10) + (output_text[i + 1] - 0xDC00);
        }
        return c;
    }
    return 0;
}

// Sends a typed character. Sunshine takes it as text, whatever its keyboard
// layout or language; other hosts get the key presses for it on a US keyboard.
static void send_character(uint32_t c) {
    if (c >= 0x20 && session_host_takes_text()) {
        char utf8[4];
        unsigned int length;
        if (c < 0x80) {
            utf8[0] = (char)c;
            length = 1;
        } else if (c < 0x800) {
            utf8[0] = (char)(0xC0 | (c >> 6));
            utf8[1] = (char)(0x80 | (c & 0x3F));
            length = 2;
        } else if (c < 0x10000) {
            utf8[0] = (char)(0xE0 | (c >> 12));
            utf8[1] = (char)(0x80 | ((c >> 6) & 0x3F));
            utf8[2] = (char)(0x80 | (c & 0x3F));
            length = 3;
        } else {
            utf8[0] = (char)(0xF0 | (c >> 18));
            utf8[1] = (char)(0x80 | ((c >> 12) & 0x3F));
            utf8[2] = (char)(0x80 | ((c >> 6) & 0x3F));
            utf8[3] = (char)(0x80 | (c & 0x3F));
            length = 4;
        }
        vita_debug_log("[IME MOONLIGHT] text U+%04X", c);
        LiSendUtf8TextEvent(utf8, length);
        return;
    }
    int vk = 0, needs_shift = 0;
    if (c <= 0xFFFF && find_vk_for_char((wchar_t)c, &vk, &needs_shift)) {
        vita_debug_log("[IME MOONLIGHT] key U+%04X -> VK 0x%02X", c, vk);
        if (needs_shift) LiSendKeyboardEvent(0x10, KEY_ACTION_DOWN, 0);
        LiSendKeyboardEvent(vk, KEY_ACTION_DOWN, 0);
        LiSendKeyboardEvent(vk, KEY_ACTION_UP, 0);
        if (needs_shift) LiSendKeyboardEvent(0x10, KEY_ACTION_UP, 0);
    }
}

static int prev_caret_index = 1;
static int logical_caret = 0;
static SceWChar16 ime_working_buffer[4] = {1, 0, 0, 0};
static SceImeCaret caret_rev;
static int caret_toggle = 0;
static int ime_just_opened = 1;
static int forzar_centro = 0;

static void keyboardsystem_ime_event_handler(void *arg, const SceImeEventData *e) {
    // Log extendido como ime_test.c
    vita_debug_log("[IME MOONLIGHT] Evento IME id=%d, caretIndex=%d, buffer=[%04X %04X %04X %04X]", e->id, e->param.caretIndex, output_text[0], output_text[1], output_text[2], output_text[3]);
    int caret = e->param.caretIndex;
    // --- IGNORAR primer evento de borrado tras abrir el teclado si ch==0 ---
    if (ime_just_opened && e->id == 1 && caret == 0) {
        wchar_t ch = 0;
        for (int i = 0; i < 4; ++i) {
            if (output_text[i] != 0 && output_text[i] != 1) {
                ch = output_text[i];
                break;
            }
        }
        if (ch == 0) {
            vita_debug_log("[IME MOONLIGHT] Primer evento de borrado tras abrir teclado ignorado (ch==0).");
            ime_just_opened = 0;
            // Limpiar buffer/caret
            SceWChar16 dummy[4] = {1, 1, 1, 0};
            sceClibMemset(&caret_rev, 0, sizeof(SceImeCaret));
            caret_rev.index = 1;
            sceImeSetCaret(&caret_rev);
            sceImeSetText(dummy, 4);
            for (int i = 0; i < 4; ++i) output_text[i] = 1;
            forzar_centro = 1;
            return;
        }
    }
    // --- BACKSPACE (borrado) y TECLA NORMAL/ESPECIAL ---
    // A character outside the basic range (an emoji) takes two UTF-16 units,
    // so the caret ends one further on
    if (e->id == 1 && (caret == 0 || caret == 1 || (caret == 2 && typed_character() > 0xFFFF))) {
        wchar_t ch = 0;
        for (int i = 0; i < 4; ++i) {
            if (output_text[i] != 0 && output_text[i] != 1) {
                ch = output_text[i];
                break;
            }
        }
        // BACKSPACE
        if (caret == 0 && (ch == 0x08 || ch == 0x7F || ch == 0)) {
            vita_debug_log("[IME MOONLIGHT] VK_BACKSPACE (id=1, caretIndex=0, ch=0x%04X '%lc')", ch, ch);
            LiSendKeyboardEvent(0x08, KEY_ACTION_DOWN, 0);
            LiSendKeyboardEvent(0x08, KEY_ACTION_UP, 0);
            for (int i = 0; i < 4; ++i) output_text[i] = 1;
            forzar_centro = 1;
            return;
        }
        // A typed character
        if (ch && (caret == 1 || !((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')))) {
            send_character(typed_character());
            for (int i = 0; i < 4; ++i) output_text[i] = 1;
            forzar_centro = 1;
            return;
        }
    }
    // --- FLECHA IZQUIERDA ---
    if (e->id == 2 && caret == 0) {
        vita_debug_log("[IME MOONLIGHT] VK_LEFT (id=2, caretIndex=0)");
        LiSendKeyboardEvent(0x25, KEY_ACTION_DOWN, 0);
        LiSendKeyboardEvent(0x25, KEY_ACTION_UP, 0);
        for (int i = 0; i < 4; ++i) output_text[i] = 1;
        forzar_centro = 1;
        return;
    }
    // --- FLECHA DERECHA ---
    if (e->id == 2 && caret == 2) {
        vita_debug_log("[IME MOONLIGHT] VK_RIGHT (id=2, caretIndex=2)");
        LiSendKeyboardEvent(0x27, KEY_ACTION_DOWN, 0);
        LiSendKeyboardEvent(0x27, KEY_ACTION_UP, 0);
        for (int i = 0; i < 4; ++i) output_text[i] = 1;
        forzar_centro = 1;
        return;
    }
    // --- ENTER (Aceptar) ---
    if (e->id == 5) {
        vita_debug_log("[IME MOONLIGHT] VK_RETURN (id=5, caretIndex=%d)", caret);
        LiSendKeyboardEvent(0x0D, KEY_ACTION_DOWN, 0);
        LiSendKeyboardEvent(0x0D, KEY_ACTION_UP, 0);
        for (int i = 0; i < 4; ++i) output_text[i] = 1;
        forzar_centro = 1;
        return;
    }
    // --- CERRAR IME (Minimizar/cancelar) ---
    if (e->id == 4) {
        vita_debug_log("[IME MOONLIGHT] Cerrar IME (id=4, caretIndex=%d)", caret);
        sceImeClose();
        return;
    }
    // --- RECUPERACIÓN: Si caretIndex no es 0, 1 o 2 y hay un carácter válido, limpiar buffer/caret ---
    if (e->id == 1 && (caret != 0 && caret != 1 && caret != 2)) {
        wchar_t ch = 0;
        for (int i = 0; i < 4; ++i) {
            if (output_text[i] != 0 && output_text[i] != 1) {
                ch = output_text[i];
                break;
            }
        }
        if (ch) {
            forzar_centro = 1;
        }
    }
}

void keyboardsystem_open_keyboard(void) {
    // Marcar overlay como abierto
    _keyboard_overlay_open = true;

    sceClibPrintf("[IME MOONLIGHT] Teclado abierto\n");
    // Inicializar buffer y caret IME robustos para movimiento infinito
    ime_working_buffer[0] = 1;
    ime_working_buffer[1] = 1;
    ime_working_buffer[2] = 1;
    ime_working_buffer[3] = 0;
    caret_toggle = 0;
    sceClibMemset(&caret_rev, 0, sizeof(SceImeCaret));
    caret_rev.index = 1;
    logical_caret = 0;
    prev_caret_index = 1;
    ime_just_opened = 1;
    forzar_centro = 0;

    // 1) Cargar el módulo de IME (si no está cargado)
    sceSysmoduleLoadModule(SCE_SYSMODULE_IME);

    // 2) Inicializar parámetros de IME a cero y fijar sdkVersion
    SceImeParam param;
    sceImeParamInit(&param);

    // 3) Configurar campos obligatorios de SceImeParam
    // Text is sent as text, so any keyboard language works: offer the ones
    // the player chose in the Vita's keyboard settings
    param.supportedLanguages = 0x000FFFFF;
    param.languagesForced   = SCE_FALSE;
    param.type              = SCE_IME_TYPE_DEFAULT;
    param.option            = SCE_IME_OPTION_NO_ASSISTANCE;
    param.work              = work_buffer;
    param.handler           = keyboardsystem_ime_event_handler;
    param.initialText       = ime_working_buffer;
    param.maxTextLength     = 4;
    param.inputTextBuffer   = output_text;
    param.enterLabel        = SCE_IME_ENTER_LABEL_DEFAULT;

    // 4) Abrir el teclado en pantalla
    int res = sceImeOpen(&param);
    if (res < 0) {
        sceClibPrintf("Error al abrir IME: 0x%08X\n", res);
        _keyboard_overlay_open = false;
        return;
    }
    // Solo aquí es seguro llamar a setText y setCaret
    sceImeSetText(ime_working_buffer, 4);
    sceImeSetCaret(&caret_rev);
    // 5) Bucle de actualización: llamar a sceImeUpdate() hasta que devuelva < 0
    while (1) {
        if (forzar_centro) {
            SceWChar16 dummy[4] = {1, 1, 1, 0};
            sceImeSetText(dummy, 4);
            ime_working_buffer[0] = 1;
            ime_working_buffer[1] = 1;
            ime_working_buffer[2] = 1;
            ime_working_buffer[3] = 0;
            sceClibMemset(&caret_rev, 0, sizeof(SceImeCaret));
            caret_rev.index = 1;
            sceImeSetCaret(&caret_rev);
            forzar_centro = 0;
        }
        int status = sceImeUpdate();
        if (status < 0) {
            sceClibPrintf("[IME MOONLIGHT] Teclado cerrado\n");
            break;
        }
        sceKernelDelayThread(1000); // Esperar 1 ms
    }
    // Marcar overlay como cerrado al salir del bucle
    _keyboard_overlay_open = false;
}
