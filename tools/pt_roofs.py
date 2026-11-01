"""Roof volumes of a replay pack's level for the offline PT reference (tools/pt_reference.hlsl PT_ROOFS).

    py -3 tools/pt_roofs.py --pack <replay dir> [--db data/assetdb/bg3rtx_assetdb.bin]
                            [--out captures/pt_roofs.hlsli] [--radius 300] [--level NAME] [--trust FILE]

The pack holds no roof volumes (they are the sun set's b8, not the gi_hit_direct contract) and a pack
captured with rt_cascade_casters=0 has no cut-away roof in its TLAS. This reads the asset database's
roof triggers as the layer does (layer/assetdb.cpp api_roof_volumes + trigger_box: roofed rooms,
covered floors, roof floors; unpadded oriented boxes), around the pack's absolute camera (set 0 b2,
cam2.c[13], s0b29.bin), in the level whose bounds contain it (smallest such level, or --level), and
writes them, nearest first, camera-relative (the lighting space), as an HLSL header. The padding (core/sun_trust.cpp refresh: 3 m around,
50 m up for the sun) is applied in the shader. Writes into captures/ (gitignored) by default.
--trust FILE also writes the D-LS sun set's b8 image as the core stages it (core/sun_trust.h: 192-byte header with
the count and flag 2 "roof volumes known", then the padded 48-byte Bg3DbVolume records in absolute world space,
nearest first, at most 256) for `frame_replay --sun-trust FILE` (offline gi_hit_sun / gi_far).
"""
from __future__ import annotations

import argparse
import math
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
from tools.assets.dbformat import Reader  # noqa: E402

KIND_ROOM, KIND_FLOOR = 0, 2
TF_ROOF, TF_ROOM_HAS_ROOF, TF_COVERED = 1, 16, 32


def qrot(q, v):
    """Rotate v by the unit quaternion q = (x, y, z, w)."""
    u = q[:3]
    w = q[3]
    t = [2 * (u[1] * v[2] - u[2] * v[1]), 2 * (u[2] * v[0] - u[0] * v[2]), 2 * (u[0] * v[1] - u[1] * v[0])]
    c = [u[1] * t[2] - u[2] * t[1], u[2] * t[0] - u[0] * t[2], u[0] * t[1] - u[1] * t[0]]
    return [v[i] + w * t[i] + c[i] for i in range(3)]


def trigger_box(db, t, tpts):
    """layer/assetdb.cpp trigger_box: (centre, rot, half) or None."""
    pos, scale, rot, ext, height, radius = t[0:3], t[3], t[4:8], t[8:11], t[11], t[12]
    shape, first_point, point_count = t[17], t[23], t[24]
    s = scale if scale > 1e-6 else 1.0
    lc = [0.0, 0.0, 0.0]
    if shape == 2:
        h = [0.5 * e for e in ext]
    elif shape == 1:
        h = [radius] * 3
    else:
        if point_count < 3:
            return None
        P = tpts[first_point:first_point + point_count]
        xs, zs = [p[0] for p in P], [p[1] for p in P]
        lc = [0.5 * (min(xs) + max(xs)), 0.5 * height, 0.5 * (min(zs) + max(zs))]
        h = [0.5 * (max(xs) - min(xs)), 0.5 * height, 0.5 * (max(zs) - min(zs))]
    lc = [c * s for c in lc]
    h = [x * s for x in h]
    if not all(math.isfinite(x) and x >= 0 for x in h):
        return None
    wc = qrot(list(rot), lc)   # local -> world
    return [pos[i] + wc[i] for i in range(3)], list(rot), h


