#!/usr/bin/env python3
"""Checks that text shown on one fixed line fits, in every language.

    tools/i18n_widths.py           report the rows that don't fit
    tools/i18n_widths.py --all     report every row checked, with its width

Menu rows (src/gui/guilib.c draw_menu) aren't wrapped: the label sits on the
left and the value on the right, and text that's too long overlaps or runs
off the box. This estimates each row's width in pixels the way the app draws
it: the app's font (assets/nerdfont.ttf) at 18 px, read with the standard
library, and anything that font lacks (Japanese, Korean, Chinese, Turkish ğ
and İ) in the Vita's system font, estimated as full width (18 px) for CJK
and 10 px otherwise. Button markers such as {start} count as their symbol.

It checks the Settings rows (label, value and arrows, with the widest value
each can show), the Settings notes, the Recommended settings rows, the tab
bar, the alert buttons, the host list's new theme hint, and the rows of the deadzone editor, the special
keys menu and a PC's menus. Values that depend on the player (a PC or app
name, a typed or stepped number) are checked at their realistic widest.
Exits 1 if anything doesn't fit.

    tools/i18n_widths.py --theme morpheus [--all]

checks the Morpheus theme's Settings screens instead (DESIGN.md S5, S6 and
S10): its fonts (assets/morpheus, M PLUS 1p at the theme's type ramp, with
unhinted advances as src/gui/themes/morpheus/mph_text.c lays them out) and
its row widths. Morpheus cuts what doesn't fit with an ellipsis rather than
letting it overlap, so a line reported there shows cut, not broken: the tool
says which part is cut. Characters the subset lacks (CJK) are drawn with the
system font, estimated at full width for CJK and 0.6 em otherwise.
"""

import argparse
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import i18n_check  # noqa: E402  (shares the parsing of strings.def and .ini files)

FONT = ROOT / "assets" / "nerdfont.ttf"
SETTINGS_C = ROOT / "src" / "settings.c"
SIZE = 18
CJK_WIDTH = SIZE
OTHER_SYSTEM_WIDTH = 10
GAP = 10  # the least space to leave between a label and its value

# draw_button_glyph() widths at the 14 px glyph size text uses; pills are
# measured from their label
GLYPH = 14
PILLS = {"l": "L", "r": "R", "l2": "L2", "r2": "R2", "start": "START", "select": "SELECT"}
MARKERS = {"cross", "circle", "triangle", "square", "confirm", "cancel", "dpad", "up", "down", "left",
           "right", "ps", "left_stick", "right_stick", "l3", "r3", "front_touch", "rear_touch"} | set(PILLS)
# Stand-ins for what the player chooses, at their realistic widest: a
# Windows PC name is at most 15 characters, and DESKTOP- plus 7 is the default
PC_NAME = "DESKTOP-ABC1234"
APP_NAME = "Steam Big Picture"
DEADZONE = "945"
ADDRESS = "255.255.255.255"
ARROWS = ""  # ICON_LEFT_RIGHT_ARROWS, drawn after an adjustable row's value


def font_tables(data):
    count = struct.unpack(">H", data[4:6])[0]
    tables = {}
    for i in range(count):
        tag, _, offset, _ = struct.unpack(">4sIII", data[12 + 16 * i:28 + 16 * i])
        tables[tag] = offset
    return tables


def cmap_glyphs(data, cmap):
    """{code point: glyph id} from the cmap's format 4 and 12 subtables."""
    glyphs = {}
    count = struct.unpack(">H", data[cmap + 2:cmap + 4])[0]
    for i in range(count):
        platform, encoding, offset = struct.unpack(">HHI", data[cmap + 4 + 8 * i:cmap + 12 + 8 * i])
        offset += cmap
        fmt = struct.unpack(">H", data[offset:offset + 2])[0]
        if fmt == 4 and (platform, encoding) in ((3, 1), (0, 3), (0, 4)):
            segs = struct.unpack(">H", data[offset + 6:offset + 8])[0] // 2
            ends = struct.unpack(f">{segs}H", data[offset + 14:offset + 14 + 2 * segs])
            starts_at = offset + 16 + 2 * segs
            starts = struct.unpack(f">{segs}H", data[starts_at:starts_at + 2 * segs])
            deltas = struct.unpack(f">{segs}h", data[starts_at + 2 * segs:starts_at + 4 * segs])
            range_at = starts_at + 4 * segs
            ranges = struct.unpack(f">{segs}H", data[range_at:range_at + 2 * segs])
            for s in range(segs):
                for c in range(starts[s], min(ends[s], 0xFFFE) + 1):
                    if ranges[s] == 0:
                        glyph = (c + deltas[s]) & 0xFFFF
                    else:
                        at = range_at + 2 * s + ranges[s] + 2 * (c - starts[s])
                        glyph = struct.unpack(">H", data[at:at + 2])[0]
                        glyph = (glyph + deltas[s]) & 0xFFFF if glyph else 0
                    if glyph:
                        glyphs.setdefault(c, glyph)
        elif fmt == 12:
            groups = struct.unpack(">I", data[offset + 12:offset + 16])[0]
            for g in range(groups):
                start, end, glyph = struct.unpack(">III", data[offset + 16 + 12 * g:offset + 28 + 12 * g])
                if glyph:
                    for k, c in enumerate(range(start, end + 1)):
                        glyphs.setdefault(c, glyph + k)
    return glyphs


