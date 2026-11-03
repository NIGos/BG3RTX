"""tools/hair_groom (card -> strand groom experiment) on synthetic cards and masks (CTest hair_groom_test)."""
import json
import struct
import sys
import tempfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.hair_groom import analysis, bench_data, convert, mesh, render, strands2d  # noqa: E402

FAILS = []


def check(name, ok, info=""):
    print(f"{'PASS' if ok else 'FAIL'} {name}{(': ' + str(info)) if not ok else ''}")
    if not ok:
        FAILS.append(name)


def strand_mask(h, w, centres, width):
    """alpha (h, w) with one painted strand per centre function row -> x."""
    a = np.zeros((h, w), np.uint8)
    for f in centres:
        for y in range(h):
            x = f(y)
            lo = int(np.floor(x - width / 2 + 0.5))
            a[y, max(0, lo):lo + width] = 255
    return a


def test_trace():
    a = strand_mask(200, 64, [lambda y: 16 + 3 * np.sin(y / 20), lambda y: 46.0], 3)
    traced, typical = strands2d.trace(a)
    check("trace finds two strands", len(traced) == 2, len(traced))
    check("trace typical width", typical == 3, typical)
    check("trace strands run root to tip", all(s.y[0] == 0 and s.y[-1] == 199 for s in traced))
    r = strands2d.redraw(traced, a.shape)
    check("trace redraw recall", (r & (a > 0)).sum() / (a > 0).sum() > 0.95)
    # Two strands crossing: the pieces cut at the crossing are stitched back to full length.
    b = strand_mask(240, 64, [lambda y: 10 + y * 0.18, lambda y: 53 - y * 0.18], 3)
    traced, _ = strands2d.trace(b)
    long_ = [s for s in traced if s.y[-1] - s.y[0] > 200]
    check("crossing strands stitched", len(long_) == 2, [(s.y[0], s.y[-1]) for s in traced])


def test_cross_sections():
    row = np.zeros(40, bool)
    row[5:8] = True       # one strand, width 3
    row[20:29] = True     # clump of three strands
    cx, cw = strands2d.cross_sections(row, 3.0)
    check("clump split", len(cx) == 4 and np.allclose(cw, 3), (cx, cw))
    check("clump centres", np.allclose(cx, [6, 21, 24, 27]), cx)


def test_stitch():
    a = strands2d.Strand2D(list(np.full(50, 10.0)), list(range(50)), [3] * 50)
    b = strands2d.Strand2D(list(np.full(50, 10.5)), list(range(60, 110)), [3] * 50)
    c = strands2d.Strand2D(list(np.full(50, 30.0)), list(range(60, 110)), [3] * 50)
    out = strands2d.stitch([a, b, c])
    check("stitch joins the aligned piece only", sorted(len(s.y) for s in out) == [50, 100], [len(s.y) for s in out])


def card_mesh(length=0.2, width=0.02, rows=4):
    """One vertical card strip from y=0 (root) down to -length, UV covering the whole mask."""
    pos, uv, joints, weights = [], [], [], []
    for r in range(rows + 1):
        v = r / rows
        for u in (0.0, 1.0):
            pos.append([(u - 0.5) * width, -v * length, 0.0])
            uv.append([u, v])
            joints.append([0, 1, 0, 0])
            weights.append([1 - v, v, 0, 0])
    tris = []
    for r in range(rows):
        i = 2 * r
        tris += [[i, i + 1, i + 2], [i + 1, i + 3, i + 2]]
    return mesh.Mesh(np.array(pos, np.float32), np.array(uv, np.float32), np.array(joints), np.array(weights, np.float32),
                     np.array(tris), ["A", "B"], np.zeros((2, 3), np.float32))


