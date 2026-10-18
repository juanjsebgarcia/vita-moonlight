#pragma once

// The UI's flow, shared by every theme: frames, input, list screens and
// dialogs. What each screen shows is described in ui_view.h; the active
// theme (theme.h) draws it.

#include <stdbool.h>
#include <vita2d.h>

#include <psp2/touch.h>

#include "ui_view.h"

// The screen, in pixels
#define WIDTH 960
#define HEIGHT 544

#define SCE_CTRL_HOLD 0x80000000

// What a press means, whatever the buttons: confirm and cancel follow Swap X & O,
// and L and R arrive on either of their bits. Screens act on these, so the
// controls are decided in one place and any UI can drive the same screens.
enum {
  UI_INTENT_UP        = 1u << 0,
  UI_INTENT_DOWN      = 1u << 1,
  UI_INTENT_LEFT      = 1u << 2,
  UI_INTENT_RIGHT     = 1u << 3,
  UI_INTENT_CONFIRM   = 1u << 4,
  UI_INTENT_CANCEL    = 1u << 5,
  UI_INTENT_HELP      = 1u << 6,  // triangle
  UI_INTENT_SECONDARY = 1u << 7,  // square
  UI_INTENT_TAB_PREV  = 1u << 8,  // L
  UI_INTENT_TAB_NEXT  = 1u << 9,  // R
  UI_INTENT_START     = 1u << 10,
  UI_INTENT_SELECT    = 1u << 11,
  // Set when the intents repeat because the buttons are being held
  UI_INTENT_REPEAT    = 1u << 31,
};

typedef struct input_data {
  int buttons;           // raw, for the rare check that needs a particular button
  unsigned int intents;  // UI_INTENT_* flags
  SceTouchData touch;
} input_data;

// Reads this frame's buttons and touch, with their intents
input_data read_input(void);

// Pressed this frame, and not repeating from being held: for actions
static inline bool ui_pressed(const input_data *input, unsigned int intent) {
  return (input->intents & intent) != 0 && (input->intents & UI_INTENT_REPEAT) == 0;
}

// Pressed, or repeating while held: for moving through lists and values
static inline bool ui_held(const input_data *input, unsigned int intent) {
  return (input->intents & intent) != 0;
}

// Whether L and R are both held down now. read_input() only reports newly
// pressed buttons, so combinations check the held state with this.
bool ui_shoulders_held(void);

typedef int (*gui_loop_callback) (int, void *, const input_data *);
typedef int (*gui_back_callback) (void *);

bool was_button_pressed(short id);
bool is_button_down(short id);
bool is_rectangle_touched(const SceTouchData *touch, int lx, int ly, int rx, int ry);

// The clock and battery, for themes' status bars. The battery is read at
// most every 10 seconds.
typedef struct ui_status {
  int hour, minute;
  int battery_percent;
  bool battery_charging;
} ui_status;
void ui_status_read(ui_status *status);

// The next display_menu opens with the cursor on the row with this id,
// instead of the first, if it's there and can be selected
void display_menu_select(int id);

// Start and finish a frame. display_menu() finishes the frame its caller
// started, then draws its own; a callback run mid-frame can finish that frame
// early and start a fresh one for display_menu() to finish instead
void ui_start();
void ui_end();

// Shows a list screen until a callback returns non-zero (which it returns)
// or cancel is pressed with no back callback, or one that returns 0 (1)
int display_menu(
        menu_entry menu[],
        int total_elements,
        const menu_screen *screen,
        gui_loop_callback loop_callback,
        gui_back_callback back_callback,
        void *context
        );

void display_alert(
        char *message,
        char *button_captions[],
        int buttons_count,
        gui_loop_callback cb,
        void *context
        );

void display_error(char *format, ...);
// Shows a larger dialog with a title and explanatory text, closed with
// confirm, cancel or triangle
void display_help(const char *title, const char *message);

// Call between frames after the language changes, so text uses the right
// system font for it (Chinese or Japanese forms of Han characters), and the
// theme measures again
void guilib_language_changed(void);

void flash_message(char *format, ...);
// A progress message that shows a pairing PIN, which themes may show apart
void flash_message_pin(const char *pin, const char *message);

void guilib_init(gui_loop_callback global_loop_cb);

// Asks a yes or no question; returns 1 for yes
int display_confirm(const char* message);
