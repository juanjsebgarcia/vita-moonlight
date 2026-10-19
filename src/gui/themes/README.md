# Themes

A theme is the app's look: geometry, fonts, colours, text measurement,
scrolling and every vita2d draw call. Everything else is shared and written
once: which screens exist, their rows and tabs, their text, what each button
does and where it leads (`src/gui/ui*.c`, `src/settings.c`). Changing a
setting, a row or a menu in one place changes it in every theme.

| Theme | Folder | State |
|---|---|---|
| Homebrew | `homebrew/` | The original look, pixel for pixel. The default on every launch. |
| Morpheus | `morpheus/` | The new look, from `claude_bundle/demo/index.html`. Every screen, dialog and stream overlay has its own layout. Opt-in only. See `morpheus/README.md`. |

On the host list, Homebrew shows "Press R and L to use new theme"
(`STR_THEME_TRY_NEW`). Pressing L and R together there switches to Morpheus
until the app closes. The choice isn't saved, there's no setting, and there's
no way back except relaunching.

## Rules

- **No shared drawing.** A theme never calls another theme's code. Both may
  use the shared, look-free helpers: `ui_view.h` (the view models),
  `text.h` (UTF-8, button markers, the system font fallback, line breaking),
  `guilib.h` (`ui_status_read()` for the clock and battery, `WIDTH` and
  `HEIGHT`), and the i18n strings.
- **Shared code never draws.** No `vita2d_draw_*`, fonts, colours or
  positions outside `themes/`. What's left outside is frame management
  (`ui_start()`, `ui_end()`), the video frame itself (`src/video/vita.c`
  `draw_streaming`), box art texture loading (`ui_boxart.c`) and the system
  font (`text.c`).
- **Measuring never draws.** Width functions must be pure: some run between
  frames (`flash_message()` lays out outside a frame), where a GXM draw
  crashes.
- **Switch only between frames.** `theme_apply_pending()` runs at the top of
  `ui_main_menu()`, after the last frame finished. A theme's `init` and
  `fini` run there.
- **No new strings, no new behaviour.** A theme may re-arrange and re-present
  what a view contains (a "yes" value as a switch, a heading row as a
  title), but every word comes from the view or an existing `tr()` string.
- **Cheap stream overlays.** `draw_stats` and `draw_stream_overlay` run on the
  video thread on every decoded frame. Cache measurements (`stats_view.version`
  changes when the lines do), keep glyph counts as they are.

## The contract (`src/gui/theme.h`)

```c
struct theme {
  const char *name;
  void (*init)(void);              // load fonts and textures, set the clear colour
  void (*fini)(void);              // free them, rendering finished
  void (*language_changed)(void);  // drop anything measured for the old language
  void (*draw_menu)(const menu_view *view);
  void (*menu_follow)(menu_view *view);  // update view->scroll after the cursor moves
  void (*draw_alert)(const alert_view *view);
  int (*help_prepare)(const char *title, const char *message);  // returns the scroll limit
  void (*draw_help)(const help_view *view);
  void (*draw_ime_backdrop)(void);
  void (*draw_stats)(const stats_view *view);
  void (*draw_stream_overlay)(const stream_overlay_view *view);
};
```

`theme_active()` is one pointer read. Every function is called inside a
frame, except `init`, `fini`, `language_changed` and `help_prepare`.

### List screens

`display_menu(rows, count, screen, loop, back, context)` in `guilib.c` owns
the loop: input, the cursor (which counts selectable rows only, moves up and
down, clamps, never wraps), the callbacks and the frames. Each frame it fills
a `menu_view` and calls `draw_menu`; after reading input and moving the
cursor it calls `menu_follow`.

- `view->screen` is the fixed `menu_screen`: a stable `id`
  (`UI_SCREEN_MAIN`, `UI_SCREEN_SETTINGS`...), a `title` for themes with a
  status bar title, and the screen's fixed data (`main`, `host`, `search`,
  `connected`).
- `view->rows` are `menu_entry`s: `name`, `subname` (the value), `suffix`,
  `disabled`, `separator`, `adjustable` (left and right change it),
  `checked`, `status`, plus `kind` (`MENU_ROW_TOGGLE`, `CHOICE`, `ACTION`,
  `INFO`, `NOTE`, `HEADER`, `CHECK`, `CYCLE`, or `PLAIN`), `on` (a toggle's
  state as a bool) and `danger` (Delete, Quit app, Disconnect).
- `view->cursor` is the selected row among selectable ones, `cursor_row` its
  index in `rows` (equal to `row_count` when nothing can be selected).
- `view->scroll` belongs to the theme: an offset in pixels, a first row,
  whatever it needs. `display_menu` starts it at 0 and keeps it between
  frames. Row heights, rows per page and hit areas are the theme's.
- The screen's `update` callback runs before `draw_menu` from the 4th frame
  (`view->decorated` is true from then): it sets `hint`, `help`, `tabs`,
  `boxart`, `recommended`, `special_keys` and `deadzone`. Draw the rows
  always; draw what update provides only when `decorated` (Homebrew's
  decorations appeared from the 4th frame, as the old draw callbacks did).
