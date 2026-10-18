#include "../i18n/i18n.h"
#include "ui_sendkeys.h"

#include "Limelight.h"
#include "guilib.h"

#include "../debug.h"

#include <stdio.h>
#include <string.h>

#include <psp2/kernel/threadmgr.h>

// Win32 virtual key codes. The modifiers are the left-hand keys, as a real
// keyboard sends them; moonlight-common-c adjusts their flags for GFE.
#define VK_TAB      0x09
#define VK_ESCAPE   0x1B
#define VK_DELETE   0x2E
#define VK_C        0x43
#define VK_D        0x44
#define VK_V        0x56
#define VK_LWIN     0x5B
#define VK_F4       0x73
#define VK_LSHIFT   0xA0
#define VK_LCONTROL 0xA2
#define VK_LMENU    0xA4  // Alt

#define CHORD_MAX_KEYS 3

// A pause between key events, so the host sees separate presses, as from a
// keyboard, rather than a burst some apps miss
#define KEY_GAP_US (20 * 1000)

typedef struct {
  str_id name;        // the keys, such as "Ctrl+Alt+Del"
  str_id action;      // what they do on Windows
  bool danger;        // closes something
  int count;
  short keys[CHORD_MAX_KEYS];  // modifiers first, the key last
} chord;

static const chord chords[] = {
  {STR_KEYS_ESC, STR_KEYS_ESC_ACTION, false, 1, {VK_ESCAPE}},
  {STR_KEYS_ALT_TAB, STR_KEYS_ALT_TAB_ACTION, false, 2, {VK_LMENU, VK_TAB}},
  {STR_KEYS_WIN, STR_KEYS_WIN_ACTION, false, 1, {VK_LWIN}},
  {STR_KEYS_WIN_TAB, STR_KEYS_WIN_TAB_ACTION, false, 2, {VK_LWIN, VK_TAB}},
  {STR_KEYS_WIN_D, STR_KEYS_WIN_D_ACTION, false, 2, {VK_LWIN, VK_D}},
  {STR_KEYS_CTRL_C, STR_KEYS_CTRL_C_ACTION, false, 2, {VK_LCONTROL, VK_C}},
  {STR_KEYS_CTRL_V, STR_KEYS_CTRL_V_ACTION, false, 2, {VK_LCONTROL, VK_V}},
  {STR_KEYS_ALT_F4, STR_KEYS_ALT_F4_ACTION, true, 2, {VK_LMENU, VK_F4}},
  {STR_KEYS_CTRL_SHIFT_ESC, STR_KEYS_CTRL_SHIFT_ESC_ACTION, false, 3, {VK_LCONTROL, VK_LSHIFT, VK_ESCAPE}},
  {STR_KEYS_CTRL_ALT_DEL, STR_KEYS_CTRL_ALT_DEL_ACTION, false, 3, {VK_LCONTROL, VK_LMENU, VK_DELETE}},
};

#define CHORD_COUNT ((int)(sizeof(chords) / sizeof(chords[0])))

// The modifier flag a key sets while it's held, or 0 for other keys
static char modifier_flag(short key) {
  switch (key) {
    case VK_LSHIFT: return MODIFIER_SHIFT;
    case VK_LCONTROL: return MODIFIER_CTRL;
    case VK_LMENU: return MODIFIER_ALT;
    case VK_LWIN: return MODIFIER_META;
    default: return 0;
  }
}

// Presses the keys in order, then releases them in reverse, with each event
// carrying the modifiers held at that moment, as a real keyboard's would
static void send_chord(const chord *c) {
  char held = 0;
  for (int i = 0; i < c->count; i++) {
    held |= modifier_flag(c->keys[i]);
    LiSendKeyboardEvent2(c->keys[i], KEY_ACTION_DOWN, held, 0);
    sceKernelDelayThread(KEY_GAP_US);
  }
  for (int i = c->count - 1; i >= 0; i--) {
    held &= ~modifier_flag(c->keys[i]);
    LiSendKeyboardEvent2(c->keys[i], KEY_ACTION_UP, held, 0);
    if (i > 0) {
      sceKernelDelayThread(KEY_GAP_US);
    }
  }
  vita_debug_log("[KEYS] sent %s\n", tr(c->name));
}

#define SEND_KEYS_SENT 2

static int send_keys_loop(int id, void *context, const input_data *input) {
  if (!ui_pressed(input, UI_INTENT_CONFIRM) || id < 0 || id >= CHORD_COUNT) {
    return 0;
  }
  send_chord(&chords[id]);
  return SEND_KEYS_SENT;
}

bool ui_send_keys_menu(void) {
  // One row per chord: about 1 KB each on this thread's stack
  menu_entry menu[CHORD_COUNT];
  memset(menu, 0, sizeof(menu));
  for (int i = 0; i < CHORD_COUNT; i++) {
    menu[i].id = i;
    menu[i].name = (char *)tr(chords[i].name);
    menu[i].kind = MENU_ROW_ACTION;
    menu[i].danger = chords[i].danger;
    snprintf(menu[i].subname, sizeof(menu[i].subname), "%s", tr(chords[i].action));
  }
  menu_screen screen = {.id = UI_SCREEN_SEND_KEYS, .title = tr(STR_SEND_KEYS)};
  return display_menu(menu, CHORD_COUNT, &screen, &send_keys_loop, NULL, menu) == SEND_KEYS_SENT;
}
