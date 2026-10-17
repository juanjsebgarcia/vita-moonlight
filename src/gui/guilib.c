#include "guilib.h"

#include "../config.h"
#include "../platform.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <psp2/net/net.h>
#include <psp2/sysmodule.h>

#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/rtc.h>
#include <psp2/power.h>

#define BUTTON_DELAY 150 * 1000

static gui_draw_callback gui_global_draw_callback;
static gui_loop_callback gui_global_loop_callback;

vita2d_font *font;
// The same font again, only for the small labels inside button glyphs.
// vita2d caches each character once, at the size it's first drawn, so small
// labels drawn with the main font made later full-size letters (such as the
// L in "Licence", after an L button glyph) come out small.
static vita2d_font *glyph_font;

menu_geom make_geom_centered(int w, int h) {
  menu_geom geom = {0};
  geom.x = WIDTH  / 2 - w / 2;
  geom.y = HEIGHT / 2 - h / 2;
  geom.width = w;
  geom.height = h;
  geom.total_y = geom.y + geom.height;
  geom.el = 24;
  return geom;
}

void draw_border(menu_geom geom, unsigned int border_color) {
  vita2d_draw_line(geom.x, geom.y, geom.x+geom.width, geom.y, border_color);
  vita2d_draw_line(geom.x, geom.y, geom.x, geom.y+geom.height, border_color);
  vita2d_draw_line(geom.x+geom.width, geom.y, geom.x+geom.width, geom.y+geom.height, border_color);
  vita2d_draw_line(geom.x, geom.y+geom.height, geom.x+geom.width, geom.y+geom.height, border_color);
}

void draw_text_hcentered(int x, int y, unsigned int color, char *text) {
  int width = vita2d_font_text_width(font, 18, text);
  vita2d_font_draw_text(font, x - width / 2, y, color, 18, text);
}

static int battery_percent;
static bool battery_charging;
static SceRtcTick battery_tick;

