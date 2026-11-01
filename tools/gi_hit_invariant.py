"""gi_hit_direct energy invariant on a replay pack (offline; docs/TRACK_C_GI.md "S6 darkening").

    py -3 -m tools.gi_hit_invariant --pack <replay pack dir> [--out <dir>]     # runs both variants
    py -3 -m tools.gi_hit_invariant --base <dir> --hit <dir> --size W H        # compares two replay outputs

The hit variant (gi/gi_trace_rt_hit_direct.hlsl) traces the same rays as the base pass
(gi/gi_trace_rt.hlsl: the ray directions use only pixel_seed; the hit code has its own seeds), and it
only replaces fallback0's zero bounce at qualified hidden hits; the occlusion is the same expression
on both paths. So on one frame:
  - giRaw.a (ambient visibility) is bit-equal: same t, same weight, same sum;
  - giRaw.rgb of the hit variant is >= the base value per pixel, up to the RGBA16F rounding of the
    two stored values (half an ulp each, 2^-11 relative, plus the smallest half subnormal);
  - the frame mean luminance of the hit variant is >= the base mean (the energy rule).
Pixels lower beyond the rounding are reported with the luminance they lose next to the luminance the
hit variant adds: they come from rays the two compiled shaders classify differently at the screen
hit's depth test (float contraction of the same expression), in both directions (S8 pack 2026-11-01:
6 of 2.73 M pixels lower; the hit variant classifies 0.0006% more rays as screen hits).
Gates: alpha bit-equal on every pixel, and mean(hit) >= mean(base). Exit 1 on a failure.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[1]
KY = np.array([0.2126, 0.7152, 0.0722])
HALF_REL = 2.0 ** -11      # half an ulp of a normal half, relative
HALF_MIN = 2.0 ** -25      # half of the smallest half subnormal (2^-24)


def load_giraw(out_dir: Path, w: int, h: int) -> tuple[np.ndarray, np.ndarray]:
    raw = np.fromfile(out_dir / "giRaw.raw", dtype=np.uint16)
    if raw.size != w * h * 4:
        raise SystemExit(f"{out_dir / 'giRaw.raw'}: {raw.size} halves, expected {w} x {h} x 4")
    bits = raw.reshape(h, w, 4)
    return bits, bits.view(np.float16).astype(np.float64)


def compare(base_bits: np.ndarray, base: np.ndarray, hit_bits: np.ndarray, hit: np.ndarray) -> dict:
    """The invariant's numbers for two RGBA16F giRaw images (bits as uint16, values as float)."""
    n = base.shape[0] * base.shape[1]
    alpha_equal = int(np.count_nonzero(base_bits[..., 3] == hit_bits[..., 3]))
    d = hit[..., :3] - base[..., :3]
    tol = (np.abs(base[..., :3]) + np.abs(hit[..., :3])) * HALF_REL + HALF_MIN
    lower = (d < -tol).any(axis=2)
    lb, lh = base[..., :3] @ KY, hit[..., :3] @ KY
    dl = lh - lb
    return {
        "pixels": n,
        "alpha_equal": alpha_equal,
        "rgb_equal": int(np.count_nonzero((base_bits[..., :3] == hit_bits[..., :3]).all(axis=2))),
        "lower": int(np.count_nonzero(lower)),
        "lost": float(-dl[lower & (dl < 0)].sum() / n),
        "added": float(dl[dl > 0].sum() / n),
        "mean_base": float(lb.mean()),
        "mean_hit": float(lh.mean()),
        "nonfinite": int(np.count_nonzero(~np.isfinite(base)) + np.count_nonzero(~np.isfinite(hit))),
    }


def verdict(r: dict) -> list[str]:
    fails = []
    if r["nonfinite"]:
        fails.append(f"{r['nonfinite']} non-finite giRaw components")
    if r["alpha_equal"] != r["pixels"]:
        fails.append(f"giRaw.a differs on {r['pixels'] - r['alpha_equal']} pixels (the occlusion must be bit-equal)")
    if not r["mean_hit"] >= r["mean_base"]:
        fails.append(f"mean luminance hit {r['mean_hit']:.8g} < base {r['mean_base']:.8g} (energy rule)")
    return fails


def report(r: dict) -> int:
    n = r["pixels"]
    print(f"giRaw {n} px: alpha bit-equal {r['alpha_equal'] / n:.6%}; rgb bit-equal {r['rgb_equal'] / n:.4%}")
    rel = r["mean_hit"] / r["mean_base"] - 1 if r["mean_base"] > 0 else float("nan")
    print(f"mean luminance base {r['mean_base']:.8g}, hit {r['mean_hit']:.8g} ({rel:+.4%})")
    print(f"pixels lower beyond the half rounding: {r['lower']} ({r['lower'] / n:.2e}); luminance they lose "
          f"{r['lost']:.3e} per px vs added {r['added']:.3e} per px")
    fails = verdict(r)
    print("GATE: " + ("PASS" if not fails else "FAIL: " + "; ".join(fails)))
    return 1 if fails else 0


def pack_size(pack: Path) -> tuple[int, int]:
    for line in (pack / "manifest.txt").read_text(encoding="utf-8", errors="replace").splitlines():
        m = re.match(r"output 0 8 \d+ (\d+) (\d+) ", line)
        if m:
            return int(m.group(1)), int(m.group(2))
    raise SystemExit(f"{pack}: no 'output 0 8' (giRaw) line in manifest.txt")


def run_replay(exe: Path, pack: Path, hlsl: Path, out: Path) -> None:
    cmd = [str(exe), "--pack", str(pack), "--out", str(out), "--hlsl", str(hlsl)]
    p = subprocess.run(cmd, capture_output=True, text=True)
    (out.parent / f"{out.name}.txt").write_text(p.stdout + p.stderr, encoding="utf-8")
    for line in p.stdout.splitlines():
        if "GI hit direct (" in line or "output giRaw " in line or "vs live" in line:
            print(f"  [{out.name}] {line.strip()}")
    if p.returncode != 0:
        raise SystemExit(f"frame_replay failed ({p.returncode}) for {hlsl.name}; see {out.parent / (out.name + '.txt')}")


def main() -> int:
    a = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    a.add_argument("--pack", type=Path)
    a.add_argument("--out", type=Path)
    a.add_argument("--base", type=Path)
    a.add_argument("--hit", type=Path)
    a.add_argument("--size", type=int, nargs=2, metavar=("W", "H"))
    a.add_argument("--exe", type=Path, default=REPO / "bin" / "frame_replay.exe")
    args = a.parse_args()
    if args.pack:
        w, h = pack_size(args.pack)
        out = args.out or args.pack / "gi_hit_invariant"
        out.mkdir(parents=True, exist_ok=True)
        base_dir, hit_dir = out / "base", out / "hit"
        run_replay(args.exe, args.pack, REPO / "shaders" / "gi" / "gi_trace_rt.hlsl", base_dir)
        run_replay(args.exe, args.pack, REPO / "shaders" / "gi" / "gi_trace_rt_hit_direct.hlsl", hit_dir)
    elif args.base and args.hit and args.size:
        (w, h), base_dir, hit_dir = args.size, args.base, args.hit
    else:
        a.error("--pack, or --base, --hit and --size")
    bb, b = load_giraw(base_dir, w, h)
    hb, hv = load_giraw(hit_dir, w, h)
    return report(compare(bb, b, hb, hv))


if __name__ == "__main__":
    sys.exit(main())
