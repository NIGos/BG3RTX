"""Ranked shader families for the hit-material provider, without running the game.

    python -m tools.material_families                     # docs/MATERIAL_FAMILIES.md + CSV
    python -m tools.material_families --world-cache x.json  # reuse the world count (60 s)
    python -m tools.material_families --no-world          # frame evidence and classes only

One row per Deferred VS/FS pair (G-buffer). Columns:
- world: placed Scenery/Items objects over the 113 asset-DB worlds (world.py), split between a
  template's ST and STI pairs by their traced G-buffer draws (even split when neither is
  traced); `geo` weights each placement by its GR2 geometry bytes (triangle proxy);
- scenes: the same over the sub-levels of the test scenes S1-S14 (SCENE_LEVELS);
- frames: G-buffer draw share in the traces (mean over traced scenes of the mean over that
  scene's traces), draws, traces, pass kinds, scenes; declined-pair vertices from the logs;
- class: the FS against the audited recipes of common/native_vt_surface.h (glsl.py).
combined = mean of world geo share, scene geo share and frame draw share.
"""
from __future__ import annotations

import argparse
import csv
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from scripts.qa import coverage as cov  # noqa: E402
from tools.material_families import frames, glsl  # noqa: E402

MAIN = cov.MAIN
DB = MAIN / "data" / "assetdb" / "bg3rtx_assetdb.bin"
GLSL_DIRS = [MAIN / "captures" / "alpha_cache"]
SPV_DIRS = [MAIN / "captures" / "shaders"]
SURFACE_H = ROOT / "common" / "native_vt_surface.h"

# Test scene -> (world, sub-levels). Sub-levels chosen by name from the world's placement tree;
# None = the whole world (the scene's sub-level is not identifiable by name).
SCENE_LEVELS = {
    "S1": ("CTY_Main_A", ["BGH_SteelWatchFoundry_B", "PLT_BGH_SteelWatchFoundry_CartElevator_A",
                          "PLT_BGH_SteelwatchFoundry_Elevator_A", "PLT_BGH_SteelwatchFoundry_Elevator_B"]),
    "S2": ("BGO_Main_A", None),
    "S3": ("WLD_Main_A", None),
    "S4": ("WLD_Main_A", ["WLD_GoblinCamp_D"]),
    "S5": ("WLD_Main_A", ["WLD_Underdark_C", "WLD_UnderdarkSubs_C"]),
    "S6": ("WLD_Main_A", ["WLD_Underdark_C", "WLD_UnderdarkSubs_C", "PLT_UND_AdamantineForge",
                          "PLT_UND_AdamantineForgeAnvil"]),
    "S7": ("CTY_Main_A", ["BGH_BlushingMermaid_A", "BGH_BlushingMermaidCellarB"]),
    "S8": ("CTY_Main_A", ["BGH_WaterQueenHouse_B_ART"]),
    "S9": ("CRE_Main_A", ["Cre_GithCreche_D"]),
    "S10": ("SCL_Main_A", None),
    "S11": ("SCL_Main_A", ["WLD_SharTemple_E"]),
    "S12": ("SCL_Main_A", ["SCL_MoonriseDungeon_E_ART", "CMP_Moonrise_A"]),
    "S13": ("SCL_Main_A", ["SCL_MindflayerColony_G"]),
    "S14": ("CTY_Main_A", ["CTY_LowerCity_A"]),
}


# ---- recipes and shaders ----------------------------------------------------------------

def recipes(path: Path = SURFACE_H) -> list[dict]:
    """Rows of native_vt_surface.h `recipes[]`: fs, vs, reflectance mode (0 = audited base), and
    the optional trailing fields (1:1 reflectance byte, Capture, Layout, two-sided). A row is a
    classification base only without them; a CaptureNone row is audited but not built."""
    t = path.read_text(encoding="utf-8")
    out = []
    for m in re.finditer(r"\{0x([0-9a-f]{16})ull,0x([0-9a-f]{16})ull,(\d+),(\d+),(\d+)(?:,(Reflectance\w+))?((?:,\w+)*)\}", t):
        extra = [x for x in m.group(7).split(",") if x]
        out.append({"fs": m.group(1), "vs": m.group(2), "offsets": tuple(int(m.group(i)) for i in (3, 4, 5)),
                    "mode": m.group(6) or "ReflectanceUbo", "extra": extra, "capture": "CaptureNone" not in extra})
    return out


