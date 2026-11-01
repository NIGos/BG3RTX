"""Light conventions of the asset database: python -m tools.assets.check_conventions

Reads `bg3rtx_assetdb.bin` (default: this checkout's data/assetdb, else the main checkout's) and,
with `--json-dir`, the readable dump of `build --json` for exact template names (the binary keeps
placement names only; the template is then the name without its `_NNN` suffix).

Reports, over all levels (docs/LIGHT_CONVENTIONS.md):
  1. per level / template: radius, intensity, Kelvin / colour, falloff and pre-expose flags; outliers;
  2. box lights: extents and attenuation against the ramps of shaders/rt/bg3_boxes.hlsli;
  3. the share of lights inside roofed rooms (the sun roof rule's volumes, layer/assetdb.cpp);
  4. `--at X Y Z --level NAME`: the lights around one camera, with the game's evaluation
     (f469db78 / bg3_lights.hlsli) of their direct term per light channel.
`--csv PATH` writes one row per level.
"""
from __future__ import annotations

import argparse
import csv
import math
import os
import re
import statistics as st
import sys
from collections import Counter, defaultdict

from . import dbformat

# lights.py flag bits
F = {n: 1 << i for i, n in enumerate(["enabled", "use_temperature", "shadow", "volumetric_shadow", "flicker", "moving",
                                       "sunlight", "pre_expose", "flat_falloff", "cast_shadow", "cookie", "combined",
                                       "not_starting_loaded", "half_lit"])}
TYPES = ["point", "spot", "box"]
# triggers.py
KINDS = ["room", "portal", "floor", "lighting", "atmosphere"]
T_ROOF, T_ROOM_HAS_ROOF, T_COVERED = 1, 16, 32
B13_S6 = 0.00136865   # b13[0] in the Adamantine Forge save (live log, steady exposure)


def default_db() -> str:
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(os.path.dirname(here))
    for p in (os.path.join(root, "data", "assetdb", "bg3rtx_assetdb.bin"),
              os.path.join(root, "..", "..", "..", "data", "assetdb", "bg3rtx_assetdb.bin")):
        if os.path.exists(p):
            return os.path.normpath(p)
    return os.path.join("data", "assetdb", "bg3rtx_assetdb.bin")


def stem(name: str) -> str:
    return re.sub(r"_\d+$", "", name or "")


def load(db_path: str, json_dir: str | None):
    with open(db_path, "rb") as f:
        r = dbformat.Reader(f.read())
    lv = r.rows("LVLS")
    levels = [r.string(x[0]) for x in lv]
    level_of = {}
    for i, x in enumerate(lv):
        for k in range(x[6], x[6] + x[7]):
            level_of[("l", k)] = i
        for k in range(x[8], x[8] + x[9]):
            level_of[("t", k)] = i
        for k in range(x[10], x[10] + x[11]):
            level_of[("p", k)] = i
    lights = []
    for k, x in enumerate(r.rows("LGHT")):
        lights.append(dict(level=level_of.get(("l", k), -1), pos=x[0:3], rot=x[4:8], color=x[8:11], intensity=x[11],
                           kelvin=x[12], gain=x[13], spot_inner=x[15], spot_outer=x[16], box=x[17:20],
                           att_start=x[20], att_end=x[21], att_side=x[22], flags=x[27], type=x[28], att_fn=x[29],
                           channel=x[30], bounds=x[3], name=r.string(x[33])))
    pts = r.rows("TPTS")
    trigs = []
    for k, x in enumerate(r.rows("TRIG")):
        trigs.append(dict(level=level_of.get(("t", k), -1), pos=x[0:3], scale=x[3], rot=x[4:8], extents=x[8:11],
                          height=x[11], radius=x[12], flags=x[14], kind=KINDS[x[16]] if x[16] < len(KINDS) else "?",
                          shape=x[17], points=[pts[i] for i in range(x[24], x[24] + x[25])]))
    probes = [dict(level=level_of.get(("p", k), -1), pos=x[0:3]) for k, x in enumerate(r.rows("PRBE"))]
    efx = r.rows("EFXL")
    if json_dir:
        import json
        jl = json.load(open(os.path.join(json_dir, "lights.json"), encoding="utf-8"))
        if len(jl) != len(lights):
            raise SystemExit(f"--json-dir has {len(jl)} lights, the database {len(lights)}: not the same build")
        for a, b in zip(lights, jl):
            a["template"], a["radius"] = b["template"], b["radius"]
    for a in lights:
        a.setdefault("template", stem(a["name"]))
        # the binary keeps the bounds radius; a point/spot's is its Radius
        a.setdefault("radius", a["bounds"] if a["type"] != 2 else float("nan"))
    return levels, lights, trigs, probes, efx


