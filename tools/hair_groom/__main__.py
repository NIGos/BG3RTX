"""Run the card -> groom experiment on one style and write numbers + comparison images.

python -m tools.hair_groom --gltf <LSLib glTF export> --mesh <mesh name> --out <new folder>
The coverage mask is read from the installed paks (read-only) unless --mask names a DDS file.
Nothing is written to the game installation, data/assetdb or the input folder.
"""
from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

import numpy as np
from PIL import Image

from . import analysis, convert, mesh, render

SHADOWHEART_MASK = ("Generated/Public/Shared/Assets/Characters/_Models/_SharedAssets/SHARED_Hair/"
                    "Resources/HAIR_Shadowheart_Evil_MSKA.DDS")
# Opaque stand-in for the scalp/head (the head mesh is not part of the hair GR2): fitted so
# about 1% of the card vertices lie inside it.
HEAD = (np.array([0.0, 1.735, 0.02]), np.array([0.066, 0.089, 0.083]))


def load_mask(path):
    if path:
        return np.asarray(Image.open(path).convert("RGBA"))
    from io import BytesIO
    from ..assets.locate import game_data
    from ..assets.lspk import PackageSet
    return np.asarray(Image.open(BytesIO(PackageSet(game_data()).read(SHADOWHEART_MASK))).convert("RGBA"))


def views(pixel):
    c = np.array([0.0, 1.57, 0.05])
    up = np.array([0.0, 1.0, 0.0])
    size = (int(0.36 / pixel), int(0.66 / pixel))
    face = np.array([0.0, 0.0, 1.0])   # the face looks toward -z: a front camera looks along +z
    return {
        "front": (render.View(c, face, up, pixel, size), np.array([0.4, 0.5, -1.0])),
        "side": (render.View(c, np.array([-1.0, 0, 0]), up, pixel, size), np.array([0.8, 0.6, -0.3])),
        "back": (render.View(c, -face, up, pixel, size), np.array([-0.3, 0.6, 1.0])),
        "backlit": (render.View(c, -face, up, pixel, size), np.array([0.15, 0.25, -1.0])),
        "crown_zoom": (render.View(np.array([0.0, 1.80, 0.06]), np.array([0, -0.6, 1.0]), up, pixel / 4,
                                   (int(0.08 / pixel * 4), int(0.08 / pixel * 4))), np.array([0.4, 0.5, -1.0])),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--gltf", required=True, type=Path)
    ap.add_argument("--mesh", default="HAIR_HUM_F_Shadowheart_Spring_Mesh")
    ap.add_argument("--mask", type=Path, help="DDS/PNG coverage mask; default: read the Shadowheart mask from the paks")
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--step", type=int, default=16, help="mask rows per strand segment")
    ap.add_argument("--pixel", type=float, default=0.0005, help="render pixel size in metres")
    ap.add_argument("--dedupe", action="store_true", help="drop strands repeated by stacked cards")
    ap.add_argument("--keep-fraction", type=float, default=1.0, help="distance LOD: share of strands kept, radii widened")
    ap.add_argument("--max-fibre-diameter", type=float, help="split painted clumps into fibres (metres)")
    ap.add_argument("--depth-offset", type=float,
                    help="metres per unit of the mask depth channel (CHAR_Hair PixelDepthOffset; Shadowheart 0.001)")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    t0 = time.monotonic()
    hair = mesh.load(args.gltf, args.mesh)
    mask = load_mask(args.mask)
    head_centre = (hair.joint_positions[hair.joint_names.index("Head_M")]
                   + hair.joint_positions[hair.joint_names.index("HeadEnd_M_endBone")]) / 2
    groom, report = convert.convert(hair, mask, step=args.step, max_fibre_diameter=args.max_fibre_diameter,
                                    depth_offset=args.depth_offset, head_centre=head_centre)
    if args.dedupe:
        groom, report["dedupe_dropped_strands"] = convert.dedupe(groom)
    groom = convert.thin(groom, args.keep_fraction)
    print(f"[groom] converted in {time.monotonic() - t0:.1f}s: {groom.strands} strands, {groom.segments} segments", flush=True)
    report.update(analysis.summary(groom, hair, mask, head_centre))
    np.savez_compressed(args.out / "groom.npz", points=groom.points, radii=groom.radii, joints=groom.joints,
                        weights=groom.weights, offsets=groom.offsets, card=groom.card, root=groom.root_param,
                        triangle=groom.triangle, bary=groom.bary, normal_offset=groom.normal_offset)

    def cards_build(v):
        return render.card_fragments(hair.positions, hair.uv, hair.tris, mask, v)

    def groom_build(v):
        return render.strand_fragments(groom.points, groom.radii, groom.offsets, groom.root_param, v)

    report["views"] = {}
    for name, (view, light) in views(args.pixel).items():
        t = time.monotonic()
        img_c, cov_c = render.render(cards_build, view, light, HEAD)
        img_g, cov_g = render.render(groom_build, view, light, HEAD)
        pair = np.concatenate([render.to_png(img_c), np.full((img_c.shape[0], 6, 3), 255, np.uint8),
                               render.to_png(img_g)], 1)
        Image.fromarray(pair).save(args.out / f"{name}_cards_vs_groom.png")
        hair_px = (cov_c > 0.02) | (cov_g > 0.02)
        report["views"][name] = {
            "mean_coverage_cards": float(cov_c[hair_px].mean()), "mean_coverage_groom": float(cov_g[hair_px].mean()),
            "silhouette_iou_50": float(((cov_c > 0.5) & (cov_g > 0.5)).sum() / max(((cov_c > 0.5) | (cov_g > 0.5)).sum(), 1)),
            "mean_luminance_cards": float(img_c[hair_px].mean()), "mean_luminance_groom": float(img_g[hair_px].mean())}
        print(f"[groom] view {name} {time.monotonic() - t:.1f}s {report['views'][name]}", flush=True)
    (args.out / "report.json").write_text(json.dumps(report, indent=1, default=float), encoding="utf-8")
    print(f"[groom] done in {time.monotonic() - t0:.1f}s -> {args.out}", flush=True)


if __name__ == "__main__":
    main()
