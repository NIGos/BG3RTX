"""RR before post (rr_post=1): replication check and A/B against the old path.

    py -3 scripts/rr_post_compare.py <capture>                 replication check
    py -3 scripts/rr_post_compare.py <post capture> <old capture> [--out docs/img/prefix]

Needs captures taken with dump_raw=1 (float32 dumps of the fake RR passes, the
game's grade output 0592680b and the tonemapped frame d25b8086).

Replication check (one capture with rr_post=1, rr_post_check=1 or a trace):
the layer's `check` image is grade(A * Xr + C) at render resolution, i.e. our
reconstruction of the game's own DLSS input. It is compared with the game's
grade output of the same frame. The game writes R11G11B10 (6/6/5-bit
mantissas), so ours is quantised the same way before the comparison.

A/B: the tonemapped HDR10 frames (PQ, RGB10A2) of an rr_post=1 and an
rr_post=0 trace: mean luminance (nits, PQ EOTF), mean |delta| in PQ code
values, percentiles, and where it differs; plus a x4 difference image and
1:1 crops when --out is given.
"""
import json
import sys
from pathlib import Path

import numpy as np


def load_capture(cap):
    cap = Path(cap)
    dumps = json.loads((cap / "dumps.json").read_text())
    frame = json.loads((cap / "frame.json").read_text())
    return cap, dumps, frame


def raw(cap, d):
    p = cap / d["png"].replace(".png", ".f32")
    a = np.fromfile(p, dtype=np.float32)
    return a.reshape(d["h"], d["w"], 4)


def by_fs(dumps, fs):
    return [d for d in dumps.values() if d.get("pass_fs") == fs]


def tagged(frame, dumps, tag):
    """Dumps of the fake pass with this tag, in slot order."""
    out = []
    for s in frame["submits"]:
        for c in s["cmds"]:
            ev = c["events"]
            for i, e in enumerate(ev):
                if e.get("op") == "pass" and e.get("tag") == tag:
                    for e2 in ev[i + 1:]:
                        if e2.get("op") == "end":
                            out = [dumps[k] for k in e2.get("dumps", []) if k in dumps]
                            break
    return sorted(out, key=lambda d: d["slot"])


def quant_r11g11b10(x):
    """Round to the R11G11B10 mantissas (6, 6, 5 bits), round to nearest."""
    x = np.maximum(x, 0.0)
    out = np.empty_like(x)
    for c, bits in enumerate((6, 6, 5)):
        v = x[..., c]
        m, e = np.frexp(v)  # v = m * 2^e, m in [0.5, 1)
        scale = 2.0 ** (bits + 1)
        out[..., c] = np.ldexp(np.round(m * scale) / scale, e)
    return out


def quant_trunc(x):
    x = np.maximum(x, 0.0)
    out = np.empty_like(x)
    for c, bits in enumerate((6, 6, 5)):
        m, e = np.frexp(x[..., c])
        scale = 2.0 ** (bits + 1)
        out[..., c] = np.ldexp(np.floor(m * scale) / scale, e)
    return out


def lum(rgb):
    return rgb[..., 0] * 0.2126 + rgb[..., 1] * 0.7152 + rgb[..., 2] * 0.0722


def replication(cap_path):
    cap, dumps, frame = load_capture(cap_path)
    game = by_fs(dumps, "0592680bc336dca3")
    rr = tagged(frame, dumps, "rrpost")
    check = [d for d in rr if d["fmt"] == "RGBA16F" and d["w"] == game[0]["w"]]
    if not game or not check:
        print("no game grade output or check image in", cap)
        return None
    g = raw(cap, game[0])[..., :3]
    ours = raw(cap, check[-1])[..., :3]
    res = {}
    for name, q in (("round", quant_r11g11b10), ("trunc", quant_trunc)):
        o = q(ours)
        rel = np.abs(o - g) / np.maximum(np.abs(g), 1e-4)
        res[name] = rel
    best = min(res, key=lambda k: float(np.mean(res[k])))
    rel = res[best]
    same = np.all(rel < 1e-6, axis=-1)
    one_ulp = np.all(rel <= 1.0 / 32 + 1e-6, axis=-1)
    print(f"{cap.name}: replication check, ours quantised ({best}) vs the game's grade output")
    print(f"  mean luminance game {lum(g).mean():.5f}, ours {lum(ours).mean():.5f} "
          f"({(lum(ours).mean() / lum(g).mean() - 1) * 100:+.3f}%)")
    print(f"  identical texels {same.mean() * 100:.2f}%, within 1 ULP {one_ulp.mean() * 100:.2f}%, "
          f"mean relative error {rel.mean() * 100:.4f}%, p99.9 {np.percentile(rel, 99.9) * 100:.3f}%, "
          f"max {rel.max() * 100:.2f}%")
    bad = ~one_ulp
    if bad.any():
        ys, xs = np.nonzero(bad)
        print(f"  {bad.sum()} texels beyond 1 ULP; bounding box x {xs.min()}-{xs.max()}, y {ys.min()}-{ys.max()}")
    for d in rr + game + by_fs(dumps, "d25b8086de1734d5"):
        if d.get("nonfinite"):
            print(f"  NON-FINITE: {d['png']} {d['nonfinite']}")
    print(f"  non-finite in all {len(dumps)} dumps: {sum(d.get('nonfinite', 0) for d in dumps.values())}")
    return rel


