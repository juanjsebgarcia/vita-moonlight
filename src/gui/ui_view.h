#pragma once

// What the screens show, described by meaning rather than by look: the view
// models the shared screen code (src/gui/ui_*.c) builds and every theme
// (src/gui/themes/) renders. Nothing here says where or how anything is
// drawn. See src/gui/themes/README.md for the contract.

#include <stdbool.h>
#include <stdint.h>
#include <vita2d.h>

// Vita control symbols. Text names them with markers such as {confirm} or
// {triangle} (src/gui/text.h), and themes draw them however they like.
enum {
  // Face buttons
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
  // Labelled buttons
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

// No symbol, for a hint that's only text
#define BUTTON_GLYPH_NONE (-1)

// What a row's state means; the theme decides how to show it
typedef enum {
  MENU_STATUS_NONE,
  MENU_STATUS_WARNING,  // something to look at, such as Bluetooth being on
  MENU_STATUS_ONLINE,   // a PC that answers
  MENU_STATUS_OFFLINE,  // a PC that doesn't
  MENU_STATUS_CHANGED,  // a PC that answers at a new address
} menu_status;

// What kind of row it is, for themes that show kinds differently (a switch
// for a toggle, a chevron for an action). Rows that don't say are plain.
typedef enum {
  MENU_ROW_PLAIN,   // a label, perhaps with a value
  MENU_ROW_TOGGLE,  // confirm turns it on and off: see on; subname says it in words
  MENU_ROW_CYCLE,   // confirm steps through values, shown in subname
  MENU_ROW_CHOICE,  // left and right step the value in subname
  MENU_ROW_ACTION,  // confirm opens something or does something
  MENU_ROW_INFO,    // information, selectable for its help
  MENU_ROW_NOTE,    // a message that can't be selected
  MENU_ROW_HEADER,  // a heading for the rows below (separator is set too)
  MENU_ROW_CHECK,   // one of several options; checked marks the current one
} menu_row_kind;

// One row of a menu: what it says and what state it's in, not how it looks
typedef struct menu_entry {
  int id;
  char *name;
  char *suffix;          // extra text on the right, such as an address
  char subname[1024];    // the current value, on the right
  bool disabled, separator;
  bool adjustable;       // left and right change the value
  bool checked;          // the current choice among options
  menu_status status;
  menu_row_kind kind;
  bool on;               // MENU_ROW_TOGGLE: whether it's on
  bool danger;           // ends or removes something: Delete, Quit, Disconnect
} menu_entry;

// Every list screen, so a theme can lay each out its own way and animate
// between them. The ids are stable.
typedef enum {
  UI_SCREEN_MAIN,              // the host list: src/gui/ui.c ui_main_menu
  UI_SCREEN_HOST,              // a paired PC's menu: ui.c ui_host_manage_menu
  UI_SCREEN_SEARCH,            // finding PCs: ui_device.c
  UI_SCREEN_CONNECTED,         // a connected PC's menu and apps: ui_connect.c
  UI_SCREEN_SETTINGS,          // ui_settings.c ui_settings_menu
  UI_SCREEN_RECOMMENDED,       // ui_recommended.c
  UI_SCREEN_SPECIAL_KEYS,      // the touch corner buttons: ui_settings.c
  UI_SCREEN_SPECIAL_KEY_LIST,  // choosing a corner's key: ui_settings.c
  UI_SCREEN_DEADZONE,          // the rear touch dead zone: ui_settings.c
  UI_SCREEN_KEYBOARD_LAYOUT,   // ui_keyboard.c
  UI_SCREEN_COUNT
} ui_screen_id;

// A button and what it does there, or text alone with BUTTON_GLYPH_NONE.
// No hint when text is NULL. The text may hold button markers.
typedef struct ui_hint {
  int glyph;
  const char *text;
} ui_hint;

// UI_SCREEN_MAIN: the host list's row ids, so themes can tell its rows
// apart (a PC row's id is MAIN_MENU_CONNECT_PAIRED plus its index)
enum {
  MAIN_MENU_CONNECTED = 100,
  MAIN_MENU_SEARCH,
  MAIN_MENU_CONNECT_RESUME,
  MAIN_MENU_SETTINGS,
  MAIN_MENU_CONNECT,
  MAIN_MENU_CONNECT_PAIRED,
  MAIN_MENU_QUIT = 999,
};

typedef struct main_view {
  const char *version;             // such as "v0.13.2" (the title row reads "Moonlight v0.13.2")
  bool connected;                  // a PC is connected: the rows offer to resume it
  const char *connection_label;    // that PC's name, when connected
  const char *connection_address;  // and its address
} main_view;

// UI_SCREEN_HOST
typedef struct host_view {
  const char *label;        // the PC's name as the player sees it
  menu_status status;       // ONLINE, OFFLINE or CHANGED; NONE when it isn't known
  const char *status_text;  // the same in words, such as "Paired (Online)"
  const char *address;      // its IP address
  const char *port;
} host_view;

// UI_SCREEN_SEARCH
typedef struct search_view {
  bool searching;  // the search is still running
} search_view;

// UI_SCREEN_CONNECTED: a connected PC's box art covers, the row the
// UI_SCREEN_CONNECTED menu's CONNECTED_APPS row stands for
typedef struct boxart_cover {
  int id;                   // the app's id
  const char *name;
  vita2d_texture *texture;  // NULL while it loads, or if it has none
} boxart_cover;

typedef struct boxart_view {
  int count;               // covers, 0 or more
  int selected;            // the selected cover's index
  float position;          // where the row is scrolled to, easing towards selected
  bool focused;            // the menu's cursor is on the covers' row
  bool locked;             // an app is running: only it can be chosen, the rest are dimmed
  int running_id;          // the running app's id, or 0
  const char *counter;     // "3 of 12"
  unsigned int generation; // changes whenever the covers are replaced, for caches
  const boxart_cover *(*cover)(int index);
} boxart_view;

typedef struct connected_view {
  const char *host;         // the PC's name
  const char *address;
  const char *port;
  const char *gpu;          // may be empty
  const char *gfe_version;  // may be empty
  const char *running_app;  // the app streaming now, or NULL
  bool paired;
  bool covers;              // apps are shown as box art (view->boxart), not as rows
} connected_view;

// UI_SCREEN_SETTINGS: the tab bar, only the tabs shown
typedef struct ui_tab {
  const char *name;
  bool current;
} ui_tab;

typedef struct tabs_view {
  const ui_tab *tabs;
  int count;
} tabs_view;

// UI_SCREEN_RECOMMENDED
typedef struct recommended_view {
  int wifi_percent;  // the Wi-Fi signal now, or -1 when wired, on mobile data or unknown
} recommended_view;

// UI_SCREEN_SPECIAL_KEYS: the four corners of the front screen
typedef struct special_keys_view {
  int offset, size;   // each corner square, in screen pixels
  int focused;        // the corner whose row is selected: 0 top left, 1 top right,
                      // 2 bottom left, 3 bottom right, or -1
} special_keys_view;

// UI_SCREEN_DEADZONE: the rear touchpad's active area, in screen pixels,
// and where fingers are on it
#define DEADZONE_MAX_TOUCHES 8
typedef struct deadzone_view {
  int top, right, bottom, left;   // the dead strips' widths
  int touch_count;                // fingers inside the active area
  struct {
    int x, y;
  } touches[DEADZONE_MAX_TOUCHES];
} deadzone_view;

struct menu_view;
typedef void (*gui_view_callback)(struct menu_view *view);

// What a list screen is, fixed while display_menu() shows it
typedef struct menu_screen {
  ui_screen_id id;
  const char *title;         // a title for themes that show one, or NULL
  // Fills in what changes each frame (hint, help, tabs and the screen's own
  // view below) and does any per-frame work that mustn't draw. Runs before
  // the theme draws, from the menu's 4th frame on. May be NULL.
  gui_view_callback update;
  // The screen's own fixed data, for the screens that have it
  const main_view *main;
  const host_view *host;
  const search_view *search;
  const connected_view *connected;
} menu_screen;

// A list screen, frame by frame: what the theme draws
typedef struct menu_view {
  const menu_screen *screen;
  const menu_entry *rows;
  int row_count;
  int cursor;          // the selected row, counting selectable rows only
  int cursor_row;      // the selected row's index in rows
  int drawn_row;       // menu_follow(): the row selected when the frame was drawn
  bool decorated;      // false for the menu's first 3 frames, when update hasn't run
  int scroll;          // the theme's own scroll state; display_menu keeps it, from 0
  // Set by update
  ui_hint hint;        // the button hint shown now
  const char *help;    // help for the selected row, for themes that show it beside the rows
  const tabs_view *tabs;
  const boxart_view *boxart;
  const recommended_view *recommended;
  const special_keys_view *special_keys;
  const deadzone_view *deadzone;
} menu_view;

// Dialogs
typedef enum {
  ALERT_MESSAGE,   // display_alert(): a message and its buttons
  ALERT_ERROR,     // display_error(): something went wrong, with OK
  ALERT_CONFIRM,   // display_confirm(): a question, Yes or No
  ALERT_PROGRESS,  // flash_message(): what's happening, no buttons, one frame
} alert_kind;

#define ALERT_MAX_BUTTONS 4

typedef struct alert_view {
  alert_kind kind;
  const char *message;   // wrapped by the theme; may hold \n and button markers
  int button_count;      // 0 to ALERT_MAX_BUTTONS
  // Index 0 is confirm, 1 cancel, 2 triangle, 3 square; glyphs follow Swap X & O
  int glyphs[ALERT_MAX_BUTTONS];
  const char *captions[ALERT_MAX_BUTTONS];
  const char *pin;       // the pairing PIN the message shows, or NULL
} alert_view;

typedef struct help_view {
  const char *title;
  const char *message;
  int offset;       // lines scrolled, 0 to the theme's help_prepare() result
  int max_offset;
  int close_glyph;  // the button that closes it (confirm, following Swap X & O)
  const char *close_caption;
} help_view;

// Stream overlays, drawn by the video thread on every frame

// vitastats_draw(): the stats overlay. lines[i] starts at lines + i * line_stride.
typedef struct stats_view {
  bool full;                 // every line; otherwise only the frame rate and the network
  float fps;
  int target_fps;
  int wifi_percent;          // -1 when unknown, or not on Wi-Fi
  const char *link_name;     // "Ethernet" or "Mobile data", shown in fps mode; NULL on Wi-Fi
  bool wifi_weak;            // weak enough to cost frames: show the Wi-Fi in a warning colour
  const char *lines;
  int line_stride;
  int line_count;
  int wifi_line;             // the line about Wi-Fi, or -1
  int version;               // changes whenever the lines do, for caching measurements
} stats_view;

// draw_indicators(): the start hint, and the poor network and sprint icons
typedef struct stream_overlay_view {
  const char *hint;           // the start-of-stream hint, rich text
  float hint_opacity;         // 1 while shown, fading to 0; 0 when it's gone
  bool poor_network;
  uint8_t poor_network_alpha; // its pulse, 0 to 255
  bool sprinting;
} stream_overlay_view;
