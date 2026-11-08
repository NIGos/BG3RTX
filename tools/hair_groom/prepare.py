"""First-run preparation of the strand grooms (docs/RTX_HAIR_ROADMAP.md 2b): conversion order and launcher status.

python -m tools.hair_groom.prepare status --report-dir <batch report dir> [--out-dir data/assetdb/hair_grooms]
    [--seen <hair_seen.txt>] [--all] [--jobs J] [--json <preparation.json>]
python -m tools.hair_groom.prepare order --report-dir <dir> [--seen <file>] [--all]   (the batch's --names list)

Prototype, read-only: it never converts. `order` prints the style names in the needed-first order (feed it to
`python -m tools.hair_groom.batch --names`); `status` writes the one JSON block the launcher's Preparation card
reads (styles ready / needed, size on disk, ETA). Tiers, first to last:
  0 seen     styles the layer saw this or an earlier session without a groom (hair_seen.txt: one index id per line)
  1 companion the origin companions' hair and beards (reach category "companion")
  2 player   character creation (the player's own and hireling looks)
  3 named    named NPCs (S_ templates)
  4 hat      hair drawn under a hat or helmet
  5 other    generic NPCs, items and scenery
  6 unused   no game file references it (only with --all)
Inside a tier LOD 0 goes before LOD 1 (LOD 1 is the distance mesh; until it exists the cards are drawn there).
"""
from __future__ import annotations

import argparse
import json
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TIERS = ("seen", "companion", "player", "named", "hat", "other", "unused")
_CATEGORY_TIER = {"companion": 1, "player": 2, "named NPC": 3, "hat variant": 4, "generic NPC": 5, "item": 5,
                  "CV only": 6, "unused": 6}
SECONDS_PER_STYLE = 27.0   # one job, batch 2026-11-07 (used until the report has its own timings)


def seen_keys(row):
    """The seen-list tokens a row answers to: its index id, and its index count (for draws whose id is unknown)."""
    keys = set()
    if row.get("index_id"):
        keys.add("id:" + str(row["index_id"]).lower())
    if row.get("indices"):
        keys.add(f"n:{int(row['indices'])}")
    return keys


def tier(row, seen_ids):
    """Tier index of one batch/reach row: {"name", "lod", "category"?, "index_id"?, "indices"?}."""
    if seen_ids and seen_keys(row) & seen_ids:
        return 0
    return _CATEGORY_TIER.get(row.get("category") or "", 5)


def unresolved(rows, seen_ids):
    """Seen tokens no row answers to (their styles need the GR2 decode to be named)."""
    known = set()
    for r in rows:
        known |= seen_keys(r)
    return set(seen_ids) - known


def order(rows, seen_ids=frozenset(), convert_all=False):
    """Rows in conversion order (tier, LOD, name); unused ones only with convert_all. Stable and deterministic."""
    keyed = [(tier(r, seen_ids), int(r.get("lod") or 0), r["name"], r) for r in rows]
    keep = [k for k in keyed if convert_all or k[0] < TIERS.index("unused")]
    return [k[3] for k in sorted(keep, key=lambda k: k[:3])]


TIER_TEXT = {"seen": "styles seen in game", "companion": "companions", "player": "character creation looks",
             "named": "named characters", "hat": "hair under hats", "other": "other characters", "unused": "unused styles"}