class Font:
    """Advance widths of the app's font, in pixels at SIZE, as FreeType rounds them."""

    def __init__(self, path):
        data = path.read_bytes()
        tables = font_tables(data)
        units_per_em = struct.unpack(">H", data[tables[b"head"] + 18:tables[b"head"] + 20])[0]
        metrics = struct.unpack(">H", data[tables[b"hhea"] + 34:tables[b"hhea"] + 36])[0]
        hmtx = tables[b"hmtx"]
        advances = [struct.unpack(">H", data[hmtx + 4 * i:hmtx + 4 * i + 2])[0] for i in range(metrics)]
        self.glyphs = cmap_glyphs(data, tables[b"cmap"])
        self.scale = SIZE / units_per_em
        self.advances = advances

    def advance(self, c):
        glyph = self.glyphs.get(ord(c))
        if glyph is None:
            return None
        units = self.advances[min(glyph, len(self.advances) - 1)]
        return int(units * self.scale + 0.5)


def is_cjk(c):
    o = ord(c)
    return (0x1100 <= o <= 0x11FF or 0x2E80 <= o <= 0x9FFF or 0xAC00 <= o <= 0xD7AF or
            0xF900 <= o <= 0xFAFF or 0xFF00 <= o <= 0xFFEF)


def text_width(font, text):
    width = 0
    pos = 0
    for m in re.finditer(r"\{([a-z0-9_]+)\}", text):
        width += plain_width(font, text[pos:m.start()])
        name = m.group(1)
        if name in PILLS:
            width += pill_width(font, PILLS[name])
        elif name in MARKERS:
            width += GLYPH
        else:
            width += plain_width(font, m.group(0))
        pos = m.end()
    return width + plain_width(font, text[pos:])


def plain_width(font, text, size=SIZE):
    width = 0
    for c in text:
        advance = font.advance(c)
        if advance is None:
            advance = CJK_WIDTH if is_cjk(c) else OTHER_SYSTEM_WIDTH
        elif size != SIZE:
            advance = advance * size / SIZE
        width += advance
    return int(width)


def fill(text, *args):
    for i, arg in enumerate(args):
        text = text.replace("{%d}" % i, arg)
    return text


def settings_rows():
    """(label key, kind, value function, fixed value, is a toggle) for each row in SETTINGS[]."""
    source = SETTINGS_C.read_text(encoding="utf-8")
    table = source[source.index("const setting_def SETTINGS[] = {"):]
    table = table[:table.index("\n};")]
    rows = []
    for entry in re.findall(r"\{\.tab = .*?\}(?=,\s*(?:\n|$))", table, re.S):
        label = re.search(r"\.label = STR_(\w+)", entry)
        kind = re.search(r"\.kind = SETTING_(\w+)", entry)
        value = re.search(r"\.value = (\w+)", entry)
        fixed = re.search(r'\.fixed_value = "([^"]*)"', entry)
        if label and kind:
            rows.append((label.group(1), kind.group(1), value.group(1) if value else None,
                         fixed.group(1) if fixed else None, ".is_on" in entry))
    return rows


