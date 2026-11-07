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


def synthetic(seed=3):
    """Wavy strands in front of the camera, 2 mm radius each (constant along a strand)."""
    rng = np.random.default_rng(seed)
    pts, rad, offs = [], [], [0]
    for _ in range(120):
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


def run(exe, spv, points, radii, offsets, cam, push=None):
    s0, _ = primary.segments(points, radii, offsets)
    w, h = cam.size
    with tempfile.TemporaryDirectory() as d:
        inp, out = Path(d) / "in.bin", Path(d) / "out.bin"
        inp.write_bytes(b"HPGC" + struct.pack("<7I", 1, len(points), len(s0), w, h, 0, 0) + (push or push_block(cam)) +
                        np.asarray(points, "<f4").tobytes() + np.asarray(radii, "<f4").tobytes() +
                        s0.astype("<u4").tobytes())
        p = subprocess.run([str(exe), str(inp), str(spv), str(out)], capture_output=True, text=True)
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
    volume_err = np.abs(got[-len(colours):] - volume_want) / np.maximum(volume_want, 1e-12)
    print(json.dumps({"volume_samples": len(colours), "volume_max_rel_diff": float(volume_err.max())}))
    check("volume GPU vs CPU: every channel within 1e-3 relative", volume_err.max() < 1e-3, volume_err.max())


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
    for i, (y, x) in enumerate(pick):
        t, _ = primary.capsule_hits(np.broadcast_to(cam.position, (len(a), 3)), np.broadcast_to(rays[y, x], (len(a), 3)), a, b2, r)
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
    if a.dual_spv:
        dual_check(a.exe, a.dual_spv)
    if a.shadow_spv:
        shadow_check(a.exe, a.shadow_spv, pts, rad, offs, cam)
    if a.coverage_spv:
        coverage_check(a.exe, a.coverage_spv)
    print(f"{len(FAILS)} failures" if FAILS else "all passed")
    sys.exit(1 if FAILS else 0)


if __name__ == "__main__":
    main()