void draw_statusbar(menu_geom geom) {
  SceRtcTick current_tick;
  sceRtcGetCurrentTick(&current_tick);
  if (current_tick.tick - battery_tick.tick > 10 * 1000 * 1000) {
    battery_percent = scePowerGetBatteryLifePercent();
    battery_charging = scePowerIsBatteryCharging();

    battery_tick = current_tick;
  }

  SceDateTime time;
  sceRtcGetCurrentClockLocalTime(&time);

  char dt_text[256];
  sprintf(dt_text, "%02d:%02d", time.hour, time.minute);
  int dt_width = vita2d_font_text_width(font, 18, dt_text);
  int battery_width = 30,
      battery_height = 16,
      battery_padding = 2,
      battery_plus_height = 4,
      battery_y_offset = 4,
      battery_charge_width = (float) battery_percent / 100 * battery_width;
  unsigned int battery_color = battery_charging ? 0xff99ffff : (battery_percent < 20 ? 0xff0000ff : 0xff00ff00);

  vita2d_font_draw_text(font, geom.x + geom.width - dt_width - battery_width - 5, geom.y - 5, 0xffffffff, 18, dt_text);

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

SceTouchData touch_data;
SceCtrlData ctrl_new_pad;

static SceRtcTick button_current_tick, button_until_tick;
bool was_button_pressed(short id) {
  sceRtcGetCurrentTick(&button_current_tick);

  if (ctrl_new_pad.buttons & id) {
    if (sceRtcCompareTick(&button_current_tick, &button_until_tick) > 0) {
      sceRtcTickAddMicroseconds(&button_until_tick, &button_current_tick, BUTTON_DELAY);
      return true;
    }
  }

  return false;
}

bool is_button_down(short id) {
  return ctrl_new_pad.buttons & id;
}

#define lerp(value, from_max, to_max) ((((value*10) * (to_max*10))/(from_max*10))/10)
bool is_rectangle_touched(const SceTouchData *touch, int lx, int ly, int rx, int ry) {
  for (int i = 0; i < touch->reportNum; i++) {
    int x = lerp(touch->report[i].x, 1919, WIDTH);
    int y = lerp(touch->report[i].y, 1087, HEIGHT);
    if (x < lx || x > rx || y < ly || y > ry) continue;
    return true;
  }

  return false;
}

void draw_menu(menu_entry menu[], int total_elements, menu_geom geom, int cursor, int offset) {
  vita2d_draw_rectangle(geom.x, geom.y, geom.width, geom.height, 0x18fffff);

  long border_color = 0xff006000;
  draw_border(geom, border_color);
  draw_statusbar(geom);

  for (int i = 0, cursor_idx = 0; i < total_elements; i++) {
    long text_color = 0xffffffff;
    if (cursor == cursor_idx) {
      text_color = 0xff00ff00; // Verde para seleccionado
    }
    if (!menu[i].disabled) {
      cursor_idx++;
    } else {
      text_color = 0xffaaaaaa;
    }

    // Color del puntito de estado (solo para hosts, si menu[i].color != 0)
    unsigned int dot_color = menu[i].color ? menu[i].color : 0xffaaaaaa;

    int el_x = geom.x + 10,
        el_y = geom.y + i * geom.el - offset + 10;

    if (el_y < geom.y || el_y > geom.total_y - geom.el)
      continue;

    int text_width, text_height;
    vita2d_font_text_dimensions(font, 18, menu[i].name, &text_width, &text_height);

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
    if (menu[i].is_host_entry && menu[i].name && strlen(menu[i].name) > 0) {
      int dot_radius = 8;
      int dot_x = el_x + dot_radius; // Asegura que el puntito no quede pegado al borde
      int dot_y = el_y + text_height / 2 + 2;
      unsigned int visible_dot_color = (dot_color & 0x00FFFFFF) | 0xFF000000;
      vita2d_draw_fill_circle(dot_x, dot_y, dot_radius + 2, 0xFF000000);
      vita2d_draw_fill_circle(dot_x, dot_y, dot_radius, visible_dot_color);
      vita2d_font_draw_text(
          font,
          dot_x + dot_radius + 6, // Más espacio a la derecha del puntito
          el_y + text_height,
          text_color,
          18,
          menu[i].name
          );
    } else if (menu[i].name && strlen(menu[i].name) > 0) {
      vita2d_font_draw_text(
          font,
          el_x + 2,
          el_y + text_height,
          text_color,
          18,
          menu[i].name
          );
    }

    int right_x_offset = 20;
    if (menu[i].suffix) {
      int text_width = vita2d_font_text_width(font, 18, menu[i].suffix);
      vita2d_font_draw_text(
          font,
          el_x + geom.width - text_width - right_x_offset,
          el_y + text_height,
          text_color,
          18,
          menu[i].suffix
          );

      right_x_offset += text_width + 10;
    }
    if (*menu[i].subname) {
      int text_width = vita2d_font_text_width(font, 18, menu[i].subname);
      vita2d_font_draw_text(
          font,
          el_x + geom.width - text_width - right_x_offset,
          el_y + text_height,
          text_color,
          18,
          menu[i].subname
          );
    }
  }
}

#define UI_FONT_SIZE 18
#define UI_LINE_HEIGHT 22
#define ALERT_TOP_PADDING 30
#define ALERT_SIDE_PADDING 14
#define ALERT_MAX_LINES 24
#define BUTTON_GLYPH_SIZE 14
#define CIRCLE_SEGMENTS 16

static void draw_thick_line(float x0, float y0, float x1, float y1, unsigned int color) {
  // vita2d lines are a single pixel, which is too faint for an icon
  vita2d_draw_line(x0, y0, x1, y1, color);
  vita2d_draw_line(x0 + 1, y0, x1 + 1, y1, color);
  vita2d_draw_line(x0, y0 + 1, x1, y1 + 1, color);
}

static void fill_triangle(float x0, float y0, float x1, float y1, float x2, float y2, unsigned int color) {
  // Vertices must live in vita2d's per-frame GPU pool
  vita2d_color_vertex *v = vita2d_pool_memalign(3 * sizeof(vita2d_color_vertex), sizeof(vita2d_color_vertex));
  if (v == NULL) {
    return;
  }
  float xs[3] = {x0, x1, x2};
  float ys[3] = {y0, y1, y2};
  for (int i = 0; i < 3; i++) {
    v[i].x = xs[i];
    v[i].y = ys[i];
    v[i].z = 0.5f;
    v[i].color = color;
  }
  vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, v, 3);
}

static void draw_outline_rect(float x, float y, float w, float h, unsigned int color) {
  draw_thick_line(x, y, x + w, y, color);
  draw_thick_line(x + w, y, x + w, y + h, color);
  draw_thick_line(x + w, y + h, x, y + h, color);
  draw_thick_line(x, y + h, x, y, color);
}