# ---- geometry (layer/assetdb.cpp inside_trigger, qrot_inv) ----
def qrot_inv(q, v):
    x, y, z, w = -q[0], -q[1], -q[2], q[3]
    tx, ty, tz = 2 * (y * v[2] - z * v[1]), 2 * (z * v[0] - x * v[2]), 2 * (x * v[1] - y * v[0])
    return (v[0] + w * tx + (y * tz - z * ty), v[1] + w * ty + (z * tx - x * tz), v[2] + w * tz + (x * ty - y * tx))


def inside_trigger(t, p) -> bool:
    rel = (p[0] - t["pos"][0], p[1] - t["pos"][1], p[2] - t["pos"][2])
    s = t["scale"] if t["scale"] > 1e-6 else 1.0
    l = [c / s for c in qrot_inv(t["rot"], rel)]
    if t["shape"] == 2:
        return all(abs(l[i]) <= 0.5 * t["extents"][i] for i in range(3))
    if t["shape"] == 1:
        return l[0] ** 2 + l[1] ** 2 + l[2] ** 2 <= t["radius"] ** 2
    P = t["points"]
    if len(P) < 3 or l[1] < 0 or l[1] > t["height"]:
        return False
    inside, j = False, len(P) - 1
    for i in range(len(P)):
        (xi, zi), (xj, zj) = P[i], P[j]
        if (zi > l[2]) != (zj > l[2]) and l[0] < (xj - xi) * (l[2] - zi) / (zj - zi) + xi:
            inside = not inside
        j = i
    return inside


def roofed(t) -> bool:
    return (t["kind"] == "room" and t["flags"] & (T_ROOM_HAS_ROOF | T_ROOF)) or \
           (t["kind"] == "floor" and t["flags"] & (T_ROOF | T_COVERED))


def trig_reach(t) -> float:
    s = t["scale"] if t["scale"] > 1e-6 else 1.0
    if t["shape"] == 2:
        return 0.5 * s * math.sqrt(sum(e * e for e in t["extents"]))
    if t["shape"] == 1:
        return s * t["radius"]
    r = max((math.hypot(x, z) for x, z in t["points"]), default=0.0)
    return s * math.hypot(r, t["height"])


# ---- box lights (bg3_boxes.hlsli) ----
def box_ramps(l):
    """The authored attenuation as the box-space ramps of bg3_box_falloff, under the mapping (not
    yet checked against a b29 record) start/end in metres along z (ramp.x = start/D, ramp.y =
    1 - end/D, slopes D/start, D/end) and side in metres from each side face (edge = W/side, H/side).
    Returns (peak of x*y*z over the box, z falloff discontinuous, side ramps never reach 1).
    The shader takes the start ramp for z < ramp.x, else the end ramp for z > ramp.y: when
    start + end > D the two ramps overlap and the falloff jumps at z = ramp.x."""
    w, h, d = (max(v, 1e-6) for v in l["box"])
    zs = l["att_start"] / d if l["att_start"] > 0 else 0.0
    ze = l["att_end"] / d if l["att_end"] > 0 else 0.0
    jump = zs > 0 and ze > 0 and zs + ze > 1.0
    zpk = min(1.0, 1.0 / zs) if zs > 0 else (min(1.0, 1.0 / ze) if ze > 0 else 1.0)
    side_pk = [min(1.0, 0.5 * a / l["att_side"]) if l["att_side"] > 0 else 1.0 for a in (w, h)]
    curve = [lambda t: t, lambda t: t * t, lambda t: t * t * (3 - 2 * t), lambda t: t ** 3 * (t * (t * 6 - 15) + 10)]
    c = curve[l["att_fn"]] if l["att_fn"] < 4 else (lambda t: 0.0)
    return c(zpk) * c(side_pk[0]) * c(side_pk[1]), jump, any(p < 1.0 for p in side_pk)