def test_convert():
    h, w = 256, 64
    m = np.zeros((h, w, 4), np.uint8)
    m[..., 3] = strand_mask(h, w, [lambda y: 32.0], 4)
    m[..., 2] = np.linspace(0, 255, h)[:, None].astype(np.uint8)
    card = card_mesh()
    groom, report = convert.convert(card, m, tile_grid=(1, 1), step=16)
    check("convert one strand", groom.strands == 1, groom.strands)
    p = groom.points
    check("strand on the card centre line", np.allclose(p[:, 0], 0.0, atol=1e-6) and np.allclose(p[:, 2], 0))
    check("strand root at the card root", abs(p[0, 1] - (-0.2 * 0.5 / h)) < 1e-4, p[0])
    check("strand tip at the card tip", abs(p[-1, 1] - (-0.2 * 255.5 / h)) < 1e-4, p[-1])
    # Width: 4 px of 64 across a 2 cm card = 1.25 mm, radius 0.625 mm.
    check("radius from painted width", np.allclose(groom.radii, 0.000625, rtol=0.02), groom.radii[:3])
    v = -p[:, 1] / 0.2
    check("bone weights follow the strand", np.allclose(groom.weights[:, 0], np.maximum(v, 1 - v), atol=1e-3)
          and np.allclose(groom.weights.sum(1), 1), groom.weights[:3])
    emb = np.einsum("pk,pkd->pd", groom.bary, card.positions[card.tris[groom.triangle]])
    check("embedding reproduces the points", np.allclose(emb, p, atol=1e-6))
    check("root channel carried", groom.root_param[0] < 0.05 and groom.root_param[-1] > 0.95)
    check("no overlap reported", report["uv_overlap_samples"] == 0 and report["unmapped_samples"] == 0, report)
    o = analysis.orientation(groom, np.array([0.0, 0.05, 0.0]))
    check("orientation: root nearer the head", o["strands_reversed"] == 0 and o["strands_decided"] == 1, o)
    o = analysis.orientation(groom, np.array([0.0, -0.3, 0.0]))
    check("orientation: reversed card detected", o["strands_reversed"] == 1 and o["cards_mostly_reversed"] == 1, o)
    # A second identical card on top: every point is a layered duplicate.
    two = card_mesh()
    two.positions = np.concatenate([two.positions, two.positions + [0, 0, 1e-4]]).astype(np.float32)
    for k in ("uv", "joints", "weights"):
        setattr(two, k, np.concatenate([getattr(two, k)] * 2))
    two.tris = np.concatenate([two.tris, two.tris + 10])
    g2, _ = convert.convert(two, m, tile_grid=(1, 1), step=16)
    lay = analysis.layered_duplicates(g2)
    check("layered cards detected", g2.strands == 2 and lay["strands_mostly_duplicated"] == 2, lay)


def test_quality_options():
    h, w = 256, 64
    m = np.zeros((h, w, 4), np.uint8)
    m[..., 3] = strand_mask(h, w, [lambda y: 32.0], 4)
    m[..., 1] = 255
    card = card_mesh()
    # 4 px of 64 across 2 cm = 1.25 mm; a 0.18 mm limit gives 7 fibres of 1.25/7 mm side by side.
    g, rep = convert.convert(card, m, tile_grid=(1, 1), step=16, max_fibre_diameter=0.00018)
    check("split fibre count", g.strands == 7 and rep["split_strands"] == 1 and rep["fibres_from_splits"] == 7, (g.strands, rep))
    check("split fibre width", np.allclose(g.radii * 2, 0.00125 / 7, rtol=0.02), g.radii[:2])
    xs = np.sort(g.points[g.offsets[:-1], 0])
    check("split fibres span the painted width", np.allclose(np.diff(xs), 0.00125 / 7, rtol=0.02) and abs(xs.mean()) < 1e-6, xs)
    emb = np.einsum("pk,pkd->pd", g.bary, card.positions[card.tris[g.triangle]])
    check("split embedding exact", np.allclose(emb, g.points, atol=1e-6))
    # Depth channel at 1.0 with a 1 mm offset: points move 1 mm toward the head (+z here).
    g, _ = convert.convert(card, m, tile_grid=(1, 1), step=16, depth_offset=0.001, head_centre=np.array([0, -0.1, 0.05]))
    check("depth offset toward head", np.allclose(g.points[:, 2], 0.001) and np.allclose(g.normal_offset, 0.001), g.points[:2])
    # Two stacked identical cards: dedupe keeps one strand.
    two = card_mesh()
    two.positions = np.concatenate([two.positions, two.positions + [0, 0, 1e-4]]).astype(np.float32)
    for key in ("uv", "joints", "weights"):
        setattr(two, key, np.concatenate([getattr(two, key)] * 2))
    two.tris = np.concatenate([two.tris, two.tris + 10])
    g2, _ = convert.convert(two, m, tile_grid=(1, 1), step=16)
    d, dropped = convert.dedupe(g2)
    check("dedupe drops the stacked copy", dropped == 1 and d.strands == 1 and d.card.tolist() == [0], (dropped, d.card))
    # Far apart (5 mm): both kept.
    two.positions[10:, 2] = 0.005
    g3, _ = convert.convert(two, m, tile_grid=(1, 1), step=16)
    check("dedupe keeps separate strands", convert.dedupe(g3)[1] == 0)


def test_locate_overlap():
    uv = np.array([[0, 0], [1, 0], [0, 1], [1, 1]], np.float32)
    tris = np.array([[0, 1, 2], [0, 1, 2]])  # the same UV triangle twice: an overlap
    hit, bary, inside = convert.locate(uv, tris, np.array([[0.2, 0.2], [0.9, 0.9]]))
    check("locate hit and miss", list(hit) == [0, -1], hit)
    check("locate counts overlap", list(inside) == [2, 0], inside)
    check("locate barycentrics", np.allclose(bary[0], [0.6, 0.2, 0.2]), bary[0])


