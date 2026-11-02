"""Generated hit-material recipe rows (rt_hit_material_generated; docs/GAME_DATA_AUDIT.md action 2). Offline, no game.

    py -3 -m tools.material_families.recipe_rows            # rewrite common/native_hit_generated.h, print the census
    py -3 -m tools.material_families.recipe_rows --check    # exit 1 when the header is not what the inputs give

A row is emitted only when three independent sources agree; everything else is listed with its reason, never guessed:
1. The asset DB (data/assetdb, from the paks): the template's StaticInstancedDeferred VS/FS, its StaticInstancedDepth VS
   (the prepass the row's BVH geometry comes from), the engine UBO offsets of VirtualTexture_CB_Texture (UV transform),
   VirtualTexture_TilesetDataIndex and LightChannel, the material UBO offset of Reflectance (no material UBO: none), and
   the bindings of the VT table / normal / albedo / physical layers.
2. The FS classifier (glsl.py) against the audited recipes of common/native_vt_surface.h: the FS must be one of them
   modulo names and bindings, or differ only by (a) moved UBO fields ("fields only"), (b) the reflectance read from
   another 1:1 member, (c) RT2.y the constant 0.5 instead of 1:1 (no material UBO), (d) the RT2.w flag bit 3 dropped
   (a G-buffer flag no hit row decodes). The DB offsets must equal the classifier's field map of the base's offsets.
3. The capture path, from the VS GLSL: pool = the G-buffer VS of an audited LayoutBase pool recipe (class A 2d2ac3c3);
   instanced = an audited bit-exact family of native_hit_instanced.h (2S, wind RGB, IP grass; not the RG wind family,
   whose rows use a derived tolerance) when either (R1) the template's depth VS IS the family's depth VS and its
   G-buffer VS computes gl_Position with the same normalized statements as the family's G-buffer VS, or (R2) its own
   depth and G-buffer VSs compute gl_Position with the same statements and read the same uniform bindings as the
   family's pair (the family's pairing certificate then covers every input). The UV/N/B/T output locations (the XFB
   frame patch) come from the G-buffer VS's interface. Rows stay fail closed at the hit: the consumer checks every
   vertex against the native depth-pass position and UV bit for bit.
Pool rows are one-sided only (the pool consumer has no two-sided flip); instanced rows support UBO and constant
reflectance (the per-draw gather reads one 1:1 float), pool rows also the normal-mix mode.
"""
from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.material_families import cli, glsl  # noqa: E402

HEADER = ROOT / "common" / "native_hit_generated.h"
INSTANCED_H = ROOT / "common" / "native_hit_instanced.h"
POOL_VS = {"2d2ac3c38992b7f0"}   # LayoutBase pool recipes' class A VS (54df894a / c32ce535 are detail / iridescent only)
BIT_EXACT_FAMILIES = {"AlphaTest2s", "WindRgb", "IpGrass"}
LAYOUTS = {(3, 4, 5, 6): "LayoutBase", (4, 5, 6, 7): "LayoutVt4"}   # 1:b of table, normal, albedo, physical
FLAG_TERM = re.compile(r"^out2: `\(0u \| uint\(int\(ubo1_0\._m(?:\d+| \?) != 0u\)<< 3\)\)` -> `0u`$")
CONST_TERM = re.compile(r"^out2: `clamp\(ubo1_1\._m\d+, 0\.0, 1\.0\)` -> `0\.5`(; decls: -ubo1_1)?$")
REFL_TERM = re.compile(r"^out2: `_m(\d+)` -> `_m(\d+)`(; decls: ~ubo1_1)?$")


