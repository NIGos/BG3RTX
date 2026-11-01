"""Roof-volume statistics of the asset database, for the RT sun's trust test (TRACK_D_RT 12).

Measures, per level, what the `rt_sun_roof_*` / `rt_sun_rise` settings and `kSunMaxVolumes`
(core/sun_trust.h) depend on, from the same oriented boxes `layer/assetdb.cpp`
(`trigger_box`, `api_roof_volumes`) hands the core:

  - roof volumes per level, and the most that a camera standing at any volume's centre
    gets back for a query radius R (the upload is capped at kSunMaxVolumes, nearest first);
  - vertical extent of the volumes (storey heights) and the building heights from the
    FloorTriggers of one BuildingUUID (lowest floor bottom to highest floor top): a lower
    bound of the tallest caster of a building above its ground floor;
  - per floor -> attached room: how far the floor trigger reaches beyond the room trigger
    sideways and upwards (trigger against trigger, NOT the roof mesh: the database holds no
    scenery geometry, docs/ASSET_DB.md "Not decoded").

    python scripts/roof_stats.py [path/to/bg3rtx_assetdb.bin] [--json out.json]
"""
from __future__ import annotations

import argparse
import json
import math
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from tools.assets import dbformat  # noqa: E402

KIND_ROOM, KIND_PORTAL, KIND_FLOOR = 0, 1, 2
TF_ROOF, TF_ROOM_HAS_ROOF, TF_COVERED = 1, 16, 32


def read_cap(root=None):
    """core/sun_trust.h kSunMaxVolumes (the upload cap) and shaders/gi/sun_trust.hlsli SUN_MAX_VOLUMES."""
    import re
    root = root or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    with open(os.path.join(root, "core", "sun_trust.h"), encoding="utf-8") as f:
        cpp = int(re.search(r"constexpr uint32_t kSunMaxVolumes = (\d+);", f.read()).group(1))
    with open(os.path.join(root, "shaders", "gi", "sun_trust.hlsli"), encoding="utf-8") as f:
        hlsl = int(re.search(r"#define SUN_MAX_VOLUMES (\d+)u", f.read()).group(1))
    return cpp, hlsl


K_MAX_VOLUMES = read_cap()[0]


def qrot(q, v):
    x, y, z, w = q
    vx, vy, vz = v
    tx, ty, tz = 2 * (y * vz - z * vy), 2 * (z * vx - x * vz), 2 * (x * vy - y * vx)
    return (vx + w * tx + (y * tz - z * ty), vy + w * ty + (z * tx - x * tz), vz + w * tz + (x * ty - y * tx))


def trigger_box(t, pts):
    """layer/assetdb.cpp trigger_box: (centre, rot, half) or None."""
    s = t["scale"] if t["scale"] > 1e-6 else 1.0
    lc = [0.0, 0.0, 0.0]
    if t["shape"] == 2:
        h = [0.5 * e for e in t["extents"]]
    elif t["shape"] == 1:
        h = [t["radius"]] * 3
    else:
        if t["npt"] < 3:
            return None
        P = pts[t["fpt"]:t["fpt"] + t["npt"]]
        x0, x1 = min(p[0] for p in P), max(p[0] for p in P)
        z0, z1 = min(p[1] for p in P), max(p[1] for p in P)
        lc = [0.5 * (x0 + x1), 0.5 * t["height"], 0.5 * (z0 + z1)]
        h = [0.5 * (x1 - x0), 0.5 * t["height"], 0.5 * (z1 - z0)]
    lc = [c * s for c in lc]
    h = [c * s for c in h]
    if any(not math.isfinite(c) or c < 0 for c in h):
        return None
    wc = qrot(t["rot"], lc)
    return tuple(t["pos"][k] + wc[k] for k in range(3)), t["rot"], tuple(h)


def world_aabb(box):
    c, q, h = box
    lo, hi = [1e30] * 3, [-1e30] * 3
    for sx in (-1, 1):
        for sy in (-1, 1):
            for sz in (-1, 1):
                p = qrot(q, (sx * h[0], sy * h[1], sz * h[2]))
                for k in range(3):
                    lo[k] = min(lo[k], c[k] + p[k])
                    hi[k] = max(hi[k], c[k] + p[k])
    return lo, hi


