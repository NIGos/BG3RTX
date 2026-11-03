"""tools/hair_groom (card -> strand groom experiment) on synthetic cards and masks (CTest hair_groom_test)."""
import json
import struct
import sys
import tempfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.hair_groom import analysis, asset, batch, bench_data, bsdf, convert, dual, geom, mesh, primary, render, strands2d  # noqa: E402

FAILS = []
INDEX_ID_FIXTURE = 0xce53baad8d797436


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


def test_thin():
    h, w = 256, 64
    m = np.zeros((h, w, 4), np.uint8)
    m[..., 3] = strand_mask(h, w, [lambda y, c=c: c for c in np.arange(4, 60, 4.0)], 2)
    g, _ = convert.convert(card_mesh(), m, tile_grid=(1, 1), step=16)
    t = convert.thin(g, 0.5)
    check("thin keeps about half", 0.25 * g.strands <= t.strands <= 0.75 * g.strands, (g.strands, t.strands))
    check("thin widens radii", np.allclose(t.radii, g.radii[0] * 2), t.radii[:2])
    check("thin is deterministic", convert.thin(g, 0.5).strands == t.strands)
    check("thin 1.0 is identity", convert.thin(g, 1.0) is g)
    wide = convert.thin(g, 0.5)
    wide.radii[:] = 0.004
    check("thin caps radii below the layer's bound", convert.thin(wide, 0.25).radii.max() <= convert.MAX_THIN_RADIUS)