static void draw_outline_circle(float cx, float cy, float radius, unsigned int color) {
  for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
    float a0 = (float)i / CIRCLE_SEGMENTS * 2.0f * (float)M_PI;
    float a1 = (float)(i + 1) / CIRCLE_SEGMENTS * 2.0f * (float)M_PI;
    draw_thick_line(cx + radius * cosf(a0), cy + radius * sinf(a0),
                    cx + radius * cosf(a1), cy + radius * sinf(a1), color);
  }
}

// Centres a short label in a box, using a font size that fits its height
static void draw_label(const char *label, float cx, float top, float height, unsigned int color) {
  int font_size = (int)(height * 0.62f);
  if (font_size < 8) {
    font_size = 8;
  }
  int width = vita2d_font_text_width(glyph_font, font_size, label);
  // y is the baseline, so drop it by roughly half the cap height below the middle
  float baseline = top + height / 2.0f + font_size * 0.36f;
  vita2d_font_draw_text(glyph_font, (int)(cx - width / 2.0f), (int)baseline, color, font_size, label);
}

// Width of a labelled pill for this label at this height
static int pill_width(const char *label, int size) {
  int font_size = (int)(size * 0.62f);
  if (font_size < 8) {
    font_size = 8;
  }
  int width = vita2d_font_text_width(glyph_font, font_size, label) + size / 2 + 4;
  return width > size ? width : size;
}

static int draw_pill(const char *label, int x, float top, int size, unsigned int color) {
  int width = pill_width(label, size);
  draw_outline_rect(x, top, width, size, color);
  draw_label(label, x + width / 2.0f, top, size, color);
  return width;
}

static void draw_dpad(int x, float top, int size, int highlight) {
  unsigned int dim = RGBA8(0xDD, 0xDD, 0xDD, 0x60);
  unsigned int bright = RGBA8(0xFF, 0xFF, 0xFF, 0xFF);
  float arm = size / 3.0f;
  float cx = x + size / 2.0f;
  float cy = top + size / 2.0f;
  // The plus shape, then each arm as a triangle pointing outwards
  vita2d_draw_rectangle(x + arm, top + arm, arm, arm, highlight == BUTTON_GLYPH_DPAD ? bright : dim);
  struct { int glyph; float tx, ty, ax, ay, bx, by; } arms[4] = {
    {BUTTON_GLYPH_DPAD_UP, cx, top, x + arm, top + arm, x + 2 * arm, top + arm},
    {BUTTON_GLYPH_DPAD_DOWN, cx, top + size, x + arm, top + 2 * arm, x + 2 * arm, top + 2 * arm},
    {BUTTON_GLYPH_DPAD_LEFT, x, cy, x + arm, top + arm, x + arm, top + 2 * arm},
    {BUTTON_GLYPH_DPAD_RIGHT, x + size, cy, x + 2 * arm, top + arm, x + 2 * arm, top + 2 * arm},
  };
  for (int i = 0; i < 4; i++) {
    bool lit = highlight == BUTTON_GLYPH_DPAD || highlight == arms[i].glyph;
    fill_triangle(arms[i].tx, arms[i].ty, arms[i].ax, arms[i].ay, arms[i].bx, arms[i].by, lit ? bright : dim);
  }
}

