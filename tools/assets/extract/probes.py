"""Light probes (`LightProbes/_merged.lsf`) and lighting presets (LightingBank, AtmosphereBank).

A probe has one capture per lighting resource it was baked for (`CaptureDesc.Lighting`):
a 64-byte `_SH.dat` (L1 SH, 4 x float4) stored inline here, and the diffuse/specular
cube textures referenced by path (BC6H cubes in the pak; not copied).
"""
from __future__ import annotations

import struct
from collections import Counter
from dataclasses import dataclass, field

from .. import lsf
from ..geom import Transform
from ..lspk import PackageSet
from .world import LevelIndex, World


@dataclass
class Capture:
    lighting: str
    sh: tuple                       # 16 floats, or () when the file is missing
    diffuse: str
    specular: str


@dataclass
class Probe:
    level: int
    instance: int
    name: str
    map_key: str
    shape: int                      # LightProbeShape (1 box: Size; 0 sphere: Radius)
    pos: tuple
    rot: tuple
    size: tuple
    radius: float
    transition: float
    intensity: float
    enabled: bool
    trigger_lighting: str
    captures: list = field(default_factory=list)


@dataclass
class Preset:
    id: str
    name: str
    kind: str                       # "lighting" or "atmosphere"
    sun_color: tuple = (0.0, 0.0, 0.0)
    sun_intensity: float = 0.0
    sun_yaw: float = 0.0
    sun_pitch: float = 0.0
    sun_size: float = 0.0
    sun_kelvin: float = 0.0
    sun_shadow: bool = False
    sky_color: tuple = (0.0, 0.0, 0.0)
    sky_intensity: float = 0.0
    attrs: dict = field(default_factory=dict)   # everything else, flattened (JSON export only)


def extract_probes(li: LevelIndex, worlds: list[World], ps: PackageSet, log=print) -> tuple[list[Probe], dict]:
    out: list[Probe] = []
    stats = Counter()
    sh_cache: dict[str, tuple] = {}
    for wi, w in enumerate(worlds):
        for ii, inst in enumerate(w.instances):
            for g in li.objects(inst.level, "LightProbes"):
                if g.get("Type") != "lightProbe":
                    continue
                xf = inst.transform.compose(Transform.from_node(g.child("Transform")))
                p = Probe(wi, ii, g.get("Name") or "", g.get("MapKey") or "", int(g.get("LightProbeShape", 0) or 0),
                          xf.pos, xf.rot, tuple(float(x) * xf.scale for x in (g.get("Size") or (0, 0, 0))),
                          float(g.get("Radius", 0.0) or 0.0) * xf.scale, float(g.get("TransitionDistance", 0.0) or 0.0),
                          float(g.get("Intensity", 1.0) if g.get("Intensity") is not None else 1.0),
                          bool(g.get("Enabled", True)), str(g.get("TriggerLighting") or ""))
                for cd in g.find_all("CaptureDesc"):
                    shp = cd.get("SphericalHarmonicsIBL") or ""
                    if shp not in sh_cache:
                        sh = ()
                        if shp and shp in ps:
                            raw = ps.read(shp)
                            if len(raw) >= 64:
                                sh = struct.unpack_from("<16f", raw, 0)
                        sh_cache[shp] = sh
                    sh = sh_cache[shp]
                    stats["captures"] += 1
                    stats["sh_missing"] += 0 if sh else 1
                    p.captures.append(Capture(str(cd.get("Lighting") or ""), sh, cd.get("DiffuseIBL") or "",
                                              cd.get("SpecularIBL") or ""))
                out.append(p)
                stats["probes"] += 1
    return out, dict(stats)


def _flatten(node, prefix="", out=None):
    out = {} if out is None else out
    for k, v in node.attrs.items():
        if isinstance(v, (int, float, str, bool, list)):
            out[prefix + k] = v
    for c in node.children:
        _flatten(c, prefix + c.name + ".", out)
    return out


def extract_presets(ps: PackageSet, log=print) -> tuple[list[Preset], dict]:
    out: dict[str, Preset] = {}
    for n in ps.index:
        if "/Content/" not in n or not n.endswith("_merged.lsf"):
            continue
        try:
            res = lsf.parse(ps.read(n))
        except Exception:
            continue
        for bank, kind in (("LightingBank", "lighting"), ("AtmosphereBank", "atmosphere")):
            reg = res.region(bank)
            if reg is None:
                continue
            for r in reg.children:
                pid = r.get("ID") or ""
                p = Preset(pid, r.get("Name") or "", kind)
                flat = _flatten(r)
                p.attrs = flat
                sun = next(r.find_all("Sun"), None)
                if sun is not None:
                    p.sun_color = tuple(float(x) for x in (sun.get("SunColor") or (0, 0, 0)))[:3]
                    p.sun_intensity = float(sun.get("SunIntensity", 0.0) or 0.0)
                    p.sun_yaw = float(sun.get("Yaw", 0.0) or 0.0)
                    p.sun_pitch = float(sun.get("Pitch", 0.0) or 0.0)
                    p.sun_size = float(sun.get("LightSize", 0.0) or 0.0)
                    p.sun_kelvin = float(sun.get("Kelvin", 0.0) or 0.0)
                    p.sun_shadow = bool(sun.get("ShadowEnabled", False))
                sky = next(r.find_all("SkyLight"), None)
                if sky is not None:
                    p.sky_color = tuple(float(x) for x in (sky.get("Color") or (0, 0, 0)))[:3]
                    p.sky_intensity = float(sky.get("Intensity", 0.0) or 0.0)
                out[pid] = p
    stats = Counter(p.kind for p in out.values())
    return sorted(out.values(), key=lambda p: (p.kind, p.id)), dict(stats)
