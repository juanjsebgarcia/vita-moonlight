#include "homebrew.h"

#include <stdio.h>
#include <string.h>

hb_geom hb_geom_centered(int w, int h) {
  hb_geom geom = {0};
  geom.x = WIDTH  / 2 - w / 2;
  geom.y = HEIGHT / 2 - h / 2;
  geom.width = w;
  geom.height = h;
  geom.total_y = geom.y + geom.height;
  geom.el = HB_ROW_HEIGHT;
  return geom;
}

// The spare row covers the top margin and keeps the last row clear of the
// point where the menu starts scrolling
hb_geom hb_geom_fitted(int w, int rows) {
  int h = (rows + 1) * HB_ROW_HEIGHT;
  return hb_geom_centered(w, h < HB_MENU_MAX_HEIGHT ? h : HB_MENU_MAX_HEIGHT);
}

void hb_draw_border(hb_geom geom, unsigned int border_color) {
  vita2d_draw_line(geom.x, geom.y, geom.x+geom.width, geom.y, border_color);
  vita2d_draw_line(geom.x, geom.y, geom.x, geom.y+geom.height, border_color);
  vita2d_draw_line(geom.x+geom.width, geom.y, geom.x+geom.width, geom.y+geom.height, border_color);
  vita2d_draw_line(geom.x, geom.y+geom.height, geom.x+geom.width, geom.y+geom.height, border_color);
}

// Each screen's box
static hb_geom menu_geom(const menu_view *view) {
  switch (view->screen->id) {
    case UI_SCREEN_MAIN:
      // Shrinks to its rows, and grows up to the settings menu's height so
      // Settings and Quit stay visible once a few hosts are listed
      return hb_geom_fitted(500, view->row_count);
    case UI_SCREEN_HOST:
      return hb_geom_centered(600, 320);
    case UI_SCREEN_CONNECTED:
      if (view->screen->connected != NULL && view->screen->connected->covers) {
        // At the top, as tall as its rows, with the covers beneath it
        hb_geom geom = hb_geom_centered(600, (view->row_count + 1) * HB_ROW_HEIGHT);
        geom.y = 30;
        geom.total_y = geom.y + geom.height;
        return geom;
      }
      break;
    case UI_SCREEN_RECOMMENDED:
      // A row of 24 px each, plus margins
      return hb_geom_centered(620, (view->row_count + 1) * HB_ROW_HEIGHT);
    case UI_SCREEN_DEADZONE: {
      // Top left, out of the way of the area it shows (total_y stays where
      // the centred box would end)
      hb_geom geom = hb_geom_centered(250, 120);
      geom.x = 50;
      geom.y = 50;
      geom.el = 25;
      return geom;
    }
    case UI_SCREEN_KEYBOARD_LAYOUT:
      return hb_geom_centered(320, 120);
    default:
      break;
  }
  return hb_geom_centered(600, HB_MENU_MAX_HEIGHT);
}

static void draw_statusbar(hb_geom geom) {
  ui_status status;
  ui_status_read(&status);

  char dt_text[256];
  sprintf(dt_text, "%02d:%02d", status.hour, status.minute);
  int dt_width = vita2d_font_text_width(hb_font, 18, dt_text);
  int battery_width = 30,
      battery_height = 16,
      battery_padding = 2,
      battery_plus_height = 4,
      battery_y_offset = 4,
      battery_charge_width = (float) status.battery_percent / 100 * battery_width;
  unsigned int battery_color = status.battery_charging ? 0xff99ffff : (status.battery_percent < 20 ? 0xff0000ff : 0xff00ff00);

  vita2d_font_draw_text(hb_font, geom.x + geom.width - dt_width - battery_width - 5, geom.y - 5, 0xffffffff, 18, dt_text);

  vita2d_draw_rectangle(
      geom.x + geom.width - battery_width,
      geom.y - battery_height - battery_y_offset,
      battery_width,
      battery_height,
      0xffffffff);

  vita2d_draw_rectangle(
      geom.x + geom.width - battery_width - battery_padding,
      geom.y - battery_y_offset - ((float) battery_height / 2 + (float) battery_plus_height / 2),
      battery_padding,
      battery_plus_height,
      0xffffffff
      );

  vita2d_draw_rectangle(
      geom.x + geom.width - battery_width + battery_padding,
      geom.y - battery_height + battery_padding - battery_y_offset,
      battery_width - battery_padding * 2,
      battery_height - battery_padding * 2,
      0xff000000);

  vita2d_draw_rectangle(
      geom.x + geom.width - battery_charge_width + battery_padding,
      geom.y - battery_height + battery_padding - battery_y_offset,
      battery_charge_width - battery_padding * 2,
      battery_height - battery_padding * 2,
      battery_color);
}

