"""Primary strand hair, GPU vs CPU (CTest hair_primary_gpu, docs/RTX_HAIR_PRIMARY.md 1b).

    py -3 tests/hair_primary_gpu_test.py <hair_primary_gpu_check.exe> <check.spv> [--groom groom.npz --size N]

Pixel-centre camera rays into an LSS groom on the GPU (tools/hair_primary_gpu_check.cpp, the layer's geometry
settings) against tools/hair_groom/primary.py's tracer and encodings. Default: a synthetic groom whose strands have
constant radius, so the capsule reference and the swept spheres are the same surface. --groom: a converted groom
(tapered radii: the reference uses the mean radius per segment, so edge pixels may differ; report only).
Exits 77 (skip) when the device has no linear swept spheres.
"""
import argparse
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.hair_groom import bsdf, dual, primary  # noqa: E402

FAILS = []


def check(name, ok, info=""):
    print(f"{'PASS' if ok else 'FAIL'} {name}{(': ' + str(info)) if not ok else ''}")
    if not ok:
        FAILS.append(name)


def synthetic(seed=3, n=120):
    """Wavy strands in front of the camera, 2 mm radius each (constant along a strand)."""
    rng = np.random.default_rng(seed)
    pts, rad, offs = [], [], [0]
    for _ in range(n):
        root = rng.uniform([-0.05, -0.05, 0.25], [0.05, 0.05, 0.32])
        d = rng.normal(size=3)
        d /= np.linalg.norm(d)
        s = np.linspace(0, 0.06, 12)[:, None]
        p = root + s * d + 0.004 * np.sin(s * 120 + rng.uniform(0, 6))[:, :1] * np.array([[0, 1, 0]])
        pts.append(p)
        rad.append(np.full(len(p), rng.uniform(0.0008, 0.002)))
        offs.append(offs[-1] + len(p))
    return np.concatenate(pts), np.concatenate(rad), np.array(offs)


def push_block(cam, near=0.1, far=1000.0, jitter=None, tag=0):
    """The 128-byte push block. jitter (pixels, y down) is folded into the ray basis' forward vector (the depth matrix
    keeps the unjittered forward); tag goes to forward.w as uint bits (coverage mode)."""
    r, u, f = cam.basis()
    w, h = cam.size
    k = np.tan(cam.fov_y / 2) / (h / 2)
    a = primary.depth_a(near, far)
    b = -a * far
    vp = np.zeros((4, 4))
    vp[:3, 0], vp[:3, 1], vp[:3, 2], vp[3, 2], vp[:3, 3] = r, u, a * f, b, f
    head = struct.pack("<4f", *cam.position, k) + struct.pack("<3fI", *r, w) + struct.pack("<3fI", *u, h)
    fwd = f if jitter is None else f + jitter[0] * k * r - jitter[1] * k * u
    return head + struct.pack("<3fI", *fwd, tag) + vp.astype("<f4").tobytes()


def run(exe, spv, points, radii, offsets, cam, push=None, extra=()):
    s0, _ = primary.segments(points, radii, offsets)
    w, h = cam.size
    with tempfile.TemporaryDirectory() as d:
        inp, out = Path(d) / "in.bin", Path(d) / "out.bin"
        inp.write_bytes(b"HPGC" + struct.pack("<7I", 1, len(points), len(s0), w, h, 0, 0) + (push or push_block(cam)) +
                        np.asarray(points, "<f4").tobytes() + np.asarray(radii, "<f4").tobytes() +
                        s0.astype("<u4").tobytes())
        p = subprocess.run([str(exe), str(inp), str(spv), str(out), *extra], capture_output=True, text=True)
        print(p.stdout.strip())
        if p.returncode == 77:
            print("SKIP: no linear swept spheres on this device")
            sys.exit(77)
        if p.returncode:
            print(p.stderr)
            check("GPU check ran", False, p.returncode)
            return None
        return np.frombuffer(out.read_bytes(), "<u4").reshape(h, w, 8)