def test_render():
    view = render.View(np.zeros(3), np.array([0, 0, 1.0]), np.array([0, 1.0, 0]), 0.001, (4, 4))
    pix = np.array([5, 5])
    image, trans = render.composite(pix, np.array([2.0, 1.0]), np.array([0.5, 0.5]),
                                    np.array([[0, 0, 1.0], [1.0, 0, 0]]), 16)
    check("composite transmittance", np.isclose(trans[5], 0.25), trans[5])
    check("composite order", np.allclose(image[5], [0.5, 0, 0.25], atol=1e-6), image[5])
    # A horizontal strand of radius 0.1 px across the view: coverage ~ 2r/pixel per pixel row.
    pts = np.array([[-0.002, 0.0005, 0], [0.002, 0.0005, 0]], np.float32)
    f = render.strand_fragments(pts, np.full(2, 0.0001, np.float32), np.array([0, 2]), np.zeros(2), view)
    cov = np.zeros(16)
    np.add.at(cov, f.pixel, f.alpha)
    check("strand coverage = 2r / pixel", np.allclose(cov[4:8], 0.2, atol=1e-6), cov.reshape(4, 4))


def test_mesh_loader():
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        pos = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0]], np.float32)
        uv = np.array([[0, 0], [1, 0], [0, 1]], np.float32)
        joints = np.array([[0, 1, 0, 0]] * 3, np.uint8)
        weights = np.array([[0.5, 0.5, 0, 0]] * 3, np.float32)
        ibm = np.eye(4, dtype=np.float32)
        ibm2 = np.eye(4, dtype=np.float32)
        ibm2[3, :3] = [0, -2, 0]   # column-major translation: joint origin at y = 2
        idx = np.array([0, 1, 2], np.uint16)
        # Interleaved vertex view (stride 20: position + uv) to check strided accessors.
        inter = np.concatenate([pos, uv], 1).astype(np.float32).tobytes()
        blobs = [inter, joints.tobytes(), weights.tobytes(), np.stack([ibm, ibm2]).tobytes(), idx.tobytes() + b"\0\0"]
        views, off = [], 0
        for i, b in enumerate(blobs):
            views.append({"buffer": 0, "byteOffset": off, "byteLength": len(b), **({"byteStride": 20} if i == 0 else {})})
            off += len(b)
        acc = [{"bufferView": 0, "componentType": 5126, "type": "VEC3", "count": 3},
               {"bufferView": 0, "byteOffset": 12, "componentType": 5126, "type": "VEC2", "count": 3},
               {"bufferView": 1, "componentType": 5121, "type": "VEC4", "count": 3},
               {"bufferView": 2, "componentType": 5126, "type": "VEC4", "count": 3},
               {"bufferView": 3, "componentType": 5126, "type": "MAT4", "count": 2},
               {"bufferView": 4, "componentType": 5123, "type": "SCALAR", "count": 3}]
        gltf = {"buffers": [{"uri": "m.bin", "byteLength": off}], "bufferViews": views, "accessors": acc,
                "meshes": [{"name": "hair", "primitives": [{"attributes": {"POSITION": 0, "TEXCOORD_0": 1, "JOINTS_0": 2,
                                                                             "WEIGHTS_0": 3}, "indices": 5}]}],
                "nodes": [{"name": "J0"}, {"name": "J1"}, {"mesh": 0, "skin": 0}],
                "skins": [{"joints": [0, 1], "inverseBindMatrices": 4}]}
        (d / "m.gltf").write_text(json.dumps(gltf))
        (d / "m.bin").write_bytes(b"".join(blobs))
        m = mesh.load(d / "m.gltf", "hair")
        check("loader strided positions", np.allclose(m.positions, pos) and np.allclose(m.uv, uv))
        check("loader skin + indices", m.joints.tolist() == joints.tolist() and m.tris.tolist() == [[0, 1, 2]])
        check("loader joint origins", m.joint_names == ["J0", "J1"] and np.allclose(m.joint_positions[1], [0, 2, 0]),
              m.joint_positions)


def test_bench_data():
    starts = bench_data.segment_starts(np.array([0, 3, 5, 9]))
    check("segment starts skip strand ends", starts.tolist() == [0, 1, 3, 5, 6, 7], starts)
    h, w = 64, 16
    m = np.zeros((h, w, 4), np.uint8)
    m[..., 3] = strand_mask(h, w, [lambda y: 8.0], 2)
    card = card_mesh()
    groom, _ = convert.convert(card, m, tile_grid=(1, 1), step=16)
    with tempfile.TemporaryDirectory() as d:
        path = Path(d) / "b.bin"
        bench_data.write(path, card, m, [(16, groom)])
        raw = path.read_bytes()
    hdr = struct.unpack("<4s7I", raw[:32])
    check("bench header", hdr[0] == b"HLSB" and hdr[2:7] == (10, 8, w, h, 1), hdr)
    at = 32 + 10 * 12 + 10 * 8 + 8 * 12 + h * w * 4
    step, points, segments, strands = struct.unpack("<4I", raw[at:at + 16])
    check("bench groom counts", (step, points, segments, strands) == (16, len(groom.points), groom.segments, groom.strands))
    end = at + 16 + points * (12 + 4) + segments * 4 + points * (4 + 12)
    check("bench file size", len(raw) == end, (len(raw), end))


for t in (test_bench_data, test_quality_options, test_trace, test_cross_sections, test_stitch, test_convert, test_locate_overlap, test_render, test_mesh_loader):
    t()
print(f"{len(FAILS)} failures" if FAILS else "all passed")
sys.exit(1 if FAILS else 0)
