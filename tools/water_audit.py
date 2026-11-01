"""Offline audit of the forward FLUID water FS/VS pairs (docs/PLAN_M5_WATER.md "Audited pairs").

    py -3 -m tools.water_audit [--dump DIR ...] [--check] [--emit]

For every Fluids/ forward variant of the asset DB (data/assetdb), with its dumped SPIR-V
(captures/shaders, written by dump_shaders=1):
- VS (spirv-cross GLSL): gl_Position = vec4(P, 1) * <set 0 b2>._m2 with y negated, and P is the
  transformed vertex plus a displacement vec3(0, h, 0) or none: the XFB contract of M5.1.
- FS (spirv-dis --raw-id): the end-of-main structure of the compose model (M5.3-M5.5), which names
  the common/water_fs_patch.h FsIds: out = max(vec4(C, alpha), 0), C = mix(Refr.xyz, surf, vec3(a)),
  surf = fma(inner, fog.www, fog.xyz), inner = fma(Lw, X, G e) [+ foam], Lw = ((Ld + Sd) + (Ls + Ss)) *
  mix(1, 1, m) + (Kd k + Ks k) * 1, Ks / Kd the probe-loop header phis initialised with probes * (F0
  DFG.x + DFG.y) / irradiance * (dif DFG.z), DFG sampled at (max(dot(normalize(-pos), N), eps),
  roughness), Ld / Ls the projector-loop phis fed by the spot-loop phis (fed by the point loop),
  Ss = (Lsun (F0l + (1 - F0l) pow) ...) N.L and Sd = (Lsun (dif ...)) N.L, the channel the engine
  CB's only uint member (set 1 b0), z the Input float at location 0. Every id is derived, none typed.
- The sun: M5.4 replaces the FS's sun visibility. An FS that samples a shadow map (OpImageSampleDref*) has
  the game's sun shadow Vg in Lsun (game_sun_shadow: a separable factor, derived and checked): its row
  names Vg (vg), the M5.4c record stores the sun light Vg removed, and without the M5.4c words the
  patch marks its sun term absent (the sun step skips it, the game's shadow stays: never shadowed twice).
Classes as common/water_policy.h (falls / mist / foam / bridges excluded; lava, minimap not water).

--check compares the derivation with the headers (every kFsIds entry equal to its dump's ids, every
kAudited VS passing, no kFsIds FS with a shadow map but no vg) and exits 1 on a difference; it skips (exit 0)
without the asset DB, the dumps or the SPIR-V tools. --emit prints FsIds / Audited lines for passing
pairs that are not in the headers yet.
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIELDS = ["a", "fog", "k", "X", "Ks", "f0", "dfg", "N", "C", "rough", "pos", "sun_d", "sun_s", "sun_m",
          "ls_d12", "ls_s12", "ls_d", "ls_s", "ls_dif", "ls_f0", "ls_chan", "ls_z", "refr", "vg"]


class Fail(Exception):
    pass


# ---------------------------------------------------------------- FS (raw-id disassembly)
def parse_dis(text: str):
    defs, deco, consts, main = {}, {}, {}, []
    entry, in_main = None, False
    for ln in text.splitlines():
        s = ln.strip()
        m = re.match(r"OpEntryPoint Fragment %(\d+)", s)
        if m:
            entry = int(m.group(1))
        m = re.match(r"OpDecorate %(\d+) (\w+)(?: (\S+))?", s)
        if m:
            deco.setdefault(int(m.group(1)), {})[m.group(2)] = m.group(3)
        m = re.match(r"%(\d+) = (Op\w+)(.*)", s)
        if m:
            i, op, rest = int(m.group(1)), m.group(2), m.group(3).split()
            defs[i] = (op, rest)
            if op == "OpFunction":
                in_main = i == entry
            if op == "OpConstant":
                consts[i] = rest[1]
        if in_main:
            main.append(s)
        if s == "OpFunctionEnd":
            in_main = False
    return defs, deco, consts, main


def _ids(r):
    return [int(x[1:]) for x in r if x.startswith("%")]


def match_fs(text: str) -> dict:
    """The FsIds of one FS, or Fail(step) where its structure leaves the compose model."""
    defs, deco, consts, main = parse_dis(text)

    def d(i, op=None, ext=None):
        if i not in defs:
            raise Fail(f"%{i} undefined")
        o, r = defs[i]
        if op and o != op:
            raise Fail(f"%{i} is {o}, want {op}")
        if ext and not (o == "OpExtInst" and r[2] == ext):
            raise Fail(f"%{i} is {o}{' ' + r[2] if o == 'OpExtInst' else ''}, want {ext}")
        return r

    def args(i, op=None, ext=None):
        a = _ids(d(i, op, ext))
        return a[2:] if ext else a[1:]   # drop the result type (and the extended set)

    def last(i):
        return d(i)[-1]

    out = {}
    ret = max(k for k, s in enumerate(main) if s == "OpReturn")
    st = main[ret - 1].split()
    if st[0] != "OpStore":
        raise Fail("no store before the return")
    w, _ = args(int(st[2][1:]), ext="NMax")
    x, y, z, _alpha = args(w, "OpCompositeConstruct")
    cs = {args(v, "OpCompositeExtract")[0] for v in (x, y, z)}
    if len(cs) != 1:
        raise Fail("C components")
    out["C"] = C = cs.pop()
    sh, surf, aa = args(C, ext="FMix")
    rr = args(sh, "OpVectorShuffle")
    if rr[0] != rr[1] or d(sh)[-3:] != ["0", "1", "2"]:
        raise Fail("Refr.xyz")
    out["refr"] = rr[0]
    a3 = set(args(aa, "OpCompositeConstruct"))
    if len(a3) != 1:
        raise Fail("vec3(a)")
    out["a"] = a3.pop()
    inner, www, xyz = args(surf, ext="Fma")
    f1, f2 = args(www, "OpVectorShuffle"), args(xyz, "OpVectorShuffle")
    if d(www)[-3:] != ["3", "3", "3"] or d(xyz)[-3:] != ["0", "1", "2"] or len({*f1, *f2}) != 1:
        raise Fail("fog.www / fog.xyz")
    out["fog"] = f1[0]
    if defs.get(inner, ("",))[0] == "OpFAdd":   # + foam
        inner = args(inner, "OpFAdd")[0]
    Lw, X, _ = args(inner, ext="Fma")
    out["X"] = X
    p, q = args(Lw, "OpFAdd")

    def direct(p):
        s, m = args(p, "OpVectorTimesScalar")
        l, r = args(s, "OpFAdd")
        ld, sd = args(l, "OpFAdd")
        ls, ss = args(r, "OpFAdd")
        one1, one2, _ = args(m, ext="FMix")   # mix(1, 1, b26 _m1) = 1
        if consts.get(one1) != "1" or consts.get(one2) != "1":
            raise Fail("the direct factor is not mix(1, 1, x)")
        return ld, sd, ls, ss, m

    def ambient(q):
        v, c = args(q, "OpVectorTimesScalar")
        if consts.get(c) != "1":
            raise Fail("the ambient factor is not 1")
        kd, ks = args(v, "OpFAdd")
        kd, k1 = args(kd, "OpVectorTimesScalar")
        ks, k2 = args(ks, "OpVectorTimesScalar")
        if k1 != k2:
            raise Fail("two exposures")
        return kd, ks, k1

    try:
        ld, sd, ls, ss, sm = direct(p)
        kd, ks, k = ambient(q)
    except Fail:
        ld, sd, ls, ss, sm = direct(q)
        kd, ks, k = ambient(p)
    out.update(sun_d=sd, sun_s=ss, sun_m=sm, ls_d=ld, ls_s=ls, k=k, Ks=ks)
    ks0, kd0 = args(ks, "OpPhi")[0], args(kd, "OpPhi")[0]
    _, spec = args(ks0, "OpFMul")
    v1, v2 = args(spec, "OpFAdd")
    f0, dx = args(v1, "OpVectorTimesScalar")
    dfg = args(dx, "OpCompositeExtract")[0]
    dy = set(args(v2, "OpCompositeConstruct"))
    if last(dx) != "0" or len(dy) != 1:
        raise Fail("F0 DFG.x + DFG.y")
    dy = dy.pop()
    if args(dy, "OpCompositeExtract")[0] != dfg or last(dy) != "1":
        raise Fail("DFG.y")
    out.update(f0=f0, dfg=dfg)
    _, dif = args(kd0, "OpFMul")
    ldif, dz = args(dif, "OpVectorTimesScalar")
    if args(dz, "OpCompositeExtract")[0] != dfg or last(dz) != "2":
        raise Fail("dif DFG.z")
    out["ls_dif"] = ldif
    coord = int(d(dfg, "OpImageSampleExplicitLod")[2][1:])
    nv, rough = args(coord, "OpCompositeConstruct")
    out["rough"] = rough
    vv, N = args(args(nv, ext="NMax")[0], "OpDot")
    pos = args(args(args(vv, ext="Normalize")[0], "OpFNegate")[0], "OpLoad")[0]
    out.update(N=N, pos=pos)
    lss, _ = args(ss, "OpVectorTimesScalar")
    lsun, sp = args(lss, "OpFMul")
    fr, _ = args(sp, "OpVectorTimesScalar")
    f0l, rest = args(fr, "OpFAdd")
    one_m, _ = args(rest, "OpVectorTimesScalar")
    if args(one_m, "OpFSub")[1] != f0l:
        raise Fail("sun Fresnel")
    out["ls_f0"] = f0l
    lsd, _ = args(sd, "OpVectorTimesScalar")
    lsun2, dd = args(lsd, "OpFMul")
    if lsun2 != lsun or args(dd, "OpVectorTimesScalar")[0] != ldif:
        raise Fail("sun diffuse colour")
    out["vg"] = game_sun_shadow(defs, consts, main, lsun, (sd, ss), C)
    d12, s12 = args(ld, "OpPhi")[0], args(ls, "OpPhi")[0]
    for v in (d12, s12):
        if defs.get(v, ("",))[0] != "OpPhi" or defs.get(args(v, "OpPhi")[0], ("",))[0] != "OpPhi":
            raise Fail("three light loops (point, spot, projector)")
    out.update(ls_d12=d12, ls_s12=s12)
    s1b0 = {v for v, x in deco.items() if x.get("DescriptorSet") == "1" and x.get("Binding") == "0"}
    loads = []
    for s in main:
        m = re.match(r"%(\d+) = OpLoad %(\d+) %(\d+)", s)
        if m and defs.get(int(m.group(3)), ("",))[0] == "OpAccessChain":
            ac = _ids(defs[int(m.group(3))][1])
            t = defs.get(int(m.group(2)), ("", []))
            if len(ac) == 3 and ac[1] in s1b0 and t[0] == "OpTypeInt" and t[1] == ["32", "0"]:
                loads.append((int(m.group(1)), consts.get(ac[2])))
    if not loads or len({mem for _, mem in loads}) != 1:
        raise Fail("the light channel (one uint member of set 1 b0)")
    out["ls_chan"] = loads[0][0]
    zs = [v for v, x in deco.items() if x.get("Location") == "0" and defs.get(v, ("", []))[0] == "OpVariable"
          and defs[v][1][1] == "Input"]
    if len(zs) != 1:
        raise Fail("the location-0 input")
    out["ls_z"] = zs[0]
    return out


def game_sun_shadow(defs, consts, main, lsun, sun_terms, C) -> int:
    """The game's sun visibility Vg in Lsun, or 0 (none: the FS samples no shadow map).

    Lsun = colour * (chain * b0) with chain = Vg times the cloud layers: each step a phi(p, p * x) of an
    if (the cloud layer taken or not), down to Vg = phi(1, max(0, pow(PCF, 2.2))) of the cascade block's
    if (cascades on or off). Every operation from Vg to Lsun and from Lsun to the two sun terms is a
    product, so the sun term D is linear in Vg: D = Vg D_u exactly (the M5.4c record stores D_u - D).
    Refused (Fail) unless every shadow-map sample (OpImageSampleDref*) reaches the colour only through
    Vg and Vg reaches the colour only through Lsun's sun terms."""
    drefs = {int(m.group(1)) for s in main for m in [re.match(r"%(\d+) = OpImageSampleDref", s)] if m}
    uses = {}
    for i, (op, r) in defs.items():
        for j in _ids(r[1:]):
            uses.setdefault(j, set()).add(i)
    o, r = defs[lsun]
    if o != "OpVectorTimesScalar":
        raise Fail(f"Lsun %{lsun} is {o}")
    f = _ids(r)[2]
    o, r = defs[f]
    if o != "OpFMul":
        raise Fail(f"the sun factor %{f} is {o}")
    chain, v = [], _ids(r)[1]
    while defs.get(v, ("",))[0] == "OpPhi":
        inc = _ids(defs[v][1])[1::2]   # incoming values (labels skipped)
        if len(inc) != 2:
            break
        a, b = inc
        prod = [x for x in (a, b) if defs.get(x, ("",))[0] == "OpFMul" and (a if x == b else b) in _ids(defs[x][1])[1:]]
        if len(prod) != 1:
            break
        chain.append((v, prod[0]))
        v = a if prod[0] == b else b
    vg = 0
    if defs.get(v, ("",))[0] == "OpPhi":
        inc = _ids(defs[v][1])[1::2]
        if len(inc) == 2 and any(consts.get(x) == "1" for x in inc):
            vg = v
    if not drefs:
        return 0
    if not vg:
        raise Fail("the shadow-map samples are not a separable factor of Lsun")
    back, todo = set(), [vg]
    while todo:
        x = todo.pop()
        if x in back or x not in defs:
            continue
        back.add(x)
        todo.extend(_ids(defs[x][1])[1:])
    if not drefs <= back:
        raise Fail("a shadow-map sample outside the sun visibility")
    allowed = {vg, f, lsun} | {p for p, _ in chain} | {m for _, m in chain}
    for x in allowed - {lsun}:
        if not uses.get(x, set()) <= allowed:
            raise Fail(f"the sun visibility chain %{x} is used outside Lsun")
    # Lsun only in the two sun terms' products (sun_d = (Lsun * dif) * NL, sun_s = (Lsun * spec) * NL).
    sun_prod = {_ids(defs[t][1])[1] for t in sun_terms}
    if not uses.get(lsun, set()) <= sun_prod:
        raise Fail("Lsun is used outside the sun terms")
    for p in sun_prod:
        if not uses.get(p, set()) <= set(sun_terms):
            raise Fail("a sun product is used outside the sun terms")
    # The samples reach the colour only through Vg.
    seen, todo = set(), list(drefs)
    while todo:
        x = todo.pop()
        if x in seen or x == vg:
            continue
        seen.add(x)
        if x == C:
            raise Fail("a shadow-map sample reaches the colour outside Vg")
        todo.extend(uses.get(x, ()))
    return vg


