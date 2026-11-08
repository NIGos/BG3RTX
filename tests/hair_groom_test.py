"""tools/hair_groom (card -> strand groom experiment) on synthetic cards and masks (CTest hair_groom_test)."""
import dataclasses
import json
import struct
import sys
import tempfile
from io import BytesIO
from pathlib import Path
from unittest.mock import patch

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.low_priority import lower_priority
lower_priority()
from tools.hair_groom import analysis, asset, batch, bench_data, bsdf, convert, cost, curves2d, dual, geom, mesh, primary, render, sample, strands2d  # noqa: E402

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


def test_reorient():
    points = np.array([[.3, 0, 0], [.2, 0, 0], [.05, 0, 0],
                       [.21, -.05, 0], [.205, 0, 0], [.2, .05, 0],
                       [.05, 0, 0], [.15, 0, 0], [.3, 0, 0],
                       [.1, 0, 0], [.1, 0, 0], [.1, 0, 0]], np.float32)
    n = len(points)
    offset = np.arange(n, dtype=np.float32) * .0001
    triangles = np.repeat(points[:, None, :], 3, axis=1)
    triangles[:, :, 2] -= offset[:, None]
    triangles[:, 1, 0] += .01
    triangles[:, 2, 1] += .01
    g = convert.Groom(points, np.arange(n, dtype=np.float32) * .00001,
                      np.arange(n * 4).reshape(n, 4), np.arange(n * 4, dtype=np.float32).reshape(n, 4) / 100,
                      np.arange(0, n + 1, 3), np.arange(4), np.full(n, .7, np.float32),
                      np.arange(n), np.tile([1., 0., 0.], (n, 1)), offset)
    original = {f.name: getattr(g, f.name).copy() for f in dataclasses.fields(g)}
    out, count = analysis.reorient(g, np.zeros(3))
    check("tangential strand with a nearer tip remains undecided", analysis.orientation(g, np.zeros(3))["strands_reversed"] == 2)
    permutation = np.r_[2, 1, 0, np.arange(3, n)]
    check("reorient flips only the inward radial strand", count == 1 and
          np.array_equal(out.points, points[permutation]))
    check("reorient reverses all per-point arrays consistently", all(
        np.array_equal(getattr(out, name), original[name][permutation]) for name in
        ("points", "radii", "joints", "weights", "triangle", "bary", "normal_offset")))
    embedded = np.einsum("pk,pkd->pd", out.bary, triangles[out.triangle])
    embedded[:, 2] += out.normal_offset
    check("reoriented embedding still reproduces points including normal offsets", np.allclose(embedded, out.points, atol=1e-7))
    check("reorient root parameter follows arc length, including unchanged strands", np.allclose(
        out.root_param, [0, .6, 1, 0, .5, 1, 0, .4, 1, 0, .5, 1]))
    check("reorient preserves converter arrays and strand metadata", all(
        np.array_equal(getattr(g, name), value) for name, value in original.items()) and
        np.array_equal(out.offsets, g.offsets) and np.array_equal(out.card, g.card))
    again, count = analysis.reorient(out, np.zeros(3))
    check("reorient is idempotent", count == 0 and all(np.array_equal(getattr(again, f.name), getattr(out, f.name))
          for f in dataclasses.fields(out)))


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


def curl_fixture(size=192, width=4):
    """A synthetic curly mask (R id, G 0, B root..tip, A coverage) and its painted centrelines (root to tip).

    Strand 1 is a curl running sideways (a prolate cycloid: a loop that crosses itself, then up again, so its rows do
    not run root to tip; tightest radius of curvature (d - r)^2 / d = 20 px, five strand widths, as the curls of
    HAIR_Afro_B); strand 2 rises from the bottom left and crosses it on top with another ID."""
    t = np.linspace(0, 1, 6000)
    s = 2.6 * np.pi * t
    curl = np.stack([45 + 12.0 * s - 40 * np.sin(s), 96 + 40 * np.cos(s)], 1)
    cross = np.stack([20 + 160 * t, 180 - 160 * t], 1)
    m = np.zeros((size, size, 4), np.uint8)
    yy, xx = np.mgrid[0:size, 0:size]
    for sid, c in ((90, curl), (200, cross)):
        arc = np.r_[0, np.cumsum(np.hypot(*np.diff(c, axis=0).T))]
        for (x, y), a in zip(c[::4], arc[::4] / arc[-1]):
            disc = (xx - x) ** 2 + (yy - y) ** 2 <= (width / 2) ** 2
            m[disc] = (sid, 0, int(round(a * 255)), 255)
    return m, [curl, cross]


def test_curves2d():
    m, truth = curl_fixture()
    corr = curves2d.root_row_correlation(m, (1, 1))
    rows = np.zeros((128, 128, 4), np.uint8)
    rows[..., 3] = strand_mask(128, 128, [lambda y: 30.0, lambda y: 90.0], 4)
    rows[..., 2] = np.linspace(0, 255, 128)[:, None].astype(np.uint8)
    check("curly detection: the curl's root channel does not run along the rows", corr is not None and corr < convert.CURLY_ROOT_CORR
          and convert.is_curly(m, (1, 1)) and not convert.is_curly(rows, (1, 1)), corr)
    curves, typ, claimed = curves2d.trace(m)
    cov = m[..., 3] >= 128
    length = [float(np.hypot(np.diff(c.x), np.diff(c.y)).sum()) for c in curves]
    long_ = [c for c, n in zip(curves, length) if n > 40]
    check("curves2d: two strands, the curl not cut at its crossings", len(long_) == 2, sorted(length))
    check("curves2d: typical width (painted discs are 2 r + 1 = 5 px across)", np.all((typ >= 4) & (typ <= 7)), np.unique(typ))
    check("curves2d: recall", (claimed & cov).sum() / cov.sum() > 0.9, (claimed & cov).sum() / cov.sum())
    for k, true in enumerate(truth):
        tl = np.hypot(*np.diff(true, axis=0).T).sum()
        # The traced curve closest to this painted strand: centreline distance and covered length.
        best = min(long_, key=lambda c: np.mean(np.min(np.hypot(c.x[:, None] - true[None, ::20, 0], c.y[:, None] - true[None, ::20, 1]), 1)))
        d = np.min(np.hypot(best.x[:, None] - true[None, ::4, 0], best.y[:, None] - true[None, ::4, 1]), 1)
        rx, ry = curves2d.resample(best, 4, 0.25)[:2]   # smoothed: the raw centres zigzag by half a texel
        got = np.hypot(np.diff(rx), np.diff(ry)).sum()
        check(f"curves2d strand {k}: on the painted centreline", np.median(d) < 0.75 and np.percentile(d, 95) < 1.5,
              (np.median(d), np.percentile(d, 95)))
        check(f"curves2d strand {k}: full length", 0.85 * tl < got < 1.1 * tl, (got, tl))
        check(f"curves2d strand {k}: root to tip", np.hypot(best.x[0] - true[0, 0], best.y[0] - true[0, 1]) < 4
              and np.hypot(best.x[-1] - true[-1, 0], best.y[-1] - true[-1, 1]) < 4 and best.root_corr > 0.95,
              ((best.x[0], best.y[0]), (best.x[-1], best.y[-1]), best.root_corr))
    # Resampling keeps the centreline within a quarter width and spaces points at most `step` apart.
    c = max(curves, key=lambda c: len(c.x))
    x, y, w, nx, ny = curves2d.resample(c, 16, 1.0)
    seg = np.hypot(np.diff(x), np.diff(y))
    check("curves2d resample: spacing and unit normals", seg.max() <= 16.5 and np.allclose(np.hypot(nx, ny), 1), seg.max())
    # Converter: a square card (isotropic texels, 0.1 m across the mask) holds the curl; the 2-D tracer is chosen
    # automatically and the radius is the painted width whichever way the strand runs in the texture.
    card = card_mesh(length=0.1, width=0.1)
    groom, rep = convert.convert(card, m, tile_grid=(1, 1), step=16)
    # The painted width: covered texels per texel of painted centreline (the crossings overlap a little).
    painted = cov.sum() / sum(np.hypot(*np.diff(c, axis=0).T).sum() for c in truth)
    expect = painted / 2 * 0.1 / m.shape[0]
    p5, p50, p95 = np.percentile(groom.radii, [5, 50, 95]) / expect
    check("convert auto picks the 2-D tracer on a curly mask", rep.get("tracer") == "2d" and groom.strands >= 2, rep.get("tracer"))
    check("convert curly: radius = painted width in every direction", abs(p50 - 1) < 0.15 and p5 > 0.7 and p95 < 1.3,
          (p5, p50, p95, painted))
    first = groom.offsets[:-1]
    last = groom.offsets[1:] - 1
    check("convert curly: root channel increases root to tip", np.all(groom.root_param[last] > groom.root_param[first]))
    emb = np.einsum("pk,pkd->pd", groom.bary, card.positions[card.tris[groom.triangle]])
    check("convert curly: embedding reproduces the points", np.allclose(emb, groom.points, atol=1e-6))
    # The row path is untouched: on a mask whose rows run root to tip, "auto" is bit-identical to "rows".
    g_auto, r_auto = convert.convert(card_mesh(), rows[:, :64].copy(), tile_grid=(1, 1), step=16)
    g_rows, r_rows = convert.convert(card_mesh(), rows[:, :64].copy(), tile_grid=(1, 1), step=16, tracer="rows")
    check("convert auto on a row atlas is bit-identical to the row tracer", r_auto == r_rows and all(
        np.array_equal(getattr(g_auto, f.name), getattr(g_rows, f.name)) for f in dataclasses.fields(g_auto)))


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
    # A strand running along the scalp (the head beside its middle): root and tip equally far, so it decides nothing.
    o = analysis.orientation(groom, np.array([0.1, -0.1, 0.0]))
    check("orientation: a tangential strand is not decided", o["strands_decided"] == 0 and o["cards_mostly_reversed"] == 0, o)
    # Mirrored UVs: the same card with u reflected (every UV triangle's winding flips) places the same strand,
    # reflected across the card, exactly embedded: the embedding does not depend on the UV orientation.
    mir = card_mesh()
    mir.uv = np.c_[1 - mir.uv[:, 0], mir.uv[:, 1]].astype(np.float32)
    gm, rm = convert.convert(mir, m, tile_grid=(1, 1), step=16)
    emb_m = np.einsum("pk,pkd->pd", gm.bary, mir.positions[mir.tris[gm.triangle]])
    check("mirrored UV card: same strand, exactly embedded", gm.strands == 1 and np.allclose(emb_m, gm.points, atol=1e-6)
          and np.allclose(gm.points[:, 1], p[:, 1], atol=1e-6) and np.allclose(gm.radii, groom.radii, rtol=1e-4)
          and rm["unmapped_samples"] == 0, (gm.strands, rm))
    # Collapsed card vertices (a triangle without world area: no UV -> world Jacobian, width 0 there): the strand
    # keeps a positive radius from its measured samples (the layer refuses r <= 0).
    col = card_mesh()
    col.positions = col.positions.copy()
    col.positions[4:6] = col.positions[2:4]   # row 2 onto row 1
    gc, _ = convert.convert(col, m, tile_grid=(1, 1), step=16)
    check("collapsed triangle: every radius positive", gc.strands == 1 and np.all(gc.radii > 0) and np.all(np.isfinite(gc.radii)),
          gc.radii.min() if gc.strands else None)
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


