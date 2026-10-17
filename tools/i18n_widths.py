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
bar, and the alert buttons. Values that depend on the player (a PC or app
name) are estimated with a typical one. Exits 1 if anything doesn't fit.
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
        "sprint_time_value": lambda: ["500"],
        "deadzone_value": lambda: [",".join([fill(t("UNIT_PIXELS"), "20")] * 4)],
        # A typical PC and app name: longer ones run off, as English does
        "bench_target_value": lambda: [fill(t("SET_BENCH_TARGET"), "Desktop", "MY-PC"),
                                       fill(t("SET_BENCH_NO_APP"), "MY-PC")],
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


def other_lines(font, t):
    """(where, width available, width) for the lines that aren't menu rows."""
    return [("Settings tab bar", TAB_BAR_ROOM, tab_bar_width(font, t)),
            ("Alert buttons OK/Cancel", ALERT_ROOM, alert_buttons_width(font, t))]


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
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--all", action="store_true", help="list every row checked, not only those too wide")
    args = parser.parse_args()

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