def compare(g, ref, gb, strict):
    gpu_hit = g[..., 0] != 0xFFFFFFFF
    both = gpu_hit & ref.hit
    iou = both.sum() / max((gpu_hit | ref.hit).sum(), 1)
    seg_gpu = (g[..., 0] & 0xFFFFFF).astype(np.int64)
    same = both & (seg_gpu == ref.segment)
    t = g[..., 2].view("<f4")
    dt = np.abs(t - ref.t)[both]
    depth = g[..., 3].view("<f4")
    ddepth = np.abs(depth - gb["depth"])[same] / np.maximum(np.abs(gb["depth"][same]), 1e-12)
    rg = np.stack([g[..., 4].view("<f4"), g[..., 5].view("<f4")], -1)
    n_gpu, n_ref = primary.stereo_decode(rg[same]), primary.stereo_decode(gb["rt0"][same])
    dn = np.degrees(np.arccos(np.clip((n_gpu * n_ref).sum(-1), -1, 1)))
    rt3 = np.stack([(g[..., 6] >> s) & 255 for s in (0, 8, 16)], -1)[same]
    # Compare decoded: a one-step rounding difference of the 12-bit x moves the shared middle byte by 16.
    t_gpu, t_ref = primary.oct12_decode(rt3 / 255.0), primary.oct12_decode(gb["rt3"][same])
    drt3 = np.degrees(np.arccos(np.clip((t_gpu * t_ref).sum(-1), -1, 1)))
    v = g[..., 1].view("<f4")
    dv = np.abs(v - ref.v)[same]
    rep = {"ref_pixels": int(ref.hit.sum()), "gpu_pixels": int(gpu_hit.sum()), "iou": float(iou),
           "same_segment": float(same.sum() / max(both.sum(), 1)), "t_err_max_mm": float(dt.max() * 1e3) if dt.size else 0,
           "depth_rel_err_max": float(ddepth.max()) if ddepth.size else 0, "normal_err_max_deg": float(dn.max()) if dn.size else 0,
           "tangent_err_max_deg": float(drt3.max()) if drt3.size else 0, "v_err_max": float(dv.max()) if dv.size else 0,
           "rt2w": sorted(set(int(x) for x in g[..., 7][gpu_hit].ravel()))}
    print(json.dumps(rep))
    if not strict:
        return rep
    # Constant radius: the same surface; only float32 vs float64 at grazing edges can flip a pixel.
    check("coverage IoU >= 0.995", iou >= 0.995, iou)
    check("same closest segment on >= 99% of the common pixels", rep["same_segment"] >= 0.99, rep["same_segment"])
    check("hit distance within 0.05 mm", rep["t_err_max_mm"] < 0.05, rep["t_err_max_mm"])
    check("native depth relative error < 1e-5", rep["depth_rel_err_max"] < 1e-5, rep["depth_rel_err_max"])
    check("RT0 normal within 0.2 deg", rep["normal_err_max_deg"] < 0.2, rep["normal_err_max_deg"])
    check("RT3 tangent within 0.1 deg (12:12 step)", rep["tangent_err_max_deg"] < 0.1, rep["tangent_err_max_deg"])
    check("segment parameter within 1e-3", rep["v_err_max"] < 1e-3, rep["v_err_max"])
    check("RT2.w is class 5 with light mask 3", rep["rt2w"] == [5 | (3 << 4)], rep["rt2w"])
    return rep


def bsdf_check(exe, spv, n=4000, seed=11):
    """hb_eval (float32, GPU) against bsdf.evaluate (float64) on random fibre frames, offsets, colours, roughness."""
    rng = np.random.default_rng(seed)

    def unit(k):
        v = rng.normal(size=(k, 3))
        return v / np.linalg.norm(v, axis=1, keepdims=True)
    cases = [(bsdf.CHAR_HAIR["color"], bsdf.CHAR_HAIR["roughness"]), ((0.6, 0.45, 0.3), 0.25), ((0.9, 0.9, 0.9), 0.7),
             ((0.05, 0.03, 0.02), 0.35)]
    rows, want = [], []
    for color, rough in cases:
        t, wo, wi = unit(n), unit(n), unit(n)
        h = rng.uniform(-0.99, 0.99, n)
        want.append(bsdf.evaluate(t, wo, wi, h, color, rough))
        block = np.zeros((n, 16), "<f4")
        block[:, 0:3], block[:, 3], block[:, 4:7], block[:, 7] = t, h, wo, rough
        block[:, 8:11], block[:, 12:15] = wi, color
        rows.append(block)
    want = np.concatenate(want)
    with tempfile.TemporaryDirectory() as d:
        inp, out = Path(d) / "s.bin", Path(d) / "o.bin"
        inp.write_bytes(np.concatenate(rows).tobytes())
        p = subprocess.run([str(exe), "bsdf", str(inp), str(spv), str(out)], capture_output=True, text=True)
        print(p.stdout.strip())
        if p.returncode == 77:
            return
        if p.returncode:
            print(p.stderr)
            check("BSDF GPU check ran", False, p.returncode)
            return
        got = np.frombuffer(out.read_bytes(), "<f4").reshape(-1, 4)[:, :3].astype(np.float64)
    # Relative to the sample's own value, floored at 1e-3 of the lobe peak scale (float32 exp/log of tiny tails).
    err = np.abs(got - want) / np.maximum(np.abs(want), 1e-3 * np.abs(want).max())
    rep = {"samples": len(want), "rel_err_p99": float(np.percentile(err, 99)), "rel_err_max": float(err.max()),
           "finite": bool(np.all(np.isfinite(got)))}
    print(json.dumps(rep))
    check("BSDF GPU values finite", rep["finite"])
    # float32 against float64: measured p99 4e-6, max 1.5e-5 (RTX 5090); 10x margin.
    check("BSDF GPU vs CPU: 99% of samples within 4e-5 relative", rep["rel_err_p99"] < 4e-5, rep["rel_err_p99"])
    check("BSDF GPU vs CPU: every sample within 2e-4 relative", rep["rel_err_max"] < 2e-4, rep["rel_err_max"])


