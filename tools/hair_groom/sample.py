"""Representative styles: python -m tools.hair_groom.sample --state state.json --reach reach.json --logs *.log --out names.txt."""
from __future__ import annotations

import argparse
import collections
import glob
import json
import re
from pathlib import Path

COMPANIONS = ("shadowheart", "astarion", "gale", "laezel", "karlach", "wyll", "halsin", "jaheira", "minthara", "minsc")
CLASSES = ("coverage", "silhouette", "radiance", "roots away", "root channel", "mirrored UV", "error")
DRAW = re.compile(r"\[hair groom\]\s+(?:unmatched hair draw:\s*|UV join\s+)(\d+)\s+indices\b", re.I)
UPLOAD = re.compile(r"\[hair groom\]\s+([0-9a-f]{16})\s+LOD\s+\d+\s+uploaded:", re.I)


def log_draws(lines):
    """Index counts and content IDs witnessed by draws, not the startup asset catalogue."""
    indices, ids = set(), set()
    for line in lines:
        m = DRAW.search(line)
        if m:
            indices.add(int(m[1]))
        m = UPLOAD.search(line)
        if m:
            ids.add(m[1].lower())
    return indices, ids


def failure_class(row):
    reasons = row.get("reasons", [])
    if not reasons:
        return None
    first = reasons[0].lower()
    for label in ("coverage", "silhouette", "radiance", "root channel", "mirrored UV"):
        if label.lower() in first:
            return label
    if "root" in first and "away" in first:
        return "roots away"
    return "error"


def evenly_spaced(names, n):
    names = sorted(names)
    if n <= 0:
        return []
    if len(names) <= n:
        return names
    if n == 1:
        return [names[len(names) // 2]]
    return [names[i * (len(names) - 1) // (n - 1)] for i in range(n)]


def pick_classes(state, reach, n=3):
    if n < 0:
        raise ValueError("per-class count must be nonnegative")
    reachable = {r["name"] for r in reach["rows"] if r["category"].lower() not in ("unused", "cv only")}
    groups = collections.defaultdict(list)
    for name in sorted(reachable & state.keys()):
        label = failure_class(state[name])
        if label:
            groups[label].append(name)
    picked = {}
    for label in CLASSES:
        names = groups[label]
        lod0 = [name for name in names if state[name].get("lod") == 0]
        other = [name for name in names if state[name].get("lod") != 0]
        # Spend the quota on LOD0 first; a smaller class still contributes its available LOD1s.
        picked[label] = set(evenly_spaced(lod0, n) + evenly_spaced(other, max(0, n - len(lod0))))
    return picked


def select(state, reach, indices, ids, n=3):
    by_count = {name for name, row in state.items() if row.get("triangles", -1) * 3 in indices}
    assets = {i.lower() + ".hgrm" for i in ids}
    by_id = {name for name, row in state.items() if str(row.get("asset", "")).lower() in assets}
    companions = {r["name"] for r in reach["rows"] if r["name"] in state and
                  (r["category"].lower() == "companion" or any(c in r["name"].lower() for c in COMPANIONS))}
    classes = pick_classes(state, reach, n)
    sources = {"logs": by_count | by_id, "companions": companions, "failure classes": set().union(*classes.values())}
    selected = set().union(*sources.values())
    paths = {state[name]["gr2"] for name in selected if state[name].get("lod") in (0, 1) and state[name].get("gr2")}
    siblings = {name for name, row in state.items() if row.get("gr2") in paths and row.get("lod") in (0, 1)}
    sources["LOD siblings added"] = siblings - selected
    return sorted(selected | siblings), sources, classes


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--state", type=Path, required=True)
    ap.add_argument("--reach", type=Path, required=True)
    ap.add_argument("--logs", nargs="+", required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--per-class", type=int, default=3)
    a = ap.parse_args()
    if a.per_class < 0:
        ap.error("--per-class must be nonnegative")
    paths = set()
    # PowerShell leaves wildcard arguments unexpanded for native commands.
    for pattern in a.logs:
        matches = glob.glob(pattern)
        if not matches:
            ap.error(f"no log files match {pattern}")
        paths.update(Path(p) for p in matches)
    indices, ids = set(), set()
    for path in sorted(paths):
        with path.open(encoding="utf-8", errors="replace") as f:
            counts, uploaded = log_draws(f)
        indices.update(counts)
        ids.update(uploaded)
    state = json.loads(a.state.read_text(encoding="utf-8"))
    reach = json.loads(a.reach.read_text(encoding="utf-8"))
    names, sources, classes = select(state, reach, indices, ids, a.per_class)
    a.out.write_text("".join(name + "\n" for name in names), encoding="utf-8")
    for source, selected in sources.items():
        print(f"{source}: {len(selected)}")
    for label, selected in classes.items():
        print(f"  {label}: {len(selected)}")
    print(f"total: {len(names)} (sources overlap; {len(paths)} logs)")


if __name__ == "__main__":
    main()
