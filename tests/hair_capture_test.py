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


def test_e2e_stage_never_writes_the_real_assetdb():
    """tools/live_batches/hair_capture/stage.py: setup links the real assetdb minus the target into the tree, refuses a
    tree that already has one, and cleanup removes only the links (the real files stay)."""
    import importlib.util
    import os
    import tempfile
    spec = importlib.util.spec_from_file_location("stage", ROOT / "tools/live_batches/hair_capture/stage.py")
    with tempfile.TemporaryDirectory() as d:
        main = Path(d) / "main"
        (main / ".git").mkdir(parents=True)
        real = main / "data" / "assetdb"
        (real / "hair_grooms").mkdir(parents=True)
        (real / "bg3rtx_assetdb.bin").write_bytes(b"db")
        for name in ("aaaaaaaaaaaaaaaa", "8b32681bb9ecfc30"):
            (real / "hair_grooms" / f"{name}.hgrm").write_bytes(b"groom " + name.encode())
        tree = main / ".claude" / "worktrees" / "test-main"
        tree.mkdir(parents=True)
        old = os.getcwd()
        os.chdir(tree)
        try:
            stage = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(stage)
            assert stage.setup("8b32681bb9ecfc30") == 0
            copy = tree / "data" / "assetdb"
            assert (copy / "hair_grooms" / "aaaaaaaaaaaaaaaa.hgrm").read_bytes() == b"groom aaaaaaaaaaaaaaaa"
            assert not (copy / "hair_grooms" / "8b32681bb9ecfc30.hgrm").exists()
            assert os.stat(copy / "bg3rtx_assetdb.bin").st_nlink == 2   # a hard link, not a copy
            assert stage.setup("8b32681bb9ecfc30") == 1                 # the tree has one already
            assert stage.cleanup() == 0 and not copy.exists()
            assert (real / "hair_grooms" / "8b32681bb9ecfc30.hgrm").exists() and (real / "bg3rtx_assetdb.bin").read_bytes() == b"db"
            (copy).mkdir(parents=True)                                  # someone else's assetdb: left alone
            assert stage.cleanup() == 1 and copy.exists()
        finally:
            os.chdir(old)


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
    print("hair capture: GR2-free mesh, exact embedding, index id and UV samples, head proxy from roots")