def r_sample_check(exe, spv, n=4000, seed=19):
    """M4.2: hb_sample_r (GPU, float32) against bsdf.sample_r (float64) from the same uniforms: the sampled direction and
    the weight A_0; plus the white furnace of the R lobe (the estimator against a quadrature of r_lobe, within 3%)."""
    rng = np.random.default_rng(seed)

    def unit(k):
        v = rng.normal(size=(k, 3))
        return v / np.linalg.norm(v, axis=1, keepdims=True)
    rows, want_d, want_w = [], [], []
    for rough in (0.09, 0.25, bsdf.CHAR_HAIR["roughness"], 0.7):
        t, wo = unit(n), unit(n)
        h = rng.uniform(-0.99, 0.99, n)
        u = rng.uniform(1e-4, 1 - 1e-4, (n, 3))
        d, w = bsdf.sample_r(t, wo, h, rough, u)
        want_d.append(d)
        want_w.append(w)
        block = np.zeros((n, 16), "<f4")
        block[:, 0:3], block[:, 3], block[:, 4:7], block[:, 7] = t, 2.0, wo, rough
        block[:, 8:11], block[:, 15] = u, h
        rows.append(block)
    want_d, want_w = np.concatenate(want_d), np.concatenate(want_w)
    with tempfile.TemporaryDirectory() as tmp:
        inp, out = Path(tmp) / "s.bin", Path(tmp) / "o.bin"
        inp.write_bytes(np.concatenate(rows).tobytes())
        p = subprocess.run([str(exe), "bsdf", str(inp), str(spv), str(out)], capture_output=True, text=True)
        if p.returncode == 77:
            return
        if p.returncode:
            print(p.stderr)
            check("R sample GPU check ran", False, p.returncode)
            return
        got = np.frombuffer(out.read_bytes(), "<f4").reshape(-1, 4).astype(np.float64)
    dirr = np.linalg.norm(got[:, :3] - want_d, axis=1)
    werr = np.abs(got[:, 3] - want_w) / np.maximum(want_w, 1e-6)
    rep = {"samples": len(want_w), "dir_err_p99": float(np.percentile(dirr, 99)), "dir_err_max": float(dirr.max()),
           "weight_rel_err_max": float(werr.max())}
    print(json.dumps(rep))
    # float32 log/exp of the narrow longitudinal lobe (v ~ 0.004 at beta 0.09): the direction to 1e-3 at the 99th
    # percentile, 1e-2 everywhere (beta 0.09: a 1e-6 change of u moves theta_i by up to ~1e-3 rad near u -> 0).
    check("R sample GPU vs CPU: direction within 1e-3 (99%) and 1e-2 (all)", rep["dir_err_p99"] < 1e-3 and
          rep["dir_err_max"] < 1e-2, rep)
    check("R sample GPU vs CPU: weight A_0 within 1e-4 relative", rep["weight_rel_err_max"] < 1e-4, rep)
    furnace = []
    for rough in (0.09, bsdf.CHAR_HAIR["roughness"], 0.7):
        for theta in (0.0, 30.0, 60.0, 80.0):
            e, se, q = bsdf.r_furnace(rough, theta, n=100000, n_quad=(121, 240), n_h=24)
            furnace.append({"beta": rough, "theta_o": theta, "estimator": round(e, 5), "se": round(se, 6),
                            "quadrature": round(q, 5), "rel": round(e / q - 1, 4)})
    print(json.dumps({"r_furnace": furnace}))
    worst = max(abs(f["rel"]) for f in furnace)
    check("R lobe white furnace: sampled estimator = quadrature within 3%", worst < 0.03, worst)


def gpu_rows(exe, spv, rows, label):
    """One "bsdf"-mode dispatch over 16-float rows; float4 per row, or None (skip / failure recorded)."""
    with tempfile.TemporaryDirectory() as tmp:
        inp, out = Path(tmp) / "s.bin", Path(tmp) / "o.bin"
        inp.write_bytes(np.asarray(rows, "<f4").tobytes())
        p = subprocess.run([str(exe), "bsdf", str(inp), str(spv), str(out)], capture_output=True, text=True)
        if p.returncode == 77:
            return None
        if p.returncode:
            print(p.stderr)
            check(label + " GPU check ran", False, p.returncode)
            return None
        return np.frombuffer(out.read_bytes(), "<f4").reshape(-1, 4).astype(np.float64)


def fibonacci(n):
    i = np.arange(n) + 0.5
    z = 1 - 2 * i / n
    a = np.pi * (1 + 5 ** 0.5) * i
    r = np.sqrt(1 - z * z)
    return np.stack([r * np.cos(a), r * np.sin(a), z], 1)