def mapped_offsets(base: dict, fields: tuple) -> str:
    """The recipe's engine-UBO 1:0 offsets (transform, tile-set index, channel) and the 1:1
    reflectance byte, moved through a VARIANT's field map."""
    mv = {(b, f): t for b, f, t in fields}
    o = [mv.get(("ubo1_0", x), x) for x in base["offsets"]]
    r = mv.get(("ubo1_1", 0), 0)
    return f"1:0 offsets {o[0]}/{o[1]}/{o[2]}" + (f", reflectance 1:1 byte {r}" if r else "")


class Sources:
    """GLSL of a shader hash: captures/alpha_cache, else spirv-cross of captures/shaders."""

    def __init__(self):
        self.tool = shutil.which("spirv-cross") or os.path.join(
            os.environ.get("VULKAN_SDK", r"C:\VulkanSDK\1.4.357.0"), "Bin", "spirv-cross.exe")
        self.tmp = Path(tempfile.mkdtemp(prefix="material_families_"))
        self.cache = {}

    def glsl(self, h: str) -> str | None:
        if h in self.cache:
            return self.cache[h]
        txt = None
        for d in GLSL_DIRS:
            p = d / f"{h}.glsl"
            if p.exists():
                txt = p.read_text(encoding="utf-8", errors="ignore")
                break
        if txt is None and os.path.exists(self.tool):
            for d in SPV_DIRS:
                p = d / f"{h}.spv"
                if p.exists():
                    r = subprocess.run([self.tool, str(p), "-V"], capture_output=True)
                    if r.returncode == 0:
                        txt = r.stdout.decode("utf-8", "ignore")
                    break
        self.cache[h] = txt
        return txt

    def close(self):
        shutil.rmtree(self.tmp, ignore_errors=True)


# ---- asset DB: template -> Deferred pairs ---------------------------------------------------

def template_pairs(db_path: Path = DB) -> tuple[dict, dict]:
    """{template: {"Static": (vs, fs), "StaticInstanced": (vs, fs)}}, {fs: [template names]}."""
    from tools.assets.dbformat import Reader
    from tools.assets.extract.materials import VARIANTS
    r = Reader(db_path.read_bytes())
    tpl, var, sref = r.rows("MTPL"), r.rows("MVAR"), r.rows("SREF")
    by = defaultdict(dict)
    for h, first, cnt, stage, _pss, _canon, _ in r.rows("SHDR"):
        for i in range(cnt):
            v = var[sref[first + i][0]]
            vn = VARIANTS[v[1]]
            if vn not in ("StaticDeferred", "StaticInstancedDeferred"):
                continue
            by[(v[0], vn)][stage] = f"{h:016x}"
    out, fs_t = defaultdict(dict), defaultdict(list)
    for (ti, vn), st in by.items():
        if 0 in st and 4 in st:
            name = r.string(tpl[ti][0])
            out[name][vn.replace("Deferred", "")] = (st[0], st[4])
            fs_t[st[4]].append(name)
    return dict(out), dict(fs_t)


# ---- ranking ----------------------------------------------------------------------------

def world_rows(wd: dict, tpairs: dict, gb_draws: dict) -> tuple[dict, dict, dict]:
    """Pair -> [count, bytes] over all worlds, over the test scenes; unmapped templates."""
    def split(tname):
        p = tpairs.get(tname, {})
        st, sti = p.get("Static"), p.get("StaticInstanced")
        if not st and not sti:
            return []
        if not st or not sti:
            return [(st or sti, 1.0)]
        a, b = gb_draws.get(st, 0), gb_draws.get(sti, 0)
        f = 0.5 if a + b == 0 else a / (a + b)
        return [(st, f), (sti, 1.0 - f)]

    allw, scene, unmapped = defaultdict(lambda: [0.0, 0.0]), defaultdict(lambda: [0.0, 0.0]), defaultdict(int)

    def add(dst, per, weight=1.0):
        for t, (c, b) in per.items():
            if t.startswith("engine:"):
                continue
            sp = split(t)
            if not sp:
                unmapped[t] += c
            for pair, f in sp:
                dst[pair][0] += c * f * weight
                dst[pair][1] += b * f * weight

    for w, per in wd["worlds"].items():
        add(allw, per)
    for sid, (w, levels) in SCENE_LEVELS.items():
        if levels is None:
            per = wd["worlds"].get(w, {})
        else:
            per = defaultdict(lambda: [0, 0])
            for lv in levels:
                for t, (c, b) in wd["sublevels"].get(lv, {}).items():
                    per[t][0] += c
                    per[t][1] += b
        # every scene counts equally: normalize by the scene's own total
        tot = sum(b for t, (c, b) in per.items() if not t.startswith("engine:")) or 1.0
        add(scene, per, 1.0 / tot / len(SCENE_LEVELS))
    return allw, scene, unmapped