def pq_eotf(e):
    m1, m2, c1, c2, c3 = 0.1593017578125, 78.84375, 0.8359375, 18.8515625, 18.6875
    p = np.power(np.clip(e, 0, 1), 1 / m2)
    return 10000.0 * np.power(np.maximum(p - c1, 0) / (c2 - c3 * p), 1 / m1)


def ab(post_path, old_path, out=None):
    cp, dp, fp = load_capture(post_path)
    co, do, fo = load_capture(old_path)
    tp = by_fs(dp, "d25b8086de1734d5")
    to = by_fs(do, "d25b8086de1734d5")
    if not tp or not to:
        print("tonemap output missing")
        return
    a = raw(cp, tp[0])[..., :3]
    b = raw(co, to[0])[..., :3]
    la, lb = lum(pq_eotf(a)), lum(pq_eotf(b))
    d = np.abs(a - b) * 1023.0
    dl = np.abs(la - lb)
    print(f"A/B tonemapped HDR10: {cp.name} (rr_post=1) vs {co.name} (rr_post=0)")
    print(f"  mean luminance {la.mean():.3f} vs {lb.mean():.3f} nits ({(la.mean() / lb.mean() - 1) * 100:+.3f}%)")
    print(f"  per-channel means (PQ) {a.reshape(-1, 3).mean(0)} vs {b.reshape(-1, 3).mean(0)}")
    print(f"  |delta| in 10-bit PQ codes: mean {d.mean():.3f}, p50 {np.percentile(d, 50):.2f}, "
          f"p99 {np.percentile(d, 99):.2f}, p99.9 {np.percentile(d, 99.9):.2f}, max {d.max():.1f}")
    print(f"  pixels differing by > 4 codes: {(d.max(-1) > 4).mean() * 100:.2f}%; > 16 codes: "
          f"{(d.max(-1) > 16).mean() * 100:.2f}%")
    # Signed luminance change by region: where is the difference?
    h, w = la.shape
    for name, sl in (("left third", np.s_[:, : w // 3]), ("centre", np.s_[:, w // 3: 2 * w // 3]),
                     ("right third", np.s_[:, 2 * w // 3:])):
        print(f"  {name}: mean luminance {la[sl].mean():.3f} vs {lb[sl].mean():.3f} nits")
    # High-frequency energy (noise proxy): mean |laplacian| of PQ luminance.
    def hf(x):
        y = x.mean(-1)
        return np.abs(4 * y[1:-1, 1:-1] - y[:-2, 1:-1] - y[2:, 1:-1] - y[1:-1, :-2] - y[1:-1, 2:]).mean() * 1023
    print(f"  high-frequency energy (mean |laplacian|, PQ codes): {hf(a):.3f} vs {hf(b):.3f}")
    if out:
        from PIL import Image
        out = Path(out)
        out.parent.mkdir(parents=True, exist_ok=True)
        # Display: PQ codes mapped to an SDR preview (PQ 0..0.75 -> 0..1, gamma as is).
        def prev(x):
            return Image.fromarray((np.clip(x / 0.75, 0, 1) * 255 + 0.5).astype(np.uint8))
        prev(a).resize((w // 2, h // 2), Image.LANCZOS).save(str(out) + "_post.png")
        prev(b).resize((w // 2, h // 2), Image.LANCZOS).save(str(out) + "_old.png")
        diff = np.clip(np.abs(a - b) * 4 / 0.75, 0, 1)
        Image.fromarray((diff * 255 + 0.5).astype(np.uint8)).resize((w // 2, h // 2), Image.LANCZOS).save(
            str(out) + "_diff_x4.png")
        crops = [(w // 2 - 320, h // 2 - 200), (w // 6 - 320, h // 3 - 200), (5 * w // 6 - 320, 2 * h // 3 - 200)]
        tiles = []
        for x0, y0 in crops:
            x0, y0 = max(0, x0), max(0, y0)
            tiles.append(np.concatenate([a[y0:y0 + 400, x0:x0 + 640], b[y0:y0 + 400, x0:x0 + 640]], 0))
        prev(np.concatenate(tiles, 1)).save(str(out) + "_crops.png")
        print(f"  images: {out}_post.png, _old.png, _diff_x4.png, _crops.png (top rr_post=1, bottom old)")


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    out = None
    if "--out" in sys.argv:
        out = sys.argv[sys.argv.index("--out") + 1]
        args = [a for a in args if a != out]
    replication(args[0])
    if len(args) > 1:
        ab(args[0], args[1], out)