def game_light_check(exe, game_spv, bsdf_spv, dual_spv, n=4000, seed=29):
    """M4.3 (rt_hair_fibre_mapped): per light, the game's hair term of a shadow-mapped light that gi/hair_fibre.hlsl
    removes (GPU bg3_materials lobes vs bsdf.game_hair_light), and the fibre response that replaces it on a fixture of
    N lights: a uniform sphere of N = 20000 lights of irradiance 4 pi / N each (a white environment, v = 1) gives
    sum E F = the integral of F: for the white single-scattering response exactly 1 (energy conservation); the dual
    response at n = 0..3 fibres in front against its CPU twin summed over the same lights."""
    rng = np.random.default_rng(seed)

    def unit(k):
        v = rng.normal(size=(k, 3))
        return v / np.linalg.norm(v, axis=1, keepdims=True)
    c = bsdf.CHAR_HAIR
    rows, want = [], []
    for color, rough in ((c["color"], c["roughness"]), ((0.6, 0.45, 0.3), 0.55), ((0.85, 0.8, 0.7), 0.3)):
        t, wo, wi = unit(n), unit(n), unit(n)
        S = rng.uniform(0, 1, n)
        S[:200] = 1.0
        nrm = wo - t * np.einsum("nd,nd->n", wo, t)[:, None]
        nrm /= np.linalg.norm(nrm, axis=1, keepdims=True)
        want.append(bsdf.game_hair_light(t, nrm, wo, wi, color, rough, c["scatter"], c["backlit"], S))
        block = np.zeros((n, 16))
        block[:, 0:3], block[:, 3], block[:, 4:7], block[:, 7] = t, S, wo, rough
        block[:, 8:11], block[:, 11], block[:, 12:15], block[:, 15] = wi, c["scatter"], color, c["backlit"]
        rows.append(block)
    want = np.concatenate(want)
    got = gpu_rows(exe, game_spv, np.concatenate(rows), "game light")
    if got is not None:
        err = np.abs(got[:, :3] - want) / np.maximum(np.abs(want), 1e-3 * np.abs(want).max())
        rep = {"samples": len(want), "rel_err_p99": float(np.percentile(err, 99)), "rel_err_max": float(err.max())}
        print(json.dumps({"game_light": rep}))
        check("M4.3 game hair term GPU vs CPU: 99% within 1e-4, all within 1e-3 relative",
              rep["rel_err_p99"] < 1e-4 and rep["rel_err_max"] < 1e-3, rep)
    # The N-light fixture: a fibre seen at 30 degrees from its normal plane, every light at 4 pi / N.
    N = 20000
    L = fibonacci(N)
    t = np.array([0.0, 0.0, 1.0])
    wo = np.array([np.cos(np.radians(30)), 0.0, np.sin(np.radians(30))])
    E = 4 * np.pi / N
    fixture = {}
    for rough in (0.41, 0.7):
        block = np.zeros((4 * N, 16))
        block[:, 0:3], block[:, 4:7], block[:, 7], block[:, 12:15] = t, wo, rough, 1.0
        block[:, 8:11] = np.repeat(L, 4, 0)
        block[:, 3] = np.tile([-0.75, -0.25, 0.25, 0.75], N)   # hb_eval_pixel's four offsets
        s = gpu_rows(exe, bsdf_spv, block, "N-light single scattering")
        if s is None:
            return
        single = float(E * s[:, :3].sum(0).mean() / 4)
        fixture[f"single_white_beta{rough}"] = round(single, 5)
        # Lights at N = 20000 directions: the quadrature error of the sum (lobe width / spacing), 1%.
        check(f"N-light fixture: white single scattering sums to 1 (beta {rough}) within 1%", abs(single - 1) < 0.01, single)
        for nf in (0, 1, 3):
            blk = np.zeros((N, 16))
            blk[:, 0:3], blk[:, 3], blk[:, 4:7], blk[:, 7] = t, 2.0, wo, rough
            blk[:, 8:11], blk[:, 11], blk[:, 12:15] = L, nf, (0.85, 0.7, 0.5)
            g = gpu_rows(exe, dual_spv, blk, "N-light dual")
            if g is None:
                return
            cpu = dual.shade_table_pixel(np.tile(t, (N, 1)), np.tile(wo, (N, 1)), L, np.full(N, nf), (0.85, 0.7, 0.5), rough)
            gs, cs = E * g[:, :3].sum(0), E * cpu.sum(0)
            rel = float(np.max(np.abs(gs - cs) / cs))
            fixture[f"dual_beta{rough}_n{nf}"] = {"gpu": np.round(gs, 5).tolist(), "cpu": np.round(cs, 5).tolist(), "rel": rel}
            check(f"N-light fixture: sum of the dual response (beta {rough}, n {nf}) GPU = CPU within 1e-4", rel < 1e-4, rel)
    print(json.dumps({"n_light_fixture": fixture}))