def test_run_radii():
    # Compare against the original per-run definition, including holes and collapsed triangles.
    rng = np.random.default_rng(15)
    lengths = rng.integers(1, 12, 300)
    sid = np.repeat(np.arange(len(lengths)), lengths)
    hit = np.where(rng.random(len(sid)) < 0.25, -1, 0)
    width = rng.uniform(1e-6, 0.02, len(sid))
    width[rng.random(len(sid)) < 0.2] = 0
    width[rng.random(len(sid)) < 0.1] = np.nan
    width[rng.random(len(sid)) < 0.1] = np.inf
    # Odd/even measured counts, 3x median cap, MAX_RADIUS, all-invalid and singleton runs.
    sid = np.r_[np.repeat(np.arange(300, 306), [5, 4, 4, 2, 1, 4]), sid]
    hit = np.r_[np.zeros(20, int), hit]
    width = np.r_[[0.0002, 0.0004, 0.0006, 0, np.nan, 0.0002, 0.0004, 0, np.inf,
                   0.0001, 0.0001, 0.0001, 0.1, 0, np.inf, 0.1, 0.01, 0.02, 0.03, 0.04], width]
    want_i, want_n, want_r, dropped = [], [], [], 0
    a = 0
    while a < len(sid):
        b = a + 1
        while b < len(sid) and sid[b] == sid[a] and (hit[b] >= 0) == (hit[a] >= 0):
            b += 1
        if hit[a] >= 0 and b - a >= 2:
            r = width[a:b] / 2
            ok = np.isfinite(r) & (r > 0)
            if ok.any():
                r = np.where(ok, r, np.median(r[ok]))
                r = np.minimum(np.minimum(r, 3 * np.median(r)), convert.MAX_RADIUS)
                want_i.extend(range(a, b)); want_n.append(b - a); want_r.extend(r)
            else:
                dropped += 1
        a = b
    idx, n, radii, got_dropped = convert.run_radii(hit, sid, width)
    check("run radii: exact original indices, lengths, medians and caps",
          np.array_equal(idx, want_i) and np.array_equal(n, want_n) and np.array_equal(radii, want_r)
          and got_dropped == dropped, (got_dropped, dropped))


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
        # A LOD the layer would refuse (a zero radius) raises before the file exists: no header-only stub.
        bad = [(k, g) for k, g in lods]
        bad[1] = (bad[1][0], dataclasses.replace(bad[1][1], radii=np.where(np.arange(len(bad[1][1].radii)) == 0, 0.0,
                                                                         bad[1][1].radii).astype(np.float32)))
        try:
            asset.write(Path(d) / "bad.hgrm", 0x1234, card, bad)
            check("refused LOD writes no file", False)
        except ValueError:
            check("refused LOD writes no file", not (Path(d) / "bad.hgrm").exists())
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
    # Include exact cell/distance boundaries and duplicate positions, not just uniform noise.
    pts = np.r_[rng.integers(-4, 5, (80, 3)).astype(float), np.zeros((4, 3))]
    for distance in (0.5, 1.0, 1.5, 3.0):
        pairs = geom.pairs_within(pts, distance)
        got = {tuple(sorted(x)) for x in pairs.tolist()}
        delta = pts[:, None] - pts[None]
        d2 = np.einsum("ijk,ijk->ij", delta, delta)
        want = {(i, j) for i, j in zip(*np.nonzero(d2 < distance * distance)) if i < j}
        check(f"pairs grid boundaries and duplicates, distance {distance}", got == want and len(pairs) == len(got))


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
    rng = np.random.default_rng(7)
    for dtype in (np.float32, np.float64):
        uv = rng.random((40, 2)).astype(dtype)
        tris = np.r_[rng.integers(0, len(uv), (30, 3)), [[0, 1, 2], [2, 1, 0], [0, 0, 1]]]
        points = np.r_[rng.random((200, 2)), uv].astype(dtype)
        hit = np.full(len(points), -1)
        bary = np.zeros((len(points), 3))
        inside = np.zeros(len(points), int)
        order = np.argsort(points[:, 1])
        for t, tri in enumerate(tris):
            v = uv[tri, 1]
            lo, hi = np.searchsorted(points[order, 1], [v.min() - convert.EPS, v.max() + convert.EPS])
            idx = order[lo:hi]
            b = convert.barycentric(uv[tri], points[idx])
            inside[idx] += np.all(b > convert.EPS, 1)
            keep = np.all(b >= -convert.EPS, 1) & (hit[idx] < 0)
            hit[idx[keep]] = t
            bary[idx[keep]] = b[keep]
        got = convert.locate(uv, tris, points)
        check(f"locate batched exact first hits, counts and barycentrics {dtype.__name__}",
              all(np.array_equal(a, b) for a, b in zip(got, (hit, bary, inside))))


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
    # Tilted cards (the batch's strand reference, fibres=True): a card turned 60 deg about its strand axis shows half its
    # area while its fibres keep their width: the plain card halves the coverage, the fibre reference keeps the strands'.
    # Fibres far thinner than a pixel (the batch: 2 mm pixels, 0.1-0.3 mm strands): 0.23 mm strands every 1 mm.
    h, w = 512, 256
    m = np.zeros((h, w, 4), np.uint8)
    m[..., 3] = strand_mask(h, w, [lambda y, c=c: c for c in np.arange(4, 252, 4.0)], 1)
    m[..., 2] = np.linspace(0, 255, h)[:, None].astype(np.uint8)
    flat = card_mesh(length=0.12, width=0.06)
    groom, _ = convert.convert(flat, m, tile_grid=(1, 1), step=16)
    far = (np.array([0.0, 1e4, 0.0]), np.array([1e-3, 1e-3, 1e-3]))

    def sums(deg, axis):
        a = np.radians(deg)
        rot = np.eye(3)
        i, j = [(0, 2), (1, 2)][axis == 0]
        rot[i, i], rot[i, j], rot[j, i], rot[j, j] = np.cos(a), -np.sin(a), np.sin(a), np.cos(a)
        card = card_mesh(length=0.12, width=0.06)
        card.positions = (card.positions @ rot.T).astype(np.float32)
        pts = (groom.points @ rot.T).astype(np.float32)
        view = render.View(card.positions.mean(0).astype(float), np.array([0, 0, 1.0]), np.array([0, 1.0, 0]), 0.002, (50, 80))
        light = np.array([0, 0, -1.0])
        _, plain = render.render(lambda v: render.card_fragments(card.positions, card.uv, card.tris, m, v), view, light, far)
        _, fib = render.render(lambda v: render.card_fragments(card.positions, card.uv, card.tris, m, v, fibres=True), view, light, far)
        _, st = render.render(lambda v: render.strand_fragments(pts, groom.radii, groom.offsets, groom.root_param, v), view, light, far)
        return st.sum(), plain.sum(), fib.sum()
    # The card raster and the strand raster differ a little face on (texel point sampling vs the fibre area): the
    # tilted ratios are judged against that face-on ratio.
    s0, p0, _ = sums(0.0, 1)
    flat_ratio = p0 / s0
    for axis, name in ((1, "about its strand axis"), (0, "about the across axis")):
        s, p, fb = sums(60.0, axis)
        check(f"fibre card reference tilted {name}: card/strand ratio within 10% of face on", abs(fb / s / flat_ratio - 1) < 0.1,
              (fb / s, flat_ratio))
        if axis == 1:
            check("plain card reference halves the tilted fibres' coverage", abs(p / s / flat_ratio - 0.5) < 0.1, (p / s, flat_ratio))


