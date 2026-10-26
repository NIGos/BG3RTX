"""A/B stills from two auto-run captures (docs/QUALITY.md R6).

    py -3 scripts/ab_compare.py <capture_off> <capture_on> <out_prefix> [--crop x,y,w,h ...]
                                [--pass-fs <hash prefix> --exposure k]

Picks the frame each capture presented (the swapchain image written by the
final pass), or with --pass-fs the last dump written by that pass (e.g.
5c3fdabc, BG3's compose: lit HDR before transparents, post and exposure; the
dump's fixed v/(1+v) preview is inverted and re-tonemapped at `exposure`), writes <out_prefix>_off.png, _on.png, _diff.png (|on - off| x4)
and, for each --crop (in output pixels), _crop<i>.png with off | on | diff side
by side at 1:1. Prints the mean luminance of both and the change, and the
non-finite counts of every dump in both captures.
"""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def final_frame(cap: Path, pass_fs=None, exposure=1.0):
    dumps = json.loads((cap / "dumps.json").read_text())
    nonfinite = sum(int(d.get("nonfinite", 0) or 0) for d in dumps.values())
    if pass_fs:
        cand = [(int(k[1:]), k, d) for k, d in dumps.items()
                if str(d.get("pass_fs", "")).startswith(pass_fs) and d.get("slot") == 0]
        if not cand:
            raise SystemExit(f"{cap}: no dump of pass {pass_fs}")
        _, k, d = max(cand)
        p = np.asarray(Image.open(cap / d["png"]).convert("RGB")).astype(np.float32) / 255.0
        t = np.clip(p ** 2.2, 0, 0.999)
        lin = t / (1.0 - t) * exposure
        out = (lin / (1.0 + lin)) ** (1 / 2.2)
        return Image.fromarray((out * 255 + 0.5).astype(np.uint8)), k, nonfinite
    report = (cap / "report.md").read_text(errors="replace")
    swap = None
    for line in report.splitlines():   # the image of the swapchain row: "| iNNN WxH ... swapchain |"
        if line.startswith("| i") and "swapchain" in line:
            swap = line.split()[1]
            break
    best = None
    for k, d in dumps.items():
        if swap and d.get("image") != swap:
            continue
        if best is None or int(k[1:]) > int(best[0][1:]):
            best = (k, d)
    if best is None:
        raise SystemExit(f"{cap}: no swapchain dump")
    return Image.open(cap / best[1]["png"]).convert("RGB"), best[0], nonfinite


def lum(a):
    return (a[..., 0] * 0.2126 + a[..., 1] * 0.7152 + a[..., 2] * 0.0722).mean()


def main():
    args = sys.argv[1:]
    crops = []
    while "--crop" in args:
        i = args.index("--crop")
        crops.append(tuple(int(v) for v in args[i + 1].split(",")))
        del args[i:i + 2]
    opt = {}
    for name in ("--pass-fs", "--exposure"):
        if name in args:
            i = args.index(name)
            opt[name] = args[i + 1]
            del args[i:i + 2]
    pfs, ex = opt.get("--pass-fs"), float(opt.get("--exposure", 1.0))
    off_dir, on_dir, out = Path(args[0]), Path(args[1]), args[2]
    off, koff, nf_off = final_frame(off_dir, pfs, ex)
    on, kon, nf_on = final_frame(on_dir, pfs, ex)
    a = np.asarray(off).astype(np.float32) / 255.0
    b = np.asarray(on).astype(np.float32) / 255.0
    diff = np.clip(np.abs(b - a) * 4.0, 0, 1)
    if pfs:
        # Light removed (off - on, linear), 5x5 box filtered: the render-resolution
        # frames carry different sub-pixel jitter, which an unfiltered diff shows as edges.
        def lin(v):
            t = np.clip(v ** 2.2, 0, 0.999)
            return t / (1.0 - t)
        r = np.maximum(lin(a) - lin(b), 0.0)
        k = 5
        pad = np.pad(r, ((k // 2, k // 2), (k // 2, k // 2), (0, 0)), mode="edge")
        acc = np.zeros_like(r)
        for dy in range(k):
            for dx in range(k):
                acc += pad[dy:dy + r.shape[0], dx:dx + r.shape[1]]
        r = acc / (k * k) * 2.0
        diff = np.clip((r / (1.0 + r)) ** (1 / 2.2), 0, 1)
    off.save(out + "_off.png")
    on.save(out + "_on.png")
    Image.fromarray((diff * 255 + 0.5).astype(np.uint8)).save(out + "_diff.png")
    for n, (x, y, w, h) in enumerate(crops):
        tiles = [Image.fromarray((v[y:y + h, x:x + w] * 255 + 0.5).astype(np.uint8)) for v in (a, b, diff)]
        strip = Image.new("RGB", (w * 3 + 8, h), (255, 255, 255))
        for i, t in enumerate(tiles):
            strip.paste(t, (i * (w + 4), 0))
        strip.save(f"{out}_crop{n}.png")
    la, lb = lum(a), lum(b)
    changed = (np.abs(b - a).max(axis=2) > 2.0 / 255).mean()
    print(f"off {off_dir.name} {koff} {off.size}, on {on_dir.name} {kon}")
    print(f"mean luminance (display-referred) off {la:.4f} on {lb:.4f} change {100 * (lb - la) / max(la, 1e-6):+.2f}%;"
          f" pixels changed > 2/255: {100 * changed:.2f}%")
    print(f"non-finite values over all dumps: off {nf_off}, on {nf_on}")


if __name__ == "__main__":
    main()