# ---- the game's point/spot term (bg3_lights.hlsli bg3_light_eval, before N.L and the BRDF) ----
def point_incident(l, p, b13=B13_S6, m4=1.0, bias=0.0):
    """Luminance of colour x k at p, with m1 = Intensity x colour (ASSET_DB "Intensity": runtime
    max(m1) ~ Intensity x 1.00-1.19), m3 < 0 (x b13) for PreExpose lights, m5 < 0 (flat) for
    FlatFalloff lights. m4 (the falloff exponent) and |m5| are not in the files: 1 and 0 here."""
    r = l["radius"]
    d2 = sum((a - b) ** 2 for a, b in zip(l["pos"], p))
    if not r > 0 or d2 >= r * r:
        return 0.0
    x = d2 / (r * r)
    a = max(1 - x * x, 1e-4) ** m4
    a *= (4 / (r * r) + bias) if l["flags"] & F["flat_falloff"] else (1 / max(d2, 1e-4) + bias)
    lum = 0.2126 * l["color"][0] + 0.7152 * l["color"][1] + 0.0722 * l["color"][2]
    k = 0.07957747 if l["type"] == 0 else 0.31830987   # spots: cone factor <= 1 not applied
    return l["intensity"] * lum * a * k * (b13 if l["flags"] & F["pre_expose"] else 1.0)


def q(v, f):
    v = sorted(v)
    return v[min(len(v) - 1, int(f * len(v)))] if v else float("nan")


