"""Where strand hair turns grainy: high-frequency energy per image-chain stage over the hair region.

Reads trace folders (layer/dump.cpp: dumps.json + img/*.png previews + *_p.f32), finds each stage by pass
fragment shader + slot (image ids differ between runs), builds the hair region from the G-buffer class
(RT2 alpha, class 5) and prints, per stage, the HF energy of strands vs cards.

  py -3 tools/hair_groom/grain_stages.py <trace dir> [...] [--label a,b,...]
  py -3 tools/hair_groom/grain_stages.py --ab on,off,on <trace dirs>   (the 5g live gates, see ab())

HF = rms(x - box3(x)) / mean(x) over the region (relative, so it is independent of exposure); for unit vectors
(tangent) and motion vectors (pixels) the rms of the vector difference to the 3x3 mean, unnormalised.
COH = mean structure-tensor coherence (0 isotropic speckle, 1 parallel lines) of scalar stages: line structure.
RT0 (stereographic normal) is not measured: its RG16F preview spans +-max|v| (~23) in 8 bits, too coarse.
Two regions: "mask" = class-5 pixels whose 3x3 neighbourhood is all class 5 (grain inside the hair surface);
"hull" = the class-5 mask closed by 2 px (holes between strands count: coverage grain). Display-resolution
stages use the render mask scaled by nearest neighbour.
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

FS_GBUF = "3f982de3c126181f"     # G-buffer pass with all five targets (class in RT2 alpha)
FS_LIGHT = "06e66a6fbd88a6b3"    # first write of the scene colour after deferred lighting
FS_COMPOSE = "5c3fdabc477f316a"  # scene colour before post (rrpost.cpp kFsCompose; *_p.f32)
FS_FINAL = "d25b8086de1734d5"    # tonemapped display frame
FS_MOTION = "788dfdf60d8ed4cd"   # motion vectors, RG16F pixels (FRAME_MAP 6b)


def load_png(d, v):
    return np.asarray(Image.open(d / v["png"]).convert("RGB"), dtype=np.float32) / 255.0


def load_alpha(d, v):
    p = d / v["png"].replace(".png", "_a.png")
    return np.asarray(Image.open(p).convert("L"), dtype=np.uint8)


def undo_preview(t, v):
    """Inverse of dump.cpp's preview mapping (8-bit quantised)."""
    fmt = v["fmt"]
    mn, mx = np.array(v["min"], np.float32), np.array(v["max"], np.float32)
    ch = len(v["min"])
    if fmt in ("R11G11B10F", "RGBA16F", "RGBA32F"):
        g = np.clip(t, 0, 0.999) ** 2.2
        return g / (1.0 - g)
    if ch == 2:
        am = np.maximum(np.abs(mn), np.abs(mx))[:2] + 1e-20
        return (t[..., :2] - 0.5) * 2.0 * am
    if ch == 1:
        return mn[0] + t[..., 0] * (mx[0] - mn[0])
    return t


def pick(D, fs=None, slot=None, fmt=None, w=None, last=True, depth=False):
    c = [v for v in D.values() if (fs is None or v["pass_fs"] == fs) and (slot is None or v["slot"] == slot)
         and (fmt is None or v["fmt"] == fmt) and (w is None or v["w"] == w)
         and ((v["slot"] == "depth") == depth)]
    return (c[-1] if last else c[0]) if c else None


def lum(c):
    return 0.2126 * c[..., 0] + 0.7152 * c[..., 1] + 0.0722 * c[..., 2]


def oct12_decode(t):
    q = np.round(t * 255).astype(np.int32)
    qx = (q[..., 0] << 4) | (q[..., 1] >> 4)
    qy = ((q[..., 1] & 15) << 8) | q[..., 2]
    e = np.stack([qx, qy], -1) / 2047.0 - 1.0
    z = 1.0 - np.abs(e).sum(-1)
    x, y = e[..., 0], e[..., 1]
    neg = z < 0
    x2 = np.where(neg, (1 - np.abs(y)) * np.sign(x + 1e-12), x)
    y2 = np.where(neg, (1 - np.abs(x)) * np.sign(y + 1e-12), y)
    v = np.stack([x2, y2, z], -1)
    return v / np.maximum(np.linalg.norm(v, axis=-1, keepdims=True), 1e-9)


