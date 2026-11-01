"""Per-scene table of the native ambient vs the offline PT reference (frame_replay --native-ambient).

    py -3 tools/ambient_table.py [--scene NAME] <pack or report dir> [<pack or report dir> ...]

Each argument is a replay pack (its `replay/` or `native-ambient/` folder holds the report), the report
folder itself, or a native_ambient_report.txt. The scene is --scene (one argument), else the pack's
scene.txt (written by tools/live_batches/replay_capture/post_replay.ps1 -Scene), else the folder name.

Columns (tools/frame_replay.md "Native ambient"; luminance, game units):
  E x AO      the game's diffuse ambient at the GI pixels times its AO (E_native x AO)
  k PT far    the PT's far field (total minus the live domain) calibrated by k (game / PT at visible hits)
  env         the sky the PT's misses see (PT_ENV)
  ratio       E x AO / (k x PT far + env): above 1 the native term is brighter than the reference
  tiles       the same ratio per 16 x 9 tile: p10 / median / p90
  far field   the traced bounce's share beyond gi_rt_radius: on screen (visible + hidden) + off screen
  sun / local the traced bounce by light (needs the "PT split" line: frame_replay of 2026-11-01 or later)
  roofs       PT_CASTERS (the pack's casters), PT_ROOFS (asset-DB volumes) or none
"""
from __future__ import annotations

import argparse
import os
import re
import sys

NUM = r"([-+0-9.eE]+|nan|inf)"


def find_report(path: str) -> str | None:
    if os.path.isfile(path):
        return path
    for sub in ("", "replay", "native-ambient"):
        p = os.path.join(path, sub, "native_ambient_report.txt")
        if os.path.isfile(p):
            return p
    return None


def scene_of(path: str, report: str) -> str:
    d = os.path.dirname(report)
    for c in (path, d, os.path.dirname(d)):
        s = os.path.join(c, "scene.txt")
        if os.path.isfile(s):
            t = open(s, encoding="utf-8").read().strip()
            if t:
                return t
    pack = os.path.dirname(d) if os.path.basename(d) in ("replay", "native-ambient") else d
    return os.path.basename(os.path.normpath(pack))


def parse(text: str) -> dict:
    r: dict = {}

    def grab(key, pattern, n=1):
        m = re.search(pattern, text)
        if m:
            r[key] = tuple(float(m.group(i + 1)) for i in range(n)) if n > 1 else float(m.group(1))

    grab("eao", r"x game AO " + NUM + r" \(AO")
    grab("ao", r"\(AO " + NUM + r"\)")
    grab("k", r"; k = " + NUM + ";")
    grab("kfar", r"k x PT far " + NUM + ";")
    grab("env", r"PT environment through misses " + NUM)
    grab("ratio", r"E_native x AO / \(k x PT far \+ PT env\) " + NUM + ",")
    grab("tiles", r"per tile \(16 x 9, \d+ with pixels\): E_native x AO / \(k x PT far \+ PT env\) p10 " + NUM + " median " + NUM + " p90 " + NUM, 3)
    grab("split", r"far field on screen beyond gi_rt_radius " + NUM + r"% \(visible " + NUM + ", hidden " + NUM + r"\), off screen " + NUM +
         r"%, live domain " + NUM + r"%; sun " + NUM + r"%, local point/spot " + NUM + "%", 7)
    m = re.search(r"^native ambient: (.*?); \d+x\d+ GI pixels", text, re.M)
    params = m.group(1) if m else ""
    r["roofs"] = "casters" if "PT_CASTERS=1" in params else "volumes" if "PT_ROOFS=1" in params else "none"
    return r


def row(scene: str, r: dict) -> str:
    f = lambda k, fmt="{:.3g}": fmt.format(r[k]) if k in r else "n/a"
    tiles = "{:.2f} / {:.2f} / {:.2f}".format(*r["tiles"]) if "tiles" in r else "n/a"
    if "split" in r:
        s = r["split"]
        far = "{:.1f}% + {:.1f}% off ({:.1f}%)".format(s[0], s[3], s[0] + s[3])
        light = "{:.0f} / {:.0f}".format(s[5], s[6])
    else:
        far = light = "n/a"
    return (f"| {scene} | {f('eao')} | {f('kfar')} | {f('env')} | {f('ratio', '{:.2f}')} | {tiles} | {far} | {light} | "
            f"{f('k', '{:.2f}')} | {r.get('roofs', 'n/a')} |")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--scene", default="")
    ap.add_argument("paths", nargs="+")
    a = ap.parse_args(argv)
    if a.scene and len(a.paths) != 1:
        ap.error("--scene names one argument")
    print("| scene | E x AO | k x PT far | env | E x AO / (k PT far + env) | tiles p10 / med / p90 | far field (on screen + off) | sun / local % | k | roofs |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    missing = 0
    for p in a.paths:
        rep = find_report(p)
        if not rep:
            print(f"| {a.scene or os.path.basename(os.path.normpath(p))} | no native_ambient_report.txt | | | | | | | | |")
            missing += 1
            continue
        print(row(a.scene or scene_of(p, rep), parse(open(rep, encoding="utf-8", errors="replace").read())))
    return 1 if missing == len(a.paths) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
