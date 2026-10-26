"""Interior and lighting volumes: RoomTrigger, PortalTrigger, FloorTrigger (IsRoof),
TriggerLighting and TriggerAtmosphere.

Shapes (`PhysicsType`, from the placement or its template):
  2  box: `Extents` (full size) centred on the Transform position, rotated by it
  3  prism: a 2D polygon `Points` (x, z in the trigger's local frame) extruded by `Height`
     upwards from the Transform position
  other values are kept raw (sphere: `Radius`).
The box-centred and prism-upwards conventions are the editor's (LSLib/bg3se do not
document them); docs/ASSET_DB.md lists them as unverified until checked in game.
"""
from __future__ import annotations

from collections import Counter
from dataclasses import dataclass, field

from .. import lsf
from ..geom import Transform
from ..lspk import PackageSet
from .world import LevelIndex, World

KINDS = ["room", "portal", "floor", "lighting", "atmosphere"]
TRIGGER_TYPES = {"RoomTrigger": "room", "PortalTrigger": "portal", "FloorTrigger": "floor",
                 "TriggerLighting": "lighting", "TriggerAtmosphere": "atmosphere"}
SHAPE_BOX, SHAPE_PRISM, SHAPE_SPHERE = 2, 3, 1

T_ROOF, T_ONE_SHOT, T_FADE_GROUP_ONLY, T_NOT_STARTING_LOADED, T_ROOM_HAS_ROOF, T_COVERED = 1, 2, 4, 8, 16, 32


@dataclass
class Trigger:
    level: int
    instance: int
    kind: str
    name: str
    map_key: str
    shape: int
    pos: tuple
    rot: tuple
    scale: float
    extents: tuple
    height: float
    radius: float
    points: list
    flags: int
    floor: int = 0
    building: str = ""
    render_method: int = 0
    fade_time: float = 0.0
    resources: list = field(default_factory=list)   # lighting/atmosphere resource ids
    links: list = field(default_factory=list)       # map keys: attached rooms (floor) / portals (room)


def trigger_templates(ps: PackageSet) -> dict:
    out = {}
    for n in ps.iter_suffix("RootTemplates/_merged.lsf"):
        for g in lsf.parse(ps.read(n)).find_all("GameObjects"):
            if g.get("Type") == "trigger":
                out[g.get("MapKey")] = g
    return out


def extract(li: LevelIndex, worlds: list[World], templates: dict, log=print) -> tuple[list[Trigger], dict]:
    out: list[Trigger] = []
    stats = Counter()
    for wi, w in enumerate(worlds):
        sources = [(ii, inst, li.objects(inst.level, "Triggers")) for ii, inst in enumerate(w.instances)]
        sources.append((0, w.instances[0], li.objects(w.name, "Triggers", globals_=True)))
        for ii, inst, objs in sources:
            for g in objs:
                t = templates.get(g.get("TemplateName"))
                ttype = g.get("TriggerType") or (t.get("TriggerType") if t is not None else None)
                kind = TRIGGER_TYPES.get(ttype)
                if kind is None:
                    continue

                def r(f, d=None):
                    if f in g.attrs:
                        return g.attrs[f]
                    if t is not None and f in t.attrs:
                        return t.attrs[f]
                    return d

                xf = inst.transform.compose(Transform.from_node(g.child("Transform")))
                pts = [tuple(p.get("Object")) for p in g.children_named("Points") if p.get("Object")]
                shape = int(r("PhysicsType", SHAPE_PRISM if pts else SHAPE_BOX) or 0)
                flags = 0
                if r("IsRoof", r("IsRooof", False)):
                    flags |= T_ROOF
                if r("OneShotTrigger", False):
                    flags |= T_ONE_SHOT
                if r("FadeGroupOnly", False):
                    flags |= T_FADE_GROUP_ONLY
                if not inst.starting_loaded:
                    flags |= T_NOT_STARTING_LOADED
                res: list[str] = []
                for c in g.find_all("ResourceID"):  # under ResourceIDs (and AtmResourceIDs)
                    if c.get("Object") and c.get("Object") not in res:
                        res.append(c.get("Object"))
                links = [c.get("Object") for name in ("AttachedRooms", "AttachedPortals")
                         for c in g.children_named(name) if c.get("Object")]
                out.append(Trigger(wi, ii, kind, g.get("Name") or "", g.get("MapKey") or "", shape, xf.pos, xf.rot,
                                   xf.scale, tuple(float(x) for x in (r("Extents") or (0, 0, 0))),
                                   float(r("Height", 0.0) or 0.0), float(r("Radius", 0.0) or 0.0), pts, flags,
                                   int(r("Floor", 0) or 0), str(r("BuildingUUID", "") or ""),
                                   int(r("RenderMethod", 0) or 0), float(r("FadeTime", 0.0) or 0.0), res, links))
                stats[kind] += 1
                stats[f"shape_{shape}"] += 1
    # Roofs. `IsRoof` is set on a single FloorTrigger in the whole game, so it cannot be the
    # signal on its own. A RoomTrigger with RenderMethod 1 is an interior whose roof the game
    # cuts away when the camera follows a character inside (599 of 600 rooms): it counts as
    # roofed. A room also counts when a roof floor lists it. A floor is "covered" when its
    # building has a higher floor.
    by_key = {}
    for i, tr in enumerate(out):
        by_key.setdefault((tr.level, tr.map_key), i)
    roof_buildings = {(tr.level, tr.building) for tr in out if tr.kind == "floor" and tr.flags & T_ROOF and tr.building}
    for tr in out:
        if tr.kind != "floor":
            continue
        roofed = tr.flags & T_ROOF or (tr.level, tr.building) in roof_buildings
        if not roofed:
            continue
        for k in tr.links:
            j = by_key.get((tr.level, k))
            if j is not None and out[j].kind == "room":
                out[j].flags |= T_ROOM_HAS_ROOF
    for tr in out:
        if tr.kind == "room" and tr.render_method == 1:
            tr.flags |= T_ROOM_HAS_ROOF
    top = {}
    for tr in out:
        if tr.kind == "floor" and tr.building:
            k = (tr.level, tr.building)
            top[k] = max(top.get(k, tr.floor), tr.floor)
    for tr in out:
        if tr.kind == "floor" and tr.building and tr.floor < top[(tr.level, tr.building)]:
            tr.flags |= T_COVERED
    stats["covered_floors"] = sum(1 for tr in out if tr.kind == "floor" and tr.flags & T_COVERED)
    stats["rooms_with_roof"] = sum(1 for tr in out if tr.kind == "room" and tr.flags & T_ROOM_HAS_ROOF)
    stats["roof_floors"] = sum(1 for tr in out if tr.kind == "floor" and tr.flags & T_ROOF)
    return out, dict(stats)
