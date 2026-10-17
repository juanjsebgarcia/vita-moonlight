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
#include "input/imetext.h"

#define WORK_BUFFER_SIZE (SCE_IME_WORK_BUFFER_SIZE)

static uint8_t work_buffer[WORK_BUFFER_SIZE];
// static SceWChar16 input_text_dummy[1];   // Eliminado: no se usa
// The keyboard's text is kept at three placeholders (U+0001) with the caret
// after the first, so Backspace and both arrows always have something to act
// on. They come back after each edit, but not during a composition: text
// committed beside it (Korean commits each syllable while composing the
// next) stays in the keyboard until the composition ends, at a space or
// punctuation, say. sceImeSetText can't be told to keep a composition, so
// the keyboard gets the most room the API allows.
#define IME_PLACEHOLDERS 3
#define IME_TEXT_MAX SCE_IME_MAX_TEXT_LENGTH
static SceWChar16 output_text[IME_TEXT_MAX + 1];      // Buffer para recibir el texto (aunque no lo mostraremos)
// The text as last acted on: what an update is compared with
static SceWChar16 committed_text[IME_TEXT_MAX + 1];
static size_t committed_length = 0;
// The keyboard's text without its composition
static SceWChar16 visible_text[IME_TEXT_MAX + 1];
// A composition (kana before conversion, and the like) is in the keyboard
static int composing = 0;
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

// Sends one character as key presses on a US keyboard, for hosts that can't
// take text and for control characters
static void send_key_for(uint32_t c) {
    int vk = 0, needs_shift = 0;
    if (c <= 0xFFFF && find_vk_for_char((wchar_t)c, &vk, &needs_shift)) {
        vita_debug_log("[IME MOONLIGHT] key U+%04X -> VK 0x%02X", c, vk);
        if (needs_shift) LiSendKeyboardEvent(0x10, KEY_ACTION_DOWN, 0);
        LiSendKeyboardEvent(vk, KEY_ACTION_DOWN, 0);
        LiSendKeyboardEvent(vk, KEY_ACTION_UP, 0);
        if (needs_shift) LiSendKeyboardEvent(0x10, KEY_ACTION_UP, 0);
    }
}

// Sends typed UTF-16 text, any number of characters, surrogate pairs joined.
// Sunshine takes it as text, whatever its keyboard layout or language; other
// hosts get the key presses for it on a US keyboard. Text goes in pieces of
// up to 32 bytes (moonlight-common-c's UTF8_TEXT_EVENT_MAX_COUNT), split
// between characters.
#define IME_UTF8_CHUNK 32
static void send_text(const SceWChar16 *text, size_t length) {
    int as_text = session_host_takes_text();
    char utf8[IME_UTF8_CHUNK];
    size_t used = 0;
    while (length > 0) {
        uint32_t c;
        size_t units = ime_text_next(text, length, &c);
        text += units;
        length -= units;
        if (as_text && c >= 0x20) {
            if (used + 4 > IME_UTF8_CHUNK) {
                LiSendUtf8TextEvent(utf8, (unsigned int)used);
                used = 0;
            }
            vita_debug_log("[IME MOONLIGHT] text U+%04X", c);
            used += ime_text_utf8(c, utf8 + used);
        } else {
            if (used > 0) {
                LiSendUtf8TextEvent(utf8, (unsigned int)used);
                used = 0;
            }
            send_key_for(c);
        }
    }
    if (used > 0) {
        LiSendUtf8TextEvent(utf8, (unsigned int)used);
    }
}

static void reset_committed_text(void) {
    for (int i = 0; i < IME_PLACEHOLDERS; ++i) committed_text[i] = 1;
    committed_text[IME_PLACEHOLDERS] = 0;
    committed_length = IME_PLACEHOLDERS;
}

static int committed_is_placeholders(void) {
    if (committed_length != IME_PLACEHOLDERS) return 0;
    for (int i = 0; i < IME_PLACEHOLDERS; ++i) {
        if (committed_text[i] != 1) return 0;
    }
    return 1;
}