def box3(x):
    p = np.pad(x, [(1, 1), (1, 1)] + [(0, 0)] * (x.ndim - 2), mode="edge")
    h, w = x.shape[:2]
    return sum(p[i:i + h, j:j + w] for i in range(3) for j in range(3)) / 9.0


def erode(m, r=1):
    for _ in range(r):
        p = np.pad(m, 1, constant_values=False)
        h, w = m.shape
        m = np.logical_and.reduce([p[i:i + h, j:j + w] for i in range(3) for j in range(3)])
    return m


def dilate(m, r=1):
    return ~erode(~m, r)


def scale_mask(m, shape):
    h, w = shape
    ys = (np.arange(h) * m.shape[0] // h)
    xs = (np.arange(w) * m.shape[1] // w)
    return m[ys][:, xs]


def hf_scalar(x, m):
    if m.sum() < 50:
        return float("nan")
    lap = x - box3(x)
    return float(np.sqrt((lap[m] ** 2).mean()) / max(abs(x[m].mean()), 1e-9))


def blur(x, n):
    for _ in range(n):
        x = box3(x)
    return x


def coherence(x, m):
    if m.sum() < 50:
        return float("nan")
    gy, gx = np.gradient(x)
    jxx, jyy, jxy = blur(gx * gx, 3), blur(gy * gy, 3), blur(gx * gy, 3)
    return float((np.sqrt((jxx - jyy) ** 2 + 4 * jxy ** 2) / np.maximum(jxx + jyy, 1e-12))[m].mean())


def hf_vector(v, m):
    if m.sum() < 50:
        return float("nan")
    lap = v - box3(v)
    return float(np.sqrt((lap[m] ** 2).sum(-1).mean()))


def stages(d: Path):
    D = json.loads((d / "dumps.json").read_text())
    out = {}
    gb = [v for v in D.values() if v["slot"] == 4 and v["fmt"] == "RGBA8"]
    fs_gbuf = gb[-1]["pass_fs"] if gb else FS_GBUF   # the five-target G-buffer pass (its hash differs per build)
    rt2 = pick(D, fs_gbuf, 2)
    cls = load_alpha(d, rt2) & 7
    out["_cls"] = cls
    g = lambda slot: pick(D, fs_gbuf, slot)
    rt1, rt3 = g(1), g(3)
    out["RT1 albedo"] = ("s", lum(load_png(d, rt1)))
    t2 = load_png(d, rt2)
    out["RT2.x rough"] = ("s", t2[..., 0])
    out["RT2.z backlit"] = ("s", t2[..., 2])
    t3 = load_png(d, rt3)
    out["RT3 tangent"] = ("v", oct12_decode(t3))
    out["RT3.w"] = ("s", load_alpha(d, rt3).astype(np.float32) / 255.0)
    mv = pick(D, FS_MOTION, 0)
    if mv:
        out["motion (px)"] = ("m", undo_preview(load_png(d, mv), mv))
    dep = pick(D, fs_gbuf, depth=True)
    if dep:
        out["depth"] = ("s", undo_preview(load_png(d, dep), dep))
    li = pick(D, FS_LIGHT, 0, last=False)
    if li:
        out["lit (deferred)"] = ("s", lum(undo_preview(load_png(d, li), li)))
    co = pick(D, FS_COMPOSE, 0)
    if co:
        f = d / co["png"].replace(".png", "_p.f32")
        if f.exists():
            w, h = Image.open(d / co["png"]).size
            out["compose (pre-RR)"] = ("s", lum(np.fromfile(f, np.float32).reshape(h, w, 4)))
        else:
            out["compose (pre-RR)"] = ("s", lum(undo_preview(load_png(d, co), co)))
    z = [v for v in D.values() if v["pass_fs"] == "0000000000000000" and v["slot"] != "depth"]
    ords = sorted({v["pass_ordinal"] for v in z})
    if len(ords) >= 2:
        pg, pr = ords[-2], ords[-1]
        gd = [v for v in z if v["pass_ordinal"] == pg and v["slot"] == 0]
        gs = [v for v in z if v["pass_ordinal"] == pg and v["slot"] == 1]
        rin = [v for v in z if v["pass_ordinal"] == pr and v["slot"] == 0]
        rout = [v for v in z if v["pass_ordinal"] == pr and v["slot"] == 2]
        if gd:
            out["RR guide diffuse"] = ("s", lum(load_png(d, gd[-1])))
        if gs:
            out["RR guide specular"] = ("s", lum(undo_preview(load_png(d, gs[-1]), gs[-1])))
        if rin:
            out["RR input colour"] = ("s", lum(undo_preview(load_png(d, rin[-1]), rin[-1])))
        if rout:
            out["RR output (disp)"] = ("s", lum(undo_preview(load_png(d, rout[-1]), rout[-1])))
    fi = pick(D, FS_FINAL, 0)
    if fi:
        out["final (disp)"] = ("s", lum(load_png(d, fi)))
    return out


def measure(d: Path):
    st = stages(d)
    cls = st.pop("_cls")
    hair = cls == 5
    regions = {"mask": erode(hair, 1), "hull": erode(dilate(hair, 2), 2)}
    res = {"_pixels": int(hair.sum()), "_hull": int(regions["hull"].sum()),
           "_cover": float(hair[regions["hull"]].mean())}
    for name, (kind, x) in st.items():
        r = {}
        for rn, m in regions.items():
            mm = m if x.shape[:2] == m.shape else scale_mask(m, x.shape[:2])
            r[rn] = hf_scalar(x, mm) if kind == "s" else hf_vector(x, mm)
            r[rn + "_mean"] = float(x[mm].mean()) if kind == "s" and mm.sum() else float("nan")
            r[rn + "_coh"] = coherence(x, mm) if kind == "s" else float("nan")
        res[name] = r
    return res


def tangent_normal(d: Path):
    """5g tangent space: median |T . N| on hair pixels, T = RT3 decoded (exact 8-bit), N = the RR normal guide (world,
    rr_guides.hlsl v2) from its dump_raw float32. On strand pixels with the fibre normal, N is perpendicular to the
    true tangent, so a world-space RT3 gives ~0; a view-space RT3 read as world does not. None without dump_raw."""
    D = json.loads((d / "dumps.json").read_text())
    gb = [v for v in D.values() if v["slot"] == 4 and v["fmt"] == "RGBA8"][-1]["pass_fs"]
    cls = load_alpha(d, pick(D, gb, 2)) & 7
    z = [v for v in D.values() if v["pass_fs"] == "0" * 16 and v["slot"] == 2 and v["fmt"] == "RGBA16F"
         and v["w"] == cls.shape[1]]
    if not z:
        return None
    f = d / z[0]["png"].replace(".png", ".f32")
    if not f.exists():
        return None
    n = np.fromfile(f, np.float32).reshape(z[0]["h"], z[0]["w"], 4)[..., :3]
    t = oct12_decode(load_png(d, pick(D, gb, 3)))
    if n.shape[:2] != cls.shape:
        return None
    m = erode(cls == 5, 1)
    nn = n[m] / np.maximum(np.linalg.norm(n[m], axis=-1, keepdims=True), 1e-9)
    return float(np.median(np.abs((t[m] * nn).sum(-1))))


def ab(dirs, labels):
    """Gates of the 5g live A/B (one session; labels on/off per window, e.g. on,off,on; RTX_HAIR_PRIMARY 5g):
    macro normal: lit coherence of every on window above every off window by >= max(0.01, 2 |on - on|) (0.01: the
    strand repeat spread of 2026-11-07, 0.009); RR specular-guide HF on/off >= 1.5 (cards/strands 2.8 on 2026-11-07);
    mean hair luminance reported (a shading-model change may move it). Tangent (needs dump_raw=1 and macro normal
    off): median |T . N| <= 0.02 in tangent-on windows (oct12 < 0.1 deg, fp16 guide; derived), > 0.05 in off."""
    R = []
    for d in dirs:
        r = measure(d)
        R.append({"coh": r["lit (deferred)"]["mask_coh"] if "lit (deferred)" in r else r["compose (pre-RR)"]["mask_coh"],
                  "spec": r["RR guide specular"]["mask"], "mean": r["compose (pre-RR)"]["hull_mean"],
                  "tn": tangent_normal(d)})
    print("window   lit-COH  spec-HF  hair-mean  |T.N|")
    for l, r in zip(labels, R):
        tn = "-" if r["tn"] is None else f"{r['tn']:.4f}"
        print(f"{l:8s} {r['coh']:7.4f} {r['spec']:8.4f} {r['mean']:10.4f}  {tn}")
    on = [r for l, r in zip(labels, R) if l.startswith("on")]
    off = [r for l, r in zip(labels, R) if l.startswith("off")]
    if on and off:
        rep = max(on, key=lambda r: r["coh"])["coh"] - min(on, key=lambda r: r["coh"])["coh"] if len(on) > 1 else 0.0
        margin = max(0.01, 2 * rep)
        gap = min(r["coh"] for r in on) - max(r["coh"] for r in off)
        print(f"GATE macro coherence: min(on) - max(off) = {gap:+.4f} >= {margin:.4f}: {'PASS' if gap >= margin else 'FAIL'}")
        ratio = np.mean([r["spec"] for r in on]) / max(np.mean([r["spec"] for r in off]), 1e-9)
        print(f"GATE spec guide HF on/off = {ratio:.2f} >= 1.5: {'PASS' if ratio >= 1.5 else 'FAIL'}")
        print(f"INFO hair mean on/off = {np.mean([r['mean'] for r in on]) / np.mean([r['mean'] for r in off]):.3f}")
        tn_on = [r["tn"] for r in on if r["tn"] is not None]
        tn_off = [r["tn"] for r in off if r["tn"] is not None]
        if tn_on and tn_off:
            ok = max(tn_on) <= 0.02 and min(tn_off) > 0.05
            print(f"GATE tangent |T.N| on max {max(tn_on):.4f} <= 0.02, off min {min(tn_off):.4f} > 0.05: "
                  f"{'PASS' if ok else 'FAIL'}")
    return R


def main(argv):
    if "--ab" in argv:   # --ab on,off,on <dirs>: the 5g live gates
        i = argv.index("--ab")
        labels = argv[i + 1].split(",")
        return ab([Path(a) for a in argv[:i] + argv[i + 2:]], labels)
    labels = None
    if "--label" in argv:
        i = argv.index("--label")
        labels = argv[i + 1].split(",")
        argv = argv[:i] + argv[i + 2:]
    dirs = [Path(a) for a in argv]
    labels = labels or [p.name for p in dirs]
    R = [measure(p) for p in dirs]
    print("pixels " + " ".join(f"{l}={r['_pixels']}" for l, r in zip(labels, R)))
    print("hull coverage " + " ".join(f"{l}={r['_cover']:.3f}" for l, r in zip(labels, R)))
    names = [k for k in R[0] if not k.startswith("_")]
    for rn in ("mask", "hull"):
        print(f"\nHF ({rn})".ljust(22) + "".join(l.rjust(9) for l in labels))
        for n in names:
            print(n.ljust(21) + "".join(f"{r.get(n, {}).get(rn, float('nan')):9.4f}" for r in R))
    for key, title in (("mask_coh", "COH (mask)"), ("hull_mean", "mean (hull)")):
        print(f"\n{title}".ljust(22) + "".join(l.rjust(9) for l in labels))
        for n in names:
            print(n.ljust(21) + "".join(f"{r.get(n, {}).get(key, float('nan')):9.4f}" for r in R))
    return R


if __name__ == "__main__":
    main(sys.argv[1:])