def dual_check(exe, spv, n=3000, seed=13):
    """hd_shade (GPU) against dual.shade_table (CPU, same tables) on random frames, offsets, fibre counts 0..6."""
    rng = np.random.default_rng(seed)

    def unit(k):
        v = rng.normal(size=(k, 3))
        return v / np.linalg.norm(v, axis=1, keepdims=True)
    cases = [(bsdf.CHAR_HAIR["color"], bsdf.CHAR_HAIR["roughness"]), ((0.6, 0.45, 0.3), 0.55), ((0.85, 0.8, 0.7), 0.4)]
    rows, want = [], []
    for color, rough in cases:
        t, wo, wi = unit(n), unit(n), unit(n)
        h = rng.uniform(-0.99, 0.99, n)
        nf = rng.integers(0, 7, n).astype(float)
        pix = rng.uniform(size=n) < 0.5          # half the samples: the pixel response (h = 2 selects it)
        w = dual.shade_table(t, wo, wi, h, nf, color, rough)
        w[pix] = dual.shade_table_pixel(t[pix], wo[pix], wi[pix], nf[pix], color, rough)
        h[pix] = 2.0
        want.append(w)
        block = np.zeros((n, 16), "<f4")
        block[:, 0:3], block[:, 3], block[:, 4:7], block[:, 7] = t, h, wo, rough
        block[:, 8:11], block[:, 11], block[:, 12:15] = wi, nf, color
        rows.append(block)
    # h = 3 selects the diffuse volume response; include endpoints, clamps and random interpolation cells.
    colours = np.vstack((rng.uniform(0.02, 1, (2000, 3)), np.ones((3, 3)), np.full((3, 3), 0.02)))
    betas = np.concatenate((rng.uniform(0.05, 1, 2000), [0.35, 0.5, 0.7]*2))
    volume_want = np.array([dual.volume_albedo(c, b) for c, b in zip(colours, betas)])
    block = np.zeros((len(colours), 16), "<f4")
    block[:, 3], block[:, 7], block[:, 12:15] = 3.0, betas, colours
    rows.append(block)
    want.append(volume_want)
    # h = 4 selects the pixel response's single scattering part (hd_shade_pixel_parts). Every table entry, the same
    # directions at n = 0 and n = 1: behind one strand a fibre never gets more single scattering than directly lit
    # (the correction k scales only the backward multiple scattering).
    m = 200
    single_rows = 0
    for c in dual.TABLE_C:
        for beta in dual.TABLE_BETA:
            t, wo, wi = unit(m), unit(m), unit(m)
            f_s = sum(bsdf.evaluate(t, wo, wi, np.full(m, o), (c, c, c), beta) for o in (-0.75, -0.25, 0.25, 0.75)) / 4
            a_f = dual.averages_table((c, c, c), beta).a_f
            for nf in (0.0, 1.0):
                block = np.zeros((m, 16), "<f4")
                block[:, 0:3], block[:, 3], block[:, 4:7], block[:, 7] = t, 4.0, wo, beta
                block[:, 8:11], block[:, 11], block[:, 12:15] = wi, nf, c
                rows.append(block)
                want.append(dual.single_part(f_s, np.full(m, nf), a_f))
                single_rows += m
    want = np.concatenate(want)
    with tempfile.TemporaryDirectory() as d:
        inp, out = Path(d) / "s.bin", Path(d) / "o.bin"
        inp.write_bytes(np.concatenate(rows).tobytes())
        p = subprocess.run([str(exe), "bsdf", str(inp), str(spv), str(out)], capture_output=True, text=True)
        print(p.stdout.strip())
        if p.returncode == 77:
            return
        if p.returncode:
            print(p.stderr)
            check("dual GPU check ran", False, p.returncode)
            return
        got = np.frombuffer(out.read_bytes(), "<f4").reshape(-1, 4)[:, :3].astype(np.float64)
    err = np.abs(got - want) / np.maximum(np.abs(want), 1e-3 * np.abs(want).max())
    rep = {"samples": len(want), "rel_err_p99": float(np.percentile(err, 99)), "rel_err_max": float(err.max()),
           "finite": bool(np.all(np.isfinite(got)))}
    print(json.dumps(rep))
    check("dual GPU values finite", rep["finite"])
    check("dual GPU vs CPU: 99% of samples within 1e-4 relative", rep["rel_err_p99"] < 1e-4, rep["rel_err_p99"])
    check("dual GPU vs CPU: every sample within 1e-3 relative", rep["rel_err_max"] < 1e-3, rep["rel_err_max"])
    single = got[len(want) - single_rows:].reshape(-1, 2, m, 3)   # (entry, n = 0 / 1, sample, RGB)
    excess = float(np.max(single[:, 1] - single[:, 0]))
    print(json.dumps({"single_entries": single.shape[0], "single_n1_minus_n0_max": excess}))
    check("dual GPU: single scattering behind one strand never exceeds the directly lit fibre's (every table entry)",
          excess <= 0.0, excess)
    volume_err = np.abs(got[-single_rows - len(colours):-single_rows] - volume_want) / np.maximum(volume_want, 1e-12)
    print(json.dumps({"volume_samples": len(colours), "volume_max_rel_diff": float(volume_err.max())}))
    check("volume GPU vs CPU: every channel within 1e-3 relative", volume_err.max() < 1e-3, volume_err.max())


