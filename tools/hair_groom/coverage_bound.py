"""Recover coverage ratios of passing batch views from the unchanged geometry cache.

Only opacity is evaluated: no lighting, shader compilation, game launch or conversion.
The sum of composited opacities is the expected number of covered pixel samples,
not a count of overlapping raster fragments. The bound subtracts the limiting
minimum-ratio view's modeled one-frame uncertainty, with unknown cross-count
covariance. It assumes independent spatial Bernoulli samples within each count;
it does not measure empirical temporal/dither noise. A one-sample quantization
bound is also retained for reference.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import time
from io import BytesIO
from pathlib import Path

import numpy as np
from PIL import Image

from . import batch, render
from ..assets.locate import game_data
from ..assets.lspk import PackageSet
from ..low_priority import lower_priority


VERSION = "hair-primary-coverage-bound-1"


def valid_record(result, source):
    """Never reuse a partial, stale, or malformed style result."""
    if not isinstance(result, dict) or result.get("inputs") != source["inputs"]:
        return False
    views = result.get("views", [])
    if not isinstance(views, list) or len(views) != 3:
        return False
    try:
        if [v["view"] for v in views] != ["front", "side", "back"]:
            return False
        for v, old in zip(views, source["coverage_delta"]):
            c, s = v["card_samples"], v["strand_samples"]
            if not all(np.isfinite(v[k]) for k in ("card_samples", "strand_samples", "ratio", "lower_one_sample", "coverage_delta")):
                return False
            if c <= 0 or s < 0 or abs(v["ratio"] - s / c) > 1e-14:
                return False
            if abs(v["lower_one_sample"] - max(0., s - 1.) / (c + 1.)) > 1e-14:
                return False
            if abs(v["coverage_delta"] - old) > 0.000501:
                return False
        return len(source["coverage_delta"]) == 3
    except (KeyError, TypeError, ValueError):
        return False


def coverage(frags, view, head_depth):
    frags = frags.select(frags.depth < head_depth[frags.pixel])
    tau = np.bincount(frags.pixel, weights=-np.log1p(-np.minimum(frags.alpha, 0.999)),
                      minlength=view.size[0] * view.size[1])
    return -np.expm1(-tau)


def measure(rec, packages, report_dir):
    gr2, dds = packages.read(rec["gr2"]), packages.read(rec["mask"])
    path = report_dir / "cache" / "groom" / (batch.groom_key(gr2, dds, rec["mesh"]) + ".npz")
    cached = batch.load_groom_cache(path)
    if cached is None:
        raise RuntimeError(f"Missing or damaged groom cache: {path}")
    groom, hair, _, _, _, _, _, head = cached
    mask = np.asarray(Image.open(BytesIO(dds)).convert("RGBA"))
    views = []
    for name, (view, _) in batch.bbox_views(hair.positions, 0.002).items():
        hd, _ = render.ellipsoid_hit(view, *head)
        cards = render.card_fragments(hair.positions, hair.uv, hair.tris, mask, view, fibres=True)
        c = coverage(cards, view, hd)
        del cards
        strands = render.strand_fragments(groom.points, groom.radii, groom.offsets, groom.root_param, view)
        s = coverage(strands, view, hd)
        del strands
        px = (c > 0.02) | (s > 0.02)
        card, strand = float(c[px].sum()), float(s[px].sum())
        if card <= 0:
            raise RuntimeError(f"No visible card samples: {rec['name']} {name}")
        delta = float((s[px] - c[px]).mean()) if px.any() else 0.0
        views.append(dict(view=name, card_samples=card, strand_samples=strand,
                          ratio=strand / card, lower_one_sample=max(0., strand - 1.) / (card + 1.),
                          coverage_delta=delta))
    # The batch may reorient roots; this only changes root shading, not opacity.
    for v, old in zip(views, rec["coverage_delta"]):
        if abs(v["coverage_delta"] - old) > 0.000501:
            raise RuntimeError(f"Coverage reproduction mismatch: {rec['name']} {v['view']}: "
                               f"{v['coverage_delta']} vs {old}")
    return dict(inputs=rec["inputs"], views=views)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--report-dir", type=Path, required=True)
    ap.add_argument("--limit", type=int, default=0, help="benchmark only this many remaining styles")
    args = ap.parse_args()
    lower_priority()
    state_bytes = (args.report_dir / "state.json").read_bytes()
    state = json.loads(state_bytes)
    passing = {k: r for k, r in state.items() if r["verdict"] == "pass"}
    output = args.report_dir / "coverage_bound.json"
    try:
        doc = json.loads(output.read_text()) if output.exists() else {}
    except (OSError, ValueError):
        doc = {}
    if not isinstance(doc, dict):
        doc = {}
    records = doc.get("records", {}) if doc.get("version") == VERSION else {}
    if not isinstance(records, dict):
        records = {}
    records = {k: r for k, r in records.items() if k in passing and valid_record(r, passing[k])}
    pending = [(k, r) for k, r in passing.items() if k not in records]
    if args.limit:
        pending = pending[:args.limit]
    game = game_data()
    if not game:
        raise RuntimeError("Game data not found")
    packages = PackageSet(game)
    start = last = time.monotonic()
    done = failures = 0

    def write():
        rows = [(v["ratio"], k, v) for k, r in records.items() for v in r["views"]]
        lower = [(v["lower_one_sample"], k, v) for k, r in records.items() for v in r["views"]]
        data = dict(version=VERSION, state_sha256=hashlib.sha256(state_bytes).hexdigest(),
                    passing_styles=len(passing), measured_styles=len(records), complete=len(records) == len(passing),
                    records=records)
        if rows:
            ratio, name, view = min(rows, key=lambda x: x[0])
            bound, lname, lview = min(lower, key=lambda x: x[0])
            c, s = view["card_samples"], view["strand_samples"]
            sigma = math.sqrt(s + ratio * ratio * c) / c
            covariance_sigma = (math.sqrt(s) + ratio * math.sqrt(c)) / c
            per_view_covariance = min((
                (max(0., v["ratio"] - (math.sqrt(v["strand_samples"]) +
                                      v["ratio"] * math.sqrt(v["card_samples"])) / v["card_samples"]), k, v)
                for _, k, v in rows), key=lambda x: x[0])
            data.update(min_ratio=ratio, min_style=name, min_view=view,
                        bound=max(0., ratio - covariance_sigma), bound_style=name, bound_view=view,
                        quantization_bound=bound, quantization_style=lname, quantization_view=lview,
                        modeled_one_frame_sigma=sigma, noise_bound=max(0., ratio - sigma),
                        covariance_sigma=covariance_sigma,
                        covariance_noise_bound=max(0., ratio - covariance_sigma),
                        min_per_view_covariance_bound=per_view_covariance[0],
                        min_per_view_covariance_style=per_view_covariance[1],
                        min_per_view_covariance_view=per_view_covariance[2],
                        noise_model="first-order ratio propagation; per-count variance <= mean assumes "
                                    "independent Bernoulli samples; modeled_one_frame_sigma additionally "
                                    "assumes independent card/strand counts; covariance_sigma bounds "
                                    "unknown cross-count covariance under the same per-count variance assumption")
        batch.atomic_cache(output, lambda f: f.write(json.dumps(data, indent=1).encode()))
        return data

    print(f"[coverage bound] {len(records)}/{len(passing)} cached; {len(pending)} styles to measure", flush=True)
    for key, rec in pending:
        try:
            records[key] = measure(rec, packages, args.report_dir)
        except Exception as exc:
            failures += 1
            print(f"[coverage bound] FAIL {key}: {exc}", flush=True)
        done += 1
        data = write()
        now = time.monotonic()
        if now - last >= 30 or done == len(pending):
            rate = done / max(now - start, 1e-9)
            print(f"[coverage bound] {done}/{len(pending)} done, {rate:.2f} styles/s, "
                  f"ETA {(len(pending) - done) / max(rate, 1e-9):.0f} s, failures {failures}; "
                  f"minimum {data.get('min_ratio')}, bound {data.get('bound')}", flush=True)
            last = now
    data = write()
    print(f"[coverage bound] {data['measured_styles']}/{data['passing_styles']} measured; "
          f"complete={data['complete']}; elapsed {time.monotonic() - start:.1f} s", flush=True)
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
