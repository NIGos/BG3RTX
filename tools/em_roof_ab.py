"""gi_far_roof_emission (specialization 1054) against the PT reference on one gi_hit_sun_em pack (tools/frame_replay.md
"Emission packs"). Offline only; reads frame_replay outputs, runs nothing.

    py -3 tools/em_roof_ab.py --off <replay 1054=0> --on <replay 1054=1> --none <replay of gi_trace_rt_hit_sun.hlsl>
                              --pt <pt-reference dir with pt_emission.pfm> [--out <file>]

Inputs (same pack, same push): giRaw.raw / giRawDepth.raw of the three replays (RGBA16F / R32F, top-down) and
pt_emission.pfm (PT mode 8: first-vertex emission x the GI ray fade at hidden first hits, game units, bottom-up).
The emission term of a replay is its giRaw.rgb minus the no-emission variant's (the emission is an addition,
gi_trace_rt.hlsl: the occlusion and every other term are unchanged). The question (predicted.json "offline"):
  1. 1054=1 only lowers: no pixel above 1054=0 in any channel, giRaw.a bit-equal;
  2. where it changes giRaw (the changed pixels), is its emission term closer to the PT's than 1054=0's:
     mean |E_on - P| < mean |E_off - P| over those pixels (luminance, Rec. 709), and per 16 x 9 tile.
No pixel changed: the camera sees no hidden emitter beyond R0 behind a roof volume; say so (no evidence either way).
"""
import argparse
import sys
from pathlib import Path

import numpy as np

LUM = np.array([0.2126, 0.7152, 0.0722])


def read_pfm(path):
    data = Path(path).read_bytes()
    parts, pos = [], 0
    while len(parts) < 4:   # "PF", width, height, scale (whitespace separated)
        while data[pos:pos + 1].isspace():
            pos += 1
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        parts.append(data[pos:end].decode())
        pos = end
    pos += 1
    if parts[0] != "PF":
        raise ValueError(f"{path}: not an RGB PFM")
    w, h, scale = int(parts[1]), int(parts[2]), float(parts[3])
    a = np.frombuffer(data[pos:pos + w * h * 12], dtype="<f4" if scale < 0 else ">f4").reshape(h, w, 3)
    return a[::-1].astype(np.float64)   # PFM rows are bottom-up


def read_replay(d, w, h):
    d = Path(d)
    rgba = np.fromfile(d / "giRaw.raw", dtype="<f2").reshape(h, w, 4).astype(np.float64)
    depth = np.fromfile(d / "giRawDepth.raw", dtype="<f4").reshape(h, w)
    return rgba, depth


def compare(off, on, none, pt, depth, tiles=(16, 9)):
    """off/on/none: (h, w, 4) giRaw; pt: (h, w, 3) PT mode 8; depth: (h, w) live-shaded mask source. Returns a dict."""
    shaded = (depth > 0) & np.all(np.isfinite(off), axis=2) & np.all(np.isfinite(on), axis=2) & np.all(np.isfinite(none), axis=2)
    r = {"shaded": int(shaded.sum())}
    up = (on[..., :3] > off[..., :3]) & shaded[..., None]
    r["raised_pixels"] = int(np.any(up, axis=2).sum())
    r["alpha_changed"] = int(((on[..., 3] != off[..., 3]) & shaded).sum())
    changed = np.any(on[..., :3] != off[..., :3], axis=2) & shaded
    r["changed"] = int(changed.sum())
    lum = lambda x: x[..., :3] @ LUM
    e_off, e_on, p = lum(off - none), lum(on - none), pt @ LUM
    r["mean_off"] = float(lum(off)[shaded].mean()) if r["shaded"] else 0.0
    r["mean_on"] = float(lum(on)[shaded].mean()) if r["shaded"] else 0.0
    if not r["changed"]:
        return r
    c = changed
    r["e_off"], r["e_on"], r["pt"] = float(e_off[c].mean()), float(e_on[c].mean()), float(p[c].mean())
    r["mae_off"], r["mae_on"] = float(np.abs(e_off - p)[c].mean()), float(np.abs(e_on - p)[c].mean())
    r["dropped"] = r["e_off"] - r["e_on"]
    h, w = depth.shape
    tx, ty = tiles
    ys, xs = np.nonzero(c)
    tid = (ys * ty // h) * tx + xs * tx // w
    closer = farther = 0
    for t in np.unique(tid):
        m = tid == t
        a_off = np.abs(e_off[ys[m], xs[m]] - p[ys[m], xs[m]]).mean()
        a_on = np.abs(e_on[ys[m], xs[m]] - p[ys[m], xs[m]]).mean()
        closer += a_on < a_off
        farther += a_on > a_off
    r["tiles"], r["tiles_closer"], r["tiles_farther"] = int(len(np.unique(tid))), int(closer), int(farther)
    return r


def report(r):
    lines = [f"em_roof_ab: {r['shaded']} shaded GI pixels; 1054=1 changes {r['changed']}; raised above 1054=0: {r['raised_pixels']} "
             f"(must be 0); giRaw.a changed: {r['alpha_changed']} (must be 0)",
             f"energy (mean luminance of giRaw.rgb, shaded pixels): 1054=0 {r['mean_off']:.6g}, 1054=1 {r['mean_on']:.6g} "
             f"({100 * (r['mean_on'] / r['mean_off'] - 1) if r['mean_off'] else 0:+.3f}%)"]
    if not r["changed"]:
        lines.append("verdict: NO CHANGED PIXEL - this camera sees no hidden emitter beyond R0 behind a roof volume (no evidence for a default)")
        return lines
    lines.append(f"changed pixels: GI emission term 1054=0 {r['e_off']:.6g}, 1054=1 {r['e_on']:.6g} (dropped {r['dropped']:.6g}); "
                 f"PT hidden first-hit emission x fade {r['pt']:.6g}")
    lines.append(f"mean |E - PT| on changed pixels: 1054=0 {r['mae_off']:.6g}, 1054=1 {r['mae_on']:.6g}; tiles ({r['tiles']} with changed "
                 f"pixels): 1054=1 closer in {r['tiles_closer']}, farther in {r['tiles_farther']}")
    ok = r["raised_pixels"] == 0 and r["alpha_changed"] == 0
    closer = r["mae_on"] < r["mae_off"]
    lines.append("verdict: " + ("INVALID (1054=1 raised pixels or changed the occlusion)" if not ok else
                                "CLOSER to the PT where it acts" if closer else "FARTHER from the PT where it acts (the hypothesis is wrong)"))
    return lines


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--off", required=True)
    ap.add_argument("--on", required=True)
    ap.add_argument("--none", required=True)
    ap.add_argument("--pt", required=True)
    ap.add_argument("--out")
    a = ap.parse_args(argv)
    pt = read_pfm(Path(a.pt) / "pt_emission.pfm")
    h, w = pt.shape[:2]
    off, depth = read_replay(a.off, w, h)
    on, _ = read_replay(a.on, w, h)
    none, _ = read_replay(a.none, w, h)
    lines = report(compare(off, on, none, pt, depth))
    for line in lines:
        print(line)
    if a.out:
        Path(a.out).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