def pct(xs, p):
    if not xs:
        return float("nan")
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(round(p / 100.0 * (len(xs) - 1))))]


def main():
    ap = argparse.ArgumentParser()
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("db", nargs="?", default=os.path.join(here, "..", "data", "assetdb", "bg3rtx_assetdb.bin"))
    ap.add_argument("--json")
    ap.add_argument("--check", action="store_true",
                    help="exit 1 unless every level's roof volumes fit kSunMaxVolumes (none can be dropped)")
    a = ap.parse_args()
    with open(a.db, "rb") as f:
        r = dbformat.Reader(f.read())
    pts = r.rows("TPTS")
    tref = [x[0] for x in r.rows("TREF")]
    trigs = []
    for row in r.rows("TRIG"):
        (px, py, pz, scale, qx, qy, qz, qw, ex, ey, ez, height, radius, _fade, flags, floor, kind, shape, rm, _pad,
         inst, name, building, fpt, npt, fref, nref) = row
        trigs.append(dict(pos=(px, py, pz), scale=scale, rot=(qx, qy, qz, qw), extents=(ex, ey, ez), height=height,
                          radius=radius, flags=flags, floor=floor, kind=kind, shape=shape, name=name,
                          building=building, fpt=fpt, npt=npt, fref=fref, nref=nref))
    out = {"levels": {}}
    all_bheight, all_vol_h, all_excess_xz, all_excess_up, all_gap = [], [], [], [], []
    for lv in r.rows("LVLS"):
        lname = r.string(lv[0])
        first_trig, n_trig = lv[8], lv[9]
        vols, boxes = [], {}
        for i in range(first_trig, first_trig + n_trig):
            t = trigs[i]
            kind = 0
            if t["kind"] == KIND_ROOM and t["flags"] & (TF_ROOM_HAS_ROOF | TF_ROOF):
                kind |= 1
            if t["kind"] == KIND_FLOOR and t["flags"] & TF_COVERED:
                kind |= 2
            if t["kind"] == KIND_FLOOR and t["flags"] & TF_ROOF:
                kind |= 4
            b = trigger_box(t, pts)
            if b is None:
                continue
            boxes[i] = b
            if kind:
                vols.append((i, kind, b))
        if not vols:
            continue
        # Query results for a camera at each volume's centre (api_roof_volumes: sphere distance).
        cen = [v[2][0] for v in vols]
        rad = [math.sqrt(sum(h * h for h in v[2][2])) for v in vols]
        worst = {}
        for R in (60, 120, 300, 600, 1000):
            m = 0
            for c in cen:
                n = sum(1 for c2, r2 in zip(cen, rad) if math.dist(c, c2) - r2 <= R)
                m = max(m, n)
            worst[R] = m
        vol_h = [2 * world_aabb(v[2])[1][1] - 2 * v[2][0][1] for v in vols]   # full world height
        # Buildings: floors grouped by BuildingUUID.
        bfl = defaultdict(list)
        for i, b in boxes.items():
            t = trigs[i]
            if t["kind"] == KIND_FLOOR and t["building"]:
                bfl[t["building"]].append((t["floor"], world_aabb(b)))
        bheight, gaps = [], []
        for fl in bfl.values():
            lo = min(x[1][0][1] for x in fl)
            hi = max(x[1][1][1] for x in fl)
            bheight.append(hi - lo)
            fl.sort(key=lambda x: x[0])
            for (f0, a0), (f1, a1) in zip(fl, fl[1:]):
                if f1 == f0 + 1:
                    gaps.append(a1[0][1] - a0[1][1])   # next floor bottom - this floor top
        # Floor -> attached room: floor trigger beyond the room trigger.
        ex_xz, ex_up = [], []
        for i, b in boxes.items():
            t = trigs[i]
            if t["kind"] != KIND_FLOOR:
                continue
            flo, fhi = world_aabb(b)
            for j in tref[t["fref"]:t["fref"] + t["nref"]]:
                if j in boxes and trigs[j]["kind"] == KIND_ROOM:
                    rlo, rhi = world_aabb(boxes[j])
                    ex_xz.append(max(rlo[0] - flo[0], fhi[0] - rhi[0], rlo[2] - flo[2], fhi[2] - rhi[2]))
                    ex_up.append(fhi[1] - rhi[1])
        ymin = min(world_aabb(v[2])[0][1] for v in vols)
        ymax = max(world_aabb(v[2])[1][1] for v in vols)
        out["levels"][lname] = dict(
            volumes=len(vols), rooms=sum(1 for v in vols if v[1] & 1), covered=sum(1 for v in vols if v[1] & 2),
            roof_floors=sum(1 for v in vols if v[1] & 4), worst_in_radius=worst,
            volume_height_p50=pct(vol_h, 50), volume_height_max=max(vol_h), volume_y=[ymin, ymax],
            buildings=len(bheight), building_height_p50=pct(bheight, 50), building_height_p95=pct(bheight, 95),
            building_height_max=max(bheight) if bheight else 0.0, floor_gap_p50=pct(gaps, 50),
            floor_gap_max=max(gaps) if gaps else float("nan"), floor_room_excess_xz_p95=pct(ex_xz, 95),
            floor_room_excess_up_p95=pct(ex_up, 95), floor_room_pairs=len(ex_xz))
        all_bheight += bheight
        all_vol_h += vol_h
        all_excess_xz += ex_xz
        all_excess_up += ex_up
        all_gap += gaps
    L = out["levels"]
    out["all"] = dict(
        levels=len(L), volumes=sum(v["volumes"] for v in L.values()),
        max_volumes_per_level=max(v["volumes"] for v in L.values()),
        worst_in_radius={R: max(v["worst_in_radius"][R] for v in L.values()) for R in (60, 120, 300, 600, 1000)},
        volume_height_p50=pct(all_vol_h, 50), volume_height_p95=pct(all_vol_h, 95), volume_height_max=max(all_vol_h),
        building_height_p50=pct(all_bheight, 50), building_height_p95=pct(all_bheight, 95),
        building_height_max=max(all_bheight) if all_bheight else 0.0, floor_gap_p50=pct(all_gap, 50),
        floor_gap_p95=pct(all_gap, 95), floor_room_excess_xz_p50=pct(all_excess_xz, 50),
        floor_room_excess_xz_p95=pct(all_excess_xz, 95), floor_room_excess_up_p50=pct(all_excess_up, 50),
        floor_room_excess_up_p95=pct(all_excess_up, 95), floor_room_pairs=len(all_excess_xz),
        k_max_volumes=K_MAX_VOLUMES)
    print(f"{'level':28} {'vols':>5} {'room':>4} {'cov':>4} {'roof':>4}  worst within R=60/120/300/600/1000 m"
          f"   vol h p50/max   bldg h p50/p95/max  floor gap p50")
    for n, v in sorted(L.items(), key=lambda kv: -kv[1]["volumes"]):
        w = v["worst_in_radius"]
        print(f"{n:28} {v['volumes']:5} {v['rooms']:4} {v['covered']:4} {v['roof_floors']:4}  "
              f"{w[60]:4} {w[120]:4} {w[300]:4} {w[600]:4} {w[1000]:4}"
              f"   {v['volume_height_p50']:5.1f} {v['volume_height_max']:6.1f}"
              f"   {v['building_height_p50']:5.1f} {v['building_height_p95']:5.1f} {v['building_height_max']:5.1f}"
              f"  {v['floor_gap_p50']:5.2f}")
    print(json.dumps(out["all"], indent=1))
    if a.json:
        with open(a.json, "w") as f:
            json.dump(out, f, indent=1)
    if a.check:
        worst = out["all"]["max_volumes_per_level"]
        print(f"roof cap: at most {worst} volumes in a level, kSunMaxVolumes {K_MAX_VOLUMES}: "
              f"{'ok' if worst <= K_MAX_VOLUMES else 'TOO SMALL (volumes are dropped, pixels under them leak)'}")
        return 0 if worst <= K_MAX_VOLUMES else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