def status(rows, done, sizes, seen_ids=frozenset(), convert_all=False, jobs=4, seconds=None, running=False, paused=False,
           eta_s=None):
    """The launcher's Preparation block for hair (written by the batch as <report-dir>/preparation.json).

    rows: every catalogue row; done: {name: verdict} of the rows the batch already processed (pass, needs-review,
    reject); sizes: bytes of each groom file on disk. A processed row is finished whatever its verdict (a reject keeps
    its cards); ready counts the rows with a groom. ETA: `eta_s` (the running batch's rate) or remaining rows x
    seconds per style / jobs."""
    needed = order(rows, seen_ids, convert_all)
    left = [r for r in needed if r["name"] not in done]
    per = statistics.median(seconds) if seconds else SECONDS_PER_STYLE
    if eta_s is None:
        eta_s = len(left) * per / max(jobs, 1)
    ready = sum(1 for r in needed if done.get(r["name"]) == "pass")
    tiers = []
    for i, name in enumerate(TIERS):
        members = [r for r in needed if tier(r, seen_ids) == i]
        if members:
            tiers.append({"tier": name, "total": len(members), "finished": sum(1 for r in members if r["name"] in done)})
    first = next((t["tier"] for t in tiers if t["finished"] < t["total"]), None)
    size = int(sum(sizes))
    state = "paused" if paused else "running" if running else "done" if not left else "waiting"
    return {
        "kind": "hair_grooms",
        "ready": ready,
        "finished": len(needed) - len(left),
        "needed": len(needed),
        "all": len(rows),
        "convert_all": bool(convert_all),
        "bytes": size,
        "eta_s": int(round(eta_s)),
        "state": state,
        "next_tier": first,
        "tiers": tiers,
        "line": line(ready, len(needed), size),
        "detail": detail(state, first, len(left), eta_s),
    }


def _size(size):
    return f"{size / 1e9:.1f} GB" if size >= 1e8 else f"{size / 1e6:.0f} MB"


def line(ready, needed, size):
    """The status line of the Preparation card (plain, short)."""
    return f"{ready} of {needed} styles ready, {_size(size)} on disk."


def detail(state, tier_name, left, eta_s):
    """The second line: what happens now."""
    if not left:
        return "All needed styles are done."
    eta = f"about {max(1, round(eta_s / 60))} min left" if eta_s < 5400 else f"about {eta_s / 3600:.1f} h left"
    what = TIER_TEXT.get(tier_name or "", "styles")
    if state == "running":
        return f"Converting {what}, {eta}."
    if state == "paused":
        return f"Paused. {left} styles left, {eta}."
    return f"{left} styles left, {eta}."


def parse_seen(text):
    """The layer's hair_seen.txt (layer/hair_groom.cpp note_seen): "<index count> <index id hex>" per line, id 0 when
    the draw's index buffer is not host data (then the count names the candidates). '#' lines are comments."""
    out = set()
    for line in text.splitlines():
        parts = line.split("#", 1)[0].split()
        if len(parts) < 2 or not parts[0].isdigit():
            continue
        iid = parts[1].lower()
        if iid.strip("0"):
            out.add("id:" + iid.rjust(16, "0"))
        else:
            out.add(f"n:{int(parts[0])}")
    return frozenset(out)


def read_seen(path):
    if not path or not Path(path).is_file():
        return frozenset()
    return parse_seen(Path(path).read_text(encoding="utf-8", errors="replace"))


def _rows_from_report(report_dir):
    """Catalogue rows from the batch state (+ reach.json categories when present in the report dir)."""
    state = json.loads((Path(report_dir) / "state.json").read_text(encoding="utf-8"))
    reach = {}
    rp = Path(report_dir) / "reach.json"
    if rp.is_file():
        reach = json.loads(rp.read_text(encoding="utf-8")).get("categories", {})
    rows, done, secs = [], {}, []
    for name, r in state.items():
        rows.append({"name": name, "lod": r.get("lod", 0), "category": reach.get(name), "indices": r.get("indices"),
                     "index_id": r.get("index_id") or (r.get("asset") or "")[:16] or None})
        if r.get("verdict"):
            done[name] = r["verdict"]
        if r.get("seconds"):
            secs.append(float(r["seconds"]))
    return rows, done, secs


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("cmd", choices=("status", "order"))
    ap.add_argument("--report-dir", required=True, type=Path)
    ap.add_argument("--out-dir", type=Path, default=ROOT / "data" / "assetdb" / "hair_grooms")
    ap.add_argument("--seen", type=Path)
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--json", type=Path)
    a = ap.parse_args(argv)
    rows, done, secs = _rows_from_report(a.report_dir)
    seen = read_seen(a.seen)
    if a.cmd == "order":
        for r in order(rows, seen, a.all):
            print(r["name"])
        return 0
    sizes = [p.stat().st_size for p in a.out_dir.glob("*.hgrm")] if a.out_dir.is_dir() else []
    s = status(rows, done, sizes, seen, a.all, a.jobs, secs)
    text = json.dumps(s, indent=1)
    if a.json:
        a.json.write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
