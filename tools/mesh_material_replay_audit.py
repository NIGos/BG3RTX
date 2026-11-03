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


def pak_index(summary):
    """Index local candidates only; their SHA selects a full byte comparison."""
    summary = Path(summary)
    doc = json.loads(summary.read_text(encoding='utf-8'))
    result = {}
    for batch in doc['batches']:
        data = json.loads((summary.parent / batch).read_text(encoding='utf-8'))
        for row in data['rows']:
            result.setdefault((row['index_count'], row['geometry_sha256']), []).append(row)
    return result


def pak_geometry(pack, index):
    """Exact ordered object-space position/UV candidates, never qualified joins.

    Old packs have no game resource/UUID bridge or native COLOR streams. Soup is
    camera-relative and is excluded. Sparse pool packs remain unsupported here.
    """
    pack = Path(pack)
    report = dict(scope='pool only; bit-exact ordered position3/UV2; not hit weighted',
        examined=0, no_uv=0, unsupported=0, missing=0, unique_candidate=0, ambiguous=0,
        candidate_triangles=0, proven_joined_rows=0, joined_coverage_estimate=None,
        reason='No native texture UUID-to-view bridge or per-draw COLOR/cull/basis proof in these packs')
    pool = None
    for line in (pack/'manifest.txt').read_text().splitlines():
        fields = line.split()
        if fields[:3] == ['buffer', '1', '8'] and len(fields) == 5:
            pool = pack/fields[4]
        if fields[:3] == ['range', '1', '8']:
            report['unsupported_pool_ranges'] = True
            return report
    if pool is None:
        report['pool_missing'] = True
        return report
    import numpy as np
    size = pool.stat().st_size
    with pool.open('rb') as stream:
        for base, meta, record, tris in struct.iter_unpack('<4I', (pack/'s1b7.bin').read_bytes()):
            if not meta & (1 << 8):
                continue
            stride = meta & 255
            if stride < 24:
                report['no_uv'] += 1
                continue
            if meta & (1 << 9) or not tris or base + tris*3*stride > size:
                report['unsupported'] += 1
                continue
            report['examined'] += 1
            stream.seek(base)
            raw = stream.read(tris*3*stride)
            corners = np.empty((tris*3, 5), dtype='<f4')
            corners[:, :3] = np.ndarray((tris*3, 3), '<f4', raw, strides=(stride, 4))
            corners[:, 3:] = np.ndarray((tris*3, 2), '<f4', raw, offset=16, strides=(stride, 4))
            full = corners.tobytes()
            candidates = index.get((tris*3, hashlib.sha256(full).hexdigest()), [])
            exact = [r for r in candidates if Path(r['full_geometry_cache']).read_bytes() == full]
            contracts = {(r['material_id'], r.get('two_sided'), r.get('uses_vertex_color'),
                r.get('color_layout', 0), r.get('color_sha256'),
                Path(r.get('template', '')).name == 'Engine_Default_HLOD_GM_VT.lsf') for r in exact}
            if not exact:
                report['missing'] += 1
            elif len(contracts) > 1:
                report['ambiguous'] += 1
            else:
                report['unique_candidate'] += 1
                report['candidate_triangles'] += tris
    return report


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
    parser.add_argument('--pak-summary', type=Path, help='completed offline batch summary; exact geometry candidates only')
    args = parser.parse_args()
    out = args.out.resolve()
    if any(out.is_relative_to(p.resolve()) for p in args.pack):
        parser.error("report must be outside every read-only source pack")
    reports = [audit(p, args.candidate_root / p.name if args.candidate_root else None) for p in args.pack]
    if args.pak_summary:
        index = pak_index(args.pak_summary)
        for pack, report in zip(args.pack, reports):
            report['pak_geometry_candidates'] = pak_geometry(pack, index)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({"status": "INVENTORY_ONLY" if not args.candidate_root else "PRESERVATION_CHECK", "packs": reports}, indent=2) + "\n")
    print(f"audited {len(reports)} packs; {sum(r['active_rows'] for r in reports)} active rows; report {out}", flush=True)
    if any(not r.get("preservation", {"passed": True})["passed"] for r in reports):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
