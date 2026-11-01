"""Fold branch progress fragments into docs/PROGRESS_CODEX.md (run by the coordinator after merges).

Branches do not edit docs/PROGRESS_CODEX.md (28 commits touched it in one day and every merge
conflicted there). A branch writes its entry to docs/progress/<branch>.md instead, one file per
branch ("/" in the branch name becomes "-"), in the PROGRESS entry format:

    ## <what changed> (branch <name>, <status>, <date>)

    - what changed; the key numbers before/after; kept/rejected/paused; how the user checks it.

A fragment holds one or more "## " sections, no log dumps, at most MAX_LINES lines. Merging
it never conflicts (each branch owns its file). After merging, the coordinator runs

    py -3 scripts/fold_progress.py            # fold every fragment, then delete the fragment files
    py -3 scripts/fold_progress.py --check    # only validate the fragments (exit 1 on a problem)
    py -3 scripts/fold_progress.py --dry-run  # print the folded PROGRESS, change nothing
    py -3 scripts/fold_progress.py --main abc1234   # also set "main <sha>" in the title line

Sections go to the top of PROGRESS (below a leading user-decision section, which stays first),
newest date first. The coordinator then trims PROGRESS to ~150 lines by hand.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAX_LINES = 40
_DATE = re.compile(r"\b(20\d\d-\d\d-\d\d)\b")
_DECISION = re.compile(r"^## .*(question|decision)", re.I)


def fragments(folder):
    return sorted(p for p in folder.glob("*.md") if p.name.lower() != "readme.md")


def problems(path, text):
    out = []
    lines = text.strip().splitlines()
    if not lines or not lines[0].startswith("## "):
        out.append(f"{path.name}: must start with a '## ' section heading")
    if len(lines) > MAX_LINES:
        out.append(f"{path.name}: {len(lines)} lines (at most {MAX_LINES}: keep entries short, no log dumps)")
    if any(l.startswith(("<<<<<<<", ">>>>>>>", "=======")) for l in lines):
        out.append(f"{path.name}: conflict markers")
    if any(l.startswith("# ") for l in lines):
        out.append(f"{path.name}: a '# ' title line (only PROGRESS has the title)")
    return out


def _sort_key(item):
    path, text = item
    m = _DATE.search(text.splitlines()[0]) if text.strip() else None
    return (m.group(1) if m else "", path.name)


def fold(progress_text, items, main=None):
    """PROGRESS text with the fragments' sections inserted at the top."""
    lines = progress_text.splitlines()
    if main and lines:
        lines[0] = re.sub(r"\bmain [0-9a-f]{6,40}\b", f"main {main}", lines[0])
    heads = [i for i, l in enumerate(lines) if l.startswith("## ")]
    at = heads[0] if heads else len(lines)
    if heads and _DECISION.search(lines[heads[0]]):
        at = heads[1] if len(heads) > 1 else len(lines)
    block = []
    for _, text in sorted(items, key=_sort_key, reverse=True):
        block += text.strip().splitlines() + [""]
    if at == len(lines) and lines and lines[-1].strip():
        block = [""] + block
    out = lines[:at] + block + lines[at:]
    return "\n".join(out).rstrip("\n") + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=str(ROOT), help="repository root (tests use a scratch copy)")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--main", default="", help="main commit id for the title line")
    a = ap.parse_args(argv)
    root = Path(a.root)
    folder = root / "docs" / "progress"
    progress = root / "docs" / "PROGRESS_CODEX.md"
    paths = fragments(folder) if folder.exists() else []
    items = [(p, p.read_text(encoding="utf-8")) for p in paths]
    errs = [e for p, t in items for e in problems(p, t)]
    for e in errs:
        print(e)
    if errs:
        return 1
    if a.check:
        print(f"{len(items)} fragment(s) ok")
        return 0
    if not items and not a.main:
        print("no fragments")
        return 0
    text = fold(progress.read_text(encoding="utf-8"), items, a.main or None)
    if a.dry_run:
        sys.stdout.write(text)
        return 0
    progress.write_text(text, encoding="utf-8", newline="\n")
    for p, _ in items:
        p.unlink()  # one file at a time; the folder and its README stay
    print(f"folded {len(items)} fragment(s) into {progress.relative_to(root)}: "
          + ", ".join(p.name for p, _ in items))
    return 0


if __name__ == "__main__":
    sys.exit(main())
