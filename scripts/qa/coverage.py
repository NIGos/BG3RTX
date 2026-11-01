"""Rendering coverage matrix: what the game draws, per pass and shader family, against what
our ray tracing handles (docs/COVERAGE.md).

    python -m scripts.qa.coverage                       every capture found (main + worktrees + QA)
    python -m scripts.qa.coverage captures/2026...      these captures (folders with frame.json)
    python -m scripts.qa.coverage --md out.md --json out.json

Per capture (frame.json, plus dumps.json and img/ when present):
- each render pass is classified (depth prepass, G-buffer, decals, motion vectors, shadow
  maps, effects/emissive target, SSS, forward/transparent, UI, post) from its attachment
  formats and its draws' fragment shaders, named through the asset DB
  (data/assetdb, `python -m tools.assets build`);
- each draw gets a family (water, lava, glass, VFX, decal, emissive, G-buffer class, ...);
- the screen share of a pass is measured from the dumps: pixels that differ from the
  previous dump of the same image (or are non-zero in a fresh target); the G-buffer's
  material classes come from RT2.a & 7, sky from the G-buffer depth = 0.
The scene comes from run.json, the QA folder name, the queue's results.jsonl or
captures/scene-sweep.json. The "handled" columns are the layer's state as documented in
TRACK_D_RT.md (see HANDLING below; update it when an effect changes).
Read-only on the captures.
"""
import argparse
import glob
import json
import os
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# A worktree's captures/ and assetdb live in the main checkout.
MAIN = ROOT.parents[2] if ROOT.parent.name == "worktrees" else ROOT
sys.path.insert(0, str(ROOT))

# VkFormat ids seen in the frame map
F_RG16F, F_RGBA8, F_B10G11R11, F_D24S8, F_D16, F_D32 = 83, 37, 122, 129, 124, 126
F_RGB10A2, F_RGBA16F, F_R8, F_R16F, F_R32F = 64, 97, 9, 76, 100

SSS_FS = {"6f535075", "d1669194"}
COMPOSE_FS = {"5c3fdabc"}   # compose: lit diffuse + specular + effects target (AUDIT_ENGINE_DATA R5)
SKY_FS = {"c9f013fc": "sky", "68d5b764": "clouds"}