def depth_match_check(exe, spv, g, near=0.1, far=1000.0):
    """The fibre passes' strand-pixel test (shaders/rt/hair_depth_match.hlsli, gi/hair_strand_pixel.hlsli) on the GPU's
    real strand pixels: their view depth w (from the strand pass's device depth with this check's projection), the
    game's device depth z = 0.1 / w (reversed infinite Z) as the entry holds it, and the game's R32F linear depth from
    the D24 code of z (d = w_24 / zr). Every strand pixel must match (before 2026-11-08 none did: e.z was compared with
    d directly), and the same pixel with the depth twice the tolerance away (something else in front) must not."""
    hit = g[..., 0] != 0xFFFFFFFF
    Df = g[..., 3].view("<f4")[hit].astype(np.float64)
    a = primary.depth_a(near, far)
    w = (-a * far) / (Df - a)
    z = (0.1 / w).astype(np.float32)
    rows, want = [], []
    for zr in (1.0, 1000.0):   # the frustum rays' view depth (the game's carry the far-plane scale)
        # The negative case: twice the test's tolerance away (one code + 6 half ulps of z), in whole codes.
        far_codes = np.ceil(2 * (1 + 6 * z.astype(np.float64))) + 1   # tolerance in codes: 1 + 6 z
        for codes in (0, 1, -1):
            z24 = (np.round(z.astype(np.float64) * 2 ** 24) + codes * far_codes) / 2 ** 24
            d = ((0.1 / z24) / zr).astype(np.float32)
            block = np.zeros((len(z), 16), "<f4")
            block[:, 0], block[:, 1], block[:, 2], block[:, 3] = z, d, zr, 5.0
            rows.append(block)
            want.append(codes == 0)
    got = gpu_rows(exe, spv, np.concatenate(rows), "strand depth match")
    if got is None:
        return
    m = got[:, 0].reshape(len(rows), -1) > 0.5
    rates = [float(r.mean()) for r in m]
    z_, d_ = rows[3][:, 0].astype(np.float64), rows[3][:, 1].astype(np.float64)
    old = float(np.mean(np.abs(z_ - d_) <= 1e-6 * np.maximum(np.abs(d_), 1e-6)))   # the previous comparison
    print(json.dumps({"strand_pixels": int(hit.sum()), "match_rate": rates, "old_rule_match_rate": old}))
    check("strand pixel test: the GPU finds strand pixels to test", hit.sum() > 1000, int(hit.sum()))
    check("strand pixel test: every strand pixel matches the game's linear depth (zr 1 and 1000)",
          rates[0] == 1.0 and rates[3] == 1.0, rates)
    check("strand pixel test: a depth twice the tolerance away never matches", max(rates[1], rates[2], rates[4], rates[5]) == 0.0,
          rates)


def shadow_check(exe, spv, pts, rad, offs, cam, n_rays=3000, seed=17):
    """Strand crossings per ray on the GPU (tools/hair_shadow_gpu_check.hlsl): the candidates of one non-opaque query
    and successive closest hits, against the CPU count of capsules the ray enters (docs/RTX_HAIR_PRIMARY.md C2: the
    shadow transmittance a_f^n needs n)."""
    g = run(exe, spv, pts, rad, offs, cam)
    if g is None:
        return
    w, h = cam.size
    s0, s1 = primary.segments(pts, rad, offs)
    a, b2 = pts[s0].astype(np.float64), pts[s1].astype(np.float64)
    r = (rad[s0] + rad[s1]).astype(np.float64) / 2
    rays = cam.rays().reshape(h, w, 3)
    rng = np.random.default_rng(seed)
    hit_px = np.argwhere(g[..., 1] > 0)
    pick = hit_px[rng.choice(len(hit_px), min(n_rays, len(hit_px)), replace=False)]
    cpu = np.zeros(len(pick), int)
    t1_cpu = np.full(len(pick), np.inf)
    for i, (y, x) in enumerate(pick):
        t, _ = primary.capsule_hits(np.broadcast_to(cam.position, (len(a), 3)), np.broadcast_to(rays[y, x], (len(a), 3)), a, b2, r)
        t1_cpu[i] = np.min(t)
        # One crossing per run of adjacent segments of a strand (chained end caps: the joint between two segments
        # is one surface, so a ray through it enters one capsule, not two).
        hit = np.flatnonzero(np.isfinite(t))
        cpu[i] = int(len(hit) - np.sum((np.diff(hit) == 1) & (s1[hit[:-1]] == s0[hit[1:]]))) if len(hit) else 0
    col = lambda c: g[pick[:, 0], pick[:, 1], c].astype(int)
    rep = {"rays": len(pick), "cpu_mean": float(cpu.mean())}
    for name, c in (("candidates", 0), ("successive", 1), ("candidates_dedup", 2), ("successive_dedup", 3)):
        v = col(c)
        rep[name] = {"mean": round(float(v.mean()), 3), "equal": round(float((v == cpu).mean()), 4),
                     "over": round(float((v > cpu).mean()), 4), "under": round(float((v < cpu).mean()), 4)}
    print(json.dumps(rep))
    check("shadow: one non-opaque query, adjacent segments once, counts the strands crossed (>= 98% of rays)",
          rep["candidates_dedup"]["equal"] >= 0.98, rep["candidates_dedup"])
    check("shadow: successive closest hits deduplicated count the same (>= 98%)", rep["successive_dedup"]["equal"] >= 0.98,
          rep["successive_dedup"])
    # The stochastic transmittance (rt_groom_blocks): mean visibility d_f * a_f^n (n >= 1), a_f = 0.5, d_f = 0.7, 256 trials.
    n = col(2)
    lit = col(4)
    want = np.where(n > 0, 0.7 * 0.5 ** n, 1.0) * 256
    sigma = np.sqrt(np.sum(want * (1 - want / 256)))
    err = float(lit.sum() - want.sum())
    print(json.dumps({"transmit_lit": int(lit.sum()), "transmit_expected": round(float(want.sum()), 1), "sigma": round(float(sigma), 1)}))
    check("shadow transmittance: mean visibility = d_f a_f^n within 4 sigma (binomial)", abs(err) <= 4 * sigma, (err, sigma))
    # M2.3 (rt_hair_groom_ao_t): an AO/GI ray's closest strand hit stands with probability 1 - d_f a_f^n (the same
    # rt_groom_blocks decision), else the ray passes on; its occlusion weight is the GI fade 1 - (t1/R)^2 at the closest
    # strand t1 (rt_closest's opaque groom query). The GPU's closest strand t against the CPU's first capsule entry, and
    # the mean occlusion from the GPU trials against the CPU expectation (n and t1 from the CPU).
    t1 = g[pick[:, 0], pick[:, 1], 5].view("<f4").astype(np.float64)
    t_err = np.abs(t1 - t1_cpu)
    R = float(1.5 * np.max(t1_cpu[np.isfinite(t1_cpu)]))   # every strand inside the radius, fades 0.3..0.97
    fade = lambda tt: np.clip(1 - (tt / R) ** 2, 0, 1)
    p_block = np.where(cpu > 0, 1 - 0.7 * 0.5 ** cpu, 0.0)
    occ_gpu = float(np.sum((1 - lit / 256) * fade(t1)))
    occ_want = float(np.sum(p_block * fade(t1_cpu)))
    occ_sigma = float(np.sqrt(np.sum(fade(t1_cpu) ** 2 * p_block * (1 - p_block) / 256)))
    print(json.dumps({"ao_t1_err_max": float(t_err.max()), "ao_t1_err_p99": float(np.percentile(t_err, 99)),
                      "ao_occlusion": round(occ_gpu, 2), "ao_expected": round(occ_want, 2), "ao_sigma": round(occ_sigma, 2),
                      "ao_opaque": round(float(np.sum(fade(t1_cpu))), 2)}))
    # The swept spheres and the capsule reference are the same surface for constant radii: float32 ray t (1e-5 of
    # the 0.3 m distance, as the primary depth comparison).
    check("AO: closest strand t equals the CPU capsule entry (99% within 3e-6 m)", np.percentile(t_err, 99) <= 3e-6,
          float(np.percentile(t_err, 99)))
    check("AO transmittance: mean occlusion = (1 - d_f a_f^n) fade(t1) within 4 sigma", abs(occ_gpu - occ_want) <= 4 * occ_sigma,
          (occ_gpu, occ_want, occ_sigma))