int draw_button_glyph(int glyph, int x, int y, int size) {
  // y is the text baseline; the symbol sits on it, a little below the cap height
  float top = y - size;
  float bottom = y - 1;
  float left = x;
  float right = x + size;
  float middle = x + size / 2.0f;
  float radius = size / 2.0f;
  float cy = top + radius;
  unsigned int grey = RGBA8(0xE8, 0xE8, 0xE8, 0xFF);

  switch (glyph) {
    case BUTTON_GLYPH_DPAD:
    case BUTTON_GLYPH_DPAD_UP:
    case BUTTON_GLYPH_DPAD_DOWN:
    case BUTTON_GLYPH_DPAD_LEFT:
    case BUTTON_GLYPH_DPAD_RIGHT:
      draw_dpad(x, top, size, glyph);
      return size;
    case BUTTON_GLYPH_L:
      return draw_pill("L", x, top, size, grey);
    case BUTTON_GLYPH_R:
      return draw_pill("R", x, top, size, grey);
    case BUTTON_GLYPH_L2:
      return draw_pill("L2", x, top, size, grey);
    case BUTTON_GLYPH_R2:
      return draw_pill("R2", x, top, size, grey);
    case BUTTON_GLYPH_START:
      return draw_pill("START", x, top, size, grey);
    case BUTTON_GLYPH_SELECT:
      return draw_pill("SELECT", x, top, size, grey);
    case BUTTON_GLYPH_PS:
      draw_outline_circle(middle, cy, radius, grey);
      draw_label("PS", middle, top, size, grey);
      return size;
    case BUTTON_GLYPH_LEFT_STICK:
    case BUTTON_GLYPH_RIGHT_STICK:
      // An outer ring with the stick's cap in the middle
      draw_outline_circle(middle, cy, radius, grey);
      vita2d_draw_fill_circle(middle, cy, radius * 0.45f, grey);
      return size;
    case BUTTON_GLYPH_L3:
      draw_outline_circle(middle, cy, radius, grey);
      draw_label("L3", middle, top, size, grey);
      return size;
    case BUTTON_GLYPH_R3:
      draw_outline_circle(middle, cy, radius, grey);
      draw_label("R3", middle, top, size, grey);
      return size;
    case BUTTON_GLYPH_FRONT_TOUCH: {
      // A landscape screen with a fingertip on it
      float w = size * 1.5f;
      draw_outline_rect(x, top + size * 0.15f, w, size * 0.7f, grey);
      vita2d_draw_fill_circle(x + w * 0.6f, top + size * 0.5f, size * 0.14f, grey);
      return (int)w + 1;
    }
    case BUTTON_GLYPH_REAR_TOUCH: {
      // The same pad, split into the four corner zones the rear touch uses
      float w = size * 1.5f;
      float pad_top = top + size * 0.15f;
      float pad_h = size * 0.7f;
      draw_outline_rect(x, pad_top, w, pad_h, grey);
      vita2d_draw_line(x + w / 2, pad_top, x + w / 2, pad_top + pad_h, grey);
      vita2d_draw_line(x, pad_top + pad_h / 2, x + w, pad_top + pad_h / 2, grey);
      return (int)w + 1;
    }
  }

  switch (glyph) {
    case BUTTON_GLYPH_TRIANGLE: {
      unsigned int green = RGBA8(0x40, 0xE0, 0xB0, 0xFF);
      draw_thick_line(middle, top, right, bottom, green);
      draw_thick_line(right, bottom, left, bottom, green);
      draw_thick_line(left, bottom, middle, top, green);
      break;
    }
    case BUTTON_GLYPH_SQUARE: {
      unsigned int pink = RGBA8(0xF0, 0x90, 0xD0, 0xFF);
      draw_thick_line(left, top, right, top, pink);
      draw_thick_line(right, top, right, bottom, pink);
      draw_thick_line(right, bottom, left, bottom, pink);
      draw_thick_line(left, bottom, left, top, pink);
      break;
    }
    case BUTTON_GLYPH_CROSS: {
      unsigned int blue = RGBA8(0x80, 0xB8, 0xF0, 0xFF);
      draw_thick_line(left, top, right, bottom, blue);
      draw_thick_line(right, top, left, bottom, blue);
      break;
    }
    case BUTTON_GLYPH_CIRCLE:
      draw_outline_circle(middle, cy, radius, RGBA8(0xFF, 0x70, 0x70, 0xFF));
      break;
  }
  return size;
}

int wrap_text(const char *text, int max_width, char lines[][WRAP_LINE_MAX], int max_lines) {
  int count = 0;
  char line[WRAP_LINE_MAX] = "";
  const char *p = text;

  while (*p && count < max_lines) {
    if (*p == '\n') {
      snprintf(lines[count++], WRAP_LINE_MAX, "%s", line);
      line[0] = '\0';
      p++;
      continue;
    }
    if (*p == ' ') {
      p++;
      continue;
    }

    // The next word runs to the next space or newline
    const char *end = p;
    while (*end && *end != ' ' && *end != '\n') {
      end++;
    }
    int word_len = end - p;
    if (word_len >= WRAP_LINE_MAX) {
      word_len = WRAP_LINE_MAX - 1;
    }

    char candidate[WRAP_LINE_MAX];
    snprintf(candidate, sizeof(candidate), "%s%s%.*s", line, line[0] ? " " : "", word_len, p);
    if (vita2d_font_text_width(font, UI_FONT_SIZE, candidate) <= max_width) {
      snprintf(line, sizeof(line), "%s", candidate);
      p = end;
      continue;
    }

    if (line[0]) {
      // The word doesn't fit after what is already on the line, so start a new one
      snprintf(lines[count++], WRAP_LINE_MAX, "%s", line);
      line[0] = '\0';
      continue;
    }

    // A single word wider than the line: split it where it overflows
    int fit = 1;
    while (fit < word_len) {
      snprintf(candidate, sizeof(candidate), "%.*s", fit + 1, p);
      if (vita2d_font_text_width(font, UI_FONT_SIZE, candidate) > max_width) {
        break;
      }
      fit++;
    }
    snprintf(lines[count++], WRAP_LINE_MAX, "%.*s", fit, p);
    p += fit;
  }

  if (line[0] && count < max_lines) {
    snprintf(lines[count++], WRAP_LINE_MAX, "%s", line);
  }
  return count;
}

