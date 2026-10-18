#!/usr/bin/env python3
"""Writes the LiveArea template with the tagline in every system language.

    tools/gen_livearea.py --version 0.13.2 --out build/livearea/template.xml

The build runs this (see CMakeLists.txt); the VPK packs its output. The input
is sce_sys/livearea/contents/template.xml, the English design. It sets the
version shown in the corner, and replaces the <liveitem> that follows the
comment <!-- i18n: KEY --> with one <liveitem> per PS Vita system language,
each carrying <lang>code</lang> and the text of KEY from lang/<tag>.ini (or
the English from src/i18n/strings.def where a file lacks it).

The language codes are the ones Sony's own templates use, for example the
Settings app's (NPXS10015): every system language has its own <liveitem>, and
none is left untagged, as an untagged one would show in every language.
"""

import argparse
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import i18n_check  # noqa: E402  (shares the parsing of strings.def and .ini files)

TEMPLATE = ROOT / "sce_sys" / "livearea" / "contents" / "template.xml"
LANG_DIR = ROOT / "lang"

# LiveArea <lang> code -> lang/<tag>.ini (None: the English in strings.def).
# Sony's "zh" is Simplified Chinese and "ch" Traditional; "pt" is Portugal's
# Portuguese, which the app shows in pt-BR, as it does for the menus.
LIVEAREA_LANGUAGES = [
    ("ja", "ja"),
    ("en", None),
    ("en-gb", None),
    ("fr", "fr"),
    ("es", "es"),
    ("de", "de"),
    ("it", "it"),
    ("nl", "nl"),
    ("pt", "pt-BR"),
    ("pt-br", "pt-BR"),
    ("ru", "ru"),
    ("ko", "ko"),
    ("ch", "zh-Hant"),
    ("zh", "zh-Hans"),
    ("fi", "fi"),
    ("sv", "sv"),
    ("da", "da"),
    ("no", "no"),
    ("pl", "pl"),
    ("tr", "tr"),
]

MARKER = re.compile(r"^([ \t]*)<!-- i18n: (\w+) -->\n\1<liveitem>\n((?:.*\n)*?)\1</liveitem>\n", re.M)
STR_TEXT = re.compile(r"(<str\b[^>]*>)(.*?)(</str>)", re.S)
VERSION = re.compile(r"(<str size=\"18\"[^>]*>)v[0-9]+\.[0-9]+\.[0-9]+(</str>)")


def translation(tag, key, english):
    """KEY's text from lang/<tag>.ini, falling back to the English as the app does."""
    path = LANG_DIR / f"{tag}.ini" if tag else None
    if path is None or not path.exists():
        return english
    for kind, value in i18n_check.read_ini(path):
        if kind == "error":
            sys.exit(f"gen_livearea: {value}")
        if kind == "sections":
            text = value.get("strings", {}).get(key, ("", 0))[0]
            return text or english
    return english


def expand(match, english):
    indent, key, inner = match.groups()
    if key not in english:
        sys.exit(f"gen_livearea: {key} isn't in src/i18n/strings.def")
    shown = STR_TEXT.search(inner)
    if not shown:
        sys.exit(f"gen_livearea: the <liveitem> after <!-- i18n: {key} --> has no <str>")
    if shown.group(2) != escape(english[key]):
        sys.exit(f"gen_livearea: template.xml shows {shown.group(2)!r} for {key}, but strings.def has "
                 f"{english[key]!r}; make them match")
    child_indent = re.match(r"[ \t]*", inner).group(0)
    items = []
    for code, tag in LIVEAREA_LANGUAGES:
        text = translation(tag, key, english[key])
        if "\\n" in text:
            sys.exit(f"gen_livearea: {key} in lang/{tag}.ini has a line break; keep it on one line")
        tagged = STR_TEXT.sub(lambda m: m.group(1) + escape(text) + m.group(3), inner, count=1)
        items.append(f"{indent}<liveitem>\n{child_indent}<lang>{code}</lang>\n{tagged}{indent}</liveitem>\n")
    return "".join(items)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", required=True, help="the version shown in the corner, as A.B.C")
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()

    english = {key: text for key, text, _ in i18n_check.read_english()}
    source = TEMPLATE.read_text(encoding="utf-8")

    text, count = VERSION.subn(lambda m: m.group(1) + "v" + args.version + m.group(2), source)
    if count != 1:
        sys.exit("gen_livearea: template.xml should have one version <str size=\"18\">vA.B.C</str>")
    text, count = MARKER.subn(lambda m: expand(m, english), text)
    if count == 0:
        sys.exit("gen_livearea: template.xml has no <!-- i18n: KEY --> before a <liveitem>")

    try:
        ET.fromstring(text.encode("utf-8"))
    except ET.ParseError as error:
        sys.exit(f"gen_livearea: the generated template isn't well-formed XML: {error}")

    # Always written, so make sees it newer than its inputs
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