def coverage_check(exe, spv, frames=16):
    """M3.2: the two rays and hp_cov_pick on the GPU (HP_CHECK_COVERAGE build of the check shader) against the CPU twins
    on the sub-pixel fixture, frame by frame with the Halton jitter, then the GPU masks against the supersampled area
    coverage (the offline gate of tests/hair_groom_test.py test_coverage)."""
    pts, rad, offs, cam = primary.subpixel_fixture(seed=1)
    w, h = cam.size
    py, px = np.mgrid[0:h, 0:w]
    ref = primary.coverage_reference(pts, rad, offs, cam)
    masks, agree = [], []
    for fi in range(frames):
        j = primary.jitter(fi)
        g = run(exe, spv, pts, rad, offs, cam, push_block(cam, jitter=j, tag=fi + 1))
        if g is None:
            return
        a = primary.trace(pts, rad, offs, cam, j + primary.coverage_offset(px, py, 0))
        b = primary.trace(pts, rad, offs, cam, j + primary.coverage_offset(px, py, 1))
        cpu = primary.coverage_pick(a.hit, b.hit, a.t, b.t, px, py, fi + 1)
        gpu = g[..., 1].astype(int)
        any_hit = a.hit | b.hit | (g[..., 3] != 0)
        agree.append(float((cpu == gpu)[any_hit].mean()) if any_hit.any() else 1.0)
        masks.append(gpu != 2)
    single = primary.coverage_metrics(primary.coverage_frames(pts, rad, offs, cam, frames, False), ref)
    rep = primary.coverage_metrics(np.array(masks), ref)
    rep["pick_agreement_min"] = min(agree)
    print(json.dumps({"coverage_gpu": rep, "coverage_cpu_single": single}))
    # Constant radius: the same capsules; only float32 vs float64 at grazing edges can flip a pixel (as the IoU gate).
    check("coverage GPU pick equals the CPU twin on >= 99% of the pixels any ray hits, every frame",
          rep["pick_agreement_min"] >= 0.99, rep["pick_agreement_min"])
    tol = 4 * rep["bias_sigma"] / np.sqrt(frames)
    check(f"coverage GPU pair unbiased against the 8 x 8 area coverage (4 sigma {tol:.4f})", abs(rep["bias"]) <= tol, rep["bias"])
    check("coverage GPU pair: 3 x 3 box error below the CPU single ray's", rep["box_rmse"] < single["box_rmse"],
          (rep["box_rmse"], single["box_rmse"]))


