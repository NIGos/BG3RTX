"""Write the input file of tools/hair_lss_bench.cpp: cards, mask and grooms at several segment lengths.

python -m tools.hair_groom.bench_data --gltf <LSLib export> --out <new .bin> [--mask <dds>] [--steps 16 32 64]
    [--dedupe] [--max-fibre-diameter 0.00018] [--depth-offset 0.001] [--keep-fraction 0.5]

Layout (little endian; all counts uint32):
  header  'HLSB', version 1, card_vertices V, card_triangles T, mask_w, mask_h, groom_sets G, 0
  cards   float32 positions[V*3], float32 uv[V*2], uint32 triangles[T*3]
  mask    uint8 rgba[mask_h*mask_w*4]
  G times: step, points P, segments S, strands; float32 points[P*3], float32 radii[P],
           uint32 segment_first_point[S] (successive indexing: segment = point i, i+1),
           uint32 triangle[P], float32 bary[P*3]
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

import numpy as np

from . import convert, mesh
from .__main__ import load_mask

MAGIC = b"HLSB"


def segment_starts(offsets):
    """First point of every segment: all points except each strand's last one."""
    last = np.zeros(int(offsets[-1]), bool)
    last[np.asarray(offsets[1:]) - 1] = True
    return np.flatnonzero(~last).astype(np.uint32)


def write(path, hair, mask, grooms):
    with open(path, "xb") as f:
        f.write(MAGIC + struct.pack("<7I", 1, len(hair.positions), len(hair.tris), mask.shape[1], mask.shape[0], len(grooms), 0))
        f.write(hair.positions.astype("<f4").tobytes())
        f.write(hair.uv.astype("<f4").tobytes())
        f.write(hair.tris.astype("<u4").tobytes())
        f.write(np.ascontiguousarray(mask, np.uint8).tobytes())
        for step, g in grooms:
            starts = segment_starts(g.offsets)
            f.write(struct.pack("<4I", step, len(g.points), len(starts), g.strands))
            f.write(g.points.astype("<f4").tobytes())
            f.write(g.radii.astype("<f4").tobytes())
            f.write(starts.astype("<u4").tobytes())
            f.write(g.triangle.astype("<u4").tobytes())
            f.write(g.bary.astype("<f4").tobytes())


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--gltf", required=True, type=Path)
    ap.add_argument("--mesh", default="HAIR_HUM_F_Shadowheart_Spring_Mesh")
    ap.add_argument("--mask", type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--steps", type=int, nargs="+", default=[16, 32, 64])
    ap.add_argument("--dedupe", action="store_true", help="drop strands repeated by stacked cards")
    ap.add_argument("--max-fibre-diameter", type=float, help="split painted clumps into fibres (metres)")
    ap.add_argument("--depth-offset", type=float, help="metres per unit of the mask depth channel")
    ap.add_argument("--keep-fraction", type=float, default=1.0, help="distance LOD: share of strands kept, radii widened")
    args = ap.parse_args()
    hair = mesh.load(args.gltf, args.mesh)
    mask = load_mask(args.mask)
    grooms = []
    for step in args.steps:
        head = (hair.joint_positions[hair.joint_names.index("Head_M")]
                + hair.joint_positions[hair.joint_names.index("HeadEnd_M_endBone")]) / 2
        g, _ = convert.convert(hair, mask, step=step, max_fibre_diameter=args.max_fibre_diameter,
                               depth_offset=args.depth_offset, head_centre=head)
        if args.dedupe:
            g, dropped = convert.dedupe(g)
            print(f"[bench data] step {step}: dedupe dropped {dropped} strands", flush=True)
        g = convert.thin(g, args.keep_fraction)
        print(f"[bench data] step {step}: {g.strands} strands, {g.segments} segments", flush=True)
        grooms.append((step, g))
    write(args.out, hair, mask, grooms)


if __name__ == "__main__":
    main()