# Our coverage per family (main d9234f5, 2026-11-01; TRACK_D 9.10, 10, 12, 13, TRACK_D_EMISSIVE,
# PLAN_M4_REFLECTIONS_GI, PLAN_M5_WATER). Columns: caster (in the TLAS), sun, local (D-LS),
# gi (receives RT GI / AO), refl (D-RF on it), em (its emission captured by D-EM).
# "key": implemented in main behind an experimental key that is off by default (HANDLING_KEYS
# names it); it counts as handled only once the key is qualified and on in a preset.
Y, N, NA, PART, KEY = "yes", "no", "n/a", "partial", "key"
HANDLING_DATE = "2026-11-01 (main d9234f5)"
HANDLING = {
    "G-buffer: standard": (Y, Y, Y, Y, Y, NA),
    "G-buffer: unlit": (Y, NA, NA, NA, NA, N),
    "G-buffer: anisotropic": (Y, Y, Y, Y, Y, NA),
    "G-buffer: skin": (Y, Y, Y, Y, Y, NA),
    "G-buffer: thin translucency": (PART, Y, Y, Y, Y, NA),
    "G-buffer: hair": (Y, Y, PART, Y, Y, NA),
    "G-buffer: cloth": (Y, Y, Y, Y, Y, NA),
    "G-buffer: cloth/GGX": (Y, Y, Y, Y, Y, NA),
    "G-buffer: empty (sky or forward-only)": (NA, NA, NA, NA, NA, N),
    "decal": (NA, Y, Y, Y, Y, N),
    "effects: emissive (EMI)": (NA, NA, NA, NA, NA, KEY),
    # Counted by D-EM, not captured: a box projected onto the surface, not geometry.
    "effects: emissive decal": (NA, NA, NA, NA, NA, N),
    "effects: sky/clouds": (NA, NA, NA, NA, NA, N),
    "effects: backdrop": (N, NA, NA, NA, NA, N),
    "effects: VFX plane": (N, N, N, N, N, N),
    # M5.1 builds a water TLAS of its own that only the water passes trace: not a caster.
    "forward: water": (N, KEY, N, N, KEY, NA),
    # Emissive forward materials (ENV_Lava StaticInstancedForward) are captured as the
    # approximate "forward" kind; the forward pass itself gets no RT lighting.
    "forward: lava/emissive": (N, N, N, N, N, KEY),
    "forward: glass/refractive": (N, N, N, N, N, NA),
    "forward: VFX/particles": (N, N, N, N, N, N),
    "forward: transparent other": (N, N, N, N, N, N),
    "forward: unknown FS": (N, N, N, N, N, N),
    "SSS blur": (NA, NA, NA, NA, NA, NA),
    "UI": (NA, NA, NA, NA, NA, NA),
}
# The off-by-default keys behind the "key" cells, and the experimental keys that refine a
# family's "yes" (all 0 in bg3rtx.ini and in every preset on 2026-11-01).
HANDLING_KEYS = {
    "G-buffer: standard": "native materials at GI/reflection hits: rt_hit_material, rt_hit_material_instanced "
                          "(wind and two-sided alpha-test rows), gi_hit_direct, gi_hit_sun, rf_hit_shade; emission at "
                          "hits: rt_hit_emission; cut-away roofs and off-screen casters for the sun: rt_cascade_casters",
    "effects: emissive (EMI)": "rt_emissive (capture), rt_hit_emission (added at GI/reflection hits)",
    "forward: lava/emissive": "rt_emissive (capture, approximate forward kind), rt_hit_emission",
    "forward: water": "M5.1 rt_water_geometry, M5.2 rt_water_reflections, M5.3 rt_water_compose (refl), "
                      "M5.4 rt_water_shadows (sun)",
}
BEGIN, END = "<!-- coverage:begin -->", "<!-- coverage:end -->"
GCLASS = ["standard", "unlit", "anisotropic", "skin", "thin translucency", "hair", "cloth", "cloth/GGX"]


class Db:
    """Fragment-shader hash -> (template, variant, class, flags) or engine name."""

    def __init__(self, path):
        self.shd, self.eng, self.ok = {}, {}, False
        if not path or not os.path.exists(path):
            return
        from tools.assets.dbformat import Reader
        from tools.assets.extract.materials import CLASSES, FLAG_NAMES, VARIANTS
        r = Reader(open(path, "rb").read())
        tpl, var, sref = r.rows("MTPL"), r.rows("MVAR"), r.rows("SREF")
        for row in r.rows("ESHD"):
            self.eng[f"{row[0]:016x}"] = r.string(row[1])
        for h, first, cnt, _stage, _pss, canon, _ in r.rows("SHDR"):
            k = first + (canon if 0 <= canon < cnt else 0)
            v = var[sref[k][0]]
            t = tpl[v[0]]
            flags = {n for b, n in enumerate(FLAG_NAMES) if t[1] >> b & 1}
            self.shd[f"{h:016x}"] = (r.string(t[0]), VARIANTS[v[1]], CLASSES[t[2]], flags)
        self.ok = True

    def name(self, fs):
        if fs in self.shd:
            t, v, c, _ = self.shd[fs]
            return f"{t}:{v}"
        return self.eng.get(fs, "")