def share(lst, flag):
    return sum(1 for l in lst if l["flags"] & F[flag]) / max(len(lst), 1)


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python -m tools.assets.check_conventions", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--db", default=default_db())
    ap.add_argument("--json-dir", help="build --json output (exact template names)")
    ap.add_argument("--csv", help="write the per-level table here")
    ap.add_argument("--level", help="camera section: level name")
    ap.add_argument("--at", nargs=3, type=float, help="camera section: world position")
    ap.add_argument("--near", type=float, default=80.0, help="camera section: radius (m)")
    a = ap.parse_args(argv)
    levels, lights, trigs, probes, efx = load(a.db, a.json_dir)
    out = sys.stdout
    nt = Counter(TYPES[l["type"]] for l in lights)
    out.write(f"database {a.db}: {len(levels)} levels, {len(lights)} lights ({nt['point']} point, {nt['spot']} spot, "
              f"{nt['box']} box), {len(trigs)} triggers, {len(probes)} probes, {len(efx)} effect lights\n")

    # 1. flags and units
    ps = [l for l in lights if l["type"] != 2]
    bx = [l for l in lights if l["type"] == 2]
    out.write("\n## 1. Flags, units, distributions\n")
    for name, grp in (("point/spot", ps), ("box", bx)):
        out.write(f"{name}: pre_expose {100 * share(grp, 'pre_expose'):.1f}%, flat_falloff {100 * share(grp, 'flat_falloff'):.1f}%, "
                  f"use_temperature {100 * share(grp, 'use_temperature'):.1f}%, shadow {100 * share(grp, 'shadow'):.1f}%, "
                  f"cast_shadow {100 * share(grp, 'cast_shadow'):.1f}%, sunlight {100 * share(grp, 'sunlight'):.1f}%, "
                  f"cookie {100 * share(grp, 'cookie'):.1f}%, flicker {100 * share(grp, 'flicker'):.1f}%\n")
    for pe in (True, False):
        g = [l["intensity"] for l in ps if bool(l["flags"] & F["pre_expose"]) == pe and l["intensity"] > 0]
        out.write(f"point/spot intensity, pre_expose={int(pe)}: n {len(g)}, p5 {q(g, .05):.4g}, median {q(g, .5):.4g}, "
                  f"p95 {q(g, .95):.4g}\n")
    g = [l["intensity"] for l in bx if l["intensity"] > 0]
    out.write(f"box intensity: n {len(g)}, p5 {q(g, .05):.4g}, median {q(g, .5):.4g}, p95 {q(g, .95):.4g}\n")
    rr = [l["radius"] for l in ps]
    out.write(f"point/spot radius: p5 {q(rr, .05):.3g}, median {q(rr, .5):.3g}, p95 {q(rr, .95):.3g}, max {max(rr):.3g}; "
              f">= 10 m {100 * sum(1 for r in rr if r >= 10) / len(rr):.1f}%\n")
    kt = [l["kelvin"] for l in lights if l["flags"] & F["use_temperature"]]
    out.write(f"Kelvin (use_temperature): n {len(kt)}, p5 {q(kt, .05):.0f}, median {q(kt, .5):.0f}, p95 {q(kt, .95):.0f}; "
              f"Kelvin lights with a non-white Color {sum(1 for l in lights if l['flags'] & F['use_temperature'] and min(l['color']) < 0.999)}\n")
    gains = Counter(round(l["gain"], 2) for l in lights)
    out.write(f"Gain values (most common): {', '.join(f'{k:g} x{v}' for k, v in gains.most_common(6))}\n")
    # outliers
    by_t = defaultdict(list)
    for l in lights:
        by_t[(l["type"], l["template"])].append(l)
    out_i = out_r = 0
    for (_t, _n), g in by_t.items():
        mi = st.median([l["intensity"] for l in g])
        mr = st.median([l["radius"] for l in g]) if _t != 2 else None
        for l in g:
            if mi > 0 and l["intensity"] > 0 and not (0.1 <= l["intensity"] / mi <= 10):
                out_i += 1
            if mr and mr > 0 and not (0.1 <= l["radius"] / mr <= 10):
                out_r += 1
    zero_on = sum(1 for l in lights if l["flags"] & F["enabled"] and l["intensity"] <= 0)
    black = sum(1 for l in lights if l["flags"] & F["enabled"] and max(l["color"]) <= 0)
    spot_bad = sum(1 for l in lights if l["type"] == 1 and l["spot_inner"] > l["spot_outer"])
    kel_bad = sum(1 for l in kt if not 1000 <= l <= 40000)
    big_i = sorted(ps, key=lambda l: -l["intensity"])[:3]
    out.write(f"outliers: intensity > 10x off its template median {out_i}, radius > 10x off {out_r}; enabled with intensity 0 "
              f"{zero_on}, black colour {black}; spots inner > outer {spot_bad}; Kelvin outside 1000-40000 {kel_bad}; "
              "brightest point/spot: " + ", ".join("%s %.3g (r %.3g, %s)" % (l["template"], l["intensity"], l["radius"],
                                                                          levels[l["level"]]) for l in big_i) + "\n")
    out.write(f"templates: {len(by_t)}; largest groups: "
              + ", ".join(f"{n} {TYPES[t]} x{len(g)}" for (t, n), g in sorted(by_t.items(), key=lambda kv: -len(kv[1]))[:5]) + "\n")

    # 2. boxes
    out.write("\n## 2. Box lights\n")
    dims = [l["box"] for l in bx]
    pk = [box_ramps(l) for l in bx]
    out.write(f"extents (W x H x D, m): median {q([d[0] for d in dims], .5):.3g} x {q([d[1] for d in dims], .5):.3g} x "
              f"{q([d[2] for d in dims], .5):.3g}; p95 depth {q([d[2] for d in dims], .95):.3g}; zero-size {sum(1 for d in dims if min(d) <= 0)}\n")
    out.write(f"attenuation function: {dict(sorted(Counter(l['att_fn'] for l in bx).items()))} (curve index m4 >> 24 has 4 "
              f"entries: linear, square, smoothstep, smootherstep); start = 0 {sum(1 for l in bx if l['att_start'] == 0)}, "
              f"end = 0 {sum(1 for l in bx if l['att_end'] == 0)}, side = 0 {sum(1 for l in bx if l['att_side'] == 0)}\n")
    out.write(f"under the metre mapping: z falloff jumps (start + end > D) {sum(1 for p in pk if p[1])}, side ramps "
              f"never reach 1 (side > W/2 or H/2) {sum(1 for p in pk if p[2])}; peak falloff < 0.5 {sum(1 for p in pk if p[0] < .5)}, "
              f"< 0.9 {sum(1 for p in pk if p[0] < .9)} of {len(bx)}\n")
    out.write(f"box flags: moving {sum(1 for l in bx if l['flags'] & F['moving'])}, not starting loaded "
              f"{sum(1 for l in bx if l['flags'] & F['not_starting_loaded'])}, combined {sum(1 for l in bx if l['flags'] & F['combined'])}, "
              f"shadow {sum(1 for l in bx if l['flags'] & F['shadow'])}, cookie {sum(1 for l in bx if l['flags'] & F['cookie'])}\n")
    # same-anchor duplicates: boxes within 10 cm of each other (the join takes the nearest)
    grid = defaultdict(list)
    for i, l in enumerate(bx):
        grid[tuple(int(math.floor(c / 0.1)) for c in l["pos"])].append(i)
    dup = sum(len(v) for v in grid.values() if len(v) > 1)
    out.write(f"boxes sharing a 10 cm cell with another box: {dup}\n")

    # 3. roofed rooms
    out.write("\n## 3. Lights inside roofed rooms (room HasRoof/IsRoof, roof or covered floor)\n")
    t_by = defaultdict(list)
    for t in trigs:
        if roofed(t):
            t_by[t["level"]].append((t, trig_reach(t)))
    rows = []
    for li, name in enumerate(levels):
        L = [l for l in lights if l["level"] == li]
        P = [p for p in probes if p["level"] == li]
        R = t_by.get(li, [])

        def inside(p):
            return any(math.dist(p, t["pos"]) <= reach + 1e-3 and inside_trigger(t, p) for t, reach in R)
        lin = sum(1 for l in L if inside(l["pos"])) if R else 0
        pin = sum(1 for p in P if inside(p["pos"])) if R else 0
        lp = [l for l in L if l["type"] != 2]
        lb = [l for l in L if l["type"] == 2]
        rows.append(dict(level=name, lights=len(L), point_spot=len(lp), box=len(lb), roofed_volumes=len(R),
                         lights_in_roofed=lin, share_in_roofed=round(lin / len(L), 3) if L else 0.0,
                         probes=len(P), probes_in_roofed=pin,
                         radius_median=round(q([l["radius"] for l in lp], .5), 2) if lp else "",
                         intensity_median=round(q([l["intensity"] for l in lp], .5), 1) if lp else "",
                         intensity_p95=round(q([l["intensity"] for l in lp], .95), 1) if lp else "",
                         pre_expose=round(share(L, "pre_expose"), 3), flat_falloff=round(share(lp, "flat_falloff"), 3),
                         use_temperature=round(share(L, "use_temperature"), 3), shadow=round(share(L, "shadow"), 3),
                         box_peak_below_half=sum(1 for l in lb if box_ramps(l)[0] < .5)))
    tot = sum(r["lights"] for r in rows)
    tin = sum(r["lights_in_roofed"] for r in rows)
    with_r = [r for r in rows if r["roofed_volumes"]]
    out.write(f"all levels: {tin}/{tot} lights ({100 * tin / max(tot, 1):.1f}%) inside a roofed volume; levels with roofed "
              f"volumes {len(with_r)}/{len(rows)}\n")
    for r in sorted(rows, key=lambda r: -r["lights"])[:12]:
        out.write(f"  {r['level']}: {r['lights_in_roofed']}/{r['lights']} ({100 * r['share_in_roofed']:.0f}%), "
                  f"{r['roofed_volumes']} roofed volumes, probes {r['probes_in_roofed']}/{r['probes']}\n")
    if a.csv:
        with open(a.csv, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0]))
            w.writeheader()
            w.writerows(rows)
        out.write(f"wrote {a.csv}\n")

    # 4. one camera
    if a.at and a.level:
        li = levels.index(a.level)
        c = tuple(a.at)
        near = [l for l in lights if l["level"] == li and math.dist(l["pos"], c) < a.near]
        out.write(f"\n## 4. {a.level} around ({c[0]:.1f}, {c[1]:.1f}, {c[2]:.1f}), {a.near:.0f} m: {len(near)} lights\n")
        g = defaultdict(list)
        for l in near:
            g[(TYPES[l["type"]], l["template"])].append(l)
        for (t, n), v in sorted(g.items(), key=lambda kv: -len(kv[1])):
            fl = Counter(f for l in v for f in F if l["flags"] & F[f] and f != "enabled")
            size = (f"box {q([l['box'][0] for l in v], .5):.3g} x {q([l['box'][1] for l in v], .5):.3g} x "
                    f"{q([l['box'][2] for l in v], .5):.3g}" if t == "box" else f"r {q([l['radius'] for l in v], .5):.3g}")
            out.write(f"  {len(v):3d} {t:5s} {n}: {size}, I {q([l['intensity'] for l in v], .5):.4g}, "
                      f"channels {sorted(set(l['channel'] for l in v))}, flags {dict(fl)}\n")
        pts = [l for l in near if l["type"] != 2]
        out.write(f"point/spot: pre_expose {sum(1 for l in pts if l['flags'] & F['pre_expose'])}/{len(pts)}, flat_falloff "
                  f"{sum(1 for l in pts if l['flags'] & F['flat_falloff'])}, shadow {sum(1 for l in pts if l['flags'] & (F['shadow'] | F['cast_shadow']))}; "
                  f"boxes with shadow {sum(1 for l in near if l['type'] == 2 and l['flags'] & F['shadow'])}/{sum(1 for l in near if l['type'] == 2)}\n")
        # the game's incident term over a grid of receivers (3 m, +-30 m, 0/-3/-6 m below the camera)
        E = [[] for _ in range(8)]
        for dx in range(-30, 31, 3):
            for dz in range(-30, 31, 3):
                for dy in (-6, -3, 0):
                    p = (c[0] + dx, c[1] + dy, c[2] + dz)
                    for ch in range(8):
                        E[ch].append(sum(point_incident(l, p) for l in pts if l["channel"] >> ch & 1))
        m = [st.mean(e) for e in E]
        out.write("point/spot incident luminance (x b13 " + f"{B13_S6:g}, before N.L and albedo), grid mean per surface "
                  "channel: " + ", ".join(f"ch{i} {v:.4f}" for i, v in enumerate(m)) + "\n")
        R = t_by.get(li, [])
        cam_in = any(inside_trigger(t, c) for t, _ in R)
        n_in = sum(1 for l in near if any(math.dist(l["pos"], t["pos"]) <= r + 1e-3 and inside_trigger(t, l["pos"]) for t, r in R))
        out.write(f"camera inside a roofed volume: {cam_in}; lights inside one: {n_in}/{len(near)}\n")
        out.write(f"without b13 (exposure missing) ch0 would be x{1 / B13_S6:.0f}; ch0 / ch1 = {m[0] / max(m[1], 1e-12):.2f}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