def next_step(cls: str, built: bool, vs_ok: bool, verdict, vs: str, bases: dict | None = None,
              uncaptured: bool = False) -> str:
    if uncaptured:
        return f"FS decode audited; VS {vs[:8]} not class A: no capture path (PLAN_M4 next families)"
    if built:
        return "built: live gate pending (`[hit material]` qualified up)" if cls != "SUPPORTED" else "audited recipe"
    if cls == "COPY":
        base = f"recipe row: {verdict.term.split(';')[0]}"
        return base if vs_ok else f"audit VS {vs[:8]} (outputs, XFB), then {base}"
    vsa = "" if vs_ok else f"; audit VS {vs[:8]} (outputs, XFB)"
    if cls == "VARIANT" and verdict.term.startswith("fields only") and bases and verdict.recipe in bases:
        plus = verdict.term.split("; plus ", 1)
        extra = f" + term {plus[1].split(':')[0]}" if len(plus) > 1 else ""
        return f"recipe row like {verdict.recipe[:8]} with {mapped_offsets(bases[verdict.recipe], verdict.fields)}{extra}{vsa}"
    if cls == "VARIANT":
        return f"field-decode audit vs {verdict.recipe[:8]} (outputs {','.join(verdict.outputs) or '-'}){vsa}"
    if cls == "NEW":
        return "full recipe audit (" + verdict.term.split(";")[0].replace("sampling ", "") + ")"
    return "dump the FS (dump_shaders=1)"


def build(world_cache: str | None, no_world: bool, game: str, log=print) -> list[dict]:
    db = cov.Db(str(DB))
    tpairs, fs_templates = template_pairs()
    ev = frames.trace_evidence(log=log)
    share, scenes = frames.gbuffer_share(ev)
    gb = {k: v["gbuffer_draws"] for k, v in ev["pairs"].items()}
    lv = frames.log_vertices()
    if no_world:
        wd = {"worlds": {}, "sublevels": {}, "stats": {}}
    else:
        from tools.material_families import world
        wd = world.load_or_count(world_cache, game, log)
    allw, scene, unmapped = world_rows(wd, tpairs, gb)
    tot_c = sum(c for c, b in allw.values()) or 1.0
    tot_b = sum(b for c, b in allw.values()) or 1.0
    tot_s = sum(b for c, b in scene.values()) or 1.0
    rec = recipes()
    bases = {r["fs"]: r for r in rec if r["mode"] == "ReflectanceUbo" and not r["extra"]}
    base = set(bases)
    built = {(r["vs"], r["fs"]) for r in rec if r["capture"]}
    vs_ok = {r["vs"] for r in rec if r["capture"]}
    uncaptured = {(r["vs"], r["fs"]) for r in rec if not r["capture"]}
    src = Sources()
    rec_glsl = {h: src.glsl(h) for h in sorted(base)}
    rec_glsl = {h: t for h, t in rec_glsl.items() if t}
    keys = {k for k, v in ev["pairs"].items() if v["gbuffer_draws"]} | set(allw)
    verdicts = {}
    rows = []
    for vs, fs in keys:
        if fs not in verdicts:
            t = src.glsl(fs)
            verdicts[fs] = glsl.classify(fs, t, rec_glsl) if t and rec_glsl else None
        v = verdicts[fs]
        cls = v.cls if v else "NO DUMP"
        e = ev["pairs"].get((vs, fs), {"draws": 0, "gbuffer_draws": 0, "passes": set(), "scenes": set(), "captures": 0})
        c, b = allw.get((vs, fs), (0.0, 0.0))
        sc = scene.get((vs, fs), (0.0, 0.0))[1] / tot_s
        fsh = share.get((vs, fs), 0.0)
        lvx = lv.get((vs, fs), {})
        names = sorted(set(fs_templates.get(fs, [])))
        nm = db.name(fs) or (names[0] if names else "")
        row = {
            "vs": vs, "fs": fs, "name": nm, "templates": len(names), "class": cls,
            "recipe": v.recipe[:8] if v and v.cls != "SUPPORTED" else "", "term": v.term if v else "",
            "built": (vs, fs) in built, "vs_audited": vs in vs_ok,
            "world_objects": round(c), "world_share": c / tot_c, "world_geo_share": b / tot_b,
            "scene_geo_share": sc, "frame_share": fsh, "gbuffer_draws": e["gbuffer_draws"],
            "traces": e["captures"], "passes": "/".join(sorted(e["passes"])),
            "scenes": " ".join(sorted(s for s in e["scenes"] if re.fullmatch(r"S\d+", s))),
            "log_vertices": lvx.get("vertices", 0),
        }
        row["combined"] = (row["world_geo_share"] + row["scene_geo_share"] + row["frame_share"]) / 3
        row["next"] = next_step(cls, row["built"], row["vs_audited"], v, vs, bases, (vs, fs) in uncaptured) if v else next_step(cls, False, False, None, vs)
        rows.append(row)
    src.close()
    rows.sort(key=lambda r: -r["combined"])
    meta = {"traces": ev["traces"], "trace_scenes": scenes, "pairs": len(ev["pairs"]), "logs": len(lv),
            "world": wd.get("stats", {}), "unmapped": sorted(unmapped.items(), key=lambda x: -x[1])[:8],
            "engine": _engine_totals(wd), "recipes": sorted(base)}
    return rows, meta