def value_candidates(fn, t, language_names):
    """The values a settings value function can show, the widest that matter."""
    if fn is None:
        return []
    return {
        "resolution_value": lambda: ["1920x1080"],
        "fps_value": lambda: ["60"],
        "bitrate_value": lambda: [fill(t("UNIT_MBPS"), "12.5"), t("SET_VALUE_CUSTOM")],
        "custom_bitrate_value": lambda: [fill(t("UNIT_KBPS"), "100000")],
        "packet_size_value": lambda: [fill(t("UNIT_BYTES"), "1392")],
        "stats_overlay_value": lambda: [t("STATS_OFF"), t("STATS_FPS"), t("STATS_FULL")],
        "app_list_value": lambda: [t("APP_LIST_TEXT"), t("APP_LIST_BOXART")],
        "controller_type_value": lambda: ["PlayStation"],
        "mouse_accel_value": lambda: ["100"],
        "touch_mode_value": lambda: [t("TOUCH_OFF"), t("TOUCH_DS4"), t("TOUCH_MOUSE_ABSOLUTE"), t("TOUCH_TABLET")],
        # Typed in, without a limit; a double tap takes well under 10 s
        "sprint_time_value": lambda: ["1000"],
        # deadzone_loop() moves each edge 15 px at a time, without a limit;
        # three digits is as wide as a useful edge gets on a 960x544 screen
        "deadzone_value": lambda: [",".join([fill(t("UNIT_PIXELS"), DEADZONE)] * 4)],
        "bench_target_value": lambda: [fill(t("SET_BENCH_TARGET"), APP_NAME, PC_NAME),
                                       fill(t("SET_BENCH_NO_APP"), PC_NAME)],
        "bench_seconds_value": lambda: [fill(t("UNIT_SECONDS"), "60")],
        "language_value": lambda: [fill(t("SET_LANGUAGE_AUTO"), name) for name in language_names],
        "version_value": lambda: ["v0.13.2"],
    }.get(fn, lambda: [])()