def family_pairs(path: Path = INSTANCED_H) -> list[dict]:
    """native_hit_instanced.h kPairs: depth VS, G-buffer VS, FS, family name."""
    t = path.read_text(encoding="utf-8")
    body = t[t.index("kPairs[] = {"):]
    body = body[:body.index("};")]
    return [{"depth_vs": a, "vs": b, "fs": c, "family": f}
            for a, b, c, f in re.findall(r"\{0x([0-9a-f]{16})ull, 0x([0-9a-f]{16})ull, 0x([0-9a-f]{16})ull, (\w+)\}", body)]


def db_templates(db_path: Path = cli.DB) -> dict:
    """{template name: {"vs", "fs", "depth_vs", "depth_fs", "params": [(role, kind, set, binding, offset)], "mcb": bytes,
    "alpha": bool}} of every template with a StaticInstancedDeferred variant."""
    from tools.assets.dbformat import Reader
    from tools.assets.extract.materials import FLAG_NAMES, KINDS, ROLES, VARIANTS
    r = Reader(db_path.read_bytes())
    tpl, var, prm, sref = r.rows("MTPL"), r.rows("MVAR"), r.rows("MPRM"), r.rows("SREF")
    stages = defaultdict(dict)
    for h, first, cnt, stage, *_ in r.rows("SHDR"):
        for i in range(cnt):
            v = var[sref[first + i][0]]
            stages[(v[0], VARIANTS[v[1]])][stage] = f"{h:016x}"
    var_of = {(v[0], VARIANTS[v[1]]): v for v in var}
    alpha_bit = 1 << FLAG_NAMES.index("alpha_test")
    out = {}
    for ti, t in enumerate(tpl):
        d, dep = stages.get((ti, "StaticInstancedDeferred"), {}), stages.get((ti, "StaticInstancedDepth"), {})
        v = var_of.get((ti, "StaticInstancedDeferred"))
        if 0 not in d or 4 not in d or v is None:
            continue
        params = [(ROLES[p[0]], KINDS[p[1]], p[2], p[3], p[4]) for p in prm[v[10]:v[10] + v[7]]]
        out[r.string(t[0])] = {"vs": d[0], "fs": d[4], "depth_vs": dep.get(0, ""), "depth_fs": dep.get(4, ""),
                               "params": params, "mcb": v[9], "alpha": bool(t[1] & alpha_bit)}
    return out


def db_fields(params, mcb: int) -> dict | None:
    """Offsets and layout from the DB's variant parameters (None: a role is missing or the layout is not audited)."""
    by = defaultdict(list)
    for role, kind, s, b, off in params:
        by[role].append((kind, s, b, off))
    try:
        (t,), (i,), (c,) = by["vt_uv_transform"], by["vt_tileset_index"], by["light_channel"]
        layers = tuple(by[k][0][2] for k in ("vt_table", "vt_normal", "vt_albedo", "vt_physical"))
    except (ValueError, IndexError, KeyError):
        return None
    if any(x[1:3] != (1, 0) for x in (t, i, c)) or any(by[k][0][1] != 1 for k in ("vt_table", "vt_normal", "vt_albedo", "vt_physical")):
        return None
    refl = [x for x in by.get("reflectance", []) if x[0] == "scalar" and x[1:3] == (1, 1)]
    return {"transform": t[3], "tileset": i[3], "channel": c[3], "layout": LAYOUTS.get(layers),
            "reflectance": refl[0][3] if len(refl) == 1 else None, "mcb": mcb}


_SET = re.compile(r"layout\(set = (\d+), binding = (\d+)")
_IOV = re.compile(r"layout\(location = (\d+)\)\s+(in|out)\s+(\w+)\s+\w+;")