def test_render_equivalence():
    rng = np.random.default_rng(583)
    pix = rng.integers(0, 31, 4000)
    depth = rng.normal(size=len(pix))
    # Several distinct float64 depths in the same float32 bucket, adjacent buckets,
    # signed zeros, underflow and overflow: buckets must never change exact ordering.
    depth[:600] = 1 + rng.integers(-20, 20, 600) * 1e-10
    depth[600:610] = [0.0, -0.0, 1e-310, -1e-310, 1e100, -1e100, 2, 2, -2, -2]
    order = np.lexsort((depth, pix))
    check("fragment radix order exactly matches stable lexsort including collisions",
          np.array_equal(render._fragment_order(pix, depth), order))
    for special_pix, special_depth in ((np.array([2 ** 33, 2 ** 33, 0]), np.array([1., -1., 0.])),
                                       (np.array([0, 0, 0]), np.array([np.nan, np.inf, -np.inf]))):
        check("fragment order fallback matches lexsort",
              np.array_equal(render._fragment_order(special_pix, special_depth), np.lexsort((special_depth, special_pix))))
    alpha, colour = rng.uniform(0, 1, len(pix)), rng.uniform(0, 2, (len(pix), 3))
    pp, aa, cc = pix[order], np.minimum(alpha[order], 0.999), colour[order]
    tau = -np.log1p(-aa)
    cum = np.cumsum(tau)
    first = np.maximum.accumulate(np.where(np.r_[True, pp[1:] != pp[:-1]], np.arange(len(pp)), 0))
    before = np.exp(-(cum - tau - (cum[first] - tau[first])))
    expected, total = np.zeros((32, 3)), np.zeros(32)
    np.add.at(expected, pp, (before * aa)[:, None] * cc)
    np.add.at(total, pp, tau)
    image, trans = render.composite(pix, depth, alpha, colour, 32)
    check("composite matches original add.at arithmetic exactly", np.array_equal(image, expected)
          and np.array_equal(trans, np.exp(-total)))
    empty = render.Fragments.concat([])
    image, trans = render.composite(empty.pixel, empty.depth, empty.alpha, empty.tangent, 4)
    check("empty composite leaves background untouched", not image.any() and np.all(trans == 1))

    card = card_mesh(length=0.06, width=0.04, rows=3)
    card.positions[:, 2] = card.positions[:, 0] * 0.8
    mask = rng.integers(0, 256, (64, 64, 4), dtype=np.uint8)
    mask[::3, :, 3] = 0
    # A small viewport exercises clipping as well as raster chunk boundaries.
    view = render.View(np.array([0.0, -0.03, 0]), np.array([0., 0, 1]), np.array([0., 1, 0]), 0.002, (13, 19))
    both = render.card_fragments_both(card.positions, card.uv, card.tris, mask, view, chunk=1)
    for i, fibres in enumerate((True, False)):
        single = render.card_fragments(card.positions, card.uv, card.tris, mask, view, fibres=fibres)
        check(f"paired cards equal independent raster, fibres={fibres}", all(
            np.array_equal(getattr(single, field), getattr(both[i], field)) for field in render.Fragments.FIELDS))
    builders = render.card_fragment_builders(card.positions, card.uv, card.tris, mask)
    equivalent = dataclasses.replace(view)
    with patch.object(render, "card_fragments_both", wraps=render.card_fragments_both) as raster:
        builders[0](view)
        builders[1](equivalent)
        check("paired builders rasterise an equivalent view only once", raster.call_count == 1)
    # Compare all light/head shadow queries against the original add.at map.
    light = both[1]
    slabs = 7
    lo, hi = np.percentile(light.depth, [0, 100])
    span = max(hi - lo, 1e-6)
    bins = np.clip(((light.depth - lo) / span * slabs).astype(np.int64), 0, slabs - 1)
    tau = np.zeros((view.size[0] * view.size[1], slabs + 1))
    np.add.at(tau, (light.pixel, bins + 1), -np.log1p(-np.minimum(light.alpha, 0.999)))
    check("shadow bins match original add.at exactly", np.array_equal(render._shadow_map(light, view, slabs)[2], np.cumsum(tau, 1)))
    query = view.project(light.world.astype(float))
    shared = render._shadow_map(light, view, 64)
    check("shared shadow lookup equals standalone lookup", np.array_equal(
        render.shadow_transmittance(light, query, view), render.shadow_transmittance(light, query, view, shadow_map=shared)))

    # More than one chunk; a strand boundary falls inside a chunk, not just at its edge.
    points = rng.uniform(-0.004, 0.004, (33010, 3)).astype(np.float32)
    offsets = np.array([0, 13, 33010])
    radii, root = np.full(len(points), 0.0001, np.float32), rng.random(len(points)).astype(np.float32)
    seg = np.flatnonzero(~np.isin(np.arange(len(points)), offsets[:-1]))
    reference = render._strand_chunk(points, radii, root, view, 0.5, seg)
    chunked = render.strand_fragments(points, radii, offsets, root, view)
    check("strand chunking preserves every fragment and sample order", all(
        np.array_equal(getattr(reference, field), getattr(chunked, field)) for field in render.Fragments.FIELDS))


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
    # M4.2 R lobe (sample_r, the strand specular indirect): the white furnace (estimator = quadrature of r_lobe within
    # 3%) and the sampled directions' distribution: E[A_0 g(wi)] against the quadrature of r_lobe g for a test function
    # g that varies over the lobe (within 4 standard errors plus 1% for the quadrature grid; furnace: 0.2%).
    rng = np.random.default_rng(23)
    for beta, theta in ((0.09, 30.0), (0.41, 0.0), (0.41, 60.0), (0.7, 80.0)):
        e, se, q = bsdf.r_furnace(beta, theta, n=60000, n_quad=(121, 240), n_h=24)
        check(f"R lobe white furnace beta {beta} theta_o {theta}: {e:.5f} vs {q:.5f}", abs(e / q - 1) < 0.03, (e, se, q))
    t, to = np.array([0.0, 0.0, 1.0]), np.radians(40.0)
    wo = np.array([np.cos(to), 0.0, np.sin(to)])
    axis = np.array([0.3, -0.5, 0.8]) / np.linalg.norm([0.3, -0.5, 0.8])
    gfun = lambda d: (1 + d @ axis) ** 2
    n = 80000
    h = rng.uniform(-1, 1, n)
    wi, a0 = bsdf.sample_r(np.tile(t, (n, 1)), np.tile(wo, (n, 1)), h, 0.41, rng.uniform(size=(n, 3)))
    est, est_se = (a0 * gfun(wi)).mean(), (a0 * gfun(wi)).std() / np.sqrt(n)
    TH, PH = np.meshgrid((np.arange(121) + 0.5) / 121 * np.pi - np.pi / 2, (np.arange(240) + 0.5) / 240 * 2 * np.pi - np.pi,
                         indexing="ij")
    grid = np.stack([np.cos(TH) * np.cos(PH), np.cos(TH) * np.sin(PH), np.sin(TH)], -1).reshape(-1, 3)
    dw = (np.cos(TH) * (np.pi / 121) * (2 * np.pi / 240)).reshape(-1)
    x, wx = np.polynomial.legendre.leggauss(24)
    m = len(grid)
    quad = sum(wk * (np.pi / 2) * np.cos(gk) / 2 * np.sum(
        bsdf.r_lobe(np.tile(t, (m, 1)), np.tile(wo, (m, 1)), grid, np.full(m, np.sin(gk)), 0.41) * gfun(grid) * dw)
        for gk, wk in zip(x * np.pi / 2, wx))
    check(f"R lobe sampled directions follow r_lobe: {est:.5f} +- {est_se:.5f} vs {quad:.5f}",
          abs(est - quad) <= 4 * est_se + 0.01 * quad, (est, est_se, quad))

def test_tips_dither():
    # Pixel centres over 4 x 5, all four frame phases, and a uniform sweep of
    # the native per-draw jitter. Its 1/20 bin width bounds integration error;
    # the spatial pattern alone is deliberately not a uniform random variable.
    samples = np.array([(x + 0.5, y + 0.5, frame + 1, j / 20)
                        for y in range(5) for x in range(4)
                        for frame in range(4) for j in range(20)])
    for alpha in (0, 0.25, 0.5, 0.75, 1):
        kept = primary.tips_keep(alpha, samples[:, :2], samples[:, 2].astype(int), samples[:, 3])
        share = float(kept.mean())
        check(f"tips alpha {alpha:g}: {kept.sum()}/{len(kept)} = {share:.6f} (quantisation 0.05)",
              abs(share - alpha) <= 1 / 20)
        if alpha in (0, 1):
            check(f"tips endpoint {alpha:g} exact", np.all(kept == bool(alpha)))
    check("tips frame phase repeats every 4 tags",
          np.array_equal(primary.tips_keep(0.5, samples[:, :2], samples[:, 2].astype(int), samples[:, 3]),
                         primary.tips_keep(0.5, samples[:, :2], samples[:, 2].astype(int) + 4, samples[:, 3])))