def _engine_totals(wd):
    out = defaultdict(int)
    for per in wd["worlds"].values():
        for t, (c, b) in per.items():
            if t.startswith("engine:"):
                out[t] += c
    return dict(out)


def pct(x: float) -> str:
    return f"{100 * x:.1f}%" if x >= 0.0005 else ("<0.1%" if x > 0 else "0")


CSV_COLS = ["rank", "vs", "fs", "name", "templates", "class", "recipe", "term", "built", "vs_audited",
            "world_objects", "world_share", "world_geo_share", "scene_geo_share", "frame_share",
            "gbuffer_draws", "traces", "passes", "scenes", "log_vertices", "combined", "next"]


def write_csv(rows, path: Path, top: int):
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=CSV_COLS, lineterminator="\n")
        w.writeheader()
        for i, r in enumerate(rows[:top], 1):
            d = {k: r.get(k, "") for k in CSV_COLS}
            d["rank"] = i
            for k in ("world_share", "world_geo_share", "scene_geo_share", "frame_share", "combined"):
                d[k] = f"{r[k]:.5f}"
            w.writerow(d)


def markdown(rows, meta, top: int) -> str:
    covered = sum(r["combined"] for r in rows if r["built"])
    L = ["# Material families for the hit-material provider", "",
         "Generated by `python -m tools.material_families` (offline: asset DB, game packages, existing",
         "traces and logs; no game run). Regenerate after new traces or recipes.", "",
         "## Sources", "",
         f"- World: {meta['world'].get('objects', 0)} placed Scenery/Items objects over the 113 asset-DB worlds, "
         f"{meta['world'].get('resolved', 0)} resolved to a material template (visual -> LOD 0 material -> "
         "template -> Deferred ST/STI VS/FS pair from the asset DB). Geometry weight: GR2 uncompressed "
         "section bytes (all LODs), a triangle proxy; there is no triangle count without decoding GR2.",
         "- Test scenes: S1-S14 by sub-level (`SCENE_LEVELS` in `tools/material_families/cli.py`; S2, S3 and "
         "S10 use their whole world). Each scene weighs equally.",
         f"- Frames: {meta['traces']} traces ({', '.join(meta['trace_scenes'])}), {meta['pairs']} VS/FS pairs. "
         "Frame share = G-buffer draws, mean over traced S-scenes. Traces have no vertex counts; "
         f"`log vtx` = vertices of declined pairs from `[hit material families]` log lines ({meta['logs']} pairs, S8 only).",
         "- Class: the FS's SPIRV-Cross GLSL (captures/alpha_cache) normalized (`glsl.py`: resources by "
         "kind/set/ordinal, I/O by location, value-numbered temporaries) against the audited recipes "
         f"{', '.join(x[:8] for x in meta['recipes'])}. COPY = differences reach one output (one term); "
         "VARIANT = same sampling, other field decode; NEW = other sampling.",
         "- combined = mean(world geo share, test-scene geo share, frame draw share). "
         f"Pairs already built (native_vt_surface.h) hold {pct(covered)} of it.",
         f"- Engine families (not ranked, other units): " +
         ", ".join(f"{k[7:]} {v} objects" for k, v in meta["engine"].items()) + ".", "",
         f"## Top {top}", "",
         "| # | VS/FS | Template (canonical) | Class | World geo | Scenes geo | Frames | Draws / traces | Log vtx | Combined | Next step |",
         "|---|---|---|---|---|---|---|---|---|---|---|"]
    for i, r in enumerate(rows[:top], 1):
        cls = r["class"] + (f" of {r['recipe']}" if r["recipe"] and r["class"] != "NEW" else "") + (" (built)" if r["built"] and r["class"] != "SUPPORTED" else "")
        name = r["name"] + (f" (+{r['templates'] - 1})" if r["templates"] > 1 else "")
        L.append(f"| {i} | {r['vs'][:8]}/{r['fs'][:8]} | {name} | {cls} | {pct(r['world_geo_share'])} | "
                 f"{pct(r['scene_geo_share'])} | {pct(r['frame_share'])} | {r['gbuffer_draws']} / {r['traces']} | "
                 f"{r['log_vertices'] // 1000}k | {pct(r['combined'])} | {r['next']} |".replace("| 0k |", "|  |"))
    L += ["", "Differing terms of the COPY and VARIANT rows (recipe -> shader):", ""]
    for r in rows[:top]:
        if r["class"] in ("COPY", "VARIANT"):
            L.append(f"- {r['vs'][:8]}/{r['fs'][:8]} {r['class']} of {r['recipe']}: {r['term']}")
    todo = [r for r in rows if not r["built"] and r["class"] != "SUPPORTED"]
    cheap = [r for r in todo if r["class"] == "COPY" or r["term"].startswith("fields only")]
    L += ["", "## Next (generated: not built, by combined share)", ""]
    if todo:
        r = todo[0]
        L.append(f"- Largest: {r['vs'][:8]}/{r['fs'][:8]} {r['name']} ({r['class']}, {pct(r['combined'])}): {r['next']}.")
    for r in cheap[:3]:
        L.append(f"- Cheap: {r['vs'][:8]}/{r['fs'][:8]} {r['name']} ({r['class']}, {pct(r['combined'])}): {r['next']}.")
    L += ["", "Caveats: world and scene shares count placements, not visibility (HLOD proxies, characters and "
          "decals are not placed objects; TileBlend pairs are placed but never traced as these pairs). "
          "A template's placements split between its ST and STI pairs by traced draws. Sub-level choice for "
          "the scenes is by name. Frame shares come from S1, S3, S6, S7 and S8 traces only."]
    if meta["unmapped"]:
        L += ["", "Most placed templates without a Deferred pair in the asset DB: " +
              ", ".join(f"{t} ({c})" for t, c in meta["unmapped"]) + "."]
    return "\n".join(L) + "\n"


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(prog="python -m tools.material_families")
    ap.add_argument("--world-cache", help="JSON cache of the world count (read if present, else written)")
    ap.add_argument("--no-world", action="store_true")
    ap.add_argument("--game", default=os.environ.get("BG3_DIR", r"D:\SteamLibrary\steamapps\common\Baldurs Gate 3"))
    ap.add_argument("--md", default=str(ROOT / "docs" / "MATERIAL_FAMILIES.md"))
    ap.add_argument("--csv", default=str(ROOT / "docs" / "evidence" / "material-families.csv"))
    ap.add_argument("--top", type=int, default=30)
    ap.add_argument("--csv-top", type=int, default=100)
    a = ap.parse_args(argv)
    rows, meta = build(a.world_cache, a.no_world, a.game)
    md = markdown(rows, meta, a.top)
    Path(a.md).write_text(md, encoding="utf-8", newline="\n")
    write_csv(rows, Path(a.csv), a.csv_top)
    print(md)
    print(f"wrote {a.md} and {a.csv}")


if __name__ == "__main__":
    main()
