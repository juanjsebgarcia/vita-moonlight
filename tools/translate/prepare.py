#!/usr/bin/env python3
"""Prepares translation work, following tools/translate/PROMPT.md.

    tools/translate/prepare.py <tag> [<tag> ...]   write tools/translate/work/<tag>.md
    tools/translate/prepare.py --all               the same for every language in LANGUAGES
    tools/translate/prepare.py --snapshot          record the current English in lang/source.ini

A work file lists the keys a language needs: those missing from
lang/<tag>.ini, and those whose English changed since lang/source.ini was
recorded (all of them when there's no snapshot yet). Each comes with its
English, the note for translators, and the current translation, if any.
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import i18n_check  # noqa: E402  (shares the parsing of strings.def and .ini files)

LANG_DIR = ROOT / "lang"
SOURCE = LANG_DIR / "source.ini"
WORK_DIR = Path(__file__).resolve().parent / "work"
GLOSSARY_DIR = Path(__file__).resolve().parent / "glossary"

# The PS Vita's system languages, as translation files: tag, name in
# English, name in the language, font profile. English is built in;
# Portugal's Portuguese and British English share pt-BR and en.
LANGUAGES = [
    ("ja", "Japanese", "日本語", "cjk-ja"),
    ("fr", "French", "Français", "latin"),
    ("es", "Spanish", "Español", "latin"),
    ("de", "German", "Deutsch", "latin"),
    ("it", "Italian", "Italiano", "latin"),
    ("nl", "Dutch", "Nederlands", "latin"),
    ("pt-BR", "Portuguese (Brazil)", "Português (Brasil)", "latin"),
    ("ru", "Russian", "Русский", "latin"),
    ("ko", "Korean", "한국어", "cjk-ko"),
    ("zh-Hant", "Chinese (Traditional)", "繁體中文", "cjk-zh-hant"),
    ("zh-Hans", "Chinese (Simplified)", "简体中文", "cjk-zh-hans"),
    ("fi", "Finnish", "Suomi", "latin"),
    ("sv", "Swedish", "Svenska", "latin"),
    ("da", "Danish", "Dansk", "latin"),
    ("no", "Norwegian", "Norsk", "latin"),
    ("pl", "Polish", "Polski", "latin"),
    ("tr", "Turkish", "Türkçe", "latin"),
]


def read_values(path):
    """The [strings] of an .ini file as {key: value}, or {} if there's none."""
    if not path.exists():
        return {}
    sections = {}
    for kind, value in i18n_check.read_ini(path):
        if kind == "sections":
            sections = value
    return {key: value for key, (value, _) in sections.get("strings", {}).items()}


def language(tag):
    for entry in LANGUAGES:
        if entry[0] == tag:
            return entry
    sys.exit(f"unknown language {tag}; add it to LANGUAGES in {Path(__file__).name}")


def write_work(tag, entries, snapshot):
    _, english_name, native_name, font = language(tag)
    current = read_values(LANG_DIR / f"{tag}.ini")
    todo = []
    for key, english, note in entries:
        escaped = i18n_check.escape_ini(english)
        changed = snapshot.get(key) != escaped
        if key not in current or not current[key] or changed:
            todo.append((key, escaped, note, current.get(key, ""), changed and key in current))
    WORK_DIR.mkdir(exist_ok=True)
    glossary = GLOSSARY_DIR / f"{tag}.md"
    lines = [
        f"# Translation work: {english_name} ({tag})",
        "",
        f"Follow `tools/translate/PROMPT.md`. Output: `lang/{tag}.ini` with `[meta]` tag = {tag}, "
        f"name = {native_name}, font = {font}. Glossary: `{glossary.relative_to(ROOT)}`"
        + (" (read it first)." if glossary.exists() else " (doesn't exist yet: create it)."),
        "",
        f"{len(todo)} of {len(entries)} strings need work. Keys are listed in the template's order; "
        "keep that order in the file, and keep any existing translations that aren't listed here.",
        "",
    ]
    for key, english, note, existing, changed in todo:
        lines.append(f"## {key}")
        if note:
            lines.append(f"Note: {note}")
        lines.append(f"English: {english}")
        if existing:
            lines.append(f"Current ({'English changed since' if changed else 'kept'}): {existing}")
        lines.append("")
    path = WORK_DIR / f"{tag}.md"
    path.write_text("\n".join(lines), encoding="utf-8")
    print(f"{path.relative_to(ROOT)}: {len(todo)} strings")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("tags", nargs="*")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--snapshot", action="store_true")
    args = parser.parse_args()

    entries = i18n_check.read_english()
    if args.snapshot:
        lines = ["; The English the translations were last brought up to date with.",
                 "; Written by tools/translate/prepare.py --snapshot; don't edit.", "", "[strings]"]
        lines += [f"{key} = {i18n_check.escape_ini(english)}" for key, english, _ in entries]
        SOURCE.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"wrote {SOURCE.relative_to(ROOT)} ({len(entries)} strings)")
        return 0
    tags = [entry[0] for entry in LANGUAGES] if args.all else args.tags
    if not tags:
        parser.error("give language tags, --all or --snapshot")
    snapshot = read_values(SOURCE)
    for tag in tags:
        write_work(tag, entries, snapshot)
    return 0


if __name__ == "__main__":
    sys.exit(main())
