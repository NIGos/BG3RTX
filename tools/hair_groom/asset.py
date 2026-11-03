"""Groom asset for the layer (rt_hair_groom): one file per hair mesh, generated on the player's PC.

python -m tools.hair_groom.asset --gltf <LSLib export> --mesh <name> [--mask <dds>] [--out-dir data/assetdb/hair_grooms]

The file name is the mesh's index-only content ID (common/hair_groom_asset.h index_content_id: the
layer computes it from the captured hair draw's host index range), so the layer can join a captured
hair draw to it. The header also keeps layer/mesh_content_id.h's v1 position ID (bind pose).
Files are only added, never overwritten (data/assetdb is shared).

Layout v2 (little endian; v1 = the same without the UV samples):
  header  'HGRM', version 2, position_id uint64, index_id uint64, card_vertices, card_indices, lods, 0
  float32 uv[32 * 2]   UV0 at corner floor(s * (card_indices - 1) / 31), s = 0..31, in index order: the layer
                       matches a captured draw by these when its index buffer is not host data (the captured
                       soup keeps each corner's UV, deformation-independent)
  per LOD: keep_fraction float32, points P, segments S, 0
           uint32 triangle[P]  (index of the card triangle in native index order)
           float32 bary[P*2]   (weights of the triangle's 2nd and 3rd vertex; 1st = 1 - b1 - b2)
           float32 radius[P]   (metres)
           uint8   root[P]     (mask root channel: 0 root .. 255 tip)
           uint32  segment_first_point[S] (segment = points i, i + 1)
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

import numpy as np

from . import convert, mesh
from .bench_data import segment_starts

MAGIC = b"HGRM"
SAMPLES = 32
# LOD 0 full, then the strand-thinning levels of the optimisation sweep (docs/RTX_HAIR_GROOM_EXPERIMENT.md).
LODS = (1.0, 0.5, 0.25)


def content_id(positions, indices, vertex_offset=0):
    """layer/mesh_content_id.h v1 for an indexed float3 triangle list: (id, words)."""
    positions = np.ascontiguousarray(positions, "<f4")
    indices = np.asarray(indices, np.int64).ravel()
    count = len(indices)
    if count == 0 or count % 3:
        raise ValueError("not a triangle list")
    lo, hi = int(indices.min()), int(indices.max())
    words = [1, hi - lo + 1, count]
    bits = positions.view("<u4").reshape(-1, 3)
    for s in range(SAMPLES):
        v = int(indices[s * (count - 1) // (SAMPLES - 1)])
        words.append(v - lo)
        words.extend(int(b) for b in bits[vertex_offset + v])
    h = 14695981039346656037
    for byte in struct.pack(f"<{len(words)}I", *words):
        h = ((h ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return (h or 1), words


def index_id(indices):
    """common/hair_groom_asset.h index_content_id: version 2 words, indices only."""
    indices = np.asarray(indices, np.int64).ravel()
    count = len(indices)
    if count < 3 or count % 3:
        raise ValueError("not a triangle list")
    lo, hi = int(indices.min()), int(indices.max())
    words = [2, hi - lo + 1, count] + [int(indices[s * (count - 1) // (SAMPLES - 1)]) - lo for s in range(SAMPLES)]
    h = 14695981039346656037
    for byte in struct.pack(f"<{len(words)}I", *words):
        h = ((h ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h or 1


def uv_samples(hair):
    """(32, 2) float32 UV0 of the sampled corners (common/hair_groom_asset.h uv_samples_match)."""
    corners = hair.tris.ravel()
    at = [s * (len(corners) - 1) // (SAMPLES - 1) for s in range(SAMPLES)]
    return np.ascontiguousarray(hair.uv[corners[at]], "<f4")


def write(path, cid, hair, lods):
    """lods: [(keep_fraction, Groom)]. cid: the position ID. Exclusive creation. Every LOD is checked before the file
    is created: a refused style leaves no file (a header-only stub once made the layer log it as malformed, and the
    batch's exists() check would never have retried it)."""
    for keep, g in lods:
        if not (np.all(g.radii > 0) and np.all(g.radii < 0.01)):
            raise ValueError(f"LOD {keep}: radius outside (0, 10 mm): the layer would reject the file")
    with open(path, "xb") as f:
        f.write(MAGIC + struct.pack("<IQQ4I", 2, cid, index_id(hair.tris), len(hair.positions), hair.tris.size, len(lods), 0))
        f.write(uv_samples(hair).tobytes())
        for keep, g in lods:
            starts = segment_starts(g.offsets)
            f.write(struct.pack("<f3I", keep, len(g.points), len(starts), 0))
            f.write(np.asarray(g.triangle, "<u4").tobytes())
            f.write(np.ascontiguousarray(g.bary[:, 1:3], "<f4").tobytes())
            f.write(np.asarray(g.radii, "<f4").tobytes())
            f.write(np.clip(np.rint(g.root_param * 255), 0, 255).astype(np.uint8).tobytes())
            f.write(starts.astype("<u4").tobytes())


def build(hair, mask, step=16, grid_mesh=None):
    """grid_mesh: the mesh whose cards define the atlas grid (LOD0 for a LOD1 mesh); default the mesh itself."""
    grid, _ = convert.detect_tile_grid(grid_mesh or hair)
    g, _ = convert.convert(hair, mask, tile_grid=grid, step=step)
    g, _ = convert.dedupe(g)
    return [(k, convert.thin(g, k)) for k in LODS]


def main():
    from .__main__ import load_mask
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--gltf", required=True, type=Path)
    ap.add_argument("--mesh", default="HAIR_HUM_F_Shadowheart_Spring_Mesh")
    ap.add_argument("--mask", type=Path)
    ap.add_argument("--out-dir", type=Path, default=Path("data/assetdb/hair_grooms"))
    args = ap.parse_args()
    hair = mesh.load(args.gltf, args.mesh)
    cid, _ = content_id(hair.positions, hair.tris)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    path = args.out_dir / f"{index_id(hair.tris):016x}.hgrm"
    if path.exists():
        print(f"[hair groom asset] {path} exists, kept", flush=True)
        return
    grid_mesh = mesh.load(args.gltf, args.mesh[:-5]) if args.mesh.endswith("_LOD1") else None
    lods = build(hair, load_mask(args.mask), grid_mesh=grid_mesh)
    write(path, cid, hair, lods)
    print(f"[hair groom asset] {path}: index id {index_id(hair.tris):016x}, position id {cid:016x}, " +
          ", ".join(f"LOD {k}: {g.strands} strands {g.segments} segments" for k, g in lods), flush=True)


if __name__ == "__main__":
    main()