def pill_width(font, label):
    """draw_pill()'s width for a symbol such as {l} or {start} at the 14 px glyph size."""
    return max(GLYPH, plain_width(font, label, int(GLYPH * 0.62)) + GLYPH // 2 + 4)


def tab_bar_width(font, t):
    """settings_draw()'s tab bar, with every tab shown: the L symbol, a 12 px
    gap, the tabs 16 px apart, a 12 px gap and the R symbol."""
    tabs = [t(k) for k in ("TAB_STREAM", "TAB_SYSTEM", "TAB_CONTROLS", "TAB_DEVELOPER", "TAB_ABOUT")]
    return (pill_width(font, "L") + 12 + sum(text_width(font, tab) for tab in tabs) + 16 * (len(tabs) - 1)
            + 12 + pill_width(font, "R"))


def alert_buttons_width(font, t):
    """draw_button_captions() for an alert's confirm and cancel: each a
    symbol, a 6 px gap, the caption and 16 px of spacing."""
    return sum(GLYPH + 6 + text_width(font, t(k)) + 16 for k in ("BUTTON_OK", "BUTTON_CANCEL"))


# The tab bar starts at x 180 and must end before the clock, which sits about
# 85 px in from the box's right edge (x 780), less a small gap
TAB_BAR_ROOM = 780 - 85 - 8 - 180
# Alert captions are right-aligned in the 400 px box; leave its left 20 px clear
ALERT_ROOM = 400 - 20
# The new theme hint is right-aligned under the 500 px host list box
THEME_HINT_ROOM = 500


def other_lines(font, t):
    """(where, width available, width) for the lines that aren't menu rows."""
    return [("Settings tab bar", TAB_BAR_ROOM, tab_bar_width(font, t)),
            ("Alert buttons OK/Cancel", ALERT_ROOM, alert_buttons_width(font, t)),
            ("Host list THEME_TRY_NEW hint", THEME_HINT_ROOM, text_width(font, t("THEME_TRY_NEW")))]


def checks(t, language_names):
    """(where, width available, label, [values], adjustable) for each menu row."""
    out = []
    # Settings: the default 600 px menu. The label starts 12 px in; the value
    # (and arrows) end 10 px before the right edge
    settings_room = 600 - 22
    for label, kind, fn, fixed, toggle in settings_rows():
        if kind == "HEADER":
            continue
        values = [t("COMMON_YES"), t("COMMON_NO")] if toggle else [fixed] if fixed else \
            value_candidates(fn, t, language_names)
        out.append((f"Settings {label}", settings_room, t(label), values, kind == "CHOICE"))
    # Recommended settings: a 620 px menu
    rec_room = 620 - 22
    out.append(("Recommended REC_ROW_PRIORITY", rec_room, t("REC_ROW_PRIORITY"),
                [t("REC_BALANCED"), t("REC_LOWEST_DELAY"), t("REC_BATTERY")], True))
    out.append(("Recommended REC_ROW_AUTO", rec_room, t("REC_ROW_AUTO"), [t("REC_QUIT_GAME_FIRST")], False))
    out.append(("Recommended REC_ROW_GOOD", rec_room, t("REC_ROW_GOOD"), [t("REC_FITS_NOW")], False))
    out.append(("Recommended REC_ROW_WEAK", rec_room, t("REC_ROW_WEAK"), [t("REC_FITS_NOW")], False))
    out.append(("Recommended (signal row)", rec_room, "",
                [t("REC_WIRED"), t("REC_MOBILE"), t("REC_NO_WIFI"), fill(t("REC_WIFI_NOW"), "100")], False))
    # The rear touch deadzone editor: a 250 px menu, a value per edge
    for key in ("DEADZONE_TOP", "DEADZONE_RIGHT", "DEADZONE_BOTTOM", "DEADZONE_LEFT"):
        out.append((f"Deadzone editor {key}", 250 - 22, t(key), [fill(t("UNIT_PIXELS"), DEADZONE)], True))
    # Touchscreen special keys: the default menu, a key per corner
    keys = [t(k) for k in ("SPECIAL_NONE", "SPECIAL_PAUSE", "SPECIAL_KEYBOARD", "SPECIAL_GUIDE",
                           "SPECIAL_MOUSE_LEFT", "SPECIAL_MOUSE_RIGHT", "SPECIAL_MOUSE_MIDDLE")]
    for key in ("SPECIAL_TOP_LEFT", "SPECIAL_TOP_RIGHT", "SPECIAL_BOTTOM_LEFT", "SPECIAL_BOTTOM_RIGHT"):
        out.append((f"Special keys {key}", settings_room, t(key), keys, False))
    for key in ("SPECIAL_OFFSET", "SPECIAL_SIZE"):
        out.append((f"Special keys {key}", settings_room, t(key), ["544"], False))
    # A PC's menu (600 px): its address, with its status on the right
    out.append(("PC menu address and status", settings_room, fill(t("HOST_ADDRESS_LINE"), ADDRESS, "47989"),
                [t(k) for k in ("HOST_UNPAIRED", "HOST_PAIRED_ONLINE", "HOST_UNPAIRED_ONLINE", "HOST_PAIRED_OFFLINE",
                                "HOST_UNPAIRED_OFFLINE", "HOST_STATUS_IP_CHANGED")], False))
    # The apps row of a connected PC's menu, with the cover counter
    out.append(("PC menu CONNECTED_APPS", settings_room, t("CONNECTED_APPS"),
                [fill(t("BOXART_COUNTER"), "100", "100")], True))
    return out


# Morpheus (--theme morpheus)

MORPHEUS_FONTS = ROOT / "assets" / "morpheus"
# The type ramp's styles used here: (face, size in px, tracking)
MPH_ROW = ("Medium", 17, 0.0)
MPH_ROW_L = ("Bold", 18, 0.0)
MPH_BTN = ("Bold", 16, 0.0)
MPH_BODY_B = ("Bold", 15, 0.0)
MPH_SMALL = ("Medium", 14, 0.0)
MPH_TAG = ("ExtraBold", 11, 0.22)
MPH_LABEL = ("ExtraBold", 12, 1.68)
# Face buttons, the D-pad and sticks are icons: 18 px in text up to 15 px,
# 22 up to 21 px; the touch pads are wider
MPH_ICON_W = {"front_touch": (27, 33), "rear_touch": (27, 33)}
MPH_KEYCAPS = {"l": "L", "r": "R", "l2": "L2", "r2": "R2", "start": "START", "select": "SELECT", "ps": "PS",
               "l3": "L3", "r3": "R3"}
ELLIPSIS = "\u2026"


class MorpheusFont:
    """M PLUS 1p's unhinted advances (FreeType's linearHoriAdvance), in px at any size."""

    def __init__(self, path):
        data = path.read_bytes()
        tables = font_tables(data)
        self.units_per_em = struct.unpack(">H", data[tables[b"head"] + 18:tables[b"head"] + 20])[0]
        metrics = struct.unpack(">H", data[tables[b"hhea"] + 34:tables[b"hhea"] + 36])[0]
        hmtx = tables[b"hmtx"]
        self.advances = [struct.unpack(">H", data[hmtx + 4 * i:hmtx + 4 * i + 2])[0] for i in range(metrics)]
        self.glyphs = cmap_glyphs(data, tables[b"cmap"])

    def advance(self, c, size):
        glyph = self.glyphs.get(ord(c))
        if glyph is None:
            return None
        return self.advances[min(glyph, len(self.advances) - 1)] * size / self.units_per_em


class Morpheus:
    """Widths as mph_text.c measures them: float pens, markers as symbols or keycaps."""

    def __init__(self):
        self.faces = {w: MorpheusFont(MORPHEUS_FONTS / f"MPLUS1p-{w}-latin.ttf") for w in ("Medium", "Bold", "ExtraBold")}

    def plain(self, style, text):
        face, size, tracking = style
        pen = 0.0
        for c in text:
            if c in "\n\r":
                continue
            advance = self.faces[face].advance(c, size)
            if advance is None:
                advance = size if is_cjk(c) else 0.6 * size
            pen += advance + tracking
        return pen - tracking if pen > 0 and tracking else pen

    def keycap(self, label):
        return self.plain(MPH_TAG, label) + 12

    def symbol(self, name, style):
        if name in MPH_KEYCAPS:
            return self.keycap(MPH_KEYCAPS[name])
        step = 0 if style[1] <= 15 else 1
        return MPH_ICON_W.get(name, (18, 22))[step]

    def width(self, style, text):
        pen = 0.0
        pos = 0
        for m in re.finditer(r"\{([a-z0-9_]+)\}", text):
            pen += self.plain(style, text[pos:m.start()])
            name = m.group(1)
            pen += self.symbol(name, style) if name in MARKERS | {"ps", "l3", "r3"} else self.plain(style, m.group(0))
            pos = m.end()
        return pen + self.plain(style, text[pos:])

    def wrap(self, style, text, max_w):
        """Lines as text_wrap() breaks them: at spaces and newlines, CJK anywhere."""
        lines = []
        for paragraph in text.split("\n"):
            line = ""
            for word in re.findall(r"[\u1100-\u11ff\u2e80-\u9fff\uac00-\ud7af\uf900-\ufaff\uff00-\uffef]|[^ \u1100-\u11ff\u2e80-\u9fff\uac00-\ud7af\uf900-\ufaff\uff00-\uffef]+", paragraph):
                joiner = "" if not line or is_cjk(word[0]) or is_cjk(line[-1]) else " "
                if line and self.width(style, line + joiner + word) > max_w:
                    lines.append(line)
                    line = word
                else:
                    line += joiner + word
            lines.append(line)
        return lines


# Geometry (DESIGN.md S5, S6, S10; src/gui/themes/morpheus/mph_widgets.c row_draw)
MPH_SETTINGS_ROW_W = 600
MPH_KEYBOARD_ROW_W = 360 - 16
MPH_ROW_PAD = 16
MPH_LABEL_GAP = 14      # the least space between the label and what's right of it
MPH_VALUE_GAP = 10      # between the value and the arrows, chevron or tick
MPH_VALUE_MIN = 64     # a value is cut no shorter than this before the label is
MPH_SWITCH = 40 + 14
MPH_CHEVRON = 14 + 10
MPH_CHECK = 18 + 10
MPH_TAB_RIGHT = 960 - 40
MPH_HELP_W = 256 - 36
MPH_HELP_TITLE_LINES = 3
MPH_READING_ROOM_METER = 470 - 14 - 92
MPH_READING_ROOM = 600 - 36
MPH_TITLE_ROOM = 600 - 22


def morpheus_row(m, row_w, label, value, kind):
    """(width needed, room, what gets cut) for one row. kind: toggle, choice,
    action, check or plain. The value takes what the label leaves, and is cut
    first, down to 64 px; past that the label is cut too."""
    room = row_w - 2 * MPH_ROW_PAD
    trailing = {"toggle": MPH_SWITCH, "action": MPH_CHEVRON, "check": MPH_CHECK}.get(kind, 0)
    label_w = m.width(MPH_ROW, label)
    value_w = m.width(MPH_ROW, value) if value else 0
    arrows = m.plain(MPH_ROW, "\u2039") + m.plain(MPH_ROW, "\u203a") + 2 * MPH_VALUE_GAP if kind == "choice" and value else 0
    gaps = MPH_VALUE_GAP + 4 if value else 4
    need = label_w + gaps + value_w + arrows + trailing
    if need <= room:
        return need, room, None
    spare = room - trailing - arrows - gaps - label_w
    value_room = max(MPH_VALUE_MIN, spare)
    if value_w > value_room:
        return need, room, "value" if label_w + gaps + value_room + arrows + trailing <= room else "value and label"
    return need, room, "label"


def morpheus_checks(m, t, language_names):
    """(where, width, room, what's cut or None) for every line of the Settings screens."""
    out = []
    for label, kind, fn, fixed, toggle in settings_rows():
        if kind == "HEADER":
            w = m.plain(MPH_LABEL, t(label).upper())
            out.append((f"Settings header {label}", w, MPH_SETTINGS_ROW_W - 32, "label" if w > MPH_SETTINGS_ROW_W - 32 else None))
            continue
        if kind == "NOTE":
            lines = m.wrap(MPH_SMALL, t(label), MPH_SETTINGS_ROW_W - 32 - 16)
            widest = max(m.width(MPH_SMALL, line) for line in lines)
            out.append((f"Settings note {label} ({len(lines)} lines)", widest, MPH_SETTINGS_ROW_W - 48,
                        "third line" if len(lines) > 2 else None))
            continue
        values = [fixed] if fixed else value_candidates(fn, t, language_names)
        row_kind = "toggle" if toggle else {"CHOICE": "choice", "ACTION": "action"}.get(kind, "plain")
        worst = max((morpheus_row(m, MPH_SETTINGS_ROW_W, t(label), v, row_kind) for v in values or [None]),
                    key=lambda r: r[0] - r[1])
        out.append((f"Settings {label}", *worst))
        # The help card's title is the row's label: two lines at most
        lines = m.wrap(MPH_ROW_L, t(label), MPH_HELP_W)
        out.append((f"Help card title {label} ({len(lines)} lines)", max(m.width(MPH_ROW_L, x) for x in lines),
                    MPH_HELP_W, "fourth line" if len(lines) > MPH_HELP_TITLE_LINES else None))
    tabs = [t(k) for k in ("TAB_STREAM", "TAB_SYSTEM", "TAB_CONTROLS", "TAB_DEVELOPER", "TAB_ABOUT")]
    right = 40 + m.keycap("L") + 4 + sum(m.width(MPH_BTN, x) + 24 + 4 for x in tabs) + m.keycap("R")
    out.append(("Settings tab bar (right edge)", right, MPH_TAB_RIGHT, "tabs" if right > MPH_TAB_RIGHT else None))

    # Apply recommended settings
    for label, values, kind in (("REC_ROW_PRIORITY", ["REC_BALANCED", "REC_LOWEST_DELAY", "REC_BATTERY"], "choice"),
                                ("REC_ROW_AUTO", ["REC_QUIT_GAME_FIRST"], "action"),
                                ("REC_ROW_GOOD", ["REC_FITS_NOW"], "action"),
                                ("REC_ROW_WEAK", ["REC_FITS_NOW"], "action")):
        worst = max((morpheus_row(m, MPH_SETTINGS_ROW_W, t(label), t(v), kind) for v in values),
                    key=lambda r: r[0] - r[1])
        out.append((f"Recommended {label}", *worst))
        lines = m.wrap(MPH_ROW_L, t(label), MPH_HELP_W)
        out.append((f"Help card title {label} ({len(lines)} lines)", max(m.width(MPH_ROW_L, x) for x in lines),
                    MPH_HELP_W, "fourth line" if len(lines) > MPH_HELP_TITLE_LINES else None))
    w = m.width(MPH_BTN, fill(t("REC_WIFI_NOW"), "100"))
    out.append(("Recommended reading REC_WIFI_NOW", w, MPH_READING_ROOM_METER,
                "reading" if w > MPH_READING_ROOM_METER else None))
    for key in ("REC_WIRED", "REC_MOBILE", "REC_NO_WIFI"):
        w = m.width(MPH_BTN, t(key))
        out.append((f"Recommended reading {key}", w, MPH_READING_ROOM, "reading" if w > MPH_READING_ROOM else None))

    # Keyboard layout
    for key in ("KEYBOARD_EN_US", "KEYBOARD_ES_ES", "KEYBOARD_ES_LATAM"):
        out.append((f"Keyboard layout {key}", *morpheus_row(m, MPH_KEYBOARD_ROW_W, t(key), None, "check")))

    # Status bar titles and the legend
    for key in ("MAIN_SETTINGS", "SET_RECOMMENDED", "SET_KEYBOARD_LAYOUT"):
        w = m.width(MPH_BODY_B, t(key))
        out.append((f"Status bar title {key}", w, MPH_TITLE_ROOM, "title" if w > MPH_TITLE_ROOM else None))
    w = m.symbol("triangle", MPH_ROW) + 7 + m.width(MPH_SMALL, t("HINT_HELP"))
    out.append(("Legend HINT_HELP", w, 960 - 40 - 40, "legend" if w > 880 else None))
    return out


def languages_and_names():
    english = {key: text for key, text, _ in i18n_check.read_english()}
    languages = [("en", {})]
    for path in sorted(i18n_check.LANG_DIR.glob("*.ini")):
        if path.name in ("template.ini", "source.ini"):
            continue
        for kind, value in i18n_check.read_ini(path):
            if kind == "sections":
                strings = {k: i18n_check.unescape_c(v.replace("\\n", "\n")) for k, (v, _) in
                           value.get("strings", {}).items() if v}
                name = value.get("meta", {}).get("name", (path.stem, 0))[0]
                languages.append((path.stem, strings, name))
    names = ["English"] + [entry[2] for entry in languages[1:]]
    return english, languages, names


def main_morpheus(show_all):
    m = Morpheus()
    english, languages, names = languages_and_names()
    wide = cut = 0
    for entry in languages:
        tag, strings = entry[0], entry[1]

        def t(key, strings=strings):
            return strings.get(key, english.get(key, key))

        for where, width, room, what in morpheus_checks(m, t, names):
            # A value ending in an ellipsis is the layout working as meant (the
            # label keeps its words); anything else loses translated text
            value_only = what == "value"
            cut += value_only
            wide += what is not None and not value_only
            if show_all or what is not None:
                mark = "ok  " if what is None else "cut " if value_only else "WIDE"
                print(f"{mark} {tag:8} {where:58} {width:6.1f} of {room:5.1f} px" + (f"  ({what} cut)" if what else ""))
    print(f"{len(languages)} languages: {wide} line(s) too wide, {cut} value(s) shortened with an ellipsis")
    return 1 if wide else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--all", action="store_true", help="list every row checked, not only those too wide")
    parser.add_argument("--theme", choices=("homebrew", "morpheus"), default="homebrew",
                        help="the theme whose layout to check (default: homebrew)")
    args = parser.parse_args()
    if args.theme == "morpheus":
        return main_morpheus(args.all)

    font = Font(FONT)
    english = {key: text for key, text, _ in i18n_check.read_english()}
    languages = [("en", {})]
    for path in sorted(i18n_check.LANG_DIR.glob("*.ini")):
        if path.name in ("template.ini", "source.ini"):
            continue
        for kind, value in i18n_check.read_ini(path):
            if kind == "sections":
                strings = {k: i18n_check.unescape_c(v.replace("\\n", "\n")) for k, (v, _) in
                           value.get("strings", {}).items() if v}
                name = value.get("meta", {}).get("name", (path.stem, 0))[0]
                languages.append((path.stem, strings, name))
    names = ["English"] + [entry[2] for entry in languages[1:]]

    too_wide = 0
    for entry in languages:
        tag, strings = entry[0], entry[1]

        def t(key, strings=strings):
            return strings.get(key, english.get(key, key))

        for where, room, label, values, adjustable in checks(t, names):
            arrows = plain_width(font, ARROWS) + 10 if adjustable else 0
            value_width = max((text_width(font, v) for v in values if v), default=0)
            label_width = text_width(font, label)
            width = label_width + (GAP + value_width if value_width else 0) + arrows
            fits = width <= room
            too_wide += not fits
            if args.all or not fits:
                print(f"{'ok  ' if fits else 'WIDE'} {tag:8} {where:42} {width:4} of {room} px  {label!r}"
                      + (f" + {max((v for v in values if v), key=lambda v: text_width(font, v))!r}"
                         if value_width else ""))
        for where, room, width in other_lines(font, t):
            fits = width <= room
            too_wide += not fits
            if args.all or not fits:
                print(f"{'ok  ' if fits else 'WIDE'} {tag:8} {where:42} {width:4} of {room} px")
    print(f"{too_wide} line(s) too wide" if too_wide else "every line fits")
    return 1 if too_wide else 0


if __name__ == "__main__":
    sys.exit(main())
