#!/usr/bin/env python3
"""Writes src/gui/font_coverage.h: the characters assets/nerdfont.ttf has.

    tools/gen_font_coverage.py

Text drawing (src/gui/guilib.c) draws a character with the app's font when
the font has it, and with the Vita's system font otherwise, such as for
Japanese, Korean and Chinese, or Turkish ğ and İ. Rerun after changing the
font. Reads the TTF's cmap (formats 4 and 12) with the standard library.
"""

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FONT = ROOT / "assets" / "nerdfont.ttf"
OUT = ROOT / "src" / "gui" / "font_coverage.h"


def cmap_codepoints(data):
    num_tables = struct.unpack(">H", data[4:6])[0]
    tables = {}
    for i in range(num_tables):
        tag, _, offset, _ = struct.unpack(">4sIII", data[12 + 16 * i:28 + 16 * i])
        tables[tag] = offset
    cmap = tables[b"cmap"]
    count = struct.unpack(">H", data[cmap + 2:cmap + 4])[0]
    subtables = []
    for i in range(count):
        platform, encoding, offset = struct.unpack(">HHI", data[cmap + 4 + 8 * i:cmap + 12 + 8 * i])
        subtables.append((platform, encoding, cmap + offset))
    codepoints = set()
    for platform, encoding, offset in subtables:
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
                for c in range(starts[s], ends[s] + 1):
                    if c == 0xFFFF:
                        continue
                    if ranges[s] == 0:
                        glyph = (c + deltas[s]) & 0xFFFF
                    else:
                        at = range_at + 2 * s + ranges[s] + 2 * (c - starts[s])
                        glyph = struct.unpack(">H", data[at:at + 2])[0]
                        glyph = (glyph + deltas[s]) & 0xFFFF if glyph else 0
                    if glyph:
                        codepoints.add(c)
        elif fmt == 12:
            groups = struct.unpack(">I", data[offset + 12:offset + 16])[0]
            for g in range(groups):
                start, end, glyph = struct.unpack(">III", data[offset + 16 + 12 * g:offset + 28 + 12 * g])
                if glyph:
                    codepoints.update(range(start, end + 1))
    return codepoints


def main():
    codepoints = sorted(cmap_codepoints(FONT.read_bytes()))
    ranges = []
    for c in codepoints:
        if ranges and ranges[-1][1] == c - 1:
            ranges[-1][1] = c
        else:
            ranges.append([c, c])
    lines = [
        "// Made by tools/gen_font_coverage.py from assets/nerdfont.ttf; don't edit.",
        "// The characters the app's font has, as sorted ranges of code points.",
        "#pragma once",
        "",
        "static const unsigned int FONT_COVERAGE[][2] = {",
    ]
    for start, end in ranges:
        lines.append(f"  {{0x{start:04X}, 0x{end:04X}}},")
    lines += ["};", ""]
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)}: {len(codepoints)} characters in {len(ranges)} ranges")
    return 0


if __name__ == "__main__":
    sys.exit(main())
