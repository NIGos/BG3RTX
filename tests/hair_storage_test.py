"""tools/hair_groom/storage.py (HGRZ prototype) and prepare.py (needed-first order, launcher status).

Synthetic groom: the thinned LODs are built with convert.thin from LOD 0, so the codec must rebuild them bit-exact;
lossless mode must round-trip every array; 16-bit barycentrics must stay within half a quantisation step.
"""
import struct
import sys
import tempfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.hair_groom import convert, prepare, storage  # noqa: E402


def synthetic(strands=500, seed=1):
    rng = np.random.default_rng(seed)
    lengths = rng.integers(2, 40, strands)
    lengths[3] = 300                     # one strand past the u8 length escape
    off = np.concatenate([[0], np.cumsum(lengths)]).astype(np.int64)
    p = int(off[-1])
    tri = np.repeat(rng.integers(0, 898, strands), lengths) + (np.arange(p) % 3)
    b = rng.random((p, 2)); b = np.where(b.sum(1, keepdims=True) > 1, 1 - b, b)
    rad = np.repeat(rng.uniform(5e-5, 3e-4, strands), lengths) * np.linspace(1, 0.5, p) % 3e-4 + 1e-5
    root = np.concatenate([np.linspace(0, 255, n).astype(np.uint8) for n in lengths])
    l0 = storage.Lod(np.float32(1.0), tri.astype(np.uint32), b.astype(np.float32), rad.astype(np.float32), root,
                     storage.segment_starts(off))
    lods = [l0] + [storage.thin(l0, k) for k in (0.5, 0.25)]
    header = b"HGRM" + struct.pack("<IQQ4I", 2, 7, 9, 2700, 2700, len(lods), 0) + bytes(256)
    return storage.Hgrm(header, lods), off


def test_thin_matches_converter():
    h, off = synthetic()
    l0 = h.lods[0]
    g = convert.Groom(points=np.zeros((l0.points, 3), np.float32), radii=l0.radius, joints=np.zeros((l0.points, 4), int),
                      weights=np.zeros((l0.points, 4), np.float32), offsets=off, card=np.zeros(len(off) - 1, int),
                      root_param=l0.root / 255.0, triangle=l0.triangle,
                      bary=np.concatenate([1 - l0.bary.sum(1, keepdims=True), l0.bary], 1), normal_offset=np.zeros(l0.points))
    for keep in (0.5, 0.25):
        t = convert.thin(g, keep)
        mine = storage.thin(l0, keep)
        assert np.array_equal(np.asarray(t.triangle, np.uint32), mine.triangle)
        assert np.array_equal(np.asarray(t.radii, np.float32), mine.radius)
        assert np.array_equal(storage.segment_starts(t.offsets), mine.segment)


def test_lossless_round_trip_through_file():
    h, _ = synthetic()
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "a.hgrm"
        p.write_bytes(storage.write_bytes(h))
        back = storage.read(p)
    for codec in ("zstd", "lz4"):
        raw = storage.encode(back, bary_bits=32, lossless_radius=True, codec=codec, level=3)
        out = storage.decode(raw, codec=codec)
        assert out.header == h.header
        assert all(storage.lods_equal(a, b) for a, b in zip(out.lods, h.lods)), codec
        assert len(raw) < len(storage.write_bytes(h))


def test_quantised_bounds():
    h, _ = synthetic()
    out = storage.decode(storage.encode(h, bary_bits=16))
    a, b = out.lods[0], h.lods[0]
    assert np.array_equal(a.triangle, b.triangle) and np.array_equal(a.root, b.root) and np.array_equal(a.segment, b.segment)
    step = (1 + 2 * storage.BARY_PAD) / 65535
    assert np.abs(a.bary.astype(np.float64) - b.bary).max() <= 0.5 * step + 1e-7
    # The files hold barycentrics slightly outside [0, 1] (-1e-4 .. 1.0001): kept, not clipped.
    edge = np.array([-1e-4, 0.0, 1.0001])
    back = storage.dequant_bary(storage.quant_bary(edge, 16), 16)
    assert np.abs(back - edge).max() <= 0.5 * step + 1e-7
    rel = np.abs(a.radius.astype(np.float64) / b.radius - 1).max()
    assert rel <= 2 ** (0.5 / storage.LOG_STEPS) - 1 + 1e-6, rel
    # Thinned LODs follow LOD 0's decoded values.
    assert np.array_equal(out.lods[2].radius, storage.thin(a, 0.25).radius)


def test_position_error_is_bary_delta_times_edges():
    h, _ = synthetic(strands=20)
    l0 = h.lods[0]
    positions = np.random.default_rng(3).random((2700, 3)) * 0.05
    tris = np.arange(2700).reshape(900, 3)
    q = l0.bary.astype(np.float64) + [1e-4, 0]
    e = storage.position_error(l0, q, positions, tris)
    t = tris[l0.triangle.astype(np.int64)]
    assert np.allclose(e, 1e-4 * np.linalg.norm(positions[t[:, 1]] - positions[t[:, 0]], axis=1))


def test_bits_for_bounds_the_error():
    h, _ = synthetic(strands=200)
    l0 = h.lods[0]
    rng = np.random.default_rng(5)
    positions = rng.random((2700, 3)) * 0.12          # card triangles up to ~15 cm
    tris = np.arange(2700).reshape(900, 3)
    bits = storage.bits_for(positions, tris, l0.triangle)
    q = storage.dequant_bary(storage.quant_bary(l0.bary, bits), bits)
    assert storage.position_error(l0, q, positions, tris).max() <= storage.POS_THRESHOLD_M
    assert 16 < bits <= 24, bits
    # One bit fewer can exceed it (the bound is not loose by more than 2x).
    assert storage.bits_for(positions * 0.5, tris, l0.triangle) == bits - 1


def test_prepare_order_and_status():
    rows = [{"name": "unused0", "lod": 0, "category": "unused"},
            {"name": "npc1", "lod": 1, "category": "named NPC"},
            {"name": "npc0", "lod": 0, "category": "named NPC"},
            {"name": "cc0", "lod": 0, "category": "player", "index_id": "00000000000000aa"},
            {"name": "comp0", "lod": 0, "category": "companion"},
            {"name": "hat0", "lod": 0, "category": "hat variant"}]
    names = [r["name"] for r in prepare.order(rows)]
    assert names == ["comp0", "cc0", "npc0", "npc1", "hat0"], names
    seen = frozenset({"00000000000000aa"})
    assert [r["name"] for r in prepare.order(rows, seen)][:2] == ["cc0", "comp0"]
    assert [r["name"] for r in prepare.order(rows, convert_all=True)][-1] == "unused0"
    s = prepare.status(rows, {"comp0": "pass", "cc0": "reject"}, [3_000_000_000], jobs=2, seconds=[30, 30, 10])
    assert (s["ready"], s["finished"], s["needed"], s["all"]) == (1, 2, 5, 6)
    assert s["eta_s"] == 3 * 30 // 2 and s["state"] == "waiting" and s["next_tier"] == "named"
    assert s["line"] == "Strand hair: 1 of 5 styles ready, 3.0 GB, waiting, about 1 min left.", s["line"]
    done = {r["name"]: "pass" for r in rows}
    assert prepare.status(rows, done, [5e8])["line"] == "Strand hair: 5 of 5 styles ready, 0.5 GB."


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
    print("hair storage: codec round trip, thinned LODs, quantisation bounds, needed-first order and status")
