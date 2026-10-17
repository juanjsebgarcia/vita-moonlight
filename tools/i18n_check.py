#!/usr/bin/env python3
"""Checks the translations against the English, and writes the template.

    tools/i18n_check.py            check lang/*.ini, and that the template is current
    tools/i18n_check.py --write    rewrite lang/template.ini from src/i18n/strings.def

The English (src/i18n/strings.def) is the source of truth. For each language
file this reports, as errors: keys the English doesn't have, placeholders
({0}, {1}...) that differ from the English, printf specifiers (%s, %d...),
and a missing [meta] tag, name or font. Keys a file leaves out stay in
English; it reports how complete each file is.
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STRINGS_DEF = ROOT / "src" / "i18n" / "strings.def"
LANG_DIR = ROOT / "lang"
TEMPLATE = LANG_DIR / "template.ini"
FONTS = {"latin", "cjk-ja", "cjk-ko", "cjk-zh-hans", "cjk-zh-hant"}

ENTRY = re.compile(r'^S\((\w+),\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)"\)', re.M)
# {0}..{9} filled in by the code, and button markers such as {triangle}
PLACEHOLDER = re.compile(r"\{(\d|[a-z][a-z0-9_]*)\}")
PRINTF = re.compile(r"%[-+ #0-9.]*[sdifuxXcp%]")


C_ESCAPES = {"n": "\n", "t": "\t"}


def unescape_c(text):
    # One pass, so an escaped backslash before an n stays a backslash
    return re.sub(r"\\(.)", lambda m: C_ESCAPES.get(m.group(1), m.group(1)), text)


def read_english():
    entries = []
    for key, english, note in ENTRY.findall(STRINGS_DEF.read_text(encoding="utf-8")):
        if key == "NONE":
            continue
        entries.append((key, unescape_c(english), unescape_c(note)))
    return entries


def escape_ini(text):
    return text.replace("\\", "\\\\").replace("\n", "\\n")


def template_text(entries):
    lines = [
        "; Moonlight for PS Vita: translation template, made by tools/i18n_check.py --write",
        "; Copy this file to lang/<tag>.ini (for example de.ini), fill in [meta], and",
        "; translate the values. Leave out any you haven't translated: they stay in English.",
        "; Keep {0}, {1}... (the app fills them in), in whatever order suits the language.",
        "; Keep button markers such as {confirm}, {triangle} or {start}: they are drawn as the button.",
        "; Write a line break as \\n. Test without rebuilding by copying the file to",
        "; ux0:data/moonlight/lang/ on the Vita.",
        "",
        "[meta]",
        "tag =",
        "name =",
        "font = latin",
        "",
        "[strings]",
    ]
    for key, english, note in entries:
        if note:
            lines.append("; " + note)
        lines.append(f"{key} = {escape_ini(english)}")
    return "\n".join(lines) + "\n"


def read_ini(path):
    """Reads an .ini the way inih does here: sections, key = value, ; comments."""
    sections = {}
    section = None
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith(";") or line.startswith("#"):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = sections.setdefault(line[1:-1].strip(), {})
            continue
        if "=" not in line or section is None:
            yield ("error", f"{path.name}:{number}: not a key = value line")
            continue
        key, value = (part.strip() for part in line.split("=", 1))
        if key in section:
            yield ("error", f"{path.name}:{number}: {key} given twice")
        section[key] = (value, number)
    yield ("sections", sections)


def check_language(path, english):
    errors, sections = [], {}
    for kind, value in read_ini(path):
        if kind == "error":
            errors.append(value)
        else:
            sections = value
    meta = {key: value for key, (value, _) in sections.get("meta", {}).items()}
    for field in ("tag", "name", "font"):
        if not meta.get(field):
            errors.append(f"{path.name}: [meta] needs a {field}")
    if meta.get("font") and meta["font"] not in FONTS:
        errors.append(f"{path.name}: unknown font {meta['font']} (one of {', '.join(sorted(FONTS))})")
    strings = sections.get("strings", {})
    translated = 0
    for key, (value, number) in strings.items():
        where = f"{path.name}:{number}: {key}"
        if key not in english:
            errors.append(f"{where}: not in strings.def")
            continue
        if not value:
            continue
        translated += 1
        if sorted(PLACEHOLDER.findall(value)) != sorted(PLACEHOLDER.findall(english[key])):
            errors.append(f"{where}: placeholders or button markers differ from the English ({english[key]!r})")
        if PRINTF.search(value):
            errors.append(f"{where}: printf specifier; use {{0}}, {{1}}... instead")
    return errors, translated, meta


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--write", action="store_true", help="rewrite lang/template.ini")
    args = parser.parse_args()

    entries = read_english()
    english = {key: text for key, text, _ in entries}
    template = template_text(entries)
    if args.write:
        LANG_DIR.mkdir(exist_ok=True)
        TEMPLATE.write_text(template, encoding="utf-8")
        print(f"wrote {TEMPLATE.relative_to(ROOT)} ({len(entries)} strings)")
        return 0

    failed = False
    if not TEMPLATE.exists() or TEMPLATE.read_text(encoding="utf-8") != template:
        print("lang/template.ini is out of date: run tools/i18n_check.py --write")
        failed = True
    for path in sorted(LANG_DIR.glob("*.ini")):
        if path == TEMPLATE:
            continue
        errors, translated, meta = check_language(path, english)
        for error in errors:
            print("error:", error)
        failed = failed or bool(errors)
        print(f"{path.name}: {meta.get('name', '?')} ({meta.get('tag', '?')}), "
              f"{translated} of {len(english)} strings ({100 * translated // max(len(english), 1)}%)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