- `view->hint` is the button hint shown now: a `glyph` (or
  `BUTTON_GLYPH_NONE`) and rich `text`, or `text == NULL` for none.

### Dialogs

All five dialog calls draw alone; the screen behind is cleared.

- `display_alert`, `display_error`, `display_confirm` and `flash_message`
  call `draw_alert` with an `alert_view`: `kind` (`ALERT_MESSAGE`,
  `ALERT_ERROR`, `ALERT_CONFIRM`, `ALERT_PROGRESS` for flash messages, which
  draw one frame and return), `message` (wrap it yourself; it may hold `\n`
  and button markers), up to four `captions` with their `glyphs` (index 0
  confirm, 1 cancel, 2 triangle, 3 square, already following Swap X & O), and
  `pin` when the message shows a pairing PIN.
- `display_help` calls `help_prepare` once (wrap and measure there), then
  `draw_help` each frame with the scroll `offset`, `max_offset` and the
  close button's glyph and caption.

### Text

Text from the view may contain button markers (`{confirm}`, `{triangle}`,
`{l}`, `{start}`...). `text_marker()` turns one into a `BUTTON_GLYPH_*`
(confirm and cancel follow Swap X & O); draw it however the theme draws
buttons. Characters your font lacks (Japanese, Korean, Chinese, Turkish ğ
and İ) can be drawn with `text_system_font()`, which picks the Chinese or
Japanese forms of Han characters for the UI language; scale it by
`size / TEXT_SYSTEM_FONT_BASE_SIZE`. `text_wrap()` breaks lines by the
language's rules and asks your measure function for widths.

## What each screen gives (for Morpheus, DESIGN.md §10)

| Need | Where |
|---|---|
| Screen identity, status bar title | `view->screen->id`, `view->screen->title` |
| Focused row, row heights, scroll | `view->cursor`, `view->cursor_row`; heights and `view->scroll` are the theme's |
| Row kind, toggle bool, status, danger | `menu_entry.kind`, `.on`, `.status`, `.danger` |
| Settings tabs and the current tab | `view->tabs` (`ui_tab` name and `current`, shown tabs only) |
| Focused row's help (Settings, Recommended) | `view->help` |
| S1 host list: connected, label, address, version | `view->screen->main` (`connected`, `connection_label`, `connection_address`, `version`); row ids `MAIN_MENU_*` in `ui_view.h` tell Search, Add, Settings, Quit and PCs (`MAIN_MENU_CONNECT_PAIRED + i`) apart |
| S2 PC menu: label, status enum and text, address | `view->screen->host` |
| S3 search running | `view->screen->search->searching` (as the list was built; the list rebuilds as devices are found) |
| S4 host name, address, port, GPU, GFE version, running app, paired, list style | `view->screen->connected` (`covers` is the box art style) |
| S4 box art: covers, selected, position, focused, locked, running id, counter | `view->boxart`; `boxart->cover(i)` gives id, name and texture. `generation` changes when the covers are replaced; cache labels by it. Textures are only valid during the frame. |
| S6 Wi-Fi percent as a number | `view->recommended->wifi_percent` (-1 when wired, mobile or unknown) |
| S7 corner offset, size, focused corner | `view->special_keys` |
| S9 dead strips and rear touch points | `view->deadzone` (points already inside the active area) |
| S11 dialog kind, captions, PIN | `alert_view.kind`, `.captions`, `.glyphs`, `.pin` |
| S11 help title, lines, offset | `help_view`; wrap in `help_prepare` |
| S12 stats lines, version, weak Wi-Fi line, fps | `stats_view` |
| S12 hint opacity, low battery notice, poor network alpha, sprint | `stream_overlay_view` (`hint` is NULL once it has faded; `battery` is NULL unless a notice shows) |
| S13 IME backdrop | `draw_ime_backdrop` |

## Checking a theme on the Vita

With `developer_mode = true` in `moonlight.conf`, `tools/bench/shot.sh`
saves the menu on screen (`src/devshot.c`). To open a screen that buttons
reach only with a PC or a stream, upload `ux0:data/moonlight/screen.req`
holding one line before launching (`devshot_open_requested_screen()`):
`flash <text>`, `error <text>`, `confirm <text>`, `help <title>|<text>`
(`\n` for a line break), or `overlay` (the stream overlays over black for
10 s; `overlay full` and `overlay poor` show every stats line, or weak
Wi-Fi with the poor network and sprint badges, with made-up numbers, and
log the overlay's drawing time when `save_debug_log` is on). Put `morpheus` before any of them (or alone) to switch to
Morpheus first. Every Homebrew screen must stay identical to the screenshots taken
before a change.

Comparing screenshots between launches needs care: vita2d packs each
character into a font's atlas in the order it's first drawn, and glyph
edges pick up their neighbours there. The status bar's clock draws
different digits from minute to minute, so the same build renders some
letters slightly differently from one launch to the next. For an exact
comparison, build both versions with the same test-only change that
draws a fixed time and battery level, and compare those. Text a theme
adds to a screen that other screens follow (like the new theme hint)
should use font instances of its own for the same reason.