static void draw_rows(const menu_entry menu[], int total_elements, hb_geom geom, int cursor, int offset) {
  vita2d_draw_rectangle(geom.x, geom.y, geom.width, geom.height, 0x18fffff);

  long border_color = 0xff006000;
  hb_draw_border(geom, border_color);
  draw_statusbar(geom);

  for (int i = 0, cursor_idx = 0; i < total_elements; i++) {
    long text_color = 0xffffffff;
    if (cursor == cursor_idx) {
      text_color = 0xff00ff00; // Verde para seleccionado
    }
    if (!menu[i].disabled) {
      if (menu[i].status == MENU_STATUS_WARNING && cursor != cursor_idx) {
        text_color = 0xff00d0ff;
      }
      cursor_idx++;
    } else {
      text_color = 0xffaaaaaa;
    }

    // PCs get a dot in their status colour: green online, red offline, yellow
    // at a new address
    bool has_dot = menu[i].status == MENU_STATUS_ONLINE || menu[i].status == MENU_STATUS_OFFLINE ||
                   menu[i].status == MENU_STATUS_CHANGED;
    unsigned int dot_color = menu[i].status == MENU_STATUS_ONLINE ? 0xFF00FF00
                             : menu[i].status == MENU_STATUS_OFFLINE ? 0xFF0000FF
                             : 0xFF00FFFF;

    int el_x = geom.x + 10,
        el_y = geom.y + i * geom.el - offset + 10;

    if (el_y < geom.y || el_y > geom.total_y - geom.el)
      continue;

    int text_width, text_height;
    vita2d_font_text_dimensions(hb_font, 18, menu[i].name, &text_width, &text_height);

    if (menu[i].separator) {
      int border = strlen(menu[i].name) ? 7 : 0;
      int height = strlen(menu[i].name) ? text_height : geom.el / 2;
      vita2d_draw_line(
          el_x + text_width + border,
          el_y + height,
          el_x + geom.width - 10 * 2,
          el_y + height,
          0xffaaaaaa
          );
    }

    // Dibuja el puntito de estado solo para hosts emparejados (solo menú principal)
    if (has_dot && menu[i].name && strlen(menu[i].name) > 0) {
      int dot_radius = 8;
      int dot_x = el_x + dot_radius; // Asegura que el puntito no quede pegado al borde
      int dot_y = el_y + text_height / 2 + 2;
      unsigned int visible_dot_color = (dot_color & 0x00FFFFFF) | 0xFF000000;
      vita2d_draw_fill_circle(dot_x, dot_y, dot_radius + 2, 0xFF000000);
      vita2d_draw_fill_circle(dot_x, dot_y, dot_radius, visible_dot_color);
      hb_draw_rich_text(dot_x + dot_radius + 6, // Más espacio a la derecha del puntito
          el_y + text_height, text_color, 18, menu[i].name);
    } else if (menu[i].name && strlen(menu[i].name) > 0) {
      hb_draw_rich_text(el_x + 2, el_y + text_height, text_color, 18, menu[i].name);
    }

    int right_x_offset = 20;
    // Arrows for a value left and right change; a star for the current choice
    const char *marker = menu[i].adjustable ? ICON_LEFT_RIGHT_ARROWS : menu[i].checked ? "*" : NULL;
    if (marker) {
      int text_width = hb_rich_text_width(18, marker);
      hb_draw_rich_text(el_x + geom.width - text_width - right_x_offset, el_y + text_height, text_color, 18, marker);

      right_x_offset += text_width + 10;
    }
    if (menu[i].suffix && *menu[i].suffix) {
      int text_width = hb_rich_text_width(18, menu[i].suffix);
      hb_draw_rich_text(el_x + geom.width - text_width - right_x_offset, el_y + text_height, text_color, 18, menu[i].suffix);

      right_x_offset += text_width + 10;
    }
    if (*menu[i].subname) {
      int text_width = hb_rich_text_width(18, menu[i].subname);
      hb_draw_rich_text(el_x + geom.width - text_width - right_x_offset, el_y + text_height, text_color, 18, menu[i].subname);
    }
  }
}

// A hint outside the box, bottom right: the button's symbol, then its text
static void draw_hint_under(hb_geom geom, ui_hint hint) {
  if (hint.text == NULL) {
    return;
  }
  const int gap = 6;
  bool has_glyph = hint.glyph != BUTTON_GLYPH_NONE;
  int width = (has_glyph ? HB_GLYPH_SIZE + gap : 0) + hb_rich_text_width(HB_FONT_SIZE, hint.text);
  int x = geom.x + geom.width - width;
  int y = geom.total_y + 26;
  if (has_glyph) {
    x += hb_draw_button_glyph(hint.glyph, x, y, HB_GLYPH_SIZE) + gap;
  }
  hb_draw_rich_text(x, y, 0xffffffff, HB_FONT_SIZE, hint.text);
}