def family(db, fs, kind):
    """The coverage family of one draw in a pass of this kind."""
    if fs[:8] in SKY_FS:
        return "effects: sky/clouds"
    rec = db.shd.get(fs)
    if kind == "effects":
        if not rec:
            return "effects: VFX plane" if not db.eng.get(fs) else "effects: sky/clouds"
        t, v, c, fl = rec
        tl = t.lower()
        if "skydome" in tl:
            return "effects: backdrop"
        if "decal" in fl or c == "decal":
            return "effects: emissive decal"
        return "effects: emissive (EMI)"
    if kind == "forward":
        if not rec:
            return "forward: unknown FS"
        t, v, c, fl = rec
        tl = t.lower()
        if "water" in fl or c == "water" or "fluid_water" in tl or "water" in tl:
            if "lava" not in tl:
                return "forward: water"
        if "lava" in tl or ("emissive" in fl and "vfx" not in fl and "vfx" not in tl):
            return "forward: lava/emissive"
        if "vfx" in fl or "vfx" in tl or "particle" in tl or "fx_" in tl:
            return "forward: VFX/particles"
        if c == "refractive" or "refractive" in fl or "glass" in tl:
            return "forward: glass/refractive"
        return "forward: transparent other"
    return kind


def scene_map():
    """Capture folder name -> save, from scene-sweep.json, results.jsonl and job files."""
    m = {}
    sw = MAIN / "captures" / "scene-sweep.json"
    if sw.exists():
        for r in json.loads(sw.read_text(encoding="utf-8-sig")):
            if r.get("Capture"):
                m[r["Capture"]] = r["Save"]
    jobs = {}
    for f in glob.glob(str(MAIN / "captures" / "queue" / "done" / "*.json")):
        try:
            j = json.loads(open(f, encoding="utf-8-sig").read())
            jobs[j.get("name") or Path(f).stem] = j.get("save", "")
        except Exception:
            pass
    res = MAIN / "captures" / "queue" / "results.jsonl"
    if res.exists():
        for line in res.read_bytes().decode("utf-8", "replace").splitlines():
            try:
                r = json.loads(line)
            except Exception:
                continue
            # A run whose log shows the 3D menu but never a loading screen took its traces in
            # the menu (the mod-warning diagnostics): they are not the scene.
            lines = " ".join(r.get("lines") or [])
            menu_only = "3D scene first seen" in lines and "loading screen after" not in lines
            for c in r.get("captures") or []:
                if menu_only:
                    MENU_ONLY.add(Path(c).name)
                elif jobs.get(r.get("job")):
                    m[Path(c).name] = jobs[r["job"]]
    return m


MENU_ONLY = set()


def scene_ids():
    text = (ROOT / "scripts" / "scene-sweep.ps1").read_text(encoding="utf-8")
    return dict(re.findall(r'(S\d+) = "([^"]+)"', text))


def scene_of(cap: Path, smap, sids):
    save = ""
    rj = cap / "run.json"
    if rj.exists():
        try:
            save = json.loads(rj.read_text(encoding="utf-8-sig")).get("save", "") or ""
        except Exception:
            pass
    if not save and re.fullmatch(r"S\d+", cap.name):
        return cap.name
    if not save and re.fullmatch(r"S\d+", cap.parent.name):
        return cap.parent.name
    save = save or smap.get(cap.name, "")
    if not save:
        return "?"
    for sid, sub in sids.items():
        if sub in save or save in sub:
            return sid
    # Different substrings of the same save ("che Yllek" vs "Yllek - 40h"): the folder.
    for folder in _save_folders():
        if save in folder:
            for sid, sub in sids.items():
                if sub in folder:
                    return sid
    return save


_FOLDERS = None


def _save_folders():
    global _FOLDERS
    if _FOLDERS is None:
        story = os.path.expandvars(r"%LOCALAPPDATA%\Larian Studios\Baldur's Gate 3\PlayerProfiles\Public\Savegames\Story")
        _FOLDERS = sorted(os.listdir(story)) if os.path.isdir(story) else []
    return _FOLDERS


def load_frame(cap: Path):
    for name in ("frame.json", "frame_on0.json"):
        p = cap / name
        if p.exists():
            try:
                return json.loads(p.read_text(encoding="utf-8", errors="replace"))
            except Exception:
                return None
    return None


