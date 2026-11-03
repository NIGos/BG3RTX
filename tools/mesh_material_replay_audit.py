"""Read-only hit-material pack inventory and conservative captured-row preservation gate.

Active published rows are a superset of rows that qualify at hits. Preserving their
bytes is necessary, but does not prove equal evaluated materials or scene energy.
No geometry hash/resource identity is present in these pack tables.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

MATERIAL_ROWS = 8192 + 4096
POOL_BASE = 32 + MATERIAL_ROWS * 256
HM_SLOT = 448 * 64


def active_rows(raw):
    if len(raw) < 16:
        raise ValueError("hit row table has no header")
    frame = struct.unpack_from("<I", raw)[0]
    result = {}
    for kind, start, count in (("captured", 16, MATERIAL_ROWS), ("pool", POOL_BASE, 4096)):
        for slot in range(count):
            at = start + slot * 256
            if at + 256 > len(raw):
                break
            row = raw[at:at + 256]
            ordinal, epoch = struct.unpack_from("<I", row)[0], struct.unpack_from("<I", row, 28)[0]
            if ordinal == slot + 1 and epoch == frame:
                result[f"{kind}:{slot}"] = (at, row)
    return result


def preservation(before, after):
    """Compare every previously active row at its original address; never relabel a changed row."""
    active = active_rows(before)
    changed = [name for name, (at, row) in active.items() if after[at:at + 256] != row]
    header_equal = before[:16] == after[:16]
    return {"passed": not changed and header_equal, "scope": "all previously active captured/pool rows",
            "active_rows_compared": len(active), "changed_rows": changed,
            "header_equal": header_equal, "evaluated_energy_proved": False}


def audit(pack, candidate=None):
    pack = Path(pack)
    if not (pack / "manifest.txt").is_file():
        raise ValueError("not a replay pack: manifest.txt missing")
    info = (pack / "s1b7.bin").read_bytes()
    if len(info) % 16:
        raise ValueError("InstInfo table size is not a multiple of 16")
    rows = (pack / "s1b12.bin").read_bytes()
    classes = {}
    for base, meta, record, tris in struct.iter_unpack("<4I", info):
        kind = "pool with row" if meta & (1 << 8) and meta & (1 << 15) else "pool without row" if meta & (1 << 8) else "soup"
        bucket = classes.setdefault(kind, {"instances": 0, "triangles": 0})
        bucket["instances"] += 1
        bucket["triangles"] += tris
    active = active_rows(rows)
    result = {"pack": pack.name, "frame": struct.unpack_from("<I", rows)[0],
              "instances": len(info) // 16, "geometry_inventory_not_hit_weighted": classes,
              "row_table_sha256": hashlib.sha256(rows).hexdigest(),
              "active_rows": len(active),
              "active_rows_sha256": hashlib.sha256(b"".join(row for _, row in active.values())).hexdigest(),
              "stable_mesh_identity_in_pack": False,
              "limitations": ["No mesh resource IDs/content fingerprints in InstInfo or hit rows",
                              "Active rows are a conservative superset of qualified rows",
                              "No new pak-derived rows or evaluated-energy proof"]}
    stats = pack / "replay" / "rtstats.bin"
    if stats.exists():
        raw = stats.read_bytes()
        n = min(result["instances"], 1024)
        if len(raw) >= 4 * (HM_SLOT + n * 4):
            values = struct.unpack_from(f"<{n * 4}I", raw, HM_SLOT * 4)
            sums = [sum(values[i::4]) for i in range(4)]
            result["existing_hm_blockers"] = dict(zip(("qualified", "base", "row", "other"), sums))
            result["existing_hm_blockers"]["available"] = sum(sums) > 0
            result["existing_hm_blockers"]["instance_limit"] = 1024
    log = pack / "replay" / "replay.txt"
    if log.exists():
        match = re.search(r"hit material \(\d+ frames\): attempted (\d+) qualified (\d+).*?without a material record (\d+)", log.read_text(errors="replace"))
        if match:
            attempted, qualified, base = map(int, match.groups())
            result["historical_replay_not_current_presets"] = {"attempted": attempted, "qualified": qualified,
                "qualified_percent": 100 * qualified / max(attempted, 1), "base_missing": base}
    if candidate is not None:
        result["preservation"] = preservation(rows, (Path(candidate) / "s1b12.bin").read_bytes())
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack", type=Path, action="append", required=True)
    parser.add_argument("--candidate-root", type=Path, help="mirror root of candidate pack tables; input only")
    parser.add_argument("--out", type=Path, required=True, help="new report outside source replay packs")
    args = parser.parse_args()
    out = args.out.resolve()
    if any(out.is_relative_to(p.resolve()) for p in args.pack):
        parser.error("report must be outside every read-only source pack")
    reports = [audit(p, args.candidate_root / p.name if args.candidate_root else None) for p in args.pack]
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({"status": "INVENTORY_ONLY" if not args.candidate_root else "PRESERVATION_CHECK", "packs": reports}, indent=2) + "\n")
    print(f"audited {len(reports)} packs; {sum(r['active_rows'] for r in reports)} active rows; report {out}", flush=True)
    if any(not r.get("preservation", {"passed": True})["passed"] for r in reports):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
