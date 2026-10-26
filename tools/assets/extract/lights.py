"""Placed lights (`Lights/` and `CombinedLights/`) resolved against their root templates.

A placement stores only what differs from its template (`TemplateName`); templates
inherit from `ParentTemplateId`. Each field is taken from the placement, else the first
template up the chain that sets it, else the engine default below.
"""
from __future__ import annotations

import math
from collections import Counter
from dataclasses import dataclass

from .. import lsf
from ..geom import Transform, quat_forward
from ..lspk import PackageSet
from .world import LevelIndex, World

# LightType: 0 point, 1 spot, 2 directional (an oriented box: DirectionLightDimensions,
# optional cookie; the "projector" loop of the tiled lighting, b29).
TYPE_POINT, TYPE_SPOT, TYPE_BOX = 0, 1, 2
TYPE_NAMES = ["point", "spot", "box"]

# flags
L_ENABLED, L_USE_TEMPERATURE, L_SHADOW, L_VOLUMETRIC_SHADOW = 1 << 0, 1 << 1, 1 << 2, 1 << 3
L_FLICKER, L_MOVING, L_SUNLIGHT, L_PRE_EXPOSE = 1 << 4, 1 << 5, 1 << 6, 1 << 7
L_FLAT_FALLOFF, L_CAST_SHADOW, L_COOKIE, L_COMBINED = 1 << 8, 1 << 9, 1 << 10, 1 << 11
L_NOT_STARTING_LOADED, L_HALF_LIT = 1 << 12, 1 << 13
FLAG_NAMES = ["enabled", "use_temperature", "shadow", "volumetric_shadow", "flicker", "moving", "sunlight",
              "pre_expose", "flat_falloff", "cast_shadow", "cookie", "combined", "not_starting_loaded", "half_lit"]

DEFAULTS = {
    "LightType": 0, "Color": (1.0, 1.0, 1.0), "Intensity": 1.0, "Radius": 5.0, "Kelvin": 6500.0,
    "UseTemperature": False, "Enabled": True, "Shadow": False, "VolumetricShadow": False, "CastShadow": False,
    "IsFlickering": False, "Speed": 0.0, "Amount": 0.0, "IsMoving": False, "MovementSpeed": 0.0,
    "MovementAmount": 0.0, "Gain": 0.0, "ScatteringScale": 0.0, "Angle": (0.0, 45.0),
    "DirectionLightDimensions": (1.0, 1.0, 1.0), "DirectionLightAttenuationStart": 0.0,
    "DirectionLightAttenuationEnd": 0.0, "DirectionLightAttenuationSide": 0.0,
    "DirectionLightAttenuationFunction": 0, "LightChannelFlag": 255, "PreExpose": False, "IsSunlight": False,
    "FlatFalloff": False, "LightCookieTexture": "", "LightCookieResource": "", "IsHalfLit": False,
}


@dataclass
class Light:
    level: int            # world index (filled by the builder)
    instance: int         # sub-level instance within the world
    name: str
    template: str         # template name (readable)
    map_key: str
    type: int
    flags: int
    pos: tuple
    rot: tuple
    color: tuple
    intensity: float
    radius: float
    kelvin: float
    gain: float
    scattering: float
    spot_inner: float     # degrees, full cone angles as authored (Angle.x, Angle.y)
    spot_outer: float
    box: tuple            # DirectionLightDimensions
    att_start: float
    att_end: float
    att_side: float
    att_function: int
    flicker_speed: float
    flicker_amount: float
    move_speed: float
    move_amount: float
    channel: int
    cookie: str


class TemplateResolver:
    def __init__(self, ps: PackageSet, types=("light", "CombinedLight")):
        self.t: dict[str, object] = {}
        for n in ps.iter_suffix("RootTemplates/_merged.lsf"):
            for g in lsf.parse(ps.read(n)).find_all("GameObjects"):
                if g.get("Type") in types:
                    self.t[g.get("MapKey")] = g
        self.missing = Counter()

    def chain(self, key: str):
        seen = set()
        while key and key in self.t and key not in seen:
            seen.add(key)
            node = self.t[key]
            yield node
            key = node.get("ParentTemplateId") or ""
        if key and key not in self.t:
            self.missing[key] += 1

    def name(self, key: str) -> str:
        n = self.t.get(key)
        return (n.get("Name") if n is not None else "") or ""