def passes_of(frame):
    """Render passes in order: attachments, draws, end dumps; plus compute dispatches."""
    imgs = frame.get("images", {})
    out, cur, order = [], None, 0
    for s in frame.get("submits", []):
        for c in s.get("cmds", []):
            for e in c.get("events", []):
                op = e.get("op")
                if op == "pass":
                    cur = {"i": order, "w": e.get("w"), "h": e.get("h"),
                           "color": [imgs.get(x, {}).get("fmt") for x in e.get("color", [])],
                           "color_ids": e.get("color", []), "depth": e.get("depth"),
                           "depth_fmt": imgs.get(e.get("depth") or "", {}).get("fmt"), "draws": [], "dumps": []}
                    order += 1
                elif op == "draw" and cur is not None:
                    cur["draws"].append((e.get("fs") or "", e.get("vs") or "", int(e.get("n", 1))))
                elif op == "end" and cur is not None:
                    cur["dumps"] = e.get("dumps", [])
                    out.append(cur)
                    cur = None
    return out


def classify(p, render, output, after_tonemap):
    col, w, h = p["color"], p["w"], p["h"]
    nd = sum(n for _, _, n in p["draws"])
    fs = {f[:8] for f, _, _ in p["draws"]}
    if fs & COMPOSE_FS:
        return "compose"
    if not col:
        if p["depth"] and (w, h) == render:
            return "depth prepass" if nd >= 64 else "depth (other)"
        return "shadow map" if p["depth"] else "other"
    if len(col) >= 5 and col[0] == F_RG16F and col[1:5] == [F_RGBA8] * 4:
        return "G-buffer"
    if len(col) == 4 and col[0] == F_RG16F and col[1:4] == [F_RGBA8] * 3:
        return "decal"
    if col == [F_RG16F] and p["depth"] and (w, h) == render:
        return "motion vectors"
    if col == [F_B10G11R11] and p["depth"] and (w, h) == render:
        if fs & SSS_FS:
            return "SSS blur"
        if fs & COMPOSE_FS:
            return "compose"
        return "b10"  # effects or forward: decided by the draws
    if w > render[0] and after_tonemap and F_RGBA8 in col:
        return "UI"
    return "post/other"


def dump_share(cap, dumps, did, prev, fresh_ok, kind):
    """Fraction of pixels a pass changed in this dump's image (vs its previous dump)."""
    try:
        import numpy as np
        from PIL import Image
    except ImportError:
        return None
    d = dumps.get(did)
    if not d or d.get("slot") == "depth":
        return None
    f = cap / d["png"]
    if not f.exists():
        return None
    a = np.asarray(Image.open(f).convert("RGB"), dtype=np.int16)
    p, pk = prev.get(d["image"], (None, None))
    prev[d["image"]] = (a, kind)
    # Only against the same image's content from a pass of the same chain: the SSS passes
    # use the later compose target as scratch, so a diff against them means nothing.
    if p is not None and p.shape == a.shape and pk not in ("SSS blur", "post/other", None):
        return float((np.abs(a - p).max(axis=2) > 3).mean())
    if p is not None and not fresh_ok:
        return None
    # No earlier dump of this image: only a freshly cleared target (the effects target)
    # can be measured as "non-zero".
    return float((a.max(axis=2) > 3).mean()) if fresh_ok else None