// Draws "<button> caption" pairs right-aligned along the bottom of the box
static void draw_button_captions(menu_geom geom, char *buttons_captions[], int buttons_count) {
  // Index 0 is confirm and 1 is cancel, which swap when X and O are swapped
  int confirm = config.jp_layout ? BUTTON_GLYPH_CIRCLE : BUTTON_GLYPH_CROSS;
  int cancel = config.jp_layout ? BUTTON_GLYPH_CROSS : BUTTON_GLYPH_CIRCLE;
  int glyphs[4] = {confirm, cancel, BUTTON_GLYPH_TRIANGLE, BUTTON_GLYPH_SQUARE};
  char *default_captions[4] = {"Ok", "Cancel", "Options", "Delete"};
  const int gap = 6;
  const int spacing = 16;

  int total_width = 0;
  for (int i = 0; i < buttons_count && i < 4; i++) {
    const char *caption = buttons_captions && buttons_captions[i] ? buttons_captions[i] : default_captions[i];
    total_width += BUTTON_GLYPH_SIZE + gap + vita2d_font_text_width(font, UI_FONT_SIZE, caption) + spacing;
  }

  int x = geom.x + geom.width - total_width;
  int y = geom.total_y - 10;
  for (int i = 0; i < buttons_count && i < 4; i++) {
    const char *caption = buttons_captions && buttons_captions[i] ? buttons_captions[i] : default_captions[i];
    x += draw_button_glyph(glyphs[i], x, y, BUTTON_GLYPH_SIZE) + gap;
    vita2d_font_draw_text(font, x, y, 0xffffffff, UI_FONT_SIZE, caption);
    x += vita2d_font_text_width(font, UI_FONT_SIZE, caption) + spacing;
  }
}

void draw_alert(char *message, menu_geom geom, char *buttons_captions[], int buttons_count) {
  vita2d_draw_rectangle(geom.x, geom.y, geom.width, geom.height, 0x18fffff);

  long border_color = 0xff006000;
  draw_border(geom, border_color);

  char lines[ALERT_MAX_LINES][WRAP_LINE_MAX];
  int line_count = wrap_text(message, geom.width - ALERT_SIDE_PADDING * 2, lines, ALERT_MAX_LINES);

  // A single line sits in the middle of the box, longer text starts at the top
  int y = line_count == 1 ? geom.height / 2 : ALERT_TOP_PADDING;
  for (int i = 0; i < line_count; i++) {
    draw_text_hcentered(geom.x + geom.width / 2, y + geom.y, 0xffffffff, lines[i]);
    y += UI_LINE_HEIGHT;
  }

  draw_button_captions(geom, buttons_captions, buttons_count);
}

void ui_start() {
  vita2d_start_drawing();
  vita2d_clear_screen();
}

void ui_end() {
  vita2d_end_drawing();
  vita2d_wait_rendering_done();
  vita2d_swap_buffers();
}

int read_buttons() {
    SceCtrlData pad = {0};
    static int old;
    static int hold_times;
    int curr, btn;

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceCtrlPeekBufferPositive(0, &pad, 1);

    if (pad.ly < 0x10) {
        pad.buttons |= SCE_CTRL_UP;
    } else if (pad.ly > 0xef) {
        pad.buttons |= SCE_CTRL_DOWN;
    }
    curr = pad.buttons;
    btn = pad.buttons & ~old;
    if (curr && old == curr) {
        hold_times += 1;
        if (hold_times >= 10) {
            btn = curr;
            hold_times = 8;
            btn |= SCE_CTRL_HOLD;
        }
    } else {
        hold_times = 0;
        old = curr;
    }
    return btn;
}