def test_strand_source_safety():
    velocity = (ROOT / "shaders/rt/hair_primary_velocity.hlsl").read_text()
    count = (ROOT / "shaders/rt/hair_primary_count.hlsl").read_text()
    check("velocity uses accepted tag and key map ownership", "accepted >> 8u" in velocity and "(accepted & 255u)" in velocity and
          "rec + 3248u + k * 4u" in velocity)
    check("zero low-24-bit count tag excluded", "(tag & 0xFFFFFFu) != 0u" in count)

    # Protect the exact cause from hv8: discard demotes, so a return must end
    # each rejected lane before a subsequent buffer read. No shader edits here.
    import re
    for name in ('gbuffer', 'velocity'):
        src = (ROOT / f'shaders/rt/hair_primary_{name}.hlsl').read_text()
        code = re.sub(r'//[^\n]*|/\*.*?\*/', '', src, flags=re.S)
        rejects = list(re.finditer(r'\bdiscard\s*;', code))
        check(f'{name}: every demoted lane immediately returns', bool(rejects) and all(
            re.match(r'\s*return\s+\w+\s*;', code[m.end():]) for m in rejects))
    src = (ROOT / 'shaders/rt/hair_primary_gbuffer.hlsl').read_text()
    check('transparent card marker uses native combined alpha and grazing before strand rejection',
          'const bool card_marked = hp_tips_keep(card_M.w * grazing, pos.xy, tag, aux.x);\n  if (card_marked)\n'
          '    vk::RawBufferStore<uint>(marker, packed_marker, 4);' in src
          and src.index('const float4 card_M = max(sign(card_ma), sign(card_mb))') < src.index('= hp_tips_keep(card_M.w')
          < src.index('if (e.x == hair_primary_no_strand)'))
    # Review 2026-11-08: card mode (submit-time header without this draw's groom) before any strand work, with the
    # prepass alpha test and card depth; the pixel trace skips slots outside the swap mask; the probe traces privately.
    check('card mode keeps card depth and the prepass alpha test, before the rebase',
          'if (((masks >> 16u) >> kDrawSlot) & 1u) {' in src and 'o.depth = pos.z;' in src
          and src.index('if (((masks >> 16u) >> kDrawSlot) & 1u) {') < src.index('if (!hp_rebase(rec, gCam.position, org))'))
    check('pixel trace accepts only swapping slots and skips the others',
          'if (slot >= 16u || ((accept >> slot) & 1u) == 0u) {' in src and 'tmin = th + 2.0 * hair_primary_max_radius;' in src
          and 'swap_slots, grooms, pool' in src and '1u << kDrawSlot, grooms, pool' in src)
    check('count accepts the skipped-marker flag on card words only',
          '(mark.x & 0xD0u) == 0u' in count and 'mark.y == card_word' in count)


def rotation(rng):
    q, r = np.linalg.qr(rng.normal(size=(3, 3)))
    return q * np.sign(np.diag(r))


def test_shading_spaces(rng):
    """5g: the spaces of the native card G-buffer (VS 41a168e2 decompiled; primary.native_card_frame) and the strand
    variant's keys rt_hair_macro_normal / rt_hair_tangent_world (specialization 1045, header flags 16 / 32)."""
    world3, view3 = rotation(rng), rotation(rng)
    n_obj, b_obj = rng.normal(size=(300, 3)), rng.normal(size=(300, 3))
    n_obj /= np.linalg.norm(n_obj, axis=1, keepdims=True)
    b_obj -= n_obj * (b_obj * n_obj).sum(1, keepdims=True)   # an orthogonal frame, as the card's
    b_obj /= np.linalg.norm(b_obj, axis=1, keepdims=True)
    t_obj = np.cross(b_obj, n_obj)
    loc1, loc3, loc4, loc5 = primary.native_card_frame(n_obj, b_obj, t_obj, world3, view3)
    native_rt0 = loc1 / np.linalg.norm(loc1, axis=1, keepdims=True)
    check("macro normal from the world card normal equals the native RT0 normal (view space)",
          np.abs(primary.macro_normal(loc3, view3) - native_rt0).max() < 1e-12)
    # A strand tangent in world space against the world card frame: the native RT3 is world (loc3-5), so the strand's
    # world tangent is comparable to it; the old view-space RT3 differs by the view rotation.
    t_world = loc4
    rt3_world = primary.oct12_decode(primary.oct12_encode(t_world))
    rt3_view = primary.oct12_decode(primary.oct12_encode(t_world @ view3))
    err = lambda a, b: np.degrees(np.arccos(np.clip(np.abs((a * b).sum(1)), 0, 1)))
    check("RT3 world tangent decodes to the native frame's direction (< 0.1 deg)", err(rt3_world, loc4).max() < 0.1)
    check("RT3 view tangent read as world (the old path) is off by the view rotation (> 1 deg median)",
          np.median(err(rt3_view, loc4)) > 1.0, np.median(err(rt3_view, loc4)))
    # The card grazing term: the frame (loc5, loc3, loc4) and the ray direction (gbuffer.hlsl dir, camera-relative
    # world) are both world vectors, so it is the native |N.V|; it is invariant under a common rotation.
    to_cam = rng.normal(size=(300, 3))
    g = primary.card_grazing(loc5, loc3, loc4, to_cam)
    ref = np.clip(np.abs((to_cam / np.linalg.norm(to_cam, axis=1, keepdims=True) * loc3).sum(1)) * 5, 0, 1)
    check("card grazing in one space is the native saturate(|N.V| 5) (orthonormal frame)",
          np.abs(g - ref).max() < 1e-9, np.abs(g - ref).max())
    check("card grazing is invariant under a common rotation of frame and ray",
          np.abs(primary.card_grazing(loc5 @ view3, loc3 @ view3, loc4 @ view3, to_cam @ view3) - g).max() < 1e-9)
    src = (ROOT / "shaders/rt/hair_primary_gbuffer.hlsl").read_text()
    check("G-buffer: RT0/RT3 spaces behind specialization 1045 and header flags 16/32",
          "[[vk::constant_id(1045)]] const uint kHairShadingSpace = 0u;" in src
          and "if ((flags & hair_primary_flag_macro_normal) != 0u) rt0_n = normalize(mul(float4(card_n, 0.0), gCam.view).xyz);" in src
          and "if ((flags & hair_primary_flag_tangent_world) != 0u) rt3_t = t_world;" in src
          and "hair_primary_flag_macro_normal = 16u" in src and "hair_primary_flag_tangent_world = 32u" in src)
    layer = (ROOT / "layer/hair_primary.cpp").read_text(encoding="utf-8")
    header = (ROOT / "common/hair_primary.h").read_text(encoding="utf-8")
    check("layer specializes 1045 on both G-buffer pipeline kinds and sets the two header flags",
          layer.count("shader_variant::kSpecIdHairShadingSpace") == 2 and "hair_primary::kFlagMacroNormal" in layer
          and "hair_primary::kFlagTangentWorld" in layer
          and "kFlagMacroNormal = 1u << 4, kFlagTangentWorld = 1u << 5" in header)


