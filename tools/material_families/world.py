"""World prevalence: placed objects per material template, over every database level.

Chain (all read-only from the game's packages, the same readers as tools/assets):
  level object (Scenery, Items; Levels and Globals, merged over mods; every world's
  sub-level instances as in the asset DB's 113 worlds)
  -> its VisualTemplate (on the object, else up the root template's ParentTemplateId chain)
  -> VisualBank resource (Public/*/Content/**.lsf): LOD 0 Objects and their MaterialID
  -> MaterialBank resource: SourceFile = the material template (`Assets/Materials/<T>.lsf`)
  -> the asset DB's template name <T>, whose Deferred ST/STI variants give the VS/FS pair.

Weights per placement: 1 (count) and a geometry proxy: the visual's GR2 uncompressed section
bytes (all LODs, from the Granny section table; vertex and index data dominate), split
evenly over its LOD 0 objects. There is no triangle count without decoding the GR2.
TileConstructions and Terrains are counted per world as their own engine families.
"""
from __future__ import annotations

import json
import os
import re
import struct
import sys
import time
from collections import Counter, defaultdict

from tools.assets import lsf
from tools.assets.build import DEFAULT_GAME, find_data_dir
from tools.assets.extract.world import LevelIndex
from tools.assets.lspk import PackageSet

CATEGORIES = ("Scenery", "Items")
ENGINE_CATEGORIES = {"TileConstructions": "engine:tileset (TileConstruction)", "Terrains": "engine:terrain"}
_MAT = re.compile(r"Assets/Materials/(.+)\.lsf$", re.I)


def template_name(source_file: str) -> str:
    """Material resource SourceFile -> the asset DB template name (path below Assets/Materials)."""
    m = _MAT.search(source_file.replace("\\", "/"))
    if m:
        return m.group(1)
    return os.path.splitext(os.path.basename(source_file))[0]


def gr2_bytes(data: bytes) -> int:
    """Sum of the uncompressed section sizes of a Granny 2 file (0 if not one)."""
    if len(data) < 32 + 24:
        return 0
    off, count = struct.unpack_from("<II", data, 32 + 12)
    if count > 64:
        return 0
    base, total = 32 + off, 0
    for i in range(count):
        if base + 44 * i + 16 > len(data):
            break
        total += struct.unpack_from("<I", data, base + 44 * i + 12)[0]
    return total


class Banks:
    """VisualBank and MaterialBank resources of every Content file."""

    def __init__(self, ps: PackageSet, log=print):
        self.visual: dict[str, tuple[str, list[str]]] = {}   # id -> (GR2 path, LOD 0 material ids)
        self.material: dict[str, str] = {}                   # id -> template name
        n = 0
        for name in ps.index:
            if "/Content/" not in name or not name.endswith(".lsf"):
                continue
            try:
                r = lsf.parse(ps.read(name))
            except Exception:
                continue
            n += 1
            for reg in r.regions:
                if reg.name == "VisualBank":
                    for res in reg.children:
                        mats = [o.get("MaterialID") or "" for o in res.children_named("Objects")
                                if int(o.get("LOD") or 0) == 0]
                        self.visual[res.get("ID")] = (res.get("SourceFile") or "", mats)
                elif reg.name == "MaterialBank":
                    for res in reg.children:
                        self.material[res.get("ID")] = template_name(res.get("SourceFile") or "")
        log(f"world: {n} content files, {len(self.visual)} visuals, {len(self.material)} materials")


class Templates:
    def __init__(self, ps: PackageSet):
        self.t = {}
        for n in ps.iter_suffix("RootTemplates/_merged.lsf"):
            for g in lsf.parse(ps.read(n)).find_all("GameObjects"):
                self.t[g.get("MapKey")] = g

    def visual(self, obj) -> str:
        v = obj.get("VisualTemplate")
        if v:
            return v
        key, seen = obj.get("TemplateName") or "", set()
        while key and key in self.t and key not in seen:
            seen.add(key)
            node = self.t[key]
            if node.get("VisualTemplate"):
                return node.get("VisualTemplate")
            key = node.get("ParentTemplateId") or ""
        return ""


def count(game: str = DEFAULT_GAME, log=print) -> dict:
    """{"worlds": {world: {template: [count, bytes]}}, "levels": {world: [sub-level names]}, "stats": {...}}"""
    t0 = time.time()
    ps = PackageSet(find_data_dir(game))
    li = LevelIndex(ps)
    worlds = li.worlds()
    banks, tpl = Banks(ps, log), Templates(ps)
    gr2_cache: dict[str, int] = {}

    def gbytes(path):
        if path not in gr2_cache:
            gr2_cache[path] = gr2_bytes(ps.read(path)) if path in ps else 0
        return gr2_cache[path]

    out, levels, sub = {}, {}, {}
    stats = Counter()
    for w in worlds:
        per = defaultdict(lambda: [0, 0])
        levels[w.name] = sorted({i.level for i in w.instances})
        for inst in w.instances:
            # per sub-level counts are cached: a sub-level placed in several worlds reads once
            if inst.level not in sub:
                lv = defaultdict(lambda: [0, 0])
                for cat in CATEGORIES:
                    for glob_ in (False, True):
                        for g in li.objects(inst.level, cat, glob_):
                            stats["objects"] += 1
                            vid = tpl.visual(g)
                            vis = banks.visual.get(vid)
                            if not vis:
                                stats["no visual"] += 1
                                continue
                            mats = [banks.material.get(m) for m in vis[1]]
                            mats = [m for m in mats if m]
                            if not mats:
                                stats["no material"] += 1
                                continue
                            stats["resolved"] += 1
                            b = gbytes(vis[0]) / len(mats)
                            for m in mats:
                                lv[m][0] += 1
                                lv[m][1] += b
                for cat, fam in ENGINE_CATEGORIES.items():
                    n = len(li.objects(inst.level, cat))
                    if n:
                        lv[fam][0] += n
                sub[inst.level] = {k: list(v) for k, v in lv.items()}
            for k, (c, b) in sub[inst.level].items():
                per[k][0] += c
                per[k][1] += b
        out[w.name] = {k: [c, round(b)] for k, (c, b) in per.items()}
    log(f"world: {len(worlds)} worlds, {stats['objects']} objects, {stats['resolved']} with a material, "
        f"{len(gr2_cache)} GR2 headers ({time.time() - t0:.0f}s)")
    return {"worlds": out, "levels": levels, "stats": dict(stats),
            "sublevels": {k: {m: [c, round(b)] for m, (c, b) in v.items()} for k, v in sub.items()}}


def load_or_count(cache: str | None, game: str, log=print) -> dict:
    if cache and os.path.exists(cache):
        return json.loads(open(cache, encoding="utf-8").read())
    d = count(game, log)
    if cache:
        os.makedirs(os.path.dirname(os.path.abspath(cache)), exist_ok=True)
        open(cache, "w", encoding="utf-8").write(json.dumps(d))
    return d


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    d = count()
    tot = Counter()
    for w in d["worlds"].values():
        for k, (c, b) in w.items():
            tot[k] += c
    for k, c in tot.most_common(30):
        print(c, k)
