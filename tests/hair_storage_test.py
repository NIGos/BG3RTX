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


FIXTURE = ROOT / "tests" / "fixtures" / "hair_v3"


def fixture_bytes():
    """The C++ test's fixture (tests/hair_groom_v3_test.cpp): a synthetic groom (no game data) as v3, and the Python
    reference decode as a v2 file (every LOD's arrays)."""
    h, _ = synthetic(strands=60, seed=7)
    bits = storage.pick_bits(h.lods[0])
    v3 = storage.encode_v3(h.header, h.lods, bits)
    ref = storage.decode_v3(v3)
    ref.header = ref.header[:4] + struct.pack("<I", 2) + ref.header[8:]
    return v3, storage.write_bytes(ref)


def test_v3_round_trip_and_fixture():
    h, _ = synthetic()
    for bits in (16, 18, 20):
        v3 = storage.encode_v3(h.header, h.lods, bits)
        assert struct.unpack_from("<I", v3, 4)[0] == 3
        back = storage.decode_v3(v3)
        a, b = back.lods[0], h.lods[0]
        assert np.array_equal(a.triangle, b.triangle) and np.array_equal(a.root, b.root) and np.array_equal(a.segment, b.segment)
        step = (1 + 2 * storage.BARY_PAD) / ((1 << bits) - 1)
        assert np.abs(a.bary.astype(np.float64) - b.bary).max() <= 0.5 * step + 1e-7
        assert [len(l.segment) for l in back.lods] == [len(l.segment) for l in h.lods]
        assert all(storage.lods_equal(x, storage.thin(a, x.keep)) for x in back.lods[1:])
    v3, ref = fixture_bytes()
    assert (FIXTURE / "groom.v3.hgrm").read_bytes() == v3, "fixture stale: python tests/hair_storage_test.py --write-fixture"
    assert (FIXTURE / "groom.ref.hgrm").read_bytes() == ref, "fixture stale: python tests/hair_storage_test.py --write-fixture"


def test_prepare_order_and_status():
    rows = [{"name": "unused0", "lod": 0, "category": "unused"},
            {"name": "npc1", "lod": 1, "category": "named NPC"},
            {"name": "npc0", "lod": 0, "category": "named NPC"},
            {"name": "cc0", "lod": 0, "category": "player", "index_id": "00000000000000aa"},
            {"name": "comp0", "lod": 0, "category": "companion"},
            {"name": "hat0", "lod": 0, "category": "hat variant"}]
    names = [r["name"] for r in prepare.order(rows)]
    assert names == ["comp0", "cc0", "npc0", "npc1", "hat0"], names
    # The layer's hair_seen.txt: "<count> <id>"; id 0 = only the count is known.
    seen = prepare.parse_seen("# seen\n1500 00000000000000AA\n2400 0\nbad line\n")
    assert seen == {"id:00000000000000aa", "n:2400"}, seen
    assert [r["name"] for r in prepare.order(rows, seen)][:2] == ["cc0", "comp0"]
    rows[1]["indices"] = 2400   # npc1 answers to the count-only entry
    assert [r["name"] for r in prepare.order(rows, seen)][:3] == ["cc0", "npc1", "comp0"]
    assert prepare.unresolved(rows, seen | {"id:00000000000000bb"}) == {"id:00000000000000bb"}
    del rows[1]["indices"]
    assert [r["name"] for r in prepare.order(rows, convert_all=True)][-1] == "unused0"
    s = prepare.status(rows, {"comp0": "pass", "cc0": "reject"}, [3_000_000_000], jobs=2, seconds=[30, 30, 10])
    assert (s["ready"], s["finished"], s["needed"], s["all"]) == (1, 2, 5, 6)
    assert s["eta_s"] == 3 * 30 // 2 and s["state"] == "waiting" and s["next_tier"] == "named"
    assert s["line"] == "1 of 5 styles ready, 3.0 GB on disk.", s["line"]
    assert s["detail"] == "3 styles left, about 1 min left.", s["detail"]
    r = prepare.status(rows, {"comp0": "pass"}, [5e7], running=True, eta_s=600)
    assert r["detail"] == "Converting character creation looks, about 10 min left.", r["detail"]
    done = {r["name"]: "pass" for r in rows}
    d = prepare.status(rows, done, [5e8])
    assert (d["line"], d["detail"], d["state"]) == ("5 of 5 styles ready, 0.5 GB on disk.", "All needed styles are done.", "done")


def test_batch_seen_and_categories():
    """batch.resolve_seen names seen styles from index_map.json (GR2 decode only for unknown GR2s);
    load_categories reuses reach.json when it covers the styles and never fails the batch."""
    import json
    from tools.hair_groom import batch
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        (d / "index_map.json").write_text(json.dumps({"a.gr2": {"HairA": [1500, "00000000000000aa"]}}))
        rows = [{"name": "A", "style": {"gr2": "a.gr2", "mesh": "HairA"}},
                {"name": "B", "style": {"gr2": "b.gr2", "mesh": "HairB"}}]
        seen = prepare.parse_seen("1500 00000000000000aa\n")
        decoded = []
        orig = batch.map_gr2
        try:
            batch.map_gr2 = lambda *a: decoded.append(a) or ("b.gr2", {})
            assert batch.resolve_seen(rows, seen, None, None, d, 1, log=lambda *_: None) == 0 and not decoded
        finally:
            batch.map_gr2 = orig
        assert rows[0]["index_id"] == "00000000000000aa" and prepare.tier(rows[0], seen) == 0
        todo = [{"name": "A", "visual": "v"}]
        (d / "reach.json").write_text(json.dumps({"categories": {"A": "companion"}}))
        assert batch.load_categories(d, None, todo, log=lambda *_: None) == {"A": "companion"}
        # Not covered and classification impossible (no paks here): {} and the batch goes on.
        assert batch.load_categories(d, None, todo + [{"name": "B", "visual": "w"}], log=lambda *_: None) == {}
    raw = bytearray(b"GR2D" + struct.pack("<II", 1, 1))
    name = b"HairA"
    raw += struct.pack("<I", len(name)) + name + struct.pack("<3I", 3, 6, 0) + bytes(3 * 28) + struct.pack("<6I", 0, 1, 2, 0, 2, 1)
    from tools.hair_groom import asset
    got = batch.dump_meshes(bytes(raw))
    assert got == {"HairA": (6, f"{asset.index_id([0, 1, 2, 0, 2, 1]):016x}")}, got


if __name__ == "__main__":
    if "--write-fixture" in sys.argv:
        FIXTURE.mkdir(parents=True, exist_ok=True)
        v3, ref = fixture_bytes()
        (FIXTURE / "groom.v3.hgrm").write_bytes(v3)
        (FIXTURE / "groom.ref.hgrm").write_bytes(ref)
        print(f"wrote {FIXTURE}: v3 {len(v3)} bytes, reference {len(ref)} bytes")
        sys.exit(0)
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
    print("hair storage: codec round trip, thinned LODs, quantisation bounds, needed-first order and status")