def test_coverage():
    """M3.2 (rt_hair_groom_coverage): the pair's twins and its offline gate against the supersampled area coverage."""
    py, px = np.mgrid[0:4, 0:4]
    a, b = primary.coverage_offset(px, py, 0), primary.coverage_offset(px, py, 1)
    check("coverage offsets: antithetic, a quarter pixel on both axes", np.allclose(a + b, 0) and np.allclose(np.abs(a), 0.25))
    check("coverage offsets: the diagonal alternates with the checkerboard parity",
          np.allclose(a[0, 0], (-0.25, -0.25)) and np.allclose(a[0, 1], (-0.25, 0.25)) and np.allclose(a[1, 1], a[0, 0]))
    t = lambda *v: np.array(v, bool)
    zeros = np.zeros(3, int)
    both = primary.coverage_pick(t(1, 1, 1), t(1, 1, 1), np.array([1.0, 2.0, 1.0]), np.array([2.0, 1.0, 1.0]), zeros, zeros, 7)
    check("coverage pick: both hit keeps the nearer, a tie ray A", list(both) == [0, 1, 0], both)
    none = primary.coverage_pick(t(0), t(0), np.zeros(1), np.zeros(1), zeros[:1], zeros[:1], 7)
    check("coverage pick: no hit keeps none", list(none) == [2])
    same = True
    for tag in range(1, 65):
        ka = primary.coverage_pick(np.ones((4, 4), bool), np.zeros((4, 4), bool), 0.0, 0.0, px, py, tag)
        kb = primary.coverage_pick(np.zeros((4, 4), bool), np.ones((4, 4), bool), 0.0, 0.0, px, py, tag)
        same &= bool(np.array_equal(ka == 0, kb == 1) and set(np.unique(ka)) <= {0, 2} and set(np.unique(kb)) <= {1, 2})
        if tag <= 4:
            kept = ka != 2
            check(f"coverage pick tag {tag}: one hit kept on exactly half the pixels, a checkerboard",
                  kept.sum() == 8 and np.all(kept[:, 1:] != kept[:, :-1]), ka)
    check("coverage pick: the one-hit decision does not depend on which ray hit", same)
    phases = [int(primary.coverage_pick(t(1), t(0), 0.0, 0.0, zeros[:1], zeros[:1], tag)[0] == 0) for tag in range(1, 257)]
    # The frame phase is a hash: over 256 tags its share is 0.5 within 4 binomial sigma (sigma = 1/32).
    check("coverage pick: frame phase keeps about half of the tags", abs(np.mean(phases) - 0.5) <= 4 / 32, np.mean(phases))
    pts, rad, offs, cam = primary.subpixel_fixture(seed=1)
    ref = primary.coverage_reference(pts, rad, offs, cam)
    frames = 16
    single = primary.coverage_metrics(primary.coverage_frames(pts, rad, offs, cam, frames, False), ref)
    pair = primary.coverage_metrics(primary.coverage_frames(pts, rad, offs, cam, frames, True), ref)
    print(json.dumps({"coverage_single": single, "coverage_pair": pair}))
    # Energy: the mean over 16 frames is unbiased; tolerance 4 binomial sigma of one sample per pixel per frame (the pair
    # has two, stratified: conservative for it).
    tol = 4 * pair["bias_sigma"] / np.sqrt(frames)
    check(f"coverage pair unbiased: total {pair['bias']:+.4f} within {tol:.4f} (4 sigma)", abs(pair["bias"]) <= tol)
    check(f"coverage single ray unbiased: total {single['bias']:+.4f} within {tol:.4f}", abs(single["bias"]) <= tol)
    check(f"coverage pair: 3 x 3 box error {pair['box_rmse']:.4f} below the single ray's {single['box_rmse']:.4f}",
          pair["box_rmse"] < single["box_rmse"])
    check(f"coverage pair: 3 x 3 box flicker {pair['box_temporal_std']:.4f} below the single ray's "
          f"{single['box_temporal_std']:.4f}", pair["box_temporal_std"] < single["box_temporal_std"])
    src = (ROOT / "shaders/rt/hair_primary_gbuffer.hlsl").read_text()
    pair_if = "if (kHairGroomCoverage != 0u && (ld(rec + 60u) & hair_primary_flag_coverage) != 0u) {"
    single = "const bool hit = hp_trace(rec, org, dir, accept, grooms, pool, pool_bytes, crumbs, pool_end, e, t, skipped);"
    check("G-buffer pair behind specialization 1042 and header flag 8, before the single ray (hp_pixel)",
          "[[vk::constant_id(1042)]] const uint kHairGroomCoverage = 0u;" in src and pair_if in src and single in src
          and src.index(pair_if) < src.index(single) and "if (e.w != hp_seal(e.xyz, tag)) {" in src)
    layer = (ROOT / "layer/hair_primary.cpp").read_text(encoding="utf-8")
    check("layer specializes the strand G-buffer pipelines and sets header flag kFlagCoverage from rt_hair_groom_coverage",
          layer.count("shader_variant::kSpecIdHairGroomCoverage") == 2 and "hair_primary::kFlagCoverage" in layer)


def test_coverage_pair_gate_bias():
    """2026-11-07 live: rt_hair_groom_coverage=1 lowered the strand swap rate (600 vs 900-945 of 1500 per window,
    more 'coverage low'). The gate's ratio is accepted strand pixels over card pixels, both counted where the card
    covers the jittered pixel centre. The pair's rays sit a quarter pixel off that centre: near a card edge one falls
    off the card (no strands there), so on narrow cards the ratio drops although the pair is unbiased against the
    pixel's area coverage. Model: strands filling a card strip of width W at a random angle, 18 jitter phases."""
    rng = np.random.default_rng(3)
    n = 64
    py, px = np.mgrid[0:n, 0:n]
    ratio = {}
    for w in (2.0, 32.0):
        rs = rp = cs = 0
        for _ in range(6):
            a, c = rng.uniform(0, np.pi), rng.uniform(n / 2 - 4, n / 2 + 4, 2)
            card = lambda x, y: np.abs(-(x - c[0]) * np.sin(a) + (y - c[1]) * np.cos(a)) <= w / 2
            strand = lambda x, y: card(x, y) & (np.mod((x - c[0]) * np.cos(a) + (y - c[1]) * np.sin(a), 0.35) < 0.33)
            oa, ob = primary.coverage_offset(px, py, 0), primary.coverage_offset(px, py, 1)
            for t in range(1, 19):
                cx, cy = px + 0.5 + primary.halton(t, 2) - 0.5, py + 0.5 + primary.halton(t, 3) - 0.5
                frag = card(cx, cy)
                cs += frag.sum()
                rs += (frag & strand(cx, cy)).sum()
                ha, hb = strand(cx + oa[..., 0], cy + oa[..., 1]), strand(cx + ob[..., 0], cy + ob[..., 1])
                rp += (frag & (primary.coverage_pick(ha, hb, 0.0 * px, 0.0 * px + 1, px, py, t) != 2)).sum()
        ratio[w] = (rs / cs, rp / cs)
    print(json.dumps({"coverage_pair_gate_ratio": {str(k): v for k, v in ratio.items()}}))
    check("pair gate bias: a 2 px card's ratio drops by more than 5% with the pair (edge rays off the card)",
          ratio[2.0][1] < 0.95 * ratio[2.0][0], ratio[2.0])
    check("pair gate bias: a 32 px card's ratio within 2% of the single ray's", abs(ratio[32.0][1] / ratio[32.0][0] - 1) < 0.02,
          ratio[32.0])


def test_post_depth():
    """M3.4: the D24S8 (and the linear depth made from it) that fog, transparents, water, DoF and the camera motion pass
    read after the G-buffer pass shows the same surface as the colour on every pixel of a strand card."""
    pts, rad, offs, cam, card = primary.card_fixture()
    strands = primary.post_depth(pts, rad, offs, cam, card)
    rep = primary.post_depth_report(strands)
    cards = primary.post_depth_report(primary.post_depth(pts, rad, offs, cam, card, keep_card_depth=True))
    print(json.dumps({"post_depth_strands": rep, "post_depth_card_depth_kept": cards}))
    check("post depth fixture has strand pixels and alpha-tested card pixels",
          rep["strand_pixels"] > 100 and rep["card_alpha_pixels"] > rep["strand_pixels"])
    check(f"post depth: strand path has no halo pixel (depth error {rep['max_err_mm']:.5f} mm within one D24 step "
          f"{rep['max_step_mm']:.5f} mm)", rep["halo_pixels"] == 0)
    check("post depth: kept card depth would put a halo on every alpha-tested card pixel (control)",
          cards["halo_pixels"] == cards["card_alpha_pixels"] and cards["halo_background_pixels"] > 0)
    layer = (ROOT / "layer/hair_primary.cpp").read_text(encoding="utf-8")
    gb = (ROOT / "shaders/rt/hair_primary_gbuffer.hlsl").read_text()
    check("prepass clone writes no card depth; strand G-buffer tests GREATER_OR_EQUAL with writes; velocity no writes",
          "dsv.depthWriteEnable = VK_FALSE;" in layer and "dsv.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;" in layer
          and "dsv.depthWriteEnable = pass == Pass::Gbuffer ? VK_TRUE : VK_FALSE;" in layer)
    check("strand G-buffer depth is the cached hit's depth (SV_Depth = e.z, the hit projected with the pass's vp)",
          "o.depth = asfloat(e.z);" in gb and "asuint(c.z / c.w)" in gb)


def test_rr_hair_guides():
    """M3.3 (rr_hair_guides): class 5 guides read RT2 as the game's hair lighting (bg3_materials.hlsli class 5)."""
    guides = (ROOT / "shaders/rr_guides.hlsl").read_text()
    mats = (ROOT / "shaders/rt/bg3_materials.hlsli").read_text()
    check("hair lighting: class 5 roughness max(0.09, RT2.x), backlit RT2.z, metalness 0",
          "m.hair_rough = max(0.09, mat.x);" in mats and "m.backlit = mat.z;" in mats and "sf.diff_col = albedo;" in mats)
    check("hair Fresnel F0 of the game's R lobe", "0.046520568 + 0.95347941" in mats)
    check("RR guides: class 5 behind specialization 1043 uses the same terms",
          "[[vk::constant_id(1043)]] const uint kRrHairGuides = 0u;" in guides and
          "if (kRrHairGuides != 0u && (uint(round(m.w * 255.0)) & 7u) == 5u) {" in guides and
          "const float hair_rough = max(0.09, rough);" in guides and "ODiffuse[id.xy] = float4(base, 1.0);" in guides and
          "env_brdf_approx2(kFibreF0.xxx, hair_rough * hair_rough" in guides and
          "static const float kFibreF0 = 0.046520568;" in guides)
    # The fibre F0 is the Fresnel reflectance at normal incidence of eta 1.55 (keratin), as the game's constant.
    check("fibre F0 = ((1.55 - 1) / (1.55 + 1))^2", abs(((1.55 - 1) / 2.55) ** 2 - 0.046520568) < 1e-6)
    ngx = (ROOT / "layer/ngx.cpp").read_text(encoding="utf-8")
    check("ngx specializes the guide pipeline from rr_hair_guides and rebuilds on a change",
          "shader_variant::kSpecIdRrHairGuides" in ngx and ngx.count('"rr_guides.hlsl", rr_hair_guides())') == 2
          and "(blob.generation << 1) | (hair_guides & 1u)" in ngx)