def fs_facts(text: str) -> dict:
    """Side effects (refused by the patch anyway) and the shadow-map samples."""
    _, _, _, main = parse_dis(text)
    return {"discard": any(re.search(r"\bOp(Kill|TerminateInvocation|DemoteToHelperInvocation)\b", s) for s in main),
            "frag_depth": "BuiltIn FragDepth" in text,
            "dref": sum(1 for s in main if "OpImageSampleDref" in s)}


# ---------------------------------------------------------------- VS (spirv-cross GLSL)
def check_vs(glsl: str) -> tuple[bool, str]:
    body = glsl[glsl.find("void main()"):]
    gp = re.search(r"gl_Position = (_\d+);", body)
    if not gp:
        return False, "no gl_Position store"
    var = gp.group(1)
    m = re.search(r"precise vec4 " + var + r" = vec4\((_\d+)(?:\.xyz)?, 1\.0\) \* spvWorkaroundRowMajor\((_\d+)\._m2\);", body)
    if not m:
        return False, "gl_Position is not vec4(P, 1) * B._m2"
    if not re.search(var + r"\.y = -" + var + r"\.y;", body):
        return False, "no y flip"
    decl = re.search(r"layout\(([^)]*)\) uniform \w+\s*\{[^}]*\}\s*" + m.group(2) + ";", glsl)
    lay = decl.group(1) if decl else ""
    setn = int(re.search(r"set = (\d+)", lay).group(1)) if "set =" in lay else 0
    bind = int(re.search(r"binding = (\d+)", lay).group(1)) if "binding" in lay else -1
    if (setn, bind) != (0, 2):
        return False, f"the matrix is set {setn} b{bind}"
    disp = re.search(r"precise vec3 " + m.group(1) + r" = _\d+\.xyz \+ vec3\((.*)\);", body)
    if not disp:
        return True, "P = the transformed vertex (no displacement)"
    a = disp.group(1)
    if a == "0.0" or (a.startswith("0.0, ") and a.endswith(", 0.0")):
        return True, "P = vertex + vec3(0, h, 0)"
    return False, "the displacement is not height only"


