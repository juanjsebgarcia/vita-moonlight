#include <stdbool.h>
#include <vita2d.h>

#include <psp2/touch.h>

#define WIDTH 960
#define HEIGHT 544

#define ICON_LEFT_ARROW   "\xef\x95\x8c"
#define ICON_RIGHT_ARROW  "\xef\x95\x93"
#define ICON_LEFT_RIGHT_ARROWS ICON_LEFT_ARROW ICON_RIGHT_ARROW
#define ICON_NETWORK      "\xef\xa6\x8c"
#define ICON_SPRINTING    "\xeF\xa5\xaD"


typedef struct menu_entry {
  int id;
  unsigned int color;
  char *name, *suffix;
  char subname[1024];
  bool disabled, separator;
  bool warning; // drawn in yellow when not selected
  bool is_host_entry; // Nuevo: solo true para hosts emparejados
} menu_entry;

typedef struct menu_geom {
  int x, y, width, height, el, total_y;
} menu_geom;

// Vita control symbols, drawn as shapes because the UI font has no glyphs for them
enum {
  // Face buttons, in their usual colours
  BUTTON_GLYPH_CROSS,
  BUTTON_GLYPH_CIRCLE,
  BUTTON_GLYPH_TRIANGLE,
  BUTTON_GLYPH_SQUARE,
  // D-pad, with the named direction highlighted, or all four for BUTTON_GLYPH_DPAD
  BUTTON_GLYPH_DPAD,
  BUTTON_GLYPH_DPAD_UP,
  BUTTON_GLYPH_DPAD_DOWN,
  BUTTON_GLYPH_DPAD_LEFT,
  BUTTON_GLYPH_DPAD_RIGHT,
  // Labelled buttons; these are wider than they are tall
  BUTTON_GLYPH_L,
  BUTTON_GLYPH_R,
  BUTTON_GLYPH_L2,
  BUTTON_GLYPH_R2,
  BUTTON_GLYPH_START,
  BUTTON_GLYPH_SELECT,
  // Round controls
  BUTTON_GLYPH_PS,
  BUTTON_GLYPH_LEFT_STICK,
  BUTTON_GLYPH_RIGHT_STICK,
  BUTTON_GLYPH_L3,
  BUTTON_GLYPH_R3,
  // Touch surfaces
  BUTTON_GLYPH_FRONT_TOUCH,
  BUTTON_GLYPH_REAR_TOUCH,
  BUTTON_GLYPH_COUNT
};

// Longest line wrap_text() produces, in bytes
#define WRAP_LINE_MAX 256

extern vita2d_font *font;

struct menu_geom make_geom_centered(int w, int h);

#define SCE_CTRL_HOLD 0x80000000

typedef struct input_data {
  int buttons;
  SceTouchData touch;
} input_data;

typedef int (*gui_loop_callback) (int, void *, const input_data *);
typedef int (*gui_back_callback) (void *);
typedef void (*gui_draw_callback) (void);

bool was_button_pressed(short id);
bool is_button_down(short id);
bool is_rectangle_touched(const SceTouchData *touch, int lx, int ly, int rx, int ry);

int display_menu(
        menu_entry menu[],
        int total_elements,
        menu_geom *geom_ptr,
        gui_loop_callback loop_callback,
        gui_back_callback back_callback,
        gui_draw_callback draw_callback,
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

// Draws a control symbol size pixels tall whose bottom sits on the text
// baseline y, and returns its width (labelled buttons are wider than size)
int draw_button_glyph(int glyph, int x, int y, int size);

// Splits text into lines no wider than max_width at the UI font size, breaking
// at spaces and newlines. Returns the number of lines written to lines.
int wrap_text(const char *text, int max_width, char lines[][WRAP_LINE_MAX], int max_lines);

void flash_message(char *format, ...);

void guilib_init(gui_loop_callback global_loop_cb, gui_draw_callback global_draw_cb);

// Declaración de display_confirm para uso global
int display_confirm(const char* message);