def vs_facts(text: str) -> dict:
    """gl_Position backward slice (normalized statements), uniform bindings, output interface of a VS."""
    sh = glsl.normalize(text)
    defs = defaultdict(list)
    for s in sh.stmts:
        lhs = glsl._lhs(s)
        if lhs:
            defs[lhs].append(s)
    out, need, seen = [], [], set()
    for s in sh.stmts:
        if s.startswith("gl_Position"):
            out.append(s)
            need += glsl._vars(s.split("=", 1)[1])
    while need:
        v = need.pop()
        if v in seen:
            continue
        seen.add(v)
        for s in defs.get(v, []):
            out.append(s)
            need += glsl._vars(s.split("=", 1)[1] if "=" in s else s)
    outs = {int(l): ty for l, d, ty in _IOV.findall(text) if d == "out"}
    frame = next((u + 1 for u, ty in sorted(outs.items()) if ty == "vec2" and
                  all(outs.get(u + k) == "vec3" for k in (1, 2, 3))), 0)
    return {"slice": sorted(out), "bindings": sorted(set(_SET.findall(text))), "frame": frame}


def classify(fs: str, src, bases: dict) -> tuple[dict | None, str, glsl.Verdict | None]:
    """(base recipe row, term kind, verdict) or (None, reason, verdict)."""
    text = src.glsl(fs)
    if not text:
        return None, "no GLSL of the FS", None
    x = glsl.Forms.of(text)
    verdicts = sorted((glsl.compare(b.setdefault("forms", glsl.Forms.of(b["glsl"])), x, h) for h, b in bases.items()),
                      key=lambda v: (glsl.CLASS_ORDER[v.cls], v.diff_stmts, v.recipe))
    first = None
    for v in verdicts:
        base, kind, _ = _accept(v, bases)
        if base is not None:
            return base, kind, v
        first = first or (kind, v)
    return None, first[0] if first else "no base", first[1] if first else None


def _accept(v: glsl.Verdict, bases: dict) -> tuple[dict | None, str, glsl.Verdict]:
    if v.cls not in ("COPY", "VARIANT"):
        return None, f"FS {v.cls}: {v.term}"[:120], v
    base = bases[v.recipe]
    term = v.term
    if v.cls == "VARIANT":
        if not term.startswith("fields only"):
            return None, f"VARIANT, not fields only: {term}"[:120], v
        term = term.split("; plus ", 1)[1] if "; plus " in term else ""
    if term in ("", "identical modulo names and bindings"):
        return base, "same", v
    if FLAG_TERM.match(term):
        return base, "flag", v
    if CONST_TERM.match(term):
        return base, "constant", v
    if REFL_TERM.match(term):
        return base, "reflectance", v
    return None, f"term not expressible as a row: {term}"[:120], v