def resolve(node, resolver: TemplateResolver, field: str):
    if field in node.attrs:
        return node.attrs[field], True
    for t in resolver.chain(node.get("TemplateName") or ""):
        if field in t.attrs:
            return t.attrs[field], True
    return DEFAULTS.get(field), False


def extract(li: LevelIndex, worlds: list[World], resolver: TemplateResolver, log=print) -> tuple[list[Light], dict]:
    out: list[Light] = []
    stats = Counter()
    defaulted = Counter()
    for wi, w in enumerate(worlds):
        for ii, inst in enumerate(w.instances):
            for cat, combined in (("Lights", False), ("CombinedLights", True)):
                for g in li.objects(inst.level, cat):
                    if g.get("Type") not in ("light", "CombinedLight"):
                        continue

                    def r(f):
                        v, found = resolve(g, resolver, f)
                        if not found:
                            defaulted[f] += 1
                        return v

                    local = Transform.from_node(g.child("Transform"))
                    xf = inst.transform.compose(local)
                    ltype = int(r("LightType") or 0)
                    dims = tuple(float(x) for x in (r("DirectionLightDimensions") or (1, 1, 1)))
                    if ltype not in (0, 1, 2):
                        stats["unknown_type"] += 1
                        ltype = 0
                    angle = r("Angle") or (0.0, 45.0)
                    cookie = r("LightCookieTexture") or r("LightCookieResource") or ""
                    flags = 0
                    for f, bit in (("Enabled", L_ENABLED), ("UseTemperature", L_USE_TEMPERATURE), ("Shadow", L_SHADOW),
                                   ("VolumetricShadow", L_VOLUMETRIC_SHADOW), ("IsFlickering", L_FLICKER),
                                   ("IsMoving", L_MOVING), ("IsSunlight", L_SUNLIGHT), ("PreExpose", L_PRE_EXPOSE),
                                   ("FlatFalloff", L_FLAT_FALLOFF), ("CastShadow", L_CAST_SHADOW),
                                   ("IsHalfLit", L_HALF_LIT)):
                        if r(f):
                            flags |= bit
                    if cookie:
                        flags |= L_COOKIE
                    if combined:
                        flags |= L_COMBINED
                    if not inst.starting_loaded:
                        flags |= L_NOT_STARTING_LOADED
                    col = tuple(float(x) for x in (r("Color") or (1, 1, 1)))[:3]
                    light = Light(
                        wi, ii, g.get("Name") or "", resolver.name(g.get("TemplateName") or ""), g.get("MapKey") or "",
                        ltype, flags, xf.pos, xf.rot, col, float(r("Intensity") or 0.0),
                        float(r("Radius") or 0.0) * inst.transform.scale,
                        float(r("Kelvin") or 0.0), float(r("Gain") or 0.0), float(r("ScatteringScale") or 0.0),
                        float(angle[0]), float(angle[1]), dims,
                        float(r("DirectionLightAttenuationStart") or 0.0), float(r("DirectionLightAttenuationEnd") or 0.0),
                        float(r("DirectionLightAttenuationSide") or 0.0), int(r("DirectionLightAttenuationFunction") or 0),
                        float(r("Speed") or 0.0), float(r("Amount") or 0.0), float(r("MovementSpeed") or 0.0),
                        float(r("MovementAmount") or 0.0), int(r("LightChannelFlag") or 0) & 0xFF, str(cookie))
                    out.append(light)
                    stats[TYPE_NAMES[ltype]] += 1
                    stats["total"] += 1
    stats_d = dict(stats)
    stats_d["defaulted_fields"] = dict(defaulted.most_common())
    stats_d["missing_templates"] = sum(resolver.missing.values())
    return out, stats_d


def light_bounds_radius(l: Light) -> float:
    """A radius that encloses everything the light can reach (for spatial queries)."""
    if l.type == TYPE_BOX:
        return 0.5 * math.sqrt(sum(d * d for d in l.box)) + max(l.att_end, 0.0)
    return max(l.radius, 0.0)


def direction(l: Light) -> tuple:
    return quat_forward(l.rot)
