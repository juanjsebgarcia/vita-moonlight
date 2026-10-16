"""
Write a copy of moonlight.conf with some top-level keys set.

Used by run.sh to switch benchmark variants. Keys go in the top-level
section, before the first [section] header, which is where Moonlight reads
them; existing keys are replaced and new ones are added.

Usage: conf_edit.py IN OUT key=value [key=value ...]
"""

import sys


def set_keys(lines: list[str], updates: dict[str, str]) -> list[str]:
    """
    Return lines with each key in updates set in the top-level section.
    """
    remaining = dict(updates)
    output = []
    in_root = True
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("["):
            if in_root:
                output.extend(f"{key} = {value}\n" for key, value in remaining.items())
                remaining.clear()
            in_root = False
        elif in_root and "=" in stripped and not stripped.startswith(("#", ";")):
            key = stripped.split("=", 1)[0].strip()
            if key in remaining:
                output.append(f"{key} = {remaining.pop(key)}\n")
                continue
        output.append(line if line.endswith("\n") else line + "\n")
    output.extend(f"{key} = {value}\n" for key, value in remaining.items())
    return output


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    source, destination, *assignments = sys.argv[1:]
    updates = {}
    for assignment in assignments:
        if "=" not in assignment:
            print(f"not a key=value pair: {assignment}", file=sys.stderr)
            return 2
        key, value = assignment.split("=", 1)
        updates[key.strip()] = value.strip()
    with open(source, encoding="utf-8") as f:
        lines = f.readlines()
    with open(destination, "w", encoding="utf-8") as f:
        f.writelines(set_keys(lines=lines, updates=updates))
    return 0


if __name__ == "__main__":
    sys.exit(main())