int display_menu(menu_entry menu[], int total_elements, menu_geom *geom_ptr,
                 gui_loop_callback cb, gui_back_callback back_cb,
                 gui_draw_callback draw_callback,
                 void *context) {
  ui_end();

  int offset = 0;
  int cursor = 0;

  menu_geom geom;

  if (!geom_ptr) {
    geom = make_geom_centered(600, 400);
    geom.el = 24;
  } else {
    geom = *geom_ptr;
  }

  int tick_number = 0;
  int exit_code = 0;

  while (true) {
    int active_elements = 0;
    for (int i = 0; i < total_elements; i++) {
        active_elements += menu[i].disabled ? 0 : 1;
    }

    ui_start();
    tick_number++;

    if (tick_number > 3) {
      if (draw_callback) {
        draw_callback();
      }
      if (gui_global_draw_callback) {
        gui_global_draw_callback();
      }
    }

    draw_menu(menu, total_elements, geom, cursor, offset);

    int real_cursor = 0;

    for (int c = 0; real_cursor < total_elements; real_cursor++) {
      if (menu[real_cursor].disabled) {
        continue;
      }
      if (cursor == c) {
        break;
      }
      c++;
    }

    // select item
    input_data input = {0};
    input.buttons = read_buttons();
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &input.touch, 1);
    if (input.buttons & SCE_CTRL_DOWN) {
      cursor += 1;
    }
    if (input.buttons & SCE_CTRL_UP) {
      cursor -= 1;
    }
    cursor = cursor < 0 ? 0 : cursor;
    cursor = cursor > active_elements - 1 ? active_elements - 1 : cursor;

    int cursor_y = geom.y + ((cursor == 0 ? 0 : real_cursor) * geom.el) - offset;
    offset -= cursor_y < geom.y ? 8 : 0;
    offset -= cursor_y > geom.total_y - geom.el * 2 ? -8 : 0;

    // The callback can swap confirm and cancel (Swap X & O in Settings), so
    // read this frame's press with the buttons it was made with. Otherwise
    // the X that opened Settings counts as cancel and closes this menu too.
    SceCtrlButtons cancel_button = config.btn_cancel;

    if (cb) {
      exit_code = cb(menu[real_cursor].id, context, &input);
      if (exit_code) {
        goto error;
      }
    }

    if (gui_global_loop_callback) {
      gui_global_loop_callback(menu[real_cursor].id, context, &input);
    }

    if (input.buttons & cancel_button && (input.buttons & SCE_CTRL_HOLD) == 0) {
      if (!back_cb || back_cb(context) == 0) {
        exit_code = 1;
        goto error;
      }
    }

    ui_end();
  }

  return 0;

error:
  ui_end();
  return exit_code;
}


void display_alert(char *message, char *button_captions[], int buttons_count,
                   gui_loop_callback cb, void *context) {

  menu_geom alert_geom = make_geom_centered(400, 200);

  while (true) {
    ui_start();

    draw_alert(message, alert_geom, button_captions, buttons_count);

    input_data input = {0};
    input.buttons = read_buttons();
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &input.touch, 1);

    int result = -1;

    if (input.buttons & SCE_CTRL_HOLD) {
      ui_end();
      continue;
    }

    if (input.buttons & config.btn_confirm) {
      result = 0;
    } else if (input.buttons & config.btn_cancel) {
      result = 1;
    } else if (input.buttons & SCE_CTRL_TRIANGLE) {
      result = 2;
    } else if (input.buttons & SCE_CTRL_SQUARE) {
      result = 3;
    }

    if (cb && result != -1 && result < buttons_count) {
      switch(cb(result, context, &input)) {
        case 1:
          return;
      }
    } else if (result == 0) {
      return;
    }

    ui_end();
  }
}

#define HELP_MAX_LINES 48