def build(log=print) -> tuple[list[dict], list[tuple]]:
    recipes = cli.recipes()
    fams = family_pairs()
    src = cli.Sources()
    try:
        built = {r["fs"] for r in recipes}
        bases = {}
        for r in recipes:
            g = src.glsl(r["fs"])
            if g and not any(x in r["extra"] for x in ("LayoutDetail", "LayoutIridescent", "LayoutMask", "CaptureNone")):
                bases[r["fs"]] = {**r, "glsl": g, "two_sided": "true" in r["extra"],
                                  "layout": next((x for x in r["extra"] if x.startswith("Layout")), "LayoutBase")}
        fam_vs = {}
        for f in fams:
            if f["family"] in BIT_EXACT_FAMILIES:
                a, b = src.glsl(f["vs"]), src.glsl(f["depth_vs"])
                if a and b:
                    fam_vs[f["family"]] = {**f, "g": vs_facts(a), "d": vs_facts(b)}
        rows, rejected, seen = [], [], set()
        for name, t in sorted(db_templates().items()):
            key = (t["vs"], t["fs"])
            if t["fs"] in built or key in seen:
                continue
            seen.add(key)
            why = None
            fields = db_fields(t["params"], t["mcb"])
            if fields is None or fields["layout"] is None:
                why = "DB: VT layers / engine offsets not an audited layout"
            base, kind, v = (None, "", None) if why else classify(t["fs"], src, bases)
            if why is None and base is None:
                why = kind
            capture, family, frame = None, None, 0
            if why is None:
                if t["vs"] in POOL_VS:
                    capture, frame = "CapturePool", 1
                elif t["alpha"] and t["depth_vs"]:
                    g, d = src.glsl(t["vs"]), src.glsl(t["depth_vs"])
                    if not g or not d:
                        why = "no GLSL of the VS pair"
                    else:
                        gf, df = vs_facts(g), vs_facts(d)
                        for fn, f in fam_vs.items():
                            r1 = t["depth_vs"] == f["depth_vs"] and gf["slice"] == f["g"]["slice"]
                            r2 = (gf["slice"] == df["slice"] and gf["bindings"] == f["g"]["bindings"] and
                                  df["bindings"] == f["d"]["bindings"])
                            if (r1 or r2) and gf["frame"]:
                                capture, family, frame = "CaptureDraw", fn, gf["frame"]
                                break
                        if capture is None:
                            why = "VS pair matches no bit-exact instanced family"
                else:
                    why = "not a pool VS, not an alpha-tested instanced draw"
            if why is None:
                mode = "ReflectanceConstant" if kind == "constant" else base["mode"]
                # The base's offsets in its own engine layout, moved by the classifier's field map: the DB must agree.
                mv = {(b, f): x for b, f, x in (v.fields if v else ())}
                exp = [mv.get(("ubo1_0", o), o) for o in base["offsets"]]
                got = [fields["transform"], fields["tileset"], fields["channel"]]
                if got != exp:
                    why = f"DB engine offsets {got} != classifier {exp}"
                refl_off = 0
                if why is None and mode == "ReflectanceConstant":
                    if fields["mcb"] != 0 or fields["reflectance"] is not None:
                        why = "constant reflectance but the DB lists a material UBO"
                elif why is None:
                    base_off = int(base["extra"][0]) if base["extra"] and base["extra"][0].isdigit() else 0
                    want = mv.get(("ubo1_1", base_off), base_off)
                    if kind == "reflectance":
                        # RT2.y reads another 1:1 member: its std140 offset in this FS's own block.
                        m = REFL_TERM.match(v.term.split("; plus ", 1)[-1])
                        blk = glsl.normalize(src.glsl(t["fs"])).blocks.get("ubo1_1", [])
                        offs = glsl.std140_offsets(blk)
                        k = int(m.group(2))
                        want = offs[k] if k < len(offs) else None
                    if fields["reflectance"] is None:
                        why = "DB: no Reflectance in the material UBO"
                    elif want != fields["reflectance"]:
                        why = f"DB reflectance @{fields['reflectance']} != classifier @{want}"
                    else:
                        refl_off = want
                two_sided = base["two_sided"]
                if why is None and capture == "CapturePool":
                    if two_sided:
                        why = "two-sided FS on a pool VS (the pool consumer has no two-sided flip)"
                    elif fields["layout"] != "LayoutBase":
                        why = "pool rows: LayoutBase only"
                if why is None and capture == "CaptureDraw" and mode not in ("ReflectanceUbo", "ReflectanceConstant"):
                    why = f"{mode} on the per-draw path (one 1:1 float only)"
                if why is None:
                    rows.append({"name": name, "vs": t["vs"], "fs": t["fs"], "depth_vs": t["depth_vs"] if capture == "CaptureDraw" else "",
                                 "transform": fields["transform"], "tileset": fields["tileset"], "channel": fields["channel"],
                                 "mode": mode, "refl": refl_off, "capture": capture, "layout": fields["layout"],
                                 "two_sided": two_sided, "family": family or "None", "frame": frame,
                                 "base": v.recipe if v else "", "term": kind})
            if why is not None:
                rejected.append((name, t["vs"], t["fs"], why))
        return rows, rejected
    finally:
        src.close()