def nearest_check(exe, spv, shift=0.03):
    """5u (rt_hair_groom_nearest): a dense card-kept groom in front of a swapped one (the harness' twin mode: the same
    groom twice, instance 1 moved `shift` metres towards the camera). Per pixel ray: the strand G-buffer variant's restart
    loop (kHpSkips = 3), hp_nearest, and the reference (instance 0 alone). The loop leaves pixels without a strand where
    the reference has one; hp_nearest finds the reference's own strand on every such pixel."""
    pts, rad, offs = synthetic(seed=5, n=600)
    cam = primary.Camera(np.zeros(3), np.array([0.0, 0.0, 1.0]), np.array([0.0, 1.0, 0.0]), np.radians(20.0), (160, 160))
    g = run(exe, spv, pts, rad, offs, cam, extra=("twin", str(shift)))
    if g is None:
        return
    none = 0xFFFFFFFF
    ref = g[..., 4] != none
    loop_hole = ref & (g[..., 0] == none)
    near_hit = g[..., 2] != none
    same = ref & near_hit & ((g[..., 2] & 0xFFFFFF) == g[..., 4]) & ((g[..., 2] >> 24) == 0) & (g[..., 3] == g[..., 5])
    loop_same = ref & (g[..., 0] == g[..., 4]) & (g[..., 1] == g[..., 5])
    rep = {"reference_pixels": int(ref.sum()), "loop_holes": int(loop_hole.sum()), "loop_same": int(loop_same.sum()),
           "nearest_same": int(same.sum()), "nearest_holes": int((ref & ~near_hit).sum()),
           "nearest_extra": int((~ref & near_hit).sum()), "restarts_max": int(g[..., 6].max()),
           "rays_with_4_restarts": int((g[..., 6] >= 4).sum()), "nearest_passed_over": int(g[..., 7].sum())}
    print(json.dumps({"nearest_gpu": rep}))
    check("nearest: the restart loop (kHpSkips 3) leaves pixels without the strand the reference finds (the hole)",
          rep["loop_holes"] > 0, rep)
    check("nearest: hp_nearest returns the reference's own strand and distance on every reference pixel",
          rep["nearest_same"] == rep["reference_pixels"], rep)
    check("nearest: hp_nearest finds no strand where instance 0 has none (the card-kept groom is never returned)",
          rep["nearest_extra"] == 0, rep)
    # The loop's other defect: a restart begins 2 x hair_primary_max_radius (2 cm) past the other row's strand, so an
    # accepted strand within 2 cm behind it is jumped over and a farther one (of the same groom) is returned.
    loop_wrong = ref & (g[..., 0] != none) & ~loop_same
    farther = g[..., 1].view("<f4") >= g[..., 5].view("<f4")   # equal: the joint of two chained segments
    rep["loop_farther_strand"] = int(loop_wrong.sum())
    print(json.dumps({"nearest_gpu_loop_farther": rep["loop_farther_strand"]}))
    check("nearest: where the loop returns another strand than the reference it is a farther one (jumped over)",
          bool(np.all(farther[loop_wrong])), rep)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe", type=Path)
    ap.add_argument("spv", type=Path)
    ap.add_argument("--groom", type=Path)
    ap.add_argument("--size", type=int, default=512)
    ap.add_argument("--distance", type=float, default=0.9)
    ap.add_argument("--bsdf-spv", type=Path, help="also compare shaders/rt/hair_bsdf.hlsli with tools/hair_groom/bsdf.py")
    ap.add_argument("--dual-spv", type=Path, help="also compare shaders/rt/hair_dual.hlsli with tools/hair_groom/dual.py")
    ap.add_argument("--shadow-spv", type=Path, help="also count strand crossings per ray (tools/hair_shadow_gpu_check.hlsl)")
    ap.add_argument("--coverage-spv", type=Path, help="also check the M3.2 coverage pair (HP_CHECK_COVERAGE build)")
    ap.add_argument("--game-light-spv", type=Path, help="also check M4.3 (tools/hair_game_light_gpu_check.hlsl), N-light fixture")
    ap.add_argument("--nearest-spv", type=Path, help="also check 5u hp_nearest (tools/hair_nearest_gpu_check.hlsl), twin groom")
    a = ap.parse_args()
    if a.groom:
        gr = np.load(a.groom)
        pts, rad, offs = gr["points"], gr["radii"], gr["offsets"]
        centre = np.array([0.0, 1.57, 0.05])
        cam = primary.Camera(centre + np.array([0.0, 0.0, -a.distance]), np.array([0.0, 0.0, 1.0]),
                             np.array([0.0, 1.0, 0.0]), np.radians(30.0), (a.size, a.size))
    else:
        pts, rad, offs = synthetic()
        cam = primary.Camera(np.zeros(3), np.array([0.0, 0.0, 1.0]), np.array([0.0, 1.0, 0.0]), np.radians(20.0), (160, 160))
    g = run(a.exe, a.spv, pts, rad, offs, cam)
    if g is not None:
        ref = primary.trace(pts, rad, offs, cam)
        compare(g, ref, primary.gbuffer(ref, cam), strict=a.groom is None)
    if a.bsdf_spv:
        bsdf_check(a.exe, a.bsdf_spv)
        r_sample_check(a.exe, a.bsdf_spv)
    if a.dual_spv:
        dual_check(a.exe, a.dual_spv)
        if g is not None:
            depth_match_check(a.exe, a.dual_spv, g)
    if a.shadow_spv:
        shadow_check(a.exe, a.shadow_spv, pts, rad, offs, cam)
    if a.coverage_spv:
        coverage_check(a.exe, a.coverage_spv)
    if a.nearest_spv:
        nearest_check(a.exe, a.nearest_spv)
    if a.game_light_spv and a.bsdf_spv and a.dual_spv:
        game_light_check(a.exe, a.game_light_spv, a.bsdf_spv, a.dual_spv)
    print(f"{len(FAILS)} failures" if FAILS else "all passed")
    sys.exit(1 if FAILS else 0)


if __name__ == "__main__":
    main()