# ---------------------------------------------------------------- inputs
def tool(name):
    p = shutil.which(name)
    if p:
        return p
    sdk = os.environ.get("VULKAN_SDK")
    if sdk:
        c = Path(sdk) / "Bin" / (name + ".exe")
        if c.exists():
            return str(c)
    return None


def default_dumps():
    out = []
    for d in (ROOT / "captures" / "shaders", ROOT.parents[2] / "captures" / "shaders" if len(ROOT.parents) > 2 else None):
        if d and d.is_dir() and d not in out:
            out.append(d)
    return out


def find_dump(dirs, h):
    for d in dirs:
        p = Path(d) / f"{h:016x}.spv"
        if p.exists() and p.stat().st_size > 16:   # a truncated file is no dump
            return p
    return None


def db_pairs(db_path):
    """(template, variant, vs, fs) of every Fluids/ forward variant of the asset DB."""
    from tools.assets import dbformat
    from tools.assets.extract.materials import VARIANTS
    r = dbformat.Reader(open(db_path, "rb").read())
    tpl, var, sref = r.rows("MTPL"), r.rows("MVAR"), r.rows("SREF")
    by_var = {}
    for h, first, cnt, stage, *_ in r.rows("SHDR"):
        for k in range(first, first + cnt):
            by_var.setdefault(sref[k][0], {})[stage] = h
    out = []
    for vi, v in enumerate(var):
        name = r.string(tpl[v[0]][0])
        vname = VARIANTS[v[1]]
        if name.startswith("Fluids/") and vname.endswith("Forward"):
            s = by_var.get(vi, {})
            out.append((name, vname, s.get(0), s.get(4)))
    return sorted(out)