def header(rows: list[dict]) -> str:
    L = ["#pragma once",
         "// GENERATED by `py -3 -m tools.material_families.recipe_rows` (asset DB offsets + FS classifier + VS capture path);",
         "// do not edit. Rows used only with rt_hit_material_generated (common/native_hit_generated.h consumers: native_hit_pool.h,",
         "// native_hit_instanced.h). Each row: the template, the audited recipe it equals, how it differs.",
         "#include <cstdint>",
         '#include "native_vt_surface.h"',
         "namespace bg3::native_hit::generated {",
         "// family: native_hit_instanced.h Family value (0 pool); frame_location: the G-buffer VS's N location (UV one below).",
         "struct Row {native_vt::Recipe recipe;uint64_t depth_vs;uint32_t family,frame_location;};",
         "inline constexpr Row kRows[]={"]
    fam_ids = {"None": 0, "Wind": 1, "AlphaTest2s": 2, "WindRgb": 3, "IpGrass": 4}
    for r in rows:
        L.append(f" // {r['name']}: = {r['base'][:8]} ({r['term']}); {r['capture']}"
                 + (f", family {r['family']}, depth VS {r['depth_vs'][:8]}" if r["capture"] == "CaptureDraw" else ""))
        L.append(f" {{{{0x{r['fs']}ull,0x{r['vs']}ull,{r['transform']},{r['tileset']},{r['channel']},native_vt::{r['mode']},"
                 f"{r['refl']},native_vt::{r['capture']},native_vt::{r['layout']},{'true' if r['two_sided'] else 'false'}}},"
                 f"0x{r['depth_vs'] or '0'}ull,{fam_ids[r['family']]}u,{r['frame']}u}},")
    if not rows:
        L.append(" {{0,0,0,0,0},0,0,0}, // none")
    L += ["};",
          "inline constexpr uint32_t kRowCount=" + str(len(rows)) + "u;",
          "// The generated row of a G-buffer pair (nullptr: none).",
          "inline const Row* row(uint64_t vs,uint64_t fs){for(uint32_t i=0;i<kRowCount;++i)if(kRows[i].recipe.fs==fs && kRows[i].recipe.vs==vs)return &kRows[i];return nullptr;}",
          "inline const Row* row_of(const native_vt::Recipe* r){for(uint32_t i=0;i<kRowCount;++i)if(&kRows[i].recipe==r)return &kRows[i];return nullptr;}",
          "}  // namespace bg3::native_hit::generated", ""]
    return "\n".join(L)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true", help="compare with the committed header instead of writing it")
    ap.add_argument("--rejected", action="store_true", help="also list every rejected pair and why")
    a = ap.parse_args(argv)
    if not cli.DB.exists():
        print(f"[recipe rows] no asset DB at {cli.DB}: skipped")
        return 0
    rows, rejected = build()
    text = header(rows)
    for r in rows:
        print(f"[recipe rows] {r['vs'][:8]}/{r['fs'][:8]} {r['name']}: {r['capture']} {r['family']} = {r['base'][:8]} ({r['term']}),"
              f" offsets {r['transform']}/{r['tileset']}/{r['channel']}, {r['mode']} @{r['refl']}, {r['layout']}, two-sided {r['two_sided']}")
    reasons = defaultdict(int)
    for *_, why in rejected:
        reasons[why.split(":")[0]] += 1
    print(f"[recipe rows] {len(rows)} rows; {len(rejected)} pairs rejected: " +
          ", ".join(f"{k} {n}" for k, n in sorted(reasons.items(), key=lambda kv: -kv[1])))
    if a.rejected:
        for name, vs, fs, why in rejected:
            print(f"   {vs[:8]}/{fs[:8]} {name}: {why}")
    if a.check:
        old = HEADER.read_text(encoding="utf-8") if HEADER.exists() else ""
        if old != text:
            print(f"[recipe rows] {HEADER} is not current: rerun without --check")
            return 1
        print("[recipe rows] header current")
        return 0
    HEADER.write_text(text, encoding="utf-8", newline="\n")
    print(f"[recipe rows] wrote {HEADER}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
