"""Level composition: which level files make up a world, and where each one sits.

BG3 levels live in `Mods/<mod>/Levels/<level>/<Category>/_merged.lsf`. A level can place
other levels (`LevelTemplates/_merged.lsf`): each placement names a LevelTemplate root
template whose `SubLevelName` is the child level, with a Transform. Children nest
(CTY_Main_A -> CTY_LowerCity_A -> BGH_SteelWatchFoundry_B), so a light's world position is
its own Transform composed with every placement above it.

A *world* here is a level that is not placed by any other level (the save's
"Current Level", e.g. `CTY_Main_A`, `WLD_Main_A`) or that places others. Its objects are
the union over its placement tree. A level's content may be split across mods
(Gustav adds, GustavDev adds more); files are merged in mod order with MapKey overrides.
Honour-mode mods are skipped unless asked for.
"""
from __future__ import annotations

import re
from collections import defaultdict
from dataclasses import dataclass, field

from .. import lsf
from ..geom import Transform
from ..lspk import PackageSet

MOD_ORDER = ["Shared", "SharedDev", "Gustav", "GustavDev", "GustavX", "Honour", "HonourX"]
HONOUR_MODS = {"Honour", "HonourX"}
_LEVEL_FILE = re.compile(r"^Mods/([^/]+)/Levels/([^/]+)/([^/]+)/_merged\.lsf$")
_GLOBAL_FILE = re.compile(r"^Mods/([^/]+)/Globals/([^/]+)/([^/]+)/_merged\.lsf$")
_META = re.compile(r"^Mods/([^/]+)/Levels/([^/]+)/meta\.lsf$")


def _mod_rank(mod: str) -> int:
    return MOD_ORDER.index(mod) if mod in MOD_ORDER else len(MOD_ORDER)


@dataclass
class Placement:
    child: str
    name: str
    transform: Transform
    starting_loaded: bool


@dataclass
class Instance:
    """One placed copy of a level inside a world."""
    level: str
    transform: Transform
    parent: int           # index of the parent instance, -1 for the world itself
    name: str
    starting_loaded: bool
    depth: int


@dataclass
class World:
    name: str
    instances: list = field(default_factory=list)
    lighting: str = ""
    atmosphere: str = ""
    bounds: tuple | None = None


class LevelIndex:
    def __init__(self, ps: PackageSet, include_honour: bool = False):
        self.ps = ps
        self.files: dict[str, dict[str, list[tuple[str, str]]]] = defaultdict(lambda: defaultdict(list))
        self.globals: dict[str, dict[str, list[tuple[str, str]]]] = defaultdict(lambda: defaultdict(list))
        self.meta: dict[str, list[tuple[str, str]]] = defaultdict(list)
        for n in ps.index:
            m = _LEVEL_FILE.match(n) or _GLOBAL_FILE.match(n)
            if m:
                mod, level, cat = m.groups()
                if mod in HONOUR_MODS and not include_honour:
                    continue
                (self.globals if "/Globals/" in n else self.files)[level][cat].append((mod, n))
                continue
            m = _META.match(n)
            if m and (m.group(1) not in HONOUR_MODS or include_honour):
                self.meta[m.group(2)].append((m.group(1), n))
        for d in (self.files, self.globals):
            for cats in d.values():
                for lst in cats.values():
                    lst.sort(key=lambda x: _mod_rank(x[0]))
        for lst in self.meta.values():
            lst.sort(key=lambda x: _mod_rank(x[0]))
        self._cache: dict[tuple[str, str, bool], list] = {}
        self._level_templates = self._read_level_templates()
        self.children = self._read_placements()

    # ---- objects of one category of one level, merged over mods ----
    def objects(self, level: str, category: str, globals_: bool = False) -> list:
        key = (level, category, globals_)
        if key in self._cache:
            return self._cache[key]
        merged: dict[str, object] = {}
        order: list[str] = []
        src = self.globals if globals_ else self.files
        for mod, path in src.get(level, {}).get(category, []):
            res = lsf.parse(self.ps.read(path))
            for i, n in enumerate(res.find_all("GameObjects")):
                k = n.get("MapKey") or f"{path}#{i}"
                if k not in merged:
                    order.append(k)
                merged[k] = n
        out = [merged[k] for k in order]
        self._cache[key] = out
        return out

    def _read_level_templates(self) -> dict:
        out = {}
        for n in self.ps.iter_suffix("RootTemplates/_merged.lsf"):
            for g in lsf.parse(self.ps.read(n)).find_all("GameObjects"):
                if g.get("Type") == "LevelTemplate" and g.get("SubLevelName"):
                    out[g.get("MapKey")] = g
        return out

    def _read_placements(self) -> dict[str, list[Placement]]:
        ch: dict[str, list[Placement]] = defaultdict(list)
        for level in list(self.files):
            for g in self.objects(level, "LevelTemplates"):
                t = self._level_templates.get(g.get("TemplateName"))
                if t is None:
                    continue
                sub = t.get("SubLevelName")
                if sub == level:
                    continue
                loaded = g.get("StartingLoaded", t.get("StartingLoaded", True))
                ch[level].append(Placement(sub, g.get("Name") or sub, Transform.from_node(g.child("Transform")),
                                           bool(loaded)))
        return ch

    def level_meta(self, level: str) -> dict:
        out: dict = {}
        for _mod, path in self.meta.get(level, []):
            r = lsf.parse(self.ps.read(path))
            ld = r.region("LevelData")
            if ld is None:
                continue
            out.update({k: v for k, v in ld.attrs.items() if isinstance(v, (str, int, float, bool))})
            b = ld.child("LevelBounds")
            if b is not None and b.get("Min") and b.get("Max"):
                out["bounds"] = (tuple(b.get("Min")), tuple(b.get("Max")))
        return out

    def worlds(self) -> list[World]:
        # Roots only: a level placed by another one is reached through its parent (the
        # database keeps an alias from every placed level to the worlds that contain it).
        placed = {p.child for lst in self.children.values() for p in lst}
        names = sorted(lv for lv in set(self.files) | set(self.globals) if lv not in placed)
        out = []
        for name in names:
            w = World(name)
            self._expand(w, name, Transform(), -1, name, True, 0, set())
            meta = self.level_meta(name)
            w.lighting = str(meta.get("LevelLighting", "") or "")
            w.atmosphere = str(meta.get("LevelAtmosphere", "") or "")
            w.bounds = meta.get("bounds")
            out.append(w)
        return out

    def _expand(self, w: World, level: str, xf: Transform, parent: int, name: str, loaded: bool, depth: int,
                stack: set) -> None:
        if level in stack or depth > 16:
            return
        idx = len(w.instances)
        w.instances.append(Instance(level, xf, parent, name, loaded, depth))
        stack = stack | {level}
        for p in self.children.get(level, []):
            self._expand(w, p.child, xf.compose(p.transform), idx, p.name, loaded and p.starting_loaded, depth + 1,
                         stack)