def test_asset():
    # The canonical v1 fixture of tests/mesh_content_id_test.cpp (layer/mesh_content_id.h).
    quad = np.array([[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]], np.float32)
    cid, words = asset.content_id(quad, [0, 1, 2, 0, 2, 3])
    check("content id matches the layer fixture", cid == 0x5f673b88e4625698, hex(cid))
    check("content id word count", len(words) == 3 + 32 * 4)
    h, w = 256, 64
    m = np.zeros((h, w, 4), np.uint8)
    m[..., 3] = strand_mask(h, w, [lambda y, c=c: c for c in np.arange(4, 60, 4.0)], 2)
    m[..., 2] = np.linspace(0, 255, h)[:, None].astype(np.uint8)
    card = card_mesh()
    lods = asset.build(card, m)
    with tempfile.TemporaryDirectory() as d:
        path = Path(d) / "x.hgrm"
        asset.write(path, 0x1234, card, lods)
        raw = path.read_bytes()
        try:
            asset.write(path, 0x1234, card, lods)
            check("asset never overwrites", False)
        except FileExistsError:
            check("asset never overwrites", True)
    magic, ver, cid, iid, nv, ni, nl, _ = struct.unpack("<4sIQQ4I", raw[:40])
    check("asset header", (magic, ver, cid, nv, ni, nl) == (b"HGRM", 2, 0x1234, 10, 24, 3) and iid == asset.index_id(card.tris))
    uvs = np.frombuffer(raw, "<f4", 64, 40).reshape(32, 2)
    corners = card.tris.ravel()
    check("asset UV samples: UV0 at the sampled corners", np.allclose(uvs[0], card.uv[corners[0]]) and
          np.allclose(uvs[31], card.uv[corners[-1]]) and np.allclose(uvs[16], card.uv[corners[16 * 23 // 31]]), uvs[:2])
    # Shared fixture with tests/hair_groom_asset_test.cpp (C++ index_content_id).
    check("index id fixture", asset.index_id([0, 1, 2, 0, 2, 3]) == INDEX_ID_FIXTURE, hex(asset.index_id([0, 1, 2, 0, 2, 3])))
    at, sizes = 40 + 256, []
    for keep, g in lods:
        k, p, sgm, _ = struct.unpack("<f3I", raw[at:at + 16])
        sizes.append((k, p, sgm))
        tri = np.frombuffer(raw, "<u4", p, at + 16)
        bary = np.frombuffer(raw, "<f4", 2 * p, at + 16 + 4 * p).reshape(p, 2)
        full = np.c_[1 - bary.sum(1), bary]
        pts = np.einsum("pk,pkd->pd", full, card.positions[card.tris[tri]])
        check(f"asset LOD {keep} reproduces points", np.allclose(pts, g.points, atol=1e-6))
        at += 16 + p * (4 + 8 + 4 + 1) + sgm * 4
    check("asset size", at == len(raw), (at, len(raw)))
    check("asset LODs thin", sizes[0][1] > sizes[1][1] > sizes[2][1], sizes)


def test_geom():
    count, label = geom.connected_components(np.array([[0, 1, 2], [2, 3, 4], [5, 6, 7]]), 9)
    check("components", count == 3 and len(set(label[:5])) == 1 and label[5] == label[7] != label[0] and label[8] not in label[:8])
    rng = np.random.default_rng(3)
    pts = rng.random((400, 3)) * 0.01
    got = {tuple(sorted(x)) for x in geom.pairs_within(pts, 0.001).tolist()}
    d = np.linalg.norm(pts[:, None] - pts[None], axis=2)
    want = {(i, j) for i, j in zip(*np.nonzero(d < 0.001)) if i < j}
    check("pairs within distance = brute force", got == want and len(want) > 10, (len(got), len(want)))


def test_batch_catalogue():
    check("tile grid of one full-atlas card", convert.detect_tile_grid(card_mesh()) == ((1, 1), 1.0))
    quarter = card_mesh()
    quarter.uv = (quarter.uv * [0.25, 0.5] + [0.5, 0.5]).astype(np.float32)
    check("tile grid 4 x 2", convert.detect_tile_grid(quarter) == ((4, 2), 1.0), convert.detect_tile_grid(quarter))
    hair = {"source": "Public/Shared/Assets/Materials/Characters/CHAR_Hair.lsf",
            "native": {"children": [{"_": "Texture2DParameters", "ParameterName": "ID_Depth_Root_Alpha_MSKA", "ID": "t1"}]}}
    doc = {"materials": {"m1": hair, "m2": {"source": "x/CHAR_Skin.lsf", "native": {}}},
           "textures": {"t1": {"native": {"SourceFile": "Generated/A_MSKA.DDS"}}},
           "rows": [{"lod": 0, "material_id": "m1", "object_id": "HAIR_A.HAIR_A_Mesh.1", "source": "G/HAIR_A.GR2", "visual_id": "v1"},
                    {"lod": 1, "material_id": "m1", "object_id": "HAIR_A.HAIR_A_Mesh_LOD1.3", "source": "G/HAIR_A.GR2", "visual_id": "v1"},
                    {"lod": 0, "material_id": "m1", "object_id": "HAIR_A.HAIR_A_Mesh.1", "source": "G/HAIR_A.GR2", "visual_id": "v2"},
                    {"lod": 0, "material_id": "m2", "object_id": "HEAD.HEAD_Mesh.0", "source": "G/HEAD.GR2", "visual_id": "v3"}]}
    with tempfile.TemporaryDirectory() as d:
        path = Path(d) / "cat.json"
        path.write_text(json.dumps(doc))
        got = batch.styles(path)
        check("batch: one CHAR_Hair style per GR2 and mesh (LOD0 and LOD1), mask resolved",
              got == [{"name": "HAIR_A:HAIR_A_Mesh", "gr2": "G/HAIR_A.GR2", "mesh": "HAIR_A_Mesh", "lod": 0, "mask": "Generated/A_MSKA.DDS", "visual": "v1"},
                      {"name": "HAIR_A:HAIR_A_Mesh_LOD1", "gr2": "G/HAIR_A.GR2", "mesh": "HAIR_A_Mesh_LOD1", "lod": 1, "mask": "Generated/A_MSKA.DDS", "visual": "v1"}], got)
        check("batch: name filter", batch.styles(path, ["nothing"]) == [])


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


def test_bsdf():
    for beta in (0.2, 0.41, 0.7):
        a, se = bsdf.furnace(beta, n=200000, return_se=True)
        # Monte Carlo: within 4 standard errors (narrow lobes at low roughness are the noisy ones).
        check(f"chiang white furnace beta {beta}", np.all(np.abs(a - 1) < np.maximum(0.01, 4 * se)), (a, se))
    check("lambert fibre white furnace", np.all(np.abs(bsdf.furnace(0.41, n=200000, model="hit") - 1) < 0.03))
    check("white colour has no absorption", np.all(bsdf.sigma_a_from_color((1, 1, 1), 0.41) == 0))
    a = bsdf.furnace(0.41, bsdf.CHAR_HAIR["color"], n=100000)
    check("absorbing colour: albedo below 1, red > green > blue", a[0] < 1 and a[0] > a[1] > a[2], a)
    g = bsdf.furnace(0.41, bsdf.CHAR_HAIR["color"], n=20000, model="game")
    check("game hair finite and positive", np.all(np.isfinite(g)) and np.all(g > 0), g)
    _, lo = bsdf.lobe_sweep("game", backlit=0.2)
    _, hi = bsdf.lobe_sweep("game", backlit=0.4)
    check("game HairBacklit scales the backlit end", hi[-1].mean() > 1.5 * lo[-1].mean(), (lo[-1], hi[-1]))
    _, ch = bsdf.lobe_sweep("chiang")
    check("chiang forward scattering (TT) above back scattering", ch[-1].mean() > 5 * ch[0].mean(), ch[[0, -1]])
    w = bsdf.slab_albedo((1, 1, 1), 0.41, n=2000, bounces=4000)
    check("white fibre slab reflects nearly everything", np.all(w > 0.9), w)

def test_primary():
    rng = np.random.default_rng(5)
    v = rng.normal(size=(20000, 3))
    v /= np.linalg.norm(v, axis=1, keepdims=True)
    # RT0 is RG16F: round through float16 like the target. Normals facing the camera (view z < 0.5) only:
    # near the pole n.z = 1 the stereographic value diverges, the game's own precision limit.
    nv = v[v[:, 2] < 0.5]
    back = primary.stereo_decode(primary.stereo_encode(nv).astype(np.float16).astype(np.float64))
    err = np.degrees(np.arccos(np.clip((back * nv).sum(1), -1, 1)))
    check("RT0 stereographic normal round trip through the game's decode < 0.2 deg", err.max() < 0.2, err.max())
    back = primary.oct12_decode(primary.oct12_encode(v))
    err = np.degrees(np.arccos(np.clip((back * v).sum(1), -1, 1)))
    # 12 bits over [-1, 1] per octahedral axis: a step of 2/4094; the angle error stays below ~0.06 deg.
    check("RT3 12:12 octahedral tangent round trip through bg3_oct12 < 0.1 deg", err.max() < 0.1, err.max())
    bytes_ok = np.allclose(primary.oct12_encode(v) * 255, np.rint(primary.oct12_encode(v) * 255))
    check("RT3 encoding is whole bytes", bytes_ok)
    check("RT2.w class 5 with light mask 3", primary.rt2_w_decode(primary.rt2_w(5, 3)) == (5, False, 3))
    # Depth along the camera ray: explicit native projection vs the ratio formula.
    near, far = 0.1, 1000.0
    a = primary.depth_a(near, far)
    b = -a * far
    check("native reversed depth: near 1, far 0", abs(a + b / near - 1) < 1e-9 and abs(a + b / far) < 1e-12)
    dirs = rng.normal(size=(1000, 3))
    dirs[:, 2] = np.abs(dirs[:, 2]) + 0.5
    dirs /= np.linalg.norm(dirs, axis=1, keepdims=True)
    tc, ts = rng.uniform(0.3, 50, 1000), rng.uniform(0.3, 50, 1000)
    dc, ds = a + b / (tc * dirs[:, 2]), a + b / (ts * dirs[:, 2])
    rel = np.abs(primary.strand_depth(dc, tc, ts, a) - ds) / ds
    check("strand depth from the card fragment's depth equals the projected hit", rel.max() < 1e-9, rel.max())
    check("orthographic strand depth is affine", abs(primary.strand_depth_ortho(0.5, 2.0, 3.0, -0.01) - 0.49) < 1e-12)
    t = rng.normal(size=(500, 3))
    t /= np.linalg.norm(t, axis=1, keepdims=True)
    to_cam = rng.normal(size=(500, 3))
    n = primary.fibre_normal(t, to_cam)
    check("fibre normal perpendicular to the tangent, facing the camera",
          np.abs((n * t).sum(1)).max() < 1e-9 and np.all((n * to_cam).sum(1) >= -1e-12))
    # Tracer: one horizontal strand of radius 2 mm, 1 m in front of a 65 px camera (row 32 on the axis), 10 deg field.
    r = 0.002
    pts = np.array([[-0.05, 0.0, 1.0], [0.05, 0.0, 1.0]])
    cam = primary.Camera(np.zeros(3), np.array([0.0, 0.0, 1.0]), np.array([0.0, 1.0, 0.0]), np.radians(10.0), (65, 65))
    hits = primary.trace(pts, np.array([r, r]), np.array([0, 2]), cam)
    pixel = 2 * np.tan(np.radians(5.0)) / 65            # metres per pixel at 1 m
    rows = np.flatnonzero(hits.hit[:, 32])
    check("tracer: strand covers 2 r / pixel rows", abs(len(rows) - 2 * r / pixel) <= 1, (len(rows), 2 * r / pixel))
    centre = hits.t[rows, 32].min()
    check("tracer: front distance 1 m - r on the axis row", abs(centre - (1.0 - r)) < 2e-4, centre)
    check("tracer: tangent +x, parameter at the centre column ~0.5",
          np.allclose(hits.tangent[rows[0], 32], [1, 0, 0]) and abs(hits.v[rows[0], 32] - 0.5) < 0.02)
    p0, p1 = pts[0], pts[1]
    hit = hits.t[rows[0], 32] * cam.rays().reshape(65, 65, 3)[rows[0], 32]
    check("segment parameter of the hit equals the tracer's", abs(primary.segment_param(hit, p0, p1) - hits.v[rows[0], 32]) < 1e-9)
    gb = primary.gbuffer(hits, cam)
    nb = primary.stereo_decode(gb["rt0"][rows[0], 32])
    check("tracer: G-buffer normal faces the camera (view z < 0)", nb[2] < -0.9, nb)
    # Motion vector: a world-static point, camera moved 5 cm; the jitter-free pixels of both frames differ by the MV.
    size = np.array([1280.0, 720.0])

    def vp_at(cam_pos):
        f, r, u = np.array([0.0, 0.0, 1.0]), np.array([1.0, 0.0, 0.0]), np.array([0.0, 1.0, 0.0])
        m = np.zeros((4, 4))
        k = np.tan(np.radians(25.0))
        m[:3, 0], m[:3, 1], m[:3, 3] = r / (k * size[0] / size[1]), u / k, f
        m[:3, 2], m[3, 2] = -1e-4 * f, 0.1
        return m

    def pixel(p_rel, vp):
        c = np.append(p_rel, 1.0) @ vp
        return ((c[:2] / c[3]) * np.array([0.5, -0.5]) + 0.5) * size

    world, cam0, cam1 = np.array([0.1, 1.6, 2.0]), np.array([0.0, 1.5, 0.0]), np.array([0.05, 1.5, 0.0])
    jit, pjit = np.array([0.3, -0.2]), np.array([-0.1, 0.4])
    frag = pixel(world - cam1, vp_at(cam1)) - np.array([jit[0], -jit[1]])   # the jittered raster position
    mv = primary.motion_vector(frag, world - cam0, vp_at(cam0), np.ones(2), np.zeros(2), size, jit, pjit)
    want = pixel(world - cam1, vp_at(cam1)) - pixel(world - cam0, vp_at(cam0)) - np.array([pjit[0], -pjit[1]])
    check("motion vector of a static point = jitter-free pixel difference (native b70cadc5 form)",
          np.allclose(mv, want, atol=1e-9), (mv, want))
    check("tracer: G-buffer tangent decodes to +x", np.allclose(primary.oct12_decode(gb["rt3"][rows[0], 32]), [1, 0, 0], atol=1e-3))


def test_dual():
    # Dual scattering (Zinke 2008, paper densities 0.7) and its path-traced per-channel correction (TABLE_K) vs a
    # path-traced slab of random fibres, flux leaving toward the upper hemisphere. Measured 2026-11-03 for the
    # CHAR_Hair colour: single 0.21/0.29/0.40, dual 0.70/0.73/0.75, corrected 1.02/1.01/1.01 of the reference;
    # held-out colours corrected 0.97-1.04 (one 1.12 at 18% reference spread).
    c, r = bsdf.CHAR_HAIR["color"], bsdf.CHAR_HAIR["roughness"]
    ref = dual.slab_retro_reference(c, r, n=6000, seed=21)
    ss = dual.slab_retro_dual(c, r, n=60000, single_only=True)
    ds = dual.slab_retro_dual(c, r, n=60000)
    dk = dual.slab_retro_dual(c, r, n=60000, corrected=True)
    # Bounds: two 12000-path references of this colour spread <= 3%; 6000 paths and a 10% margin.
    check("corrected dual scattering within 10% of the path-traced hair", np.all(np.abs(dk / ref - 1) < 0.1), dk / ref)
    check("uncorrected dual scattering below it (0.6..0.85)", np.all(ds / ref > 0.6) and np.all(ds / ref < 0.85), ds / ref)
    check("single scattering alone well below dual scattering", np.all(ss / ref < ds / ref - 0.2), (ss / ref, ds / ref))
    hue = lambda v: v / v.sum()
    check("corrected model keeps the reference hue (chromaticity within 0.02)", np.abs(hue(dk) - hue(ref)).max() < 0.02,
          (hue(dk), hue(ref)))
    av = dual.averages((1, 1, 1), r, n=100000)
    check("white fibre: forward + backward fractions ~1 (energy)", np.all(np.abs(av.a_f + av.a_b - 1) < 0.03), av.a_f + av.a_b)
    check("correction table: k > 1 everywhere (the model misses multiple scattering, never adds)",
          np.all(np.asarray(dual.TABLE_K) > 1))
    gen = (ROOT / "shaders" / "rt" / "hair_dual_table.hlsli").read_text(encoding="utf-8")
    check("shaders/rt/hair_dual_table.hlsli is current (python -m tools.hair_groom.dual --hlsl)", gen == dual.hlsl_tables())
    # The shader's table model against the per-colour Monte Carlo averages (interpolation over the grid).
    rng = np.random.default_rng(9)
    k = 20000
    tt, wo, wi = (rng.normal(size=(k, 3)) for _ in range(3))
    tt, wo, wi = (v / np.linalg.norm(v, axis=1, keepdims=True) for v in (tt, wo, wi))
    h, nf = rng.uniform(-1, 1, k), rng.integers(0, 4, k)
    a = dual.shade_table(tt, wo, wi, h, nf, c, r).mean(0)
    b_ = dual.shade(tt, wo, wi, h, nf, c, r, corrected=True).mean(0)
    check("table model within 5% of the Monte Carlo averages (CHAR_Hair colour)", np.all(np.abs(a / b_ - 1) < 0.05), a / b_)


for t in (test_dual, test_primary, test_bsdf, test_batch_catalogue, test_geom, test_asset, test_bench_data, test_quality_options, test_thin, test_trace, test_cross_sections, test_stitch, test_convert, test_locate_overlap, test_render, test_mesh_loader):
    t()
print(f"{len(FAILS)} failures" if FAILS else "all passed")
sys.exit(1 if FAILS else 0)
