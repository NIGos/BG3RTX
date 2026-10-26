"""Effect (VFX) lights: `Light` components of the `.lsfx` files in Effects.pak.

Each component has a start/end time, a light template (`Appearance.Light UUID`) and
keyframed Intensity, Radius and Color curves over normalised component life, plus flicker.
These are definitions only: where and when an effect plays is runtime state (bg3se
`EffectComponent`, or the runtime light array while it is on screen).
"""
from __future__ import annotations

import os
from collections import Counter
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass

from .. import lsf
from ..lspk import PackageSet


@dataclass
class EffectLight:
    effect: str           # path without "Public/" prefix and extension
    start: float
    end: float
    template: str
    intensity_max: float
    intensity_mean: float
    radius_max: float
    color: tuple          # colour at the intensity peak (rgb)
    flicker_amount: float
    flicker_speed: float
    offset: tuple
    flags: int            # 1 override template colour, 2 modulate template radius, 4 flicker override


def _curve(prop):
    """(times, values) of the first Frames curve of a property, or a constant."""
    if "Value" in prop.attrs and not prop.children:
        return [0.0], [prop.attrs["Value"]]
    for fr in prop.children_named("Frames"):
        ts, vs = [], []
        for f in fr.children_named("Frame"):
            ts.append(float(f.get("Time", 0.0) or 0.0))
            vs.append(f.get("Color") if "Color" in f.attrs else f.get("Value"))
        if ts:
            return ts, vs
    return [], []


def parse_effect(name: str, data: bytes) -> list[EffectLight]:
    out = []
    res = lsf.parse(data)
    for c in res.find_all("EffectComponent"):
        if str(c.get("Type") or "").lower() != "light":
            continue
        props = {}
        for p in c.find_all("Property"):
            props[p.get("FullName") or p.get("AttributeName") or ""] = p
        def scalar_curve(key):
            p = props.get(key)
            if p is None:
                return []
            return [float(v) for v in _curve(p)[1] if isinstance(v, (int, float))]
        inten = scalar_curve("Appearance.Intensity")
        rad = scalar_curve("Appearance.Radius")
        col = [1.0, 1.0, 1.0]
        cp = props.get("Appearance.Color")
        if cp is not None:
            ts, vs = _curve(cp)
            vs = [v for v in vs if isinstance(v, (list, tuple))]
            if vs:
                k = 0
                if inten and len(inten) == len(vs):
                    k = max(range(len(vs)), key=lambda i: inten[i])
                col = [float(x) for x in vs[k][:3]]
        def const(key, default):
            p = props.get(key)
            if p is None:
                return default
            v = p.get("Value")
            if v is None:
                vals = scalar_curve(key)
                return max(vals) if vals else default
            return v
        flags = 0
        if const("Override Light Template Color", False) is True or const("Appearance.Override Light Template Color", False) is True:
            flags |= 1
        if const("Modulate Light Template Radius", False) is True or const("Appearance.Modulate Light Template Radius", False) is True:
            flags |= 2
        if const("Override Light Template Flicker Speed", False) is True:
            flags |= 4
        off = const("Offset", [0.0, 0.0, 0.0])
        if not isinstance(off, (list, tuple)) or len(off) < 3:
            off = [0.0, 0.0, 0.0]
        fa = [v for k, p in props.items() if k.endswith("Flicker Amount") for v in [max(scalar_curve(k) or [0.0])]]
        fs = [v for k, p in props.items() if k.endswith("Flicker Speed") for v in [max(scalar_curve(k) or [0.0])]]
        out.append(EffectLight(
            name, float(c.get("StartTime", 0.0) or 0.0), float(c.get("EndTime", 0.0) or 0.0),
            str(const("Appearance.Light UUID", "") or ""), max(inten) if inten else 0.0,
            sum(inten) / len(inten) if inten else 0.0, max(rad) if rad else 0.0, tuple(col),
            fa[0] if fa else 0.0, fs[0] if fs else 0.0, tuple(float(x) for x in off[:3]), flags))
    return out


_PS = None


def _worker(args):
    data_dir, paks, names = args
    global _PS
    if _PS is None:
        _PS = PackageSet(data_dir, only=paks)
    out, errors = [], 0
    for n in names:
        try:
            short = n[len("Public/"):] if n.startswith("Public/") else n
            out.extend(parse_effect(short.rsplit(".", 1)[0], _PS.read(n)))
        except Exception:
            errors += 1
    return out, errors, len(names)


def extract(ps: PackageSet, data_dir: str, jobs: int = 0, log=print) -> tuple[list[EffectLight], dict]:
    jobs = jobs or min(16, os.cpu_count() or 1)
    names = sorted(n for n in ps.index if n.endswith(".lsfx"))
    paks = sorted({ps.owner(n) for n in names})
    chunks = [names[i::jobs * 4] for i in range(jobs * 4)]
    lights, errors, files = [], 0, 0
    if jobs <= 1:
        results = [_worker((data_dir, paks, names))]
    else:
        with ProcessPoolExecutor(jobs) as ex:
            results = list(ex.map(_worker, [(data_dir, paks, c) for c in chunks]))
    for l, e, f in results:
        lights.extend(l)
        errors += e
        files += f
    lights.sort(key=lambda x: (x.effect, x.start))
    stats = {"lsfx_files": files, "unreadable": errors, "effect_lights": len(lights),
             "effects_with_lights": len({l.effect for l in lights})}
    return lights, stats