def policy_class(name):
    """common/water_policy.h classify by name (the DB pass is forward here)."""
    if "Lava" in name or "FLUID_Water" not in name:
        return "not water" if ("Lava" in name or "Minimap_Const" in name) else "unknown (not FLUID_Water)"
    if any(x in name for x in ("_Fall", "Mist", "Bridge", "Foam")):
        return "excluded flow"
    return "water"


def header_tables():
    pol = (ROOT / "common" / "water_policy.h").read_text()
    pat = (ROOT / "common" / "water_fs_patch.h").read_text()
    aud = [(int(f, 16), int(v, 16), n) for f, v, n in
           re.findall(r"\{0x([0-9a-f]{16})ull, 0x([0-9a-f]{16})ull, \"(\w+)\"\}", pol)]
    body = pat[pat.find("kFsIds[] = {"):]
    body = body[:body.find("};")]
    ids = {}
    for m in re.finditer(r"\{0x([0-9a-f]{16})ull,([^}]*)\}", body):
        nums = [int(x) for x in m.group(2).replace("\n", " ").split(",") if x.strip()]
        row = dict(zip(FIELDS, nums))
        row.setdefault("vg", 0)   # rows without a game sun shadow end at refr
        ids[int(m.group(1), 16)] = row
    return aud, ids


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", default=str(ROOT / "data" / "assetdb" / "bg3rtx_assetdb.bin"))
    ap.add_argument("--dump", action="append", default=None)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--emit", action="store_true")
    a = ap.parse_args(argv)
    dumps = [Path(d) for d in a.dump] if a.dump else default_dumps()
    dis, cross = tool("spirv-dis"), tool("spirv-cross")
    if not Path(a.db).exists() or not dumps or not dis or not cross:
        print(f"water_audit: SKIP (asset DB {Path(a.db).exists()}, dumps {len(dumps)}, spirv-dis {bool(dis)}, "
              f"spirv-cross {bool(cross)})")
        return 0
    aud, ids = header_tables()
    aud_pairs = {(f, v) for f, v, _ in aud}
    tmp = Path(tempfile.mkdtemp(prefix="water_audit_"))
    cache = {}

    def text(h, kind):
        key = (h, kind)
        if key not in cache:
            p = find_dump(dumps, h)
            if not p:
                cache[key] = None
            else:
                o = tmp / f"{h:016x}.{kind}"
                cmd = [dis, "--raw-id", str(p), "-o", str(o)] if kind == "dis" else [cross, str(p), "--output", str(o)]
                subprocess.run(cmd, check=True, capture_output=True)
                cache[key] = o.read_text()
        return cache[key]

    bad, emit = [], []
    print(f"{'template:variant':86s} {'FS':16s} {'VS':16s}  result")
    for name, vname, vs, fs in db_pairs(a.db):
        cls = policy_class(name)
        short = name[len("Fluids/"):]
        row = f"{short + ':' + vname:86s} {fs or 0:016x} {vs or 0:016x}"
        if cls != "water":
            print(f"{row}  {cls}")
            continue
        fst, vst = text(fs, "dis"), text(vs, "glsl")
        if fst is None or vst is None:
            print(f"{row}  NO DUMP ({'FS' if fst is None else ''}{' ' if fst is None and vst is None else ''}{'VS' if vst is None else ''})")
            continue
        vok, vwhy = check_vs(vst)
        facts = fs_facts(fst)
        side = facts["discard"] or facts["frag_depth"]
        try:
            got, fwhy = match_fs(fst), "compose model"
        except Fail as e:
            got, fwhy = None, f"structure differs: {e}"
        except (KeyError, IndexError, ValueError) as e:
            got, fwhy = None, f"structure differs: {type(e).__name__}"
        shadow = facts["dref"] > 0
        if not vok or side:
            verdict = "FAIL " + (vwhy if not vok else "discard / FragDepth")
        elif got and not shadow:
            verdict = "PASS (supported + compose ids)"
        elif got:
            verdict = f"PASS (supported + compose ids; game sun shadow Vg %{got['vg']} from {facts['dref']} Dref samples)"
        else:
            verdict = f"PASS supported; compose NOT mapped: {fwhy}"
        in_aud = (fs, vs) in aud_pairs
        in_ids = fs in ids
        print(f"{row}  {verdict}; headers: {'audited' if in_aud else '-'}{', ids' if in_ids else ''}")
        if in_aud and not vok:
            bad.append(f"{fs:016x}/{vs:016x} is audited but its VS fails: {vwhy}")
        if in_ids:
            if not got:
                bad.append(f"{fs:016x} has ids but leaves the model: {fwhy}")
            elif got != ids[fs]:
                diff = {k: (ids[fs][k], got[k]) for k in FIELDS if ids[fs].get(k) != got[k]}
                bad.append(f"{fs:016x} ids differ from the derivation (table, derived): {diff}")
            if shadow and not ids[fs]["vg"]:
                bad.append(f"{fs:016x} has ids but samples a shadow map without its Vg")
        if vok and not side and not in_aud:
            emit.append(f'    {{0x{fs:016x}ull, 0x{vs:016x}ull, "{short}"}},')
        if vok and not side and got and not in_ids:
            keys = FIELDS if got["vg"] else FIELDS[:-1]
            emit.append(f"    {{0x{fs:016x}ull, " + ", ".join(str(got[k]) for k in keys) + "},")
    for f, v, n in aud:   # audited pairs the DB does not list are reported
        if not any(f == fs and v == vs for _, _, vs, fs in db_pairs(a.db)):
            print(f"audited pair not in the DB: {f:016x}/{v:016x} {n}")
    if a.emit:
        print("\n".join(["", "not in the headers yet:"] + emit))
    for b in bad:
        print("MISMATCH " + b)
    for p in tmp.iterdir():
        p.unlink()
    tmp.rmdir()
    if a.check:
        print(f"water_audit: {'FAIL' if bad else 'PASS'} ({len(ids)} id tables, {len(aud)} audited pairs checked)")
        return 1 if bad else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