def gbuffer_classes(cap, dumps, ids):
    """Share of G-buffer pixels per material class (RT2.a & 7), sky from depth = 0."""
    try:
        import numpy as np
        from PIL import Image
    except ImportError:
        return {}
    rt2 = [dumps[i] for i in ids if i in dumps and dumps[i].get("slot") == 2]
    dep = [dumps[i] for i in ids if i in dumps and dumps[i].get("slot") == "depth"]
    if not rt2:
        return {}
    ap = cap / rt2[0]["png"].replace(".png", "_a.png")
    if not ap.exists():
        mean = rt2[0].get("mean", [0, 0, 0, 0])
        return {"G-buffer: standard": 1.0} if len(mean) > 3 and mean[3] * 255 < 0.5 else {}
    cls = np.asarray(Image.open(ap).convert("L"), dtype=np.uint8) & 7
    sky = np.zeros(cls.shape, bool)
    if dep and (dep[0].get("min") or [1])[0] == 0 and (cap / dep[0]["png"]).exists():
        dimg = np.asarray(Image.open(cap / dep[0]["png"]).convert("L"))
        if dimg.shape == cls.shape:
            sky = dimg == 0
    out = {"G-buffer: empty (sky or forward-only)": float(sky.mean())}
    for k in range(8):
        v = float(((cls == k) & ~sky).mean())
        if v > 0:
            out[f"G-buffer: {GCLASS[k]}"] = v
    return out


def analyse(cap: Path, db: Db, smap, sids, measure=False):
    frame = load_frame(cap)
    if not frame:
        return None
    dumps = {}
    if measure and (cap / "dumps.json").exists():
        try:
            dumps = json.loads((cap / "dumps.json").read_text(encoding="utf-8"))
        except Exception:
            dumps = {}
    ps = passes_of(frame)
    if not ps:
        return None
    sizes = Counter((p["w"], p["h"]) for p in ps if p["color"])
    gb = [p for p in ps if len(p["color"]) >= 5 and p["color"][0] == F_RG16F]
    render = (gb[0]["w"], gb[0]["h"]) if gb else sizes.most_common(1)[0][0]
    output = max(sizes, key=lambda s: s[0] * s[1])
    rows = defaultdict(lambda: {"draws": 0, "fs": set(), "share": None, "passes": 0})
    kinds = []
    prev = {}
    seen_gbuffer = after_tonemap = False
    effects_img = None
    for p in ps:
        if p["w"] > render[0] and p["color"] and p["color"][0] == F_RGB10A2:
            after_tonemap = True   # the tonemap at output resolution (FRAME_MAP step 16)
        k = classify(p, render, output, after_tonemap)
        if k == "G-buffer":
            seen_gbuffer = True
        if k == "b10":
            fams = Counter(family(db, f, "effects") for f, _, _ in p["draws"])
            emi = any(f in db.shd and db.shd[f][1].endswith("Emissive") for f, _, _ in p["draws"])
            # the effects target: EMI draws, or sky/clouds, before the forward pass
            if emi or (fams.get("effects: sky/clouds") and not effects_img):
                k = "effects"
                effects_img = effects_img or (p["color_ids"] or [None])[0]
            elif p["color_ids"] and p["color_ids"][0] == effects_img:
                k = "effects"
            else:
                k = "forward" if seen_gbuffer and p["draws"] else "post/other"
        share = None
        for did in p["dumps"] if k not in ("UI", "SSS blur") else []:
            s = dump_share(cap, dumps, did, prev, k == "effects", k)
            if s is not None:
                share = s if share is None else max(share, s)
        nd = sum(n for _, _, n in p["draws"])
        kinds.append({"pass": p["i"], "kind": k, "w": p["w"], "h": p["h"], "draws": nd, "share": share})
        if k in ("effects", "forward"):
            per = defaultdict(int)
            for f, _, n in p["draws"]:
                fam = family(db, f, k)
                per[fam] += n
                rows[fam]["fs"].add(f[:8])
            for fam, n in per.items():
                rows[fam]["draws"] += n
                rows[fam]["passes"] += 1
            # a pass's share goes to a family only when it has >= 90% of the pass's draws
            if share is not None and per:
                big = max(per, key=per.get)
                if per[big] >= 0.9 * sum(per.values()):
                    rows[big]["pass_share"] = max(rows[big].get("pass_share") or 0, share)
        elif k == "G-buffer":
            for fam, v in gbuffer_classes(cap, dumps, p["dumps"]).items():
                rows[fam]["share"] = v
            rows["G-buffer: standard"]["draws"] += nd
            rows["G-buffer: standard"]["fs"] |= {f[:8] for f, _, _ in p["draws"]}
        elif k in ("decal", "SSS blur", "UI"):
            rows[k]["draws"] += nd
            rows[k]["passes"] += 1
            rows[k]["fs"] |= {f[:8] for f, _, _ in p["draws"]}
            if share is not None and k != "SSS blur":
                rows[k]["pass_share"] = max(rows[k].get("pass_share") or 0, share)
    geo = None
    gp = cap / "geo" / "report.json"
    if gp.exists():
        try:
            g = json.loads(gp.read_text(encoding="utf-8"))
            dep = g.get("depth", {})
            geo = {"coverage": dep.get("coverage"), "missing": (dep.get("missing", 0) / dep["geometry"]) if dep.get("geometry") else None}
        except Exception:
            pass
    return {"capture": str(cap), "scene": scene_of(cap, smap, sids), "render": render, "output": output,
            "has_dumps": (cap / "dumps.json").exists(), "measured": bool(dumps), "passes": kinds, "families": {k: {**v, "fs": sorted(v["fs"])} for k, v in rows.items()},
            "geo": geo}