void display_help(const char *title, const char *message) {
  // Larger than the standard alert, so a few sentences fit without crowding
  menu_geom geom = make_geom_centered(600, 320);
  char *captions[1] = {"Close"};

  // Wrap once up front; the text doesn't change while the dialog is open
  static char lines[HELP_MAX_LINES][WRAP_LINE_MAX];
  int line_count = wrap_text(message, geom.width - ALERT_SIDE_PADDING * 2 - 12, lines, HELP_MAX_LINES);

  int title_y = geom.y + ALERT_TOP_PADDING;
  int body_top = title_y + UI_LINE_HEIGHT + 10;
  // Leave room for the button captions along the bottom
  int body_bottom = geom.total_y - 34;
  int visible = (body_bottom - body_top) / UI_LINE_HEIGHT + 1;
  int max_offset = line_count > visible ? line_count - visible : 0;
  int offset = 0;

  while (true) {
    ui_start();

    vita2d_draw_rectangle(geom.x, geom.y, geom.width, geom.height, 0x18fffff);
    draw_border(geom, 0xff006000);
    draw_text_hcentered(geom.x + geom.width / 2, title_y, 0xffffffff, (char *)title);

    for (int i = 0; i < visible && offset + i < line_count; i++) {
      vita2d_font_draw_text(font, geom.x + ALERT_SIDE_PADDING, body_top + i * UI_LINE_HEIGHT,
                            0xffffffff, UI_FONT_SIZE, lines[offset + i]);
    }

    // A scroll bar on the right when the text doesn't fit
    if (max_offset > 0) {
      int track_x = geom.x + geom.width - 10;
      int track_top = body_top - UI_LINE_HEIGHT + 6;
      int track_height = body_bottom - track_top;
      int thumb_height = track_height * visible / line_count;
      int thumb_top = track_top + (track_height - thumb_height) * offset / max_offset;
      vita2d_draw_rectangle(track_x, track_top, 3, track_height, RGBA8(0xFF, 0xFF, 0xFF, 0x40));
      vita2d_draw_rectangle(track_x, thumb_top, 3, thumb_height, RGBA8(0xFF, 0xFF, 0xFF, 0xC0));
    }

    draw_button_captions(geom, captions, 1);

    // Up and down (or the left stick) scroll, repeating while held
    int buttons = read_buttons();
    if (buttons & SCE_CTRL_UP && offset > 0) {
      offset--;
    } else if (buttons & SCE_CTRL_DOWN && offset < max_offset) {
      offset++;
    }

    // Ignore the triangle press that opened this until it is released
    if ((buttons & SCE_CTRL_HOLD) == 0 &&
        (buttons & (config.btn_confirm | config.btn_cancel | SCE_CTRL_TRIANGLE))) {
      return;
    }

    ui_end();
  }
}

void display_error(char *format, ...) {
  char buf[0x1000];

  va_list opt;
  va_start(opt, format);
  vsnprintf(buf, sizeof(buf), format, opt);
  display_alert(buf, NULL, 1, NULL, NULL);
  va_end(opt);
}

void flash_message(char *format, ...) {
  char buf[0x1000];

  va_list opt;
  va_start(opt, format);
  vsnprintf(buf, sizeof(buf), format, opt);
  va_end(opt);

  ui_end();

  menu_geom alert_geom = make_geom_centered(400, 200);
  ui_start();

  vita2d_draw_rectangle(0, 0, WIDTH, HEIGHT, 0xff000000);
  draw_alert(buf, alert_geom, NULL, 0);

  ui_end();
}

void drw() {
  vita2d_draw_rectangle(0, 0, 150, 150, 0xffffffff);
}

void guilib_init(gui_loop_callback global_loop_cb, gui_draw_callback global_draw_cb) {
  vita2d_init();
  vita2d_set_clear_color(0xff000000);
  font = vita2d_load_font_file("app0:assets/nerdfont.ttf");
  glyph_font = vita2d_load_font_file("app0:assets/nerdfont.ttf");

  gui_global_draw_callback = global_draw_cb;
  gui_global_loop_callback = global_loop_cb;
}

int display_confirm(const char* message) {
    char* buttons[] = {"X: Yes", "O: No"};
    menu_geom geom = make_geom_centered(400, 200);
    while (1) {
        ui_start();
        draw_alert((char*)message, geom, buttons, 2);
        input_data input = {0};
        input.buttons = read_buttons();
        sceTouchPeek(SCE_TOUCH_PORT_FRONT, &input.touch, 1);
        if (input.buttons & config.btn_confirm) {
            ui_end();
            return 1; // X
        } else if (input.buttons & config.btn_cancel) {
            ui_end();
            return 0; // O
        }
        ui_end();
    }
}