def write_trust(path, found, pad_xz, pad_up):
    """core/sun_trust.cpp sunt_begin + refresh: header (count, flags 2 = roof volumes known, rise 6, scene top
    -1e30, margin 0, ramp 0.75, roof mode 0, near 0; the shader's fields NaN / 0), then each volume padded
    (half x/z + pad_xz, half y + pad_up / 2, centre moved pad_up / 2 along the box's local +y)."""
    hdr = bytearray(192)
    struct.pack_into("<II4fIf", hdr, 0, len(found), 2, 6.0, -1e30, 0.0, 0.75, 0, 0.0)
    struct.pack_into("<3f", hdr, 36, float("nan"), float("nan"), float("nan"))
    out = bytearray(hdr)
    for _, c, q, h, kind, trig in found:
        up = qrot(q, [0.0, 1.0, 0.0])
        cc = [c[i] + up[i] * 0.5 * pad_up for i in range(3)]
        hh = [h[0] + pad_xz, h[1] + 0.5 * pad_up, h[2] + pad_xz]
        out += struct.pack("<3fI4f3fI", *cc, kind, *q, *hh, trig)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    open(path, "wb").write(bytes(out))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--pack", required=True)
    ap.add_argument("--db", default=os.path.join(ROOT, "data", "assetdb", "bg3rtx_assetdb.bin"))
    ap.add_argument("--out", default=os.path.join(ROOT, "captures", "pt_roofs.hlsli"))
    ap.add_argument("--radius", type=float, default=300.0)
    ap.add_argument("--level", default="")
    ap.add_argument("--trust", default="", help="also write the sun trust image (core/sun_trust.h) for frame_replay --sun-trust")
    ap.add_argument("--pad-xz", type=float, default=3.0, help="rt_sun_roof_pad (core/sun_trust.cpp refresh)")
    ap.add_argument("--pad-up", type=float, default=50.0, help="rt_sun_roof_up")
    a = ap.parse_args()
    cam2 = open(os.path.join(a.pack, "s0b29.bin"), "rb").read()
    cam = list(struct.unpack_from("<3f", cam2, 208))
    db = Reader(open(a.db, "rb").read())
    levels = db.rows("LVLS")
    trigs = db.rows("TRIG")
    tpts = db.rows("TPTS")
    # Level bounds overlap (the pack does not name its level): exactly one candidate, or --level.
    def wanted(l):
        if a.level:
            return db.string(l[0]) == a.level
        return all(l[12 + k] <= cam[k] <= l[15 + k] for k in range(3))
    cands = [(i, db.string(l[0])) for i, l in enumerate(levels) if wanted(l)]
    if len(cands) != 1:
        print(f"camera {cam}: {len(cands)} candidate levels {[n for _, n in cands]}; give --level", file=sys.stderr)
        return 1
    best = (0.0, cands[0][0], cands[0][1])
    l = levels[best[1]]
    first, count = l[8], l[9]
    found = []
    for ti, t in enumerate(trigs[first:first + count]):
        flags, kind = t[14], t[16]
        k = 0
        if kind == KIND_ROOM and flags & (TF_ROOM_HAS_ROOF | TF_ROOF):
            k |= 1
        if kind == KIND_FLOOR and flags & TF_COVERED:
            k |= 2
        if kind == KIND_FLOOR and flags & TF_ROOF:
            k |= 4
        b = trigger_box(db, t, tpts) if k else None
        if not b:
            continue
        c, q, h = b
        dc = math.dist(cam, c) - math.sqrt(sum(x * x for x in h))
        if dc <= a.radius:
            found.append((dc, c, q, h, k, ti))
    found.sort(key=lambda f: f[0])
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w", newline="\n") as f:
        f.write(f"// Generated by tools/pt_roofs.py: level {best[2]}, camera {cam[0]:.2f} {cam[1]:.2f} {cam[2]:.2f}, "
                f"{len(found)} roof volumes within {a.radius:.0f} m (unpadded; centre camera-relative, rot xyzw, half).\n")
        f.write(f"static const uint kPtRoofCount = {len(found)}u;\n")
        rows = [f"float4({c[0] - cam[0]:.4f}, {c[1] - cam[1]:.4f}, {c[2] - cam[2]:.4f}, 0), float4({q[0]:.7f}, {q[1]:.7f}, {q[2]:.7f}, {q[3]:.7f}), "
                f"float4({h[0]:.4f}, {h[1]:.4f}, {h[2]:.4f}, 0)" for _, c, q, h, _k, _i in found] or ["float4(0, 0, 0, 0)"]
        f.write("static const float4 kPtRoof[] = {\n  " + ",\n  ".join(rows) + "\n};\n")
    if a.trust:
        write_trust(a.trust, found[:256], a.pad_xz, a.pad_up)
        print(f"sun trust image: {min(len(found), 256)} padded volumes -> {a.trust}")
    print(f"level {best[2]}: {len(found)} roof volumes within {a.radius:.0f} m of {cam} -> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