def find_captures():
    pats = [MAIN / "captures" / "2026*", MAIN / ".claude" / "worktrees" / "*" / "captures" / "2026*",
            MAIN / "captures" / "qa" / "*" / "S*"]
    out = []
    for p in pats:
        for d in sorted(glob.glob(str(p))):
            d = Path(d)
            if d.name in MENU_ONLY:
                continue
            if (d / "frame.json").exists() or (d / "frame_on0.json").exists():
                out.append(d)
    return out


def pct(v):
    return "" if v is None else (f"{100 * v:.1f}%" if v >= 0.001 else ("<0.1%" if v > 0 else "0%"))


def rep_key(r):
    """Representative capture of a scene: measured, then the main checkout's (baseline
    sweeps, not a branch's debug runs), then the newest."""
    c = r["capture"]
    return (not r.get("measured"), not r["has_dumps"], ".claude" in c, "qa" in Path(c).parts,
            -int(re.sub(r"\D", "", Path(c).name)[:14] or 0))


def _cell(vals):
    """Median share over the measured captures, with the maximum when it is far above."""
    vals = sorted(v for v in vals if v is not None)
    if not vals:
        return ""
    med = vals[len(vals) // 2]
    out = pct(med)
    if len(vals) > 1 and vals[-1] > 2 * med + 0.001:
        out += f" (max {pct(vals[-1])})"
    return out


def to_markdown(results):
    by_scene = defaultdict(list)
    for r in results:
        by_scene[r["scene"]].append(r)
    scenes = sorted(by_scene, key=lambda s: (not s.startswith("S"), int(s[1:]) if re.fullmatch(r"S\d+", s) else 0, s))
    lines = ["Families: the handling columns, then per scene the median screen share over the measured",
             "captures (max in brackets when much higher) and the most draws seen in one frame.", "",
             "| Family | caster | sun | local | GI | refl | D-EM | " + " | ".join(scenes) + " |",
             "|---|---|---|---|---|---|---|" + "---|" * len(scenes)]
    for fam in HANDLING:
        cells, seen = [], False
        for s in scenes:
            vals = [r["families"].get(fam) for r in by_scene[s]]
            vals = [v for v in vals if v]
            if not vals:
                cells.append("")
                continue
            seen = True
            meas = [r["families"].get(fam) or {} for r in by_scene[s] if r.get("measured")]
            shares = [v.get("share") if v.get("share") is not None else v.get("pass_share") for v in meas]
            tag = _cell(shares)
            d = max(x["draws"] for x in vals)
            if fam.startswith("G-buffer:") and fam != "G-buffer: standard":
                cells.append(tag)
            else:
                cells.append(f"{tag} ({d} dr)".strip())
        if seen:
            lines.append(f"| {fam} | " + " | ".join(HANDLING[fam]) + " | " + " | ".join(cells) + " |")
    lines += ["", f"Handling columns as of {HANDLING_DATE}. \"key\": implemented behind an experimental key,",
              "off by default. Keys per family:", ""]
    lines += [f"- {fam}: {keys}" for fam, keys in HANDLING_KEYS.items()]
    kinds = ["decal", "effects", "forward"]
    lines += ["", "Pixels changed per pass kind (median over the measured captures, sum over that",
              "kind's passes; max in brackets when much higher):", "",
              "| Pass kind | " + " | ".join(scenes) + " |", "|---|" + "---|" * len(scenes)]
    for k in kinds:
        cells = []
        for s in scenes:
            vals = []
            for r in by_scene[s]:
                if not r.get("measured"):
                    continue
                sh = [p["share"] for p in r["passes"] if p["kind"] == k and p["share"] is not None]
                vals.append(min(1.0, sum(sh)) if sh else None)
            cells.append(_cell(vals))
        lines.append(f"| {k} | " + " | ".join(cells) + " |")
    caps = ["", "Captures (all: draws; measured: pixel shares):", ""]
    for s in scenes:
        rs = sorted(by_scene[s], key=rep_key)
        m = [os.path.relpath(r["capture"], MAIN) for r in rs if r.get("measured")]
        caps.append(f"- {s}: {len(rs)} capture(s), measured: " + (", ".join(f"`{x}`" for x in m) or "none"))
    return "\n".join(lines + caps) + "\n"


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(prog="python -m scripts.qa.coverage")
    ap.add_argument("captures", nargs="*")
    ap.add_argument("--db", default=str(MAIN / "data" / "assetdb" / "bg3rtx_assetdb.bin"))
    ap.add_argument("--md")
    ap.add_argument("--json")
    ap.add_argument("--measure", type=int, default=4, help="captures per scene whose dumps are measured")
    ap.add_argument("--update-doc", action="store_true",
                    help="replace the generated block of docs/COVERAGE.md (between the coverage markers)")
    a = ap.parse_args()
    db = Db(a.db)
    if not db.ok:
        print(f"note: no asset DB at {a.db}: draws are not named (python -m tools.assets build)")
    smap, sids = scene_map(), scene_ids()   # first: it fills MENU_ONLY for find_captures
    caps = [Path(c) for c in a.captures] or find_captures()
    results = [r for r in (analyse(c, db, smap, sids) for c in caps) if r]
    # Pixel shares (slow: PNG decodes) for up to --measure captures per scene.
    by = defaultdict(list)
    for r in results:
        if r["has_dumps"]:
            by[r["scene"]].append(r)
    for sc, rs in by.items():
        for r in sorted(rs, key=rep_key)[:a.measure]:
            m = analyse(Path(r["capture"]), db, smap, sids, measure=True)
            if m:
                results[results.index(r)] = m
    md = to_markdown(results)
    print(md)
    if a.md:
        Path(a.md).write_text(md, encoding="utf-8")
    if a.update_doc:
        doc = ROOT / "docs" / "COVERAGE.md"
        t = doc.read_text(encoding="utf-8")
        i, j = t.find(BEGIN), t.find(END)
        if i < 0 or j < i:
            sys.exit(f"{doc}: markers {BEGIN} / {END} not found")
        stamp = f"Generated by `python -m scripts.qa.coverage --update-doc` from {len(results)} captures.\n\n"
        doc.write_text(t[:i + len(BEGIN)] + "\n" + stamp + md + t[j:], encoding="utf-8", newline="\n")
        print(f"updated {doc}")
    if a.json:
        Path(a.json).write_text(json.dumps(results, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