// Acts on the keyboard's committed text, that is all of it but the active
// composition (preedit_length units from preedit_index): units removed since
// the last time become Backspaces, units added are sent as text. Returns
// whether anything changed.
static int send_text_changes(SceUInt32 preedit_index, SceUInt32 preedit_length) {
    size_t length = ime_text_without(output_text, ime_text_length(output_text, IME_TEXT_MAX), preedit_index, preedit_length, visible_text);
    visible_text[length] = 0;
    ime_text_edit edit = ime_text_diff(committed_text, committed_length, visible_text, length);
    if (edit.deleted == 0 && edit.inserted_length == 0) {
        return 0;
    }
    vita_debug_log("[IME MOONLIGHT] edit at %u: %u removed, %u added", (unsigned)edit.index, (unsigned)edit.deleted, (unsigned)edit.inserted_length);
    for (size_t i = 0; i < edit.deleted; ++i) {
        LiSendKeyboardEvent(0x08, KEY_ACTION_DOWN, 0);
        LiSendKeyboardEvent(0x08, KEY_ACTION_UP, 0);
    }
    send_text(visible_text + edit.index, edit.inserted_length);
    memcpy(committed_text, visible_text, length * sizeof(SceWChar16));
    committed_text[length] = 0;
    committed_length = length;
    return 1;
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
    // For a text update, the event carries an SceImeEditText: caretIndex
    // below is its preeditIndex
    vita_debug_log("[IME MOONLIGHT] Evento IME id=%d, caretIndex=%d, preedit=%u+%u, buffer=[%04X %04X %04X %04X %04X]", e->id, e->param.caretIndex, e->param.text.preeditIndex, e->param.text.preeditLength, output_text[0], output_text[1], output_text[2], output_text[3], output_text[4]);
    int caret = e->param.caretIndex;
    // --- IGNORAR primer evento de borrado tras abrir el teclado si ch==0 ---
    if (ime_just_opened && e->id == 1 && caret == 0) {
        wchar_t ch = 0;
        for (int i = 0; i < IME_TEXT_MAX && output_text[i] != 0; ++i) {
            if (output_text[i] != 1) {
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
            memcpy(output_text, dummy, sizeof(dummy));
            reset_committed_text();
            forzar_centro = 1;
            return;
        }
    }
    // --- TEXTO: borrado y caracteres escritos ---
    // Compared with the text before: removed units are Backspaces, added ones
    // are sent as text, however many a single update brings (a conversion
    // such as 日本 is committed at once). The active composition is left out
    // and stays in the keyboard, untouched, until it is committed; text
    // committed beside it (a Korean syllable while the next is composed) is
    // sent straight away. The placeholders come back once nothing is composed.
    if (e->id == 1) {
        composing = e->param.text.preeditLength > 0;
        if (!composing && ime_text_length(output_text, IME_TEXT_MAX) == 0) {
            // Not a key press: the keyboard dropped its text. Restore it.
            reset_committed_text();
            forzar_centro = 1;
            return;
        }
        int changed = send_text_changes(e->param.text.preeditIndex, e->param.text.preeditLength);
        if (!composing && (changed || !committed_is_placeholders())) {
            forzar_centro = 1;
        }
        return;
    }
    // While composing, the caret and Enter belong to the composition
    if (composing && e->id == 2) {
        return;
    }
    // --- FLECHA IZQUIERDA ---
    if (e->id == 2 && caret == 0) {
        vita_debug_log("[IME MOONLIGHT] VK_LEFT (id=2, caretIndex=0)");
        LiSendKeyboardEvent(0x25, KEY_ACTION_DOWN, 0);
        LiSendKeyboardEvent(0x25, KEY_ACTION_UP, 0);
        forzar_centro = 1;
        return;
    }
    // --- FLECHA DERECHA ---
    if (e->id == 2 && caret == 2) {
        vita_debug_log("[IME MOONLIGHT] VK_RIGHT (id=2, caretIndex=2)");
        LiSendKeyboardEvent(0x27, KEY_ACTION_DOWN, 0);
        LiSendKeyboardEvent(0x27, KEY_ACTION_UP, 0);
        forzar_centro = 1;
        return;
    }
    // --- ENTER (Aceptar) ---
    if (e->id == 5) {
        if (composing) {
            // Enter confirms the composition: send it as typed
            vita_debug_log("[IME MOONLIGHT] Enter confirms the composition");
            composing = 0;
            send_text_changes(0, 0);
            forzar_centro = 1;
            return;
        }
        vita_debug_log("[IME MOONLIGHT] VK_RETURN (id=5, caretIndex=%d)", caret);
        LiSendKeyboardEvent(0x0D, KEY_ACTION_DOWN, 0);
        LiSendKeyboardEvent(0x0D, KEY_ACTION_UP, 0);
        forzar_centro = 1;
        return;
    }
    // --- CERRAR IME (Minimizar/cancelar) ---
    if (e->id == 4) {
        vita_debug_log("[IME MOONLIGHT] Cerrar IME (id=4, caretIndex=%d)", caret);
        sceImeClose();
        return;
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
    composing = 0;
    reset_committed_text();

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
    param.maxTextLength     = IME_TEXT_MAX;
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
            memcpy(output_text, dummy, sizeof(dummy));
            reset_committed_text();
            composing = 0;
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