// The tab bar, above the box on the left, level with the clock on the right
static void draw_tabs(hb_geom geom, const tabs_view *tabs) {
  const int gap = 6;
  const int tab_spacing = 16;
  int tab_x = geom.x;
  int tab_y = geom.y - 10;
  tab_x += hb_draw_button_glyph(BUTTON_GLYPH_L, tab_x, tab_y, HB_GLYPH_SIZE) + gap * 2;
  for (int i = 0; i < tabs->count; i++) {
    unsigned int colour = tabs->tabs[i].current ? 0xff00ff00 : 0xffaaaaaa;
    hb_draw_rich_text(tab_x, tab_y, colour, 18, tabs->tabs[i].name);
    tab_x += hb_rich_text_width(18, tabs->tabs[i].name) + tab_spacing;
  }
  hb_draw_button_glyph(BUTTON_GLYPH_R, tab_x - tab_spacing + gap * 2, tab_y, HB_GLYPH_SIZE);
}

// The rear touchpad's active area, 1:1 on the screen, with a circle under
// each finger
static void draw_deadzone(const deadzone_view *zone) {
  int vertical = (WIDTH - zone->left - zone->right) / 2 + zone->left,
      horizontal = (HEIGHT - zone->top - zone->bottom) / 2 + zone->top;

  vita2d_draw_rectangle(
      zone->left,
      zone->top,
      WIDTH - zone->right - zone->left,
      HEIGHT - zone->bottom - zone->top,
      0x3000ff00
      );

  vita2d_draw_line(vertical, zone->top, vertical, HEIGHT - zone->bottom, 0xffffffff);
  vita2d_draw_line(zone->left, horizontal, WIDTH - zone->right, horizontal, 0xffffffff);

  for (int i = 0; i < zone->touch_count; i++) {
    vita2d_draw_fill_circle(zone->touches[i].x, zone->touches[i].y, 30, 0xffffffff);
  }
}

// The four corner buttons, where they are on the front screen
static void draw_special_keys(const special_keys_view *keys) {
  int special_offset = keys->offset,
      special_size = keys->size;

  unsigned int color = 0xff006000;

  // Each corner, in the order the old switch fell through them
  for (int i = 0; i < 4; i++) {
    switch (i) {
      case 2:
        vita2d_draw_rectangle(
            special_offset,
            HEIGHT - special_size - special_offset,
            special_size,
            special_size,
            color);
        // fallthrough
      case 3:
        vita2d_draw_rectangle(
            WIDTH - special_size - special_offset,
            HEIGHT - special_size - special_offset,
            special_size,
            special_size,
            color);
        // fallthrough
      case 0:
        vita2d_draw_rectangle(
            special_offset,
            special_offset,
            special_size,
            special_size,
            color);
        // fallthrough
      case 1:
        vita2d_draw_rectangle(
            WIDTH - special_size - special_offset,
            special_offset,
            special_size,
            special_size,
            color);
    }
  }
}

// Drawn before the box, as they were drawn by the screens' callbacks
static void draw_decorations(const menu_view *view, hb_geom geom) {
  switch (view->screen->id) {
    case UI_SCREEN_SETTINGS:
      if (view->tabs != NULL) {
        draw_tabs(geom, view->tabs);
      }
      draw_hint_under(geom, view->hint);
      break;
    case UI_SCREEN_HOST:
      draw_hint_under(geom, view->hint);
      break;
    case UI_SCREEN_CONNECTED:
      if (view->boxart != NULL) {
        hb_draw_boxart(view->boxart, view->hint);
      }
      break;
    case UI_SCREEN_DEADZONE:
      if (view->deadzone != NULL) {
        draw_deadzone(view->deadzone);
      }
      break;
    case UI_SCREEN_SPECIAL_KEYS:
      if (view->special_keys != NULL) {
        draw_special_keys(view->special_keys);
      }
      break;
    default:
      break;
  }
}

void hb_draw_menu(const menu_view *view) {
  hb_geom geom = menu_geom(view);
  if (view->decorated) {
    draw_decorations(view, geom);
  }
  draw_rows(view->rows, view->row_count, geom, view->cursor, view->scroll);
}

void hb_menu_follow(menu_view *view) {
  // Scrolls 8 px a frame while the selection is outside the box
  hb_geom geom = menu_geom(view);
  int cursor_y = geom.y + ((view->cursor == 0 ? 0 : view->drawn_row) * geom.el) - view->scroll;
  view->scroll -= cursor_y < geom.y ? 8 : 0;
  view->scroll -= cursor_y > geom.total_y - geom.el * 2 ? -8 : 0;
}
