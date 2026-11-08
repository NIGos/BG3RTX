"""tools/hair_groom/capture.py: the converter's mesh rebuilt from a runtime capture (docs/RTX_HAIR_ROADMAP.md 2c).

A capture of a posed draw (per-corner positions and UVs in index order, plus the index range) must give the GR2
mesh's groom embedding exactly (triangle and barycentrics come from UV and the native triangle order), the same
index id and UV samples, and radii within the pose's stretch; a rigid pose keeps radii to float rounding.
"""
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.hair_groom import asset, capture, convert, mesh  # noqa: E402


def cards_mesh(n=6, rows=4, length=0.2, width=0.03):
    """n vertical cards side by side, each a strip of `rows` quads; UVs tile 1 x n across the atlas."""
    pos, uv, tris = [], [], []
    for c in range(n):
        base = len(pos)
        for r in range(rows + 1):
            v = r / rows
            for u in (0.0, 1.0):
                pos.append([c * 0.05 + (u - 0.5) * width, -v * length, 0.01 * c])
                uv.append([(c + u) / n, v])
        for r in range(rows):
            i = base + 2 * r
            tris += [[i, i + 1, i + 2], [i + 1, i + 3, i + 2]]
    v = len(pos)
    return mesh.Mesh(np.array(pos, np.float32), np.array(uv, np.float32), np.zeros((v, 4), np.int64),
                     np.zeros((v, 4), np.float32), np.array(tris), [], np.zeros((0, 3), np.float32))


def test_mesh_from_capture():
    m = cards_mesh()
    cp, cu, idx = capture.soup_from_mesh(m)
    got = capture.from_capture(cp, cu, idx)
    assert np.array_equal(got.tris, m.tris) and np.array_equal(got.positions, m.positions) and np.array_equal(got.uv, m.uv)
    assert asset.index_id(got.tris) == asset.index_id(m.tris)
    assert np.array_equal(asset.uv_samples(got), asset.uv_samples(m))
    # Without the index range, welding keeps the triangle order and the cards (no two cards share a vertex here).
    welded = capture.from_capture(cp, cu)
    assert len(welded.tris) == len(m.tris) and convert.cards(welded.tris, len(welded.positions))[0] == 6


def test_converter_equivalence():
    m = cards_mesh()
    mask = np.zeros((256, 384, 4), np.uint8)
    mask[..., 1] = 128
    for x in range(4, 384, 8):
        mask[8:248, x:x + 3, 3] = 255
        mask[8:248, x:x + 3, 2] = np.linspace(0, 255, 240)[:, None].astype(np.uint8)
    ref, _ = convert.convert(m, mask, tile_grid=(6, 1), tracer="rows")
    for pose, tol in ((capture.rigid_pose(), 1e-4), (capture.bend_pose(amount=0.05), 0.3)):
        cp, cu, idx = capture.soup_from_mesh(m, pose)
        g, _ = convert.convert(capture.from_capture(cp, cu, idx), mask, tile_grid=(6, 1), tracer="rows")
        assert np.array_equal(g.offsets, ref.offsets) and np.array_equal(g.triangle, ref.triangle)
        assert np.array_equal(g.bary, ref.bary)
        assert np.abs(g.radii / ref.radii - 1).max() <= tol


def test_head_from_roots():
    rng = np.random.default_rng(4)
    d = rng.normal(size=(400, 3))
    d[:, 1] = np.abs(d[:, 1])          # roots on the upper half of the scalp
    d /= np.linalg.norm(d, axis=1, keepdims=True)
    centre = np.array([0.3, 1.6, -0.2])
    pts = (centre + 0.09 * d).astype(np.float32)
    offsets = np.arange(0, 401)        # one point per strand: every point a root
    c, r = capture.head_from_roots(pts, np.zeros(400), offsets)
    assert np.linalg.norm(c - centre) < 1e-4 and abs(r[0] - 0.09) < 1e-4
    far = capture.head_from_roots(pts[:5], np.zeros(5), np.arange(6))
    assert far[0][1] == 1e4   # too few roots: no proxy (as batch.head_proxy without head joints)


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
    print("hair capture: GR2-free mesh, exact embedding, index id and UV samples, head proxy from roots")