def test_publish_lock():
    # hair_primary_publish runs inside the scene build, where rt_after_capture holds rtscene's g_mu: a locking camera
    # read there killed the game at the first publish (hv6 2026-11-04, exit 0xFFFF7001, no crash log).
    src = (ROOT / "layer/hair_primary.cpp").read_text(encoding="utf-8")
    check("hair_primary.cpp reads the build camera without the scene lock",
          ("rt_this_build_camera(" in src or "rt_current_build_camera(" in src) and "rt_build_camera(" not in src)


def test_groom_soup_bounds():
    """F01: uint32 shader twins vs unbounded byte-address arithmetic."""
    mask = (1 << 32) - 1

    def old(tri, tris, stride, base, size):
        need = ((3 * tri + 2) * stride + 24) & mask if tri < tris else mask
        return (tri < tris < (1 << 24) and 24 <= stride <= 256 and
                base <= size and need <= size - base)

    def new(tri, tris, stride, base, size):
        return (tri < tris < (1 << 24) and 24 <= stride <= 256 and
                base <= size and size - base >= 24 and
                3 * tri + 2 <= (size - base - 24) // stride)

    cases = []
    for tris in (1, 2, 5592406, (1 << 24) - 1):
        for tri in (0, tris - 1):
            for stride in (24, 256):
                for base in (0, 64, mask - 24):
                    enough = base + (3 * tri + 2) * stride + 24
                    for size in (0, 23, 24, base - 1, enough - 1, enough, mask):
                        if 0 <= size <= mask:
                            cases.append((tri, tris, stride, base, size))
    wrap = (5592405, 5592406, 256, 0, 280)
    cases.append(wrap)
    # Includes position bytes [0,12) and UV bytes [16,24) for all three vertices.
    def inside(tri, tris, stride, base, size):
        return all(0 <= base + (3 * tri + k) * stride + byte < size
                   for k in range(3) for byte in (*range(12), *range(16, 24)))
    check("F01 division bound equals every actual byte read in range",
          all(new(*c) == inside(*c) for c in cases), len(cases))
    check("F01 old uint32 bound accepts wrap (need=280), new bound rejects",
          old(*wrap) and not new(*wrap) and not inside(*wrap))
    check("F01 invalid triangle/stride rejected", all(not new(*c) for c in (
        (1, 1, 24, 0, mask), (0, 1 << 24, 24, 0, mask),
        (0, 1, 0, 0, mask), (0, 1, 23, 0, mask), (0, 1, 257, 0, mask))))
    source = (ROOT / "shaders/rt/hair_groom_update.hlsl").read_text()
    check("F01 shader uses division before load address multiplication",
          "3u * tri + 2u <= (available - 24u) / pc.stride" in source and
          "if (ok)" in source and "const uint need" not in source)


def test_pixel_seal_tearing():
    """F08: Python hp_seal twin; all 14 nontrivial word mixes, including halves."""
    rng = np.random.default_rng(808)
    passes = 0
    count = 100000
    for a, b, tag in zip(rng.integers(0, 1 << 32, (count, 3), dtype=np.uint32).tolist(),
                         rng.integers(0, 1 << 32, (count, 3), dtype=np.uint32).tolist(),
                         rng.integers(1, 1 << 32, count, dtype=np.uint32).tolist()):
        a.append(primary.seal(*a, tag))
        b.append(primary.seal(*b, tag))
        # Identical writers pass even when different words/halves win the store.
        other = a.copy()
        same = a[:2] + other[2:]
        if same[3] != primary.seal(*same[:3], tag):
            raise AssertionError("identical halves rejected")
        for mask in range(1, 15):
            torn = [b[i] if mask & (1 << i) else a[i] for i in range(4)]
            passes += torn[3] == primary.seal(*torn[:3], tag)
    print(f"F08 seal: {count} identical pairs pass; {count * 14} torn mixes, {passes} passes")
    check("F08 sampled different-entry tears fail seal", passes == 0, passes)
    source = (ROOT / "shaders/rt/hair_primary_gbuffer.hlsl").read_text()
    check("F08 cached ray uses fixed depth, independent of card layer",
          "mul(float4(ndc, 1.0, 1.0), inv)" in source and
          "const float3 dir = normalize(ray_h.xyz / ray_h.w);" in source)


def test_ndc_ray():
    # rt/hair_primary_gbuffer.hlsl and _velocity.hlsl: a card fragment's camera-relative point from its pixel. The game's
    # vertex shaders write gl_Position = (W, 1) vp with y negated (rt/xfb_to_world.hlsl, the groom points' space), so
    # the shader's NDC y must be negated before the inverse vp (hv6 2026-11-04: mirrored rays missed every strand).
    rng = np.random.default_rng(5)
    f, n_, fa = 1.2, 0.1, 1000.0
    proj = np.array([[f / 1.5, 0, 0, 0], [0, f, 0, 0], [0, 0, n_ / (fa - n_), 1], [0, 0, fa * n_ / (fa - n_), 0]])
    view = np.eye(4)
    view[:3, :3] = np.linalg.qr(rng.normal(size=(3, 3)))[0]
    vp = view @ proj                                   # row vectors: clip = (W, 1) @ vp
    w_, h_ = 2560.0, 1067.0
    ok = True
    for _ in range(50):
        d = rng.normal(size=3)
        P = (d / np.linalg.norm(d)) * rng.uniform(1, 20)
        clip = np.append(P, 1.0) @ vp
        if clip[3] <= 0.1:
            continue
        glpos = clip * np.array([1, -1, 1, 1])        # the game's y negation
        ndc_vk = glpos[:2] / glpos[3]
        pix = (ndc_vk + 1) / 2 * np.array([w_, h_])   # Vulkan viewport (0, 0, w, h)
        ndc = ((pix - 0) / np.array([w_, h_]) * 2 - 1) * np.array([1, -1])   # the shaders' formula
        hh = np.array([ndc[0], ndc[1], glpos[2] / glpos[3], 1.0]) @ np.linalg.inv(vp)
        ok = ok and np.allclose(hh[:3] / hh[3], P, rtol=1e-6, atol=1e-6)
    check("strand ray: pixel -> negated NDC y -> inverse vp returns the camera-relative point", ok)


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
    test_shading_spaces(rng)
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
    # Ownership (two overlapping grooms): groom A's strands on its card plane z = 1, groom B's on z = 0.97 in front.
    # Every pixel's closest strand is owned by exactly the card it lies on: A's fragment (t_card on z = 1) rejects
    # B's strands, B's fragment rejects A's, and each accepts its own (10 deg field, 65 px: foot from the plane).
    seg = 0.01
    xs = np.arange(-0.05, 0.05 + 1e-9, seg)
    a_pts = np.stack([xs, np.full_like(xs, 0.002), np.ones_like(xs)], 1)
    b_pts = np.stack([xs, np.full_like(xs, -0.002), np.full_like(xs, 0.97)], 1)
    both = np.concatenate([a_pts, b_pts])
    hits2 = primary.trace(both, np.full(len(both), r), np.array([0, len(xs), 2 * len(xs)]), cam)
    rays = cam.rays().reshape(65, 65, 3)
    owned, wrong = 0, 0
    for yy, xx in zip(*np.nonzero(hits2.hit)):
        d = rays[yy, xx]
        from_a = hits2.segment[yy, xx] < len(xs) - 1
        for plane, mine in ((1.0, from_a), (0.97, not from_a)):
            t_card = plane / d[2]
            foot = t_card * pixel * abs(d[0]) / d[2]   # distance spread of the plane over one pixel (small)
            ok = primary.on_card(t_card, hits2.t[yy, xx], d[2], foot, seg)
            owned += ok and mine
            wrong += ok and not mine
    check("ownership: every strand pixel kept by its own card only", owned == hits2.hit.sum() and wrong == 0,
          (owned, wrong, int(hits2.hit.sum())))
    # Pool bounds (2026-11-03 19:53: a strand read at pool + 0xFA000000 of a 2 GB pool lost the device).
    pool = 2146435072
    row = (468596768, 470000000, 476000000, 0)
    check("pool bound: a valid segment passes", primary.segment_ok(row, 1000, 5000, pool))
    check("pool bound: the crash's read (i0 * 12 = 3.9 GB past the points) fails closed",
          not primary.segment_ok(row, 1000, (0xFA000000 - row[0]) // 12, pool))
    check("pool bound: 64-bit offsets (i0 * 12 wraps in 32 bits)", not primary.segment_ok(row, 1000, 0x15555556, pool)
          and (row[0] + 0x15555556 * 12) % 2 ** 32 < pool)
    check("pool bound: a segment index past the pool fails", not primary.segment_ok(row, 0xFFFFFF, 0, 476000000 + 1000))
    s = primary.seal(5 << 24 | 7, 0x3F000000, 0x3F400000, 1234)
    check("pixel seal: changes with any word, never matches a zeroed entry",
          s != primary.seal(4 << 24 | 7, 0x3F000000, 0x3F400000, 1234) and s != primary.seal(5 << 24 | 7, 0, 0x3F400000, 1234)
          and all(primary.seal(0, 0, 0, t) != 0 for t in (1, 2, 0x7FFFFFFF)))
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
    # rt_hair_groom_primary_current (2026-11-07 hair-flicker: strands trail the animated head): a strand point on a
    # card that accelerates (1 cm, then 3 cm per frame), static camera. The card draws at p[2] with MV p[2] - p[1]. The previous frame's
    # publish (rebased only for the camera) draws the strand at p[1] with that publish's motion p[1] - p[0]: one frame
    # of motion behind, and an MV that is the previous frame's. This frame's publish: the card's position and MV.
    p = [world + np.array([0.01 * k * k, 0.0, 0.0]) for k in range(3)]   # accelerating: 1 cm, then 3 cm
    vp = vp_at(cam1)

    def drawn(point, prev_point):
        frag_s = pixel(point - cam1, vp) - np.array([jit[0], -jit[1]])
        return frag_s, primary.motion_vector(frag_s, prev_point - cam1, vp, np.ones(2), np.zeros(2), size, jit, pjit)

    card_xy, card_mv = drawn(p[2], p[1])
    old_xy, old_mv = drawn(p[1], p[0])
    cur_xy, cur_mv = drawn(p[2], p[1])
    lag = np.linalg.norm(card_xy - old_xy)
    check("previous publish: strands trail the card by its per-frame motion (> 1 px here)", lag > 1.0, lag)
    check("previous publish: its MV differs from the card's true MV", np.linalg.norm(old_mv - card_mv) > 1e-3,
          (old_mv, card_mv))
    check("this frame's publish: strand position and MV equal the card's",
          np.allclose(cur_xy, card_xy, atol=1e-9) and np.allclose(cur_mv, card_mv, atol=1e-9), (cur_xy, cur_mv))
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


def test_batch_cache():
    from PIL import Image

    key = batch.groom_key(b"gr2", b"mask", "hair")
    check("groom cache key is stable SHA-256", len(key) == 64 and key == batch.groom_key(b"gr2", b"mask", "hair"))
    check("groom cache key frames input boundaries", batch.groom_key(b"a", b"bc", "hair") != batch.groom_key(b"ab", b"c", "hair"))
    check("groom cache key includes each input", all(key != batch.groom_key(*args) for args in
          ((b"other", b"mask", "hair"), (b"gr2", b"other", "hair"), (b"gr2", b"mask", "other"))))
    with patch.object(batch, "VERSION", "validation-only"):
        check("validation version leaves groom key unchanged", batch.groom_key(b"gr2", b"mask", "hair") == key)
    with patch.object(batch, "CONVERTER_VERSION", "new-converter"):
        check("converter version invalidates groom key", batch.groom_key(b"gr2", b"mask", "hair") != key)

    hair = card_mesh()
    mask = np.zeros((32, 16, 4), np.uint8)
    mask[..., 3] = strand_mask(32, 16, [lambda y: 8.0], 2)
    mask[..., 2] = np.linspace(0, 255, 32).astype(np.uint8)[:, None]
    groom, rep = convert.convert(hair, mask, tile_grid=(1, 1), step=8)
    groom, dropped = convert.dedupe(groom)
    head, corr = batch.head_proxy(hair), [0.5, 0.75]
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        buf = BytesIO()
        Image.fromarray(mask).save(buf, format="PNG")
        raw = buf.getvalue()
        path = d / "cache" / "groom" / (batch.groom_key(b"gr2", raw, "hair") + ".npz")
        batch.save_groom_cache(path, groom, hair, rep, dropped, (1, 1), 0.75, corr, head)
        got = batch.load_groom_cache(path)
        check("groom cache round trip loads", got is not None)
        check("groom cache every dataclass field equal including dtype", all(
            np.array_equal(getattr(groom, f.name), getattr(got[0], f.name)) and
            getattr(groom, f.name).dtype == getattr(got[0], f.name).dtype for f in dataclasses.fields(groom)))
        check("groom cache retains original cards", all(np.array_equal(getattr(hair, f.name), getattr(got[1], f.name))
              for f in dataclasses.fields(hair)))
        check("groom cache retains report and validation inputs", got[2:7] == (rep, dropped, (1, 1), 0.75, corr)
              and np.array_equal(got[7], head))
        check("missing groom cache is a miss", batch.load_groom_cache(d / "missing.npz") is None)

        style = dict(name="synthetic", gr2="hair.gr2", mask="mask.dds", mesh="hair")
        class Pak(dict):
            read = dict.__getitem__

        # A cache hit must reach validation without invoking any part of the conversion pipeline.
        with patch("tools.assets.lspk.PackageSet", return_value=Pak({"hair.gr2": b"gr2", "mask.dds": raw})), \
             patch.object(batch, "gr2_dump", side_effect=AssertionError("dump called")), \
             patch.object(convert, "convert", side_effect=AssertionError("convert called")), \
             patch.object(convert, "dedupe", side_effect=AssertionError("dedupe called")), \
             patch.object(batch, "bbox_views", return_value={"test": (None, None)}), \
             patch.object(render, "render", return_value=(np.zeros((2, 2, 3)), np.zeros((2, 2)))):
            rec = batch.process(style, None, None, d / "out", d, False)
            check("cached process reaches validation without conversion", rec.get("iou") == [0.0], rec)
            with patch.object(render, "render", return_value=(np.ones((2, 2, 3)), np.ones((2, 2)))), \
                 patch.object(asset, "write") as write_asset, \
                 patch.object(analysis, "reorient", wraps=analysis.reorient) as reorient:
                geometry_head = (np.array([0., -.3, 0.]), np.array([.066, .089, .083]))
                batch.save_groom_cache(path, groom, hair, rep, dropped, (1, 1), .75, [.1], geometry_head)
                cache_bytes = path.read_bytes()
                rec = batch.process(style, None, None, d / "out", d, False)
                check("bad mask roots with a head proxy pass via geometry", rec.get("verdict") == "pass" and
                      rec.get("root_source") == "geometry" and rec.get("root_reversed_strands") == groom.strands, rec)
                written = write_asset.call_args.args[3][0][1]
                check("asset receives root-first geometry without rewriting converter cache", reorient.call_count == 1 and
                      written.points[0, 1] < written.points[-1, 1] and path.read_bytes() == cache_bytes)
                tangent_head = (np.array([.1, -.1, 0.]), geometry_head[1])
                batch.save_groom_cache(path, groom, hair, rep, dropped, (1, 1), .75, [.1], tangent_head)
                rec = batch.process(style, None, None, d / "out", d, False)
                check("undecided geometry roots retain a review reason", rec.get("verdict") == "needs-review" and
                      any(s.startswith("root from geometry (") for s in rec.get("reasons", [])), rec)
                batch.save_groom_cache(path, groom, hair, rep, dropped, (1, 1), .75, [.1], head)
                rec = batch.process(style, None, None, d / "out", d, False)
                check("bad mask roots without a head proxy still reject", rec.get("verdict") == "reject" and
                      any(s.startswith("root channel does not") for s in rec.get("reasons", [])), rec)
                reorient.reset_mock()
                batch.save_groom_cache(path, groom, hair, rep, dropped, (1, 1), .75, corr, geometry_head)
                rec = batch.process(style, None, None, d / "out", d, False)
                check("good mask root path never reorients", reorient.call_count == 0 and "root_source" not in rec, rec)
            path.write_bytes(b"corrupt npz")
            check("corrupt groom cache is a miss", batch.load_groom_cache(path) is None)
            check("revalidate cache miss does not convert or replace state", batch.process(style, None, None, d / "out", d, False, True) is None)
            dump_path = d / "cache" / "gr2" / (batch.hashlib.sha256(b"gr2").hexdigest() + ".bin")

            def read_dump(p, name):
                if p.read_bytes() != b"dump":
                    raise ValueError("corrupt dump")
                return hair

            with patch.object(batch, "gr2_dump", side_effect=lambda lib, src, dst: dst.write_bytes(b"dump")) as dump, \
                 patch.object(mesh, "load_gr2_dump", side_effect=read_dump), \
                 patch.object(convert, "convert", return_value=(groom, rep)) as conversion, \
                 patch.object(convert, "dedupe", return_value=(groom, dropped)):
                rec = batch.process(style, None, None, d / "out", d, False)
                check("corrupt groom silently rebuilds", rec.get("iou") == [0.0] and conversion.call_count == 1
                      and batch.load_groom_cache(path) is not None, rec)
                check("successful dump is cached by GR2 content", dump_path.read_bytes() == b"dump" and dump.call_count == 1)
                path.unlink()
                rec = batch.process(style, None, None, d / "out", d, False)
                check("GR2 cache skips LSLib on groom miss", rec.get("iou") == [0.0] and dump.call_count == 1, rec)
                path.unlink()
                dump_path.write_bytes(b"corrupt dump")
                rec = batch.process(style, None, None, d / "out", d, False)
                check("corrupt GR2 dump silently rebuilds", rec.get("iou") == [0.0] and dump.call_count == 2
                      and dump_path.read_bytes() == b"dump", rec)
def test_volume():
    from tools.hair_groom import volume_table, make_volume_table
    generated = (ROOT / "shaders/rt/hair_volume_table.hlsli").read_text()
    check("volume HLSL table is current", generated == make_volume_table.hlsl(volume_table.VOLUME_C, volume_table.VOLUME_A))
    table = np.asarray(volume_table.VOLUME_A)
    check("volume table is passive and monotonic in colour", np.all((table>=0)&(table<=1)) and np.all(np.diff(table,axis=0)>=0))
    cells = json.loads((ROOT / "tools/hair_groom/volume_calibration.json").read_text())
    for c in dual.TABLE_C:
        for beta in dual.TABLE_BETA:
            cell = cells[f"{c}/{beta}"]
            a = cell['single']+cell['k_volume']*cell['multiple']
            check("baked volume response equals S + k_volume M", abs(a-dual.volume_albedo((c,c,c),beta)[0])<1e-12)
    # Independently check the analytic sampler against evaluate(), including directional moments.
    # Four standard errors of the two independent estimators, per channel/moment.
    rng = np.random.default_rng(103)
    n = 200000
    t = np.tile([0.,0.,1.], (n,1))
    wo = np.tile([1.,0.,0.], (n,1))
    h = rng.uniform(-1,1,n)
    wi = bsdf._sphere(rng,n)
    col, beta = (0.3,0.2,0.1), 0.5
    uniform = bsdf.evaluate(t,wo,wi,h,col,beta)*4*np.pi
    sampled, weight = bsdf.sample(t,wo,h,col,beta,rng)
    def moments(w, d):
        return w[:,:,None]*np.column_stack((np.ones(n), d[:,0], d[:,1], d[:,2]**2))[:,None,:]
    a, b = moments(uniform,wi), moments(weight,sampled)
    se = np.sqrt((a.var(0)+b.var(0))/n)
    check("volume path sampler agrees with BSDF angular integrals within 4 sigma", np.all(np.abs(a.mean(0)-b.mean(0)) < 4*se),
          (a.mean(0)-b.mean(0))/se)
    for beta in (0.35,0.5,0.7):
        white = dual.volume_albedo((1,1,1),beta)
        print(f"volume furnace beta={beta}: {white}", flush=True)
        check("volume white furnace within 3%", np.all(np.abs(white-1) < 0.03), white)
    # Independent seeds, RGB walks and held-out colours, distinct from the grey calibration cells.
    for col, beta in ((bsdf.CHAR_HAIR['color'],0.41), ((0.6,0.45,0.3),0.55), ((0.85,0.7,0.5),0.7)):
        ref = bsdf.slab_albedo(col,beta,n=50000,bounces=4000,seed=91,diffuse=True)
        got = dual.volume_albedo(col,beta)
        error = np.abs(got/ref-1)
        print(f"volume colour={col} beta={beta}: A={got} MC={ref} relative={error}", flush=True)
        check("volume vs independent BSDF slab within 5%", np.all(error < 0.05), error)
    white_ref = bsdf.slab_albedo((1,1,1),0.5,n=30000,bounces=20000,seed=93,diffuse=True)
    print(f"volume white independent slab (finite 20000-bounce walk): {white_ref}", flush=True)
    check("independent nonabsorbing slab returns unit energy within 3%", np.all(np.abs(white_ref-1)<0.03), white_ref)


def test_cost():
    h, w = 256, 64
    m = np.zeros((h, w, 4), np.uint8)
    m[..., 3] = strand_mask(h, w, [lambda y, c=c: c for c in np.arange(4, 60, 4.0)], 2)
    m[..., 2] = np.linspace(0, 255, h)[:, None].astype(np.uint8)
    card = card_mesh()
    lods = asset.build(card, m)
    with tempfile.TemporaryDirectory() as d:
        path = Path(d) / "x.hgrm"
        asset.write(path, 0x1234, card, lods)
        indices, got = cost.lods_of(path)
        rows = cost.report(d)
    want = [(g.points.shape[0], g.segments) for _, g in lods]
    check("cost: LOD headers give every LOD's points and segments", indices == 24 and [(p, s) for _, p, s in got] == want, (got, want))
    check("cost: one row per file", len(rows) == 1 and len(rows[0]["lods"]) == len(lods))
    c = cost.groom_cost(100, cost.BENCH_SEGMENTS, 0)
    check("cost: the bench groom costs the bench numbers (60 MB BLAS, 0.10 ms refit)",
          abs(c["refit_ms"] - 0.10) < 1e-9 and abs(c["instance_mb"] - (60.0 + 1200e-6)) < 1e-6, c)
    check("cost: coarser LODs trace cheaper per ray", cost.NS_PER_RAY[0] > cost.NS_PER_RAY[1] > cost.NS_PER_RAY[2])


def test_sample():
    state, rows = {}, []

    def add(name, reason=None, lod=0, category="player", **extra):
        state[name] = dict(gr2=name + ".gr2", lod=lod, reasons=[reason] if reason else [], **extra)
        rows.append(dict(name=name, category=category))

    for name in "abcde":
        add("coverage_" + name, "coverage differs by +0.10")
    add("coverage_0_lod1", "coverage differs by +0.10", lod=1)
    add("coverage_unused", "coverage differs by +0.10", category="unused")
    add("coverage_cv", "coverage differs by +0.10", category="CV only")
    for name, reason in (("silhouette", "silhouette IoU 0.50"), ("radiance", "mean radiance differs by +20%"),
                         ("roots away", "2 cards with the root away from the head"),
                         ("root channel", "root channel does not run along the rows"),
                         ("mirrored UV", "3 cards with mirrored UV triangles"), ("error", "ValueError: no mesh")):
        add(name, reason, lod=1)
    state["silhouette"]["reasons"].append("coverage differs by -0.30")
    for name in ("draw_a", "draw_b"):
        add(name, triangles=10)
    add("uv_join", triangles=20)
    add("uploaded", asset="ABCDEF0123456789.hgrm")
    add("catalogue_only", triangles=30, asset="0000000000000001.hgrm")
    add("companion_category", category="companion")
    for name in sample.COMPANIONS:
        add("hair_" + name.upper(), category="unused")
    add("draw_lod1", lod=1)
    state["draw_lod1"]["gr2"] = state["draw_a"]["gr2"]
    add("draw_lod2", lod=2)
    state["draw_lod2"]["gr2"] = state["draw_a"]["gr2"]
    log = ("[hair groom] unmatched hair draw: 30 indices (10 triangles): no groom\n"
           "[hair groom] UV join 60 indices: matched; uv0 ...\n"
           "[hair groom] abcdef0123456789 LOD 1 uploaded: 10 strands\n"
           "[hair groom] indexed 0000000000000001.hgrm: index id 0000000000000001, 90 indices\n"
           "[other] unmatched hair draw: 90 indices\n"
           "[hair groom] unmatched hair draw: 30 indices (10 triangles): repeated\n")
    with tempfile.TemporaryDirectory() as d:
        path = Path(d) / "live.log"
        path.write_text(log, encoding="utf-8")
        with path.open(encoding="utf-8") as f:
            indices, ids = sample.log_draws(f)
    check("sample parses draw counts and upload IDs, ignores catalogue", indices == {30, 60} and ids == {"abcdef0123456789"})
    reach = {"rows": rows}
    names, sources, classes = sample.select(state, reach, indices, ids)
    check("sample maps all count collisions and upload IDs", sources["logs"] == {"draw_a", "draw_b", "uv_join", "uploaded"})
    check("sample coverage evenly spaced, LOD0 preferred, reachable only", classes["coverage"] == {"coverage_a", "coverage_c", "coverage_e"}, classes)
    check("sample first reason determines class, LOD1 fills missing LOD0", all(classes[k] == {k} for k in sample.CLASSES if k != "coverage"))
    check("sample includes category and case-insensitive companion names", len(sources["companions"]) == 11)
    check("sample adds LOD siblings by GR2", sources["LOD siblings added"] == {"draw_lod1"} and "draw_lod2" not in names)
    check("sample deterministic regardless of input order", sample.select(dict(reversed(list(state.items()))), {"rows": rows[::-1]}, indices, ids)[0] == names == sorted(set(names)))
    check("sample configurable quota", sample.pick_classes(state, reach, 1)["coverage"] == {"coverage_c"} and
          not any(sample.pick_classes(state, reach, 0).values()))
    check("sample no reasons has no failure class", sample.failure_class({"reasons": []}) is None)


for t in (test_coverage, test_coverage_pair_gate_bias, test_post_depth, test_rr_hair_guides, test_groom_soup_bounds, test_pixel_seal_tearing, test_strand_source_safety, test_ndc_ray, test_publish_lock, test_tips_dither, test_reorient, test_sample, test_batch_cache, test_cost, test_dual, test_volume, test_primary, test_bsdf, test_batch_catalogue, test_geom, test_asset, test_bench_data, test_quality_options, test_run_radii, test_thin, test_trace, test_curves2d, test_cross_sections, test_stitch, test_convert, test_locate_overlap, test_render, test_render_equivalence, test_mesh_loader):
    t()
print(f"{len(FAILS)} failures" if FAILS else "all passed")
sys.exit(1 if FAILS else 0)
