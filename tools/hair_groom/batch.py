"""Batch conversion of every CHAR_Hair hairstyle into strand grooms, on the player's PC (rt_hair_groom).

python -m tools.hair_groom.batch --report-dir <dir> [--match Shadowheart Karlach ...] [--limit N] [--jobs J]
    [--out-dir data/assetdb/hair_grooms] [--catalogue <mesh_materials v2 json>] [--lslib <LSLib Tools dir>]

1. Enumerates the LOD0 and LOD1 objects whose material template is CHAR_Hair in the mesh_materials catalogue
   (python -m tools.assets.mesh_materials) and resolves each one's ID_Depth_Root_Alpha_MSKA texture.
2. Reads the GR2 and the mask from the installed paks (read-only) and decodes the GR2 with LSLib's reader
   (gr2_dump.ps1 under PowerShell 7; granny2.dll next to LSLib.dll, as in LSLib's ExportTool package), keeping
   the native vertex and index order (the join key).
3. Converts (tile grid detected, dedupe), then validates without an artist: UV consistency (cards in one tile,
   mixed winding, unmapped samples), root direction (mask root channel along the rows), and an offline render of
   cards vs groom (front/side/back, 2 mm pixels): silhouette IoU, coverage and mean radiance.
4. Verdict per style: pass (asset written), needs-review (asset written only with --accept-review), reject.
   Assets are named by the index-only content ID (common/hair_groom_asset.h); files are only added.
Incremental and resumable: <report-dir>/state.json keys every style by the SHA-256 of its GR2 + mask + mesh name
+ converter version; unchanged styles are skipped. Progress at least every 30 s. BelowNormal priority.
Larian-derived data is generated locally from the user's own installation and never shipped.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import glob
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import time
from io import BytesIO
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
VERSION = "hair-groom-batch-1"
# Verdict thresholds (derived from the Shadowheart reference: IoU 0.92-0.94 at 0.5 mm, 0.89-0.92 at 2 mm,
# unmapped samples 0.001%, root/row correlation 0.89-0.96). Cards spanning tiles are reported, not judged: the
# converter places the strands of every tile a card touches, and IoU / unmapped samples measure the result.
IOU_PASS, IOU_REJECT = 0.85, 0.70
ROOT_REJECT = 0.3
# Review: coverage or radiance off by more than the spread of the Shadowheart views (coverage +0.02..+0.05,
# radiance -3..+2% at 2 mm) with margin; a quarter of the cards with the root away from the head (bun or
# reversed cards: root/tip shading only). Unmapped samples are reported only: with several tiles per card the
# strands of the neighbouring tiles fall outside the card by construction.
COVER_REVIEW, RADIANCE_REVIEW, REVERSED_REVIEW = 0.08, 0.15, 0.25


def say(msg):
    print(msg, flush=True)


def find_catalogue():
    for base in (ROOT, ROOT.parents[2] if len(ROOT.parents) > 2 else ROOT):
        found = sorted(glob.glob(str(base / "data" / "assetdb" / "mesh_materials*v2*.json")))
        if found:
            return Path(found[-1])
    return None


def styles(catalogue, match=()):
    """[{key fields}] of the CHAR_Hair LOD0 objects, one per (GR2, mesh), with the mask texture path."""
    doc = json.loads(Path(catalogue).read_text(encoding="utf-8"))
    hair = {k for k, v in doc["materials"].items() if v["source"].endswith("/CHAR_Hair.lsf")}

    def mask_id(native):
        stack = [native]
        while stack:
            o = stack.pop()
            if isinstance(o, dict):
                if o.get("_") == "Texture2DParameters" and o.get("ParameterName") == "ID_Depth_Root_Alpha_MSKA":
                    return o.get("ID")
                stack.extend(o.values())
            elif isinstance(o, list):
                stack.extend(o)
        return None

    out, seen = [], set()
    for r in doc["rows"]:
        # LOD0 and LOD1: the game draws LOD1 at camp/exploration distances (2026-11-03 live: no LOD0 draw at camp).
        if r["lod"] > 1 or r["material_id"] not in hair:
            continue
        parts = r["object_id"].split(".")
        mesh = parts[1] if len(parts) >= 3 else ""
        if (r["source"], mesh) in seen or not mesh:
            continue
        if match and not any(m.lower() in r["source"].lower() for m in match):
            continue
        tid = mask_id(doc["materials"][r["material_id"]]["native"])
        tex = doc["textures"].get(tid or "", {}).get("native", {}).get("SourceFile", "")
        seen.add((r["source"], mesh))
        out.append({"name": Path(r["source"]).stem + ":" + mesh, "gr2": r["source"], "mesh": mesh, "lod": r["lod"],
                    "mask": tex.replace("\\", "/"), "visual": r["visual_id"]})
    return sorted(out, key=lambda s: s["name"])


def gr2_dump(lslib, gr2_path, out_bin):
    """tools/hair_groom/gr2_dump.ps1 (LSLib's GR2 reader under PowerShell 7)."""
    r = subprocess.run(["pwsh", "-NoProfile", "-NonInteractive", "-File", str(Path(__file__).with_name("gr2_dump.ps1")),
                        "-Lslib", str(lslib), "-Gr2", str(gr2_path), "-Out", str(out_bin)],
                       capture_output=True, text=True, timeout=300)
    if r.returncode or not Path(out_bin).exists():
        text = (r.stdout + r.stderr).strip().splitlines()
        raise RuntimeError("GR2 dump: " + (text[-1][:200] if text else f"exit {r.returncode}"))


def bbox_views(positions, pixel):
    """Front/side/back orthographic views framing the mesh (the face looks toward -z in BG3 exports)."""
    from . import render
    lo, hi = positions.min(0).astype(float), positions.max(0).astype(float)
    c = (lo + hi) / 2
    ext = hi - lo
    size = (int(max(ext[0], ext[2]) * 1.3 / pixel) + 8, int(ext[1] * 1.15 / pixel) + 8)
    up = np.array([0.0, 1.0, 0.0])
    return {"front": (render.View(c, np.array([0, 0, 1.0]), up, pixel, size), np.array([0.4, 0.5, -1.0])),
            "side": (render.View(c, np.array([-1.0, 0, 0]), up, pixel, size), np.array([0.8, 0.6, -0.3])),
            "back": (render.View(c, np.array([0, 0, -1.0]), up, pixel, size), np.array([-0.3, 0.6, 1.0]))}


def head_proxy(hair):
    names = hair.joint_names
    if "Head_M" in names:
        # Shadowheart: centre between Head_M and HeadEnd (+0.16 m up); without HeadEnd, Head_M + 0.08 m.
        centre = hair.joint_positions[names.index("Head_M")] + np.array([0.0, 0.08, 0.0])
        if "HeadEnd_M_endBone" in names:
            centre = (hair.joint_positions[names.index("Head_M")] + hair.joint_positions[names.index("HeadEnd_M_endBone")]) / 2
        return np.array(centre, float), np.array([0.066, 0.089, 0.083])
    return np.array([0.0, 1e4, 0.0]), np.array([1e-3, 1e-3, 1e-3])   # no head joints: no proxy


def process(style, game, lslib, out_dir, report_dir, accept_review):
    """Converts and validates one style; returns its report record (never raises)."""
    from PIL import Image
    from ..assets.lspk import PackageSet
    from . import analysis, asset, convert, mesh, render
    t0 = time.monotonic()
    rec = dict(style)
    try:
        ps = PackageSet(game)
        gr2 = ps.read(style["gr2"])
        if not style["mask"] or style["mask"] not in ps:
            raise RuntimeError(f"mask texture not found ({style['mask'] or 'no ID_Depth_Root_Alpha_MSKA'})")
        dds = ps.read(style["mask"])
        rec["key"] = hashlib.sha256(gr2 + dds + style["mesh"].encode() + VERSION.encode()).hexdigest()[:24]
        mask = np.asarray(Image.open(BytesIO(dds)).convert("RGBA"))
        with tempfile.TemporaryDirectory(prefix="b3hg-") as tmp:
            src = Path(tmp) / "in.gr2"
            src.write_bytes(gr2)
            gr2_dump(lslib, src, Path(tmp) / "out.bin")
            hair = mesh.load_gr2_dump(Path(tmp) / "out.bin", style["mesh"])
            # LOD1 cards merge several LOD0 cards (span several tiles): the atlas grid comes from the LOD0 mesh of the
            # same GR2 (same mask) when it exists.
            ref = hair
            if style.get("lod", 0) and style["mesh"].endswith("_LOD1"):
                try:
                    ref = mesh.load_gr2_dump(Path(tmp) / "out.bin", style["mesh"][:-5])
                except KeyError:
                    pass
        grid, fit = convert.detect_tile_grid(ref)
        root_corr = analysis.root_channel_check(mask, grid)
        groom, rep = convert.convert(hair, mask, tile_grid=grid)
        groom, dropped = convert.dedupe(groom)
        head = head_proxy(hair)
        orient = analysis.orientation(groom, head[0]) if head[1][0] > 0.01 else {}
        unmapped = rep["unmapped_samples"] / max(rep["samples"], 1)
        rec.update(tile_grid=list(grid), cards=rep["cards"], triangles=rep["triangles"], cards_in_one_tile=round(fit, 4),
                   mixed_winding_cards=rep["mixed_winding_cards"], uv_overlap_samples=rep["uv_overlap_samples"],
                   unmapped_share=round(unmapped, 5), root_row_corr_median=round(float(np.median(root_corr)), 3) if root_corr else None,
                   strands=groom.strands, segments=groom.segments, dedupe_dropped=dropped,
                   reversed_cards=orient.get("cards_mostly_reversed"), images=[])
        ious, cover, light = [], [], []
        report_dir.mkdir(parents=True, exist_ok=True)
        for name, (view, light_dir) in bbox_views(hair.positions, 0.002).items():
            img_c, cov_c = render.render(lambda v: render.card_fragments(hair.positions, hair.uv, hair.tris, mask, v), view, light_dir, head)
            img_g, cov_g = render.render(lambda v: render.strand_fragments(groom.points, groom.radii, groom.offsets, groom.root_param, v),
                                         view, light_dir, head)
            px = (cov_c > 0.02) | (cov_g > 0.02)
            ious.append(float(((cov_c > 0.5) & (cov_g > 0.5)).sum() / max(((cov_c > 0.5) | (cov_g > 0.5)).sum(), 1)))
            cover.append(float(cov_g[px].mean() - cov_c[px].mean()) if px.any() else 0.0)
            light.append(float(img_g[px].mean() / max(img_c[px].mean(), 1e-9) - 1) if px.any() else 0.0)
            if name == "front":
                safe = "".join(ch if ch.isalnum() or ch in "-_" else "_" for ch in style["name"])
                png = report_dir / f"{safe}_front.png"
                pair = np.concatenate([render.to_png(img_c), np.full((img_c.shape[0], 4, 3), 255, np.uint8), render.to_png(img_g)], 1)
                Image.fromarray(pair).save(png)
                rec["images"].append(png.name)
        rec.update(iou=[round(x, 3) for x in ious], coverage_delta=[round(x, 3) for x in cover],
                   radiance_delta=[round(x, 3) for x in light])
        reasons, review = [], []
        if groom.strands == 0:
            reasons.append("no strands")
        if root_corr and np.median(root_corr) < ROOT_REJECT:
            reasons.append(f"root channel does not run along the rows (median corr {np.median(root_corr):.2f})")
        if min(ious) < IOU_REJECT:
            reasons.append(f"silhouette IoU {min(ious):.2f}")
        if max(abs(x) for x in cover) > COVER_REVIEW:
            review.append(f"coverage differs by {max(cover, key=abs):+.2f}")
        if max(abs(x) for x in light) > RADIANCE_REVIEW:
            review.append(f"mean radiance differs by {max(light, key=abs):+.0%}")
        if rep["mixed_winding_cards"]:
            review.append(f"{rep['mixed_winding_cards']} cards with mirrored UV triangles")
        if min(ious) < IOU_PASS:
            review.append(f"silhouette IoU {min(ious):.2f}")
        if orient.get("cards_decided") and orient["cards_mostly_reversed"] > REVERSED_REVIEW * orient["cards_decided"]:
            review.append(f"{orient['cards_mostly_reversed']} cards with the root away from the head")
        rec["verdict"] = "reject" if reasons else "needs-review" if review else "pass"
        rec["reasons"] = reasons + review
        if rec["verdict"] == "pass" or (rec["verdict"] == "needs-review" and accept_review):
            out_dir.mkdir(parents=True, exist_ok=True)
            path = out_dir / f"{asset.index_id(hair.tris):016x}.hgrm"
            if not path.exists():
                cid, _ = asset.content_id(hair.positions, hair.tris)
                asset.write(path, cid, hair, [(k, convert.thin(groom, k)) for k in asset.LODS])
            rec["asset"] = path.name
    except Exception as ex:  # noqa: BLE001  a style's failure is its verdict, never the batch's
        rec.update(verdict="reject", reasons=[f"{type(ex).__name__}: {ex}"[:300]])
    rec["seconds"] = round(time.monotonic() - t0, 1)
    return rec


def write_summary(report_dir, state):
    recs = sorted(state.values(), key=lambda r: r["name"])
    counts = {v: sum(r["verdict"] == v for r in recs) for v in ("pass", "needs-review", "reject")}
    lines = [f"# Hair groom batch ({VERSION})", "", f"{len(recs)} styles: {counts['pass']} pass, {counts['needs-review']} needs review, "
             f"{counts['reject']} reject.", "", "| Style | Verdict | IoU f/s/b | Strands | Grid | Reasons |", "|---|---|---|---|---|---|"]
    for r in recs:
        lines.append(f"| {r['name']} | {r['verdict']} | {'/'.join(str(x) for x in r.get('iou', []))} | {r.get('strands', '')} | "
                     f"{'x'.join(str(x) for x in r.get('tile_grid', []))} | {'; '.join(r.get('reasons', []))} |")
    (report_dir / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return counts


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--report-dir", required=True, type=Path)
    ap.add_argument("--out-dir", type=Path, default=None, help="default: the catalogue's data/assetdb/hair_grooms")
    ap.add_argument("--catalogue", type=Path, default=None)
    ap.add_argument("--game", default=None)
    ap.add_argument("--lslib", default=None, help="LSLib Tools folder with LSLib.dll and granny2.dll")
    ap.add_argument("--match", nargs="*", default=[])
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--jobs", type=int, default=max(1, min(4, (os.cpu_count() or 2) // 4)))
    ap.add_argument("--accept-review", action="store_true", help="also write the assets of needs-review styles")
    args = ap.parse_args()
    sys.path.insert(0, str(ROOT))
    from tools.low_priority import lower_priority
    from tools.assets.locate import game_data, lslib_tools
    lower_priority()
    game = args.game or game_data()
    lslib = args.lslib or lslib_tools()
    catalogue = args.catalogue or find_catalogue()
    if not game or not lslib or not catalogue:
        ap.error(f"missing input: game Data {game or '?'}, LSLib Tools {lslib or '?'} (BG3RTX_LSLIB), catalogue {catalogue or '?'}")
    if not (Path(lslib) / "granny2.dll").exists():
        ap.error(f"{lslib} has no granny2.dll (copy it next to LSLib.dll from LSLib's ExportTool package)")
    out_dir = args.out_dir or Path(catalogue).parent / "hair_grooms"
    todo = styles(catalogue, args.match)
    if args.limit:
        todo = todo[:args.limit]
    args.report_dir.mkdir(parents=True, exist_ok=True)
    state_path = args.report_dir / "state.json"
    state = json.loads(state_path.read_text(encoding="utf-8")) if state_path.exists() else {}
    # Incremental: a style is redone only when its inputs changed: the pak entries of its GR2 and mask (owner
    # package, offset, size: a game patch moves them), its mesh name and the converter version.
    from tools.assets.lspk import PackageSet
    ps = PackageSet(game)

    def entry_key(s):
        parts = [VERSION, s["mesh"]]
        for name in (s["gr2"], s["mask"]):
            if name in ps:
                pkg, e = ps.index[name]
                parts += [os.path.basename(pkg.path), str(e.offset), str(e.part), str(e.size)]
            else:
                parts.append("missing:" + name)
        return hashlib.sha256("|".join(parts).encode()).hexdigest()[:24]
    say(f"[hair groom batch] {len(todo)} styles ({len(state)} recorded), {args.jobs} jobs, LSLib {lslib}, out {out_dir}")
    start = last = time.monotonic()
    done = fails = skipped = 0
    with concurrent.futures.ProcessPoolExecutor(args.jobs) as pool:
        futs = {}
        for s in todo:
            s["inputs"] = entry_key(s)
            old = state.get(s["name"])
            if old and old.get("inputs") == s["inputs"]:
                skipped += 1
                continue
            futs[pool.submit(process, s, game, lslib, out_dir, args.report_dir, args.accept_review)] = s
        total = len(futs)
        pending = set(futs)
        while pending:
            finished, pending = concurrent.futures.wait(pending, timeout=30, return_when=concurrent.futures.FIRST_COMPLETED)
            for f in finished:
                rec = f.result()
                rec["version"] = VERSION
                state[rec["name"]] = rec
                done += 1
                fails += rec["verdict"] == "reject"
                state_path.write_text(json.dumps(state, indent=1), encoding="utf-8")
            now = time.monotonic()
            if now - last >= 30 or not pending:
                rate = done / max(now - start, 1e-9)
                say(f"[hair groom batch] {done}/{total} done ({skipped} unchanged), {rate * 60:.1f}/min, "
                    f"ETA {(total - done) / max(rate, 1e-9):.0f}s, rejects {fails}")
                last = now
    counts = write_summary(args.report_dir, state)
    say(f"[hair groom batch] complete in {time.monotonic() - start:.0f}s: {counts} -> {args.report_dir / 'summary.md'}")


if __name__ == "__main__":
    main()
