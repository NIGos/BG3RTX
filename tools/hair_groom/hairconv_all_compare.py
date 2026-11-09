"""Equivalence of bg3rtx_hairconv --all (native paks + the player's granny2.dll, docs/RTX_HAIR_ROADMAP.md 2f) with the
Python batch (tools/hair_groom/batch.py: LSLib + granny2 under PowerShell 7). Dev tool, read-only on the game install.

python -m tools.hair_groom.hairconv_all_compare --state <batch report>/state.json --cache <batch report>/cache
    --game <BG3 Data> --granny <granny2.dll> --work <scratch dir> [--sample 24] [--names <file>]
  1. catalogue: the native catalogue (<work>/report/all_catalogue.tsv) against batch.styles() on the mesh_materials
     catalogue (--catalogue, default the newest data/assetdb/mesh_materials*v2*.json): the same rows.
  2. GR2: --dump-gr2 of the sample's GR2 files, byte-identical to the batch's cached LSLib dumps (<cache>/gr2).
  3. masks: the exported .hcap masks (--export-hcap) against Pillow's decode of the same DDS.
  4. conversion: the --all records against the batch's state.json: the same index id (join key), the same verdict,
     strand counts within the dedupe-tie tolerance (pairs at the 0.5 mm threshold may flip: 3 of 125,678 measured in
     2c, so 0.01% of the strands, at least 3).
Writes <work>/compare.json and prints a summary. Outputs stay under --work (never data/assetdb).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import time
from io import BytesIO
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def sample_names(state, n):
    """n styles spread over the strand-count distribution, both LODs and all verdicts (deterministic)."""
    recs = sorted((r for r in state.values() if r.get("strands")), key=lambda r: (r["strands"], r["name"]))
    picked = {recs[round(i * (len(recs) - 1) / max(n - 1, 1))]["name"] for i in range(n)}
    by_name = sorted(state.values(), key=lambda r: r["name"])
    for v in ("needs-review", "reject"):   # at least two of each non-pass verdict
        picked |= set([r["name"] for r in by_name if r["verdict"] == v and r.get("strands")][:2])
    return sorted(picked)


def _stable_reference(args):
    """hcap.reference on one exported input with numpy's argsort made stable. strands2d.trace links runs in
    np.argsort(dist, axis=None) order, numpy's default (non-stable, SIMD-dispatched) quicksort: equal distances (centres
    on the same half-pixel, common on real masks) are linked in an implementation-defined order. The native tracer breaks
    them by flat index, which is the stable order; with it both sides must agree exactly."""
    path, out_dir = args
    import numpy as np
    sys.path.insert(0, str(ROOT))
    base = np.argsort
    if not getattr(base, "_stable", False):
        def stable(a, axis=-1, kind=None, order=None, **kw):
            return base(a, axis=axis, kind="stable" if kind is None else kind, order=order, **kw)
        stable._stable = True
        np.argsort = stable
    from tools.hair_groom import hcap
    try:
        return hcap.reference(path, out_dir)
    except Exception as ex:  # noqa: BLE001
        return {"error": f"{type(ex).__name__}: {ex}"}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--state", required=True, type=Path)
    ap.add_argument("--cache", required=True, type=Path)
    ap.add_argument("--game", required=True)
    ap.add_argument("--granny", required=True)
    ap.add_argument("--work", required=True, type=Path)
    ap.add_argument("--catalogue", type=Path)
    ap.add_argument("--exe", type=Path, default=ROOT / "bin" / "bg3rtx_hairconv.exe")
    ap.add_argument("--sample", type=int, default=24)
    ap.add_argument("--names", type=Path)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--reference", action="store_true",
                    help="also run the Python reference (stable tie order) on the exported inputs: exact equality")
    a = ap.parse_args(argv)
    sys.path.insert(0, str(ROOT))
    import numpy as np
    from PIL import Image
    from tools.assets.lspk import PackageSet
    from tools.hair_groom import batch, hcap
    state = json.loads(a.state.read_text(encoding="utf-8"))
    names = (a.names.read_text(encoding="utf-8").split() if a.names else sample_names(state, a.sample))
    a.work.mkdir(parents=True, exist_ok=True)
    (a.work / "names.txt").write_text("\n".join(names) + "\n", encoding="utf-8")
    report, out, export = a.work / "report", a.work / "grooms", a.work / "hcap"
    result = {"names": names}
    # 4 (run first: it also builds the catalogue and exports the inputs)
    t0 = time.monotonic()
    cmd = [str(a.exe), "--all", "--game", a.game, "--granny", a.granny, "--out", str(out), "--report-dir", str(report),
           "--names", str(a.work / "names.txt"), "--jobs", str(a.jobs), "--threads", str(a.threads), "--export-hcap", str(export),
           "--json", str(a.work / "records.json")]
    print("[compare] " + " ".join(cmd), flush=True)
    r = subprocess.run(cmd)
    result["run_seconds"] = round(time.monotonic() - t0, 1)
    if r.returncode:
        print(f"[compare] bg3rtx_hairconv --all failed: exit {r.returncode}")
        return 1
    native = {}
    for line in (report / "all_state.json").read_text(encoding="utf-8").splitlines():
        n, _, rec = line.split("\t", 2)
        native[n] = json.loads(rec)
    # 1. catalogue
    cat_native = []
    for line in (report / "all_catalogue.tsv").read_text(encoding="utf-8").splitlines()[1:]:
        f = line.split("\t")
        cat_native.append({"name": f[0], "gr2": f[1], "mesh": f[2], "lod": int(f[3]), "mask": f[4], "visual": f[5], "category": f[6]})
    catalogue = a.catalogue or batch.find_catalogue()
    keys = ("name", "gr2", "mesh", "lod", "mask", "visual")
    if catalogue:
        py = batch.styles(catalogue)
        pyk = {tuple(s[k] for k in keys) for s in py}
        nk = {tuple(s[k] for k in keys) for s in cat_native}
        result["catalogue"] = {"python": len(py), "native": len(cat_native), "same": len(pyk & nk),
                               "only_python": sorted(x[0] for x in pyk - nk)[:20], "only_native": sorted(x[0] for x in nk - pyk)[:20]}
    from collections import Counter
    result["categories"] = dict(Counter(s["category"] for s in cat_native))
    # 2. GR2 dumps, 3. masks
    ps = PackageSet(a.game if Path(a.game, "Shared.pak").exists() else str(Path(a.game) / "Data"))
    gr2_same = gr2_total = mask_same = mask_total = 0
    gr2_diff, mask_diff = [], []
    for name in names:
        s = state[name]
        data = ps.read(s["gr2"])
        ref = a.cache / "gr2" / (hashlib.sha256(data).hexdigest() + ".bin")
        if ref.exists():
            mine = a.work / "dump.bin"
            subprocess.run([str(a.exe), "--dump-gr2", "--game", a.game, "--granny", a.granny, s["gr2"], str(mine)],
                           check=True, capture_output=True)
            gr2_total += 1
            same = mine.read_bytes() == ref.read_bytes()
            gr2_same += same
            if not same:
                gr2_diff.append(name)
        safe = "".join(ch if ch.isalnum() or ch in "-_" else "_" for ch in name)
        hp = export / f"{safe}.hcap"
        if hp.exists() and s.get("mask") in ps:
            _, mask, _, _, _ = hcap.read(hp)
            pil = np.asarray(Image.open(BytesIO(ps.read(s["mask"]))).convert("RGBA"))
            mask_total += 1
            same = pil.shape == mask.shape and bool((pil == mask).all())
            mask_same += same
            if not same:
                mask_diff.append(name)
    result["gr2"] = {"compared": gr2_total, "identical": gr2_same, "different": gr2_diff}
    result["masks"] = {"compared": mask_total, "identical": mask_same, "different": mask_diff}
    # 4. records
    rows, bad = [], []
    for name in names:
        p, n = state[name], native.get(name, {})
        pid = (p.get("asset") or "")[:16] or None
        if pid is None:   # the batch writes no asset for review/reject: the id from its cached LSLib dump
            ref = a.cache / "gr2" / (hashlib.sha256(ps.read(p["gr2"])).hexdigest() + ".bin")
            if ref.exists():
                from tools.hair_groom import asset, mesh
                pid = f"{asset.index_id(mesh.load_gr2_dump(ref, p['mesh']).tris):016x}"
        tol = max(3, round(1e-4 * (p.get("strands") or 0)))
        row = {"name": name, "verdict": [p["verdict"], n.get("verdict")], "strands": [p.get("strands"), n.get("strands")],
               "index_id": [pid, n.get("index_id")], "iou": [p.get("iou"), n.get("iou")]}
        row["ok"] = row["verdict"][0] == row["verdict"][1] and pid == n.get("index_id")
        row["strands_in_tolerance"] = abs((p.get("strands") or 0) - (n.get("strands") or 0)) <= tol
        rows.append(row)
        if not row["ok"]:
            bad.append(row)
    rel = sorted(abs((r["strands"][0] or 0) - (r["strands"][1] or 0)) / max(r["strands"][0] or 1, 1) for r in rows)
    result["records"] = {"compared": len(rows), "same_verdict_and_id": len(rows) - len(bad), "different": bad,
                         "strands_in_tolerance": sum(r["strands_in_tolerance"] for r in rows),
                         "strand_delta_max": max(abs((r["strands"][0] or 0) - (r["strands"][1] or 0)) for r in rows),
                         "strand_delta_rel_median": round(rel[len(rel) // 2], 5), "strand_delta_rel_max": round(rel[-1], 5),
                         "verdicts": dict(Counter(r["verdict"][0] for r in rows))}
    if a.reference:
        import concurrent.futures
        fields_ = ("verdict", "strands", "segments", "dedupe_dropped", "tile_grid", "iou", "coverage_delta", "radiance_delta")
        todo = []
        for name in names:
            safe = "".join(ch if ch.isalnum() or ch in "-_" else "_" for ch in name)
            if (export / f"{safe}.hcap").exists():
                todo.append((name, str(export / f"{safe}.hcap")))
        same, diff, start, last = 0, [], time.monotonic(), time.monotonic()
        with concurrent.futures.ProcessPoolExecutor(a.jobs) as pool:
            futs = {pool.submit(_stable_reference, (p, str(a.work / "pyref"))): n for n, p in todo}
            for k, f in enumerate(concurrent.futures.as_completed(futs)):
                n, ref = futs[f], f.result()
                d = {x: [ref.get(x), native[n].get(x)] for x in fields_ if ref.get(x) != native[n].get(x)}
                if ref.get("tile_grid") and native[n].get("tile_grid") and list(ref["tile_grid"]) == list(native[n]["tile_grid"]):
                    d.pop("tile_grid", None)
                if d:
                    diff.append({"name": n, **d})
                else:
                    same += 1
                if time.monotonic() - last >= 30:
                    el = time.monotonic() - start
                    print(f"[compare] reference {k + 1}/{len(todo)}, {el:.0f}s, ETA {el / (k + 1) * (len(todo) - k - 1):.0f}s, "
                          f"different {len(diff)}", flush=True)
                    last = time.monotonic()
        result["stable_reference"] = {"compared": len(todo), "identical": same, "different": diff}
    secs = [float(sum(json.loads(json.dumps(native[n].get("seconds", {}))).values())) for n in names if n in native]
    result["native_seconds_per_style"] = {"median": sorted(secs)[len(secs) // 2] if secs else None, "max": max(secs) if secs else None,
                                          "sum": round(sum(secs), 1)}
    (a.work / "compare.json").write_text(json.dumps(result, indent=1) + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k != "names"}, indent=1))
    # Strand counts against the batch's own run differ by its argsort tie order (see _stable_reference); the exact gate
    # is the stable reference when run, else the dedupe-tie tolerance.
    strands_ok = (not result["stable_reference"]["different"]) if a.reference else \
        result["records"]["strands_in_tolerance"] == len(rows)
    ok = (not bad and strands_ok and gr2_same == gr2_total and mask_same == mask_total and
          (not catalogue or (not result["catalogue"]["only_python"] and not result["catalogue"]["only_native"])))
    print("[compare] " + ("EQUIVALENT" if ok else "DIFFERENT"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
