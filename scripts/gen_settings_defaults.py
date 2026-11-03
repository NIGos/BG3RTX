"""Regenerates the defaults table of common/settings.h from bg3rtx.ini and data/release.ini.

common/settings.h is the single source of every default (docs/SETTINGS_DESIGN.md
"Defaults: one source"): kDefaults holds every key bg3rtx.ini sets with its value, and
kReleaseDefaults the keys data/release.ini switches off for a player package. The layer's
cfg_int/cfg_float fall back to them for a key no ini layer has. tests/settings_test.cpp
asserts that the files and the tables agree exactly; after editing either ini, run

    python scripts/gen_settings_defaults.py          # rewrite the block in common/settings.h
    python scripts/gen_settings_defaults.py --check  # exit 1 when the block is stale

The ini rules are those of settings::parse_ini: key=value lines, '#' starts a comment,
keys and values trimmed, the last occurrence of a key wins (a duplicate is reported).
"""
from __future__ import annotations

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "common", "settings.h")
BEGIN = "// BEGIN GENERATED DEFAULTS"
END = "// END GENERATED DEFAULTS"


def parse_ini(path: str) -> list[tuple[str, str]]:
    out: dict[str, str] = {}
    order: list[str] = []
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            s = line.split("#", 1)[0]
            if "=" not in s:
                continue
            k, v = s.split("=", 1)
            k, v = k.strip(), v.strip()
            if not k:
                continue
            if k in out:
                print(f"{os.path.relpath(path, ROOT)}:{n}: duplicate key {k} (the last one wins)", file=sys.stderr)
            else:
                order.append(k)
            out[k] = v
    return [(k, out[k]) for k in order]


def c_str(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def table(name: str, kvs: list[tuple[str, str]], source: str) -> str:
    # No key count here: it changed with every added key on one line all branches touch (a merge conflict on
    # every branch that adds a key, 2026-11-02). sizeof(table) / sizeof(KV) gives it where needed.
    lines = [f"// The keys of {source}, in file order.", f"inline constexpr KV {name}[] = {{"]
    for k, v in kvs:
        lines.append(f"    {{{c_str(k)}, {c_str(v)}}},")
    lines.append("};")
    return "\n".join(lines)


def block() -> str:
    dev = parse_ini(os.path.join(ROOT, "bg3rtx.ini"))
    rel = parse_ini(os.path.join(ROOT, "data", "release.ini"))
    return "\n".join([
        BEGIN + " (scripts/gen_settings_defaults.py; edit the ini files, then run it)",
        table("kDefaults", dev, "bg3rtx.ini"),
        table("kReleaseDefaults", rel, "data/release.ini"),
        END,
    ])


def main() -> int:
    with open(HEADER, encoding="utf-8", newline="") as f:
        text = f.read()
    nl = "\r\n" if "\r\n" in text else "\n"
    a = text.find(BEGIN)
    b = text.find(END)
    if a < 0 or b < a:
        print(f"{HEADER}: no '{BEGIN}' ... '{END}' block", file=sys.stderr)
        return 2
    b += len(END)
    new = text[:a] + block().replace("\n", nl) + text[b:]
    if "--check" in sys.argv[1:]:
        if new != text:
            print("common/settings.h defaults are stale: run python scripts/gen_settings_defaults.py")
            return 1
        print("common/settings.h defaults match bg3rtx.ini and data/release.ini")
        return 0
    if new != text:
        with open(HEADER, "w", encoding="utf-8", newline="") as f:
            f.write(new)
        print("updated common/settings.h")
    else:
        print("common/settings.h already up to date")
    return 0


if __name__ == "__main__":
    sys.exit(main())
