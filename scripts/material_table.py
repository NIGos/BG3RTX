"""EN2: what BG3's G-buffer fragment shaders write as base colour, roughness, metalness and
reflectance, as data sources a ray query can read at a secondary hit (docs/TRACK_D_RT.md 13).

Reads the shader dump (captures/shaders/*.spv), decompiles every G-buffer fragment shader
(five colour outputs, docs/FRAME_MAP.md: RT1.rgb base colour, RT2.x perceptual roughness,
RT2.y specular reflectance, RT2.z metalness) and follows each of those output components back
through the shader's own expressions to a source the layer can reproduce per draw:

  vt    a Granite virtual-texture sample: the tile cache (set.binding), the indirection
        texture's channel holding the page entry (ich), the tile set's layer column (col) and
        the component of the sample; all through one indirection texture, one tile set
        (UBO uv transform + index, SSBO entry), as rt/rt_common.hlsli vt_alpha() reads it;
  tex   a plain texture sample at the interpolated UV (optionally x a UBO or constant vec2);
  c     a literal constant;
  u     a UBO scalar (read per draw from the game's mapped memory).

The base colour may carry one factor (a UBO vec3/scalar or a constant) on top of the sample.
Anything else (vertex colours, gradient maps, layer blends, branches, procedural terms) is
left out: the field is "unknown" for that shader, and the ray query counts the hit as an
unknown material instead of inventing a value (docs/QUALITY.md R0).

Writes shaders/rt/material_fs.txt, one line per G-buffer FS with at least one known field:

  <fs hash> vt  uv=L ubo=S.B p0=OFF idx=OFF ssbo=S.B ind=S.B alb=SRC rough=SRC metal=SRC refl=SRC [albk=K]
  <fs hash> tex uv=L [uvs=u:S.B:OFF | uvs=c:K] alb=SRC rough=SRC metal=SRC refl=SRC [albk=K]

  SRC   vt:S.B:ich:col:swz   (cache, indirection channel, tile-set column, components)
        tex:S.B:swz          (texture)
        c:V                  (constant)
        u:S.B:OFF            (UBO float)
        -                    (unknown)
  K     c:V | u:S.B:OFF:N    (N = 1 scalar or 3 vec3)

  py -3 scripts\\material_table.py [--dump captures\\shaders] [--frame captures\\<ts>\\frame.json]

With --frame, the G-buffer draws of that trace are counted per outcome (draw-weighted coverage).
"""
import argparse, collections, concurrent.futures as cf, json, os, pathlib, re, shutil, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
# A worktree's captures/ and assetdb live in the main checkout.
MAIN = ROOT.parents[2] if ROOT.parent.name == "worktrees" else ROOT
SW = "xyzw"


def spirv_cross():
    t = shutil.which("spirv-cross")
    if t: return t
    return os.path.join(os.environ.get("VULKAN_SDK", r"C:\VulkanSDK\1.4.357.0"), "Bin", "spirv-cross.exe")


def decompile(tool, spv, cache):
    g = cache / (spv.stem + ".glsl")
    if not g.exists():
        r = subprocess.run([tool, str(spv), "-V"], capture_output=True)
        if r.returncode: return None
        g.write_bytes(r.stdout)
    s = g.read_text(errors="ignore")
    if "gl_Position" in s or "local_size_x" in s: return None
    if "layout(location = 3) out vec4" not in s or "layout(location = 2) out vec4" not in s: return None
    j = cache / (spv.stem + ".json")
    if not j.exists():
        r = subprocess.run([tool, str(spv), "--reflect"], capture_output=True)
        if r.returncode: return None
        j.write_bytes(r.stdout)
    return s, json.loads(j.read_text())


def split_args(t, start):
    """The comma-separated top-level arguments of the call whose "(" is at t[start], and the end."""
    args, depth, cur, k = [], 0, start + 1, start
    while k < len(t):
        c = t[k]
        if c in "([": depth += 1
        elif c in ")]":
            depth -= 1
            if depth == 0:
                args.append(t[cur:k].strip())
                return args, k + 1
        elif c == "," and depth == 1:
            args.append(t[cur:k].strip())
            cur = k + 1
        k += 1
    return args, len(t)


def outer_parens(e):
    while e.startswith("(") and e.endswith(")"):
        args, end = split_args(e, 0)
        if end != len(e) or len(args) != 1: break
        e = args[0]
    return e


def top_split(e, ops):
    """Split e at top-level binary operators in `ops` (left to right); [] if none."""
    parts, depth, cur, k = [], 0, 0, 0
    while k < len(e):
        c = e[k]
        if c in "([": depth += 1
        elif c in ")]": depth -= 1
        elif depth == 0 and c in ops and k > 0:
            prev = e[:k].rstrip()
            # binary only: the previous token ends an operand (not an operator or "e" of 1e-5)
            if prev and (prev[-1].isalnum() or prev[-1] in "_)]") and not re.search(r"\d[eE]$", prev):
                parts.append((e[cur:k].strip(), c))
                cur = k + 1
        k += 1
    if not parts: return []
    parts.append((e[cur:].strip(), None))
    return parts


class Shader:
    def __init__(self, h, s, refl):
        self.h, self.s, self.refl = h, s, refl
        self.body = s[s.find("void main"):]
        self.types = refl.get("types", {})
        self.res = {}
        for kind in ("separate_images", "separate_samplers", "ubos", "ssbos"):
            for r in refl.get(kind, []):
                name = "_" + r["name"].split("_")[-1] if kind in ("ubos", "ssbos") else r["name"]
                self.res[name] = (kind, r)
        self.inputs = {i["name"]: i for i in refl.get("inputs", [])}
        # locals: full and component assignments, in order
        self.full = collections.defaultdict(list)
        self.part = collections.defaultdict(list)
        self.decl = {}
        for m in re.finditer(r"^\s*(?:(\w+)\s+)?(_\d+)(\.[xyzw]+)?\s*=\s*(.*?);\s*$", self.body, re.M):
            ty, v, sw, ex = m.groups()
            if ty and ty not in ("precise",): self.decl[v] = ty
            if sw: self.part[v].append((sw[1:], ex, m.start()))
            else: self.full[v].append((ex, m.start()))
        for m in re.finditer(r"^\s*(\w+)\s+(_\d+);\s*$", self.body, re.M):
            self.decl.setdefault(m.group(2), m.group(1))
        self.outputs = {o["location"]: o["name"] for o in refl.get("outputs", [])}
        # an assignment inside a branch or loop makes the value control-dependent
        self.branchy = "for (" in self.body or "while (" in self.body

    def binding(self, var):
        k = self.res.get(var)
        return (k[1]["set"], k[1]["binding"]) if k else None

    def member(self, var, mem):
        k = self.res.get(var)
        if not k: return None
        for m in self.types.get(k[1]["type"], {}).get("members", []):
            if m["name"] == mem: return m
        return None

    def width(self, e):
        e = outer_parens(e.strip())
        if re.fullmatch(r"-?[0-9.]+(e[-+]?\d+)?", e): return 1
        m = re.fullmatch(r"(?:vec|ivec|uvec|bvec)(\d)\(.*\)", e, re.S)
        if m: return int(m.group(1))
        m = re.search(r"\.([xyzw]{1,4})$", e)
        if m: return len(m.group(1))
        if re.fullmatch(r"_\d+", e):
            t = self.decl.get(e) or (self.inputs.get(e) or {}).get("type")
            return {"float": 1, "int": 1, "uint": 1, "vec2": 2, "vec3": 3, "vec4": 4}.get(t)
        m = re.fullmatch(r"(_\d+)\.(_m\d+)", e)
        if m:
            mm = self.member(*m.groups())
            return mm and {"float": 1, "vec2": 2, "vec3": 3, "vec4": 4}.get(mm["type"])
        m = re.fullmatch(r"(\w+)\(", e[:e.find("(") + 1]) if "(" in e else None
        if m:
            f = m.group(1)
            args, end = split_args(e, e.find("("))
            if end == len(e):
                if f in ("clamp", "mix", "normalize", "abs", "fract", "floor", "max", "min", "sqrt", "pow", "exp2",
                         "log2", "spvNMin", "spvNMax", "fma", "sign", "saturate"):
                    return self.width(args[0])
                if f in ("dot", "length", "distance"): return 1
                if f in ("texture", "textureGrad", "textureLod", "texelFetch"): return 4
        parts = top_split(e, "+-*/")
        if parts:
            ws = [self.width(p) for p, _ in parts]
            if None in ws: return None
            return max(ws)
        return None

    # ---- the resolver: output component -> source ----
    def resolve(self, e, c, depth=0):
        """('vt', info, comp) | ('tex', info, comp) | ('c', v) | ('u', set, binding, off) |
        ('mul', src, factor) | ('?', reason)."""
        if depth > 40: return ("?", "depth")
        e = outer_parens(e.strip())
        if re.fullmatch(r"-?[0-9]+\.[0-9]*(e[-+]?\d+)?|-?[0-9]+", e): return ("c", float(e))
        # top-level sums are blends or offsets: not a plain source
        if top_split(e, "+-"): return ("?", "sum")
        prod = top_split(e, "*")
        if prod:
            if any(op == "/" for _, op in prod): return ("?", "div")
            facs = []
            for p, _ in prod:
                w = self.width(p)
                facs.append(self.resolve(p, 0 if w == 1 else c, depth + 1))
            samples = [f for f in facs if f[0] in ("vt", "tex")]
            consts = [f for f in facs if f[0] in ("c", "u")]
            if len(samples) == 1 and len(samples) + len(consts) == len(facs) and len(consts) == 1:
                return ("mul", samples[0], consts[0])
            if not samples and len(consts) == len(facs) and all(f[0] == "c" for f in consts):
                v = 1.0
                for f in consts: v *= f[1]
                return ("c", v)
            bad = [f for f in facs if f[0] == "?"]
            return bad[0] if bad else ("?", "product")
        if top_split(e, "/"): return ("?", "div")
        if e.startswith("-"): return ("?", "neg")
        # UBO member (scalar, or a vector's component)
        m = re.fullmatch(r"(_\d+)\.(_m\d+)(?:\.([xyzw]+))?", e)
        if m and m.group(1) in self.res:
            var, mem, sw = m.groups()
            mm = self.member(var, mem)
            b = self.binding(var)
            if not mm or not b or self.res[var][0] != "ubos": return ("?", "block member")
            k = SW.index(sw[c]) if sw else c
            ty = mm["type"]
            if ty not in ("float", "vec2", "vec3", "vec4") or "array" in mm: return ("?", "ubo type")
            n = {"float": 1, "vec2": 2, "vec3": 3, "vec4": 4}[ty]
            if k >= n: return ("?", "ubo comp")
            return ("u", b[0], b[1], mm["offset"] + 4 * k)
        # swizzle of an expression
        m = re.fullmatch(r"(.*)\.([xyzw]{1,4})", e, re.S)
        if m and (m.group(1).endswith(")") or re.fullmatch(r"_\d+", m.group(1))):
            base, sw = m.groups()
            if c >= len(sw): return ("?", "swizzle")
            return self.resolve(base, SW.index(sw[c]), depth + 1)
        # a local
        if re.fullmatch(r"_\d+", e):
            if e in self.inputs: return ("?", "input")
            if e in self.res: return ("?", "resource")
            full = self.full.get(e, [])
            if len(full) > 1: return ("?", "branch")
            last_full = full[0][1] if full else -1
            for sw, ex, pos in reversed(self.part.get(e, [])):
                if pos > last_full and SW[c] in sw: return self.resolve(ex, sw.index(SW[c]), depth + 1)
            if not full: return ("?", "undefined")
            return self.resolve(full[0][0], c, depth + 1)
        # calls
        if "(" in e:
            f = e[:e.find("(")]
            args, end = split_args(e, e.find("("))
            if end != len(e): return ("?", "expr")
            if f == "clamp" and len(args) == 3 and args[1] in ("0.0", "vec3(0.0)", "vec4(0.0)") and \
                    args[2] in ("1.0", "vec3(1.0)", "vec4(1.0)"):
                return self.resolve(args[0], c, depth + 1)
            m = re.fullmatch(r"vec(\d)", f)
            if m:
                n = int(m.group(1))
                if len(args) == 1:
                    w = self.width(args[0])
                    return self.resolve(args[0], 0 if w == 1 else c, depth + 1)
                k = 0
                for a in args:
                    w = self.width(a)
                    if w is None: return ("?", "width")
                    if c < k + w: return self.resolve(a, c - k, depth + 1)
                    k += w
                return ("?", "ctor")
            if f == "mix" and len(args) == 3 and "2202.0" in args[2] and args[1] == "vec4(1.0, 0.0, 0.0, 1.0)":
                return self.vt_sample(args[0], c)
            if f == "texture" and len(args) == 2:
                return self.tex_sample(args[0], args[1], c)
            return ("?", "call " + f)
        return ("?", "expr")

    def vt_sample(self, e, c):
        m = re.fullmatch(r"textureGrad\(sampler2DArray\((_\d+), _\d+\), (.*)\)", e, re.S)
        if not m: return ("?", "vt sample shape")
        cache = self.binding(m.group(1))
        coord = m.group(2)
        mc = re.search(r"\._m0\[1\]\[2\]\.([xyzw])", coord)
        me = re.search(r"float\(\((_\d+) >> 24u\) & 127u\)", coord)
        if not (cache and mc and me): return ("?", "vt coord")
        ent = self.full.get(me.group(1), [])
        if len(ent) != 1: return ("?", "vt entry")
        mi = re.fullmatch(r"floatBitsToUint\(texelFetch\((_\d+), .*\)\.([xyzw])\)", ent[0][0].strip(), re.S)
        if not mi: return ("?", "vt entry shape")
        ind = self.binding(mi.group(1))
        # the tile set: ssbo[ubo.idx]; the uv: fma(fract(uv), ubo.p0.zw, ubo.p0.xy)
        sl = self.slice(e)
        uv = re.search(r"fma\(fract\((_\d+)\), (_\d+)\.(_m\d+)\[0\]\.zw, \2\.\3\[0\]\.xy\)", sl)
        idx = re.search(r"(_\d+)\._m0\[(_\d+)\.(_m\d+)\]\._m0\[", sl)
        if not (ind and uv and idx): return ("?", "vt tile set")
        if idx.group(2) != uv.group(2): return ("?", "vt blocks")
        uvv = uv.group(1)
        if uvv not in self.inputs or self.inputs[uvv]["type"] != "vec2": return ("?", "vt uv")
        p0m = self.member(uv.group(2), uv.group(3))
        im = self.member(idx.group(2), idx.group(3))
        # the layer reads the uv transform as two consecutive vec4 (rt_common.hlsli P0, P1)
        if not p0m or not im or p0m.get("type") != "vec4" or p0m.get("array") != [2]: return ("?", "vt uv transform layout")
        info = dict(cache=cache, ich=SW.index(mi.group(2)), col=SW.index(mc.group(1)), ind=ind,
                    ubo=self.binding(uv.group(2)), p0=p0m["offset"], idx=im["offset"],
                    ssbo=self.binding(idx.group(1)), uv=self.inputs[uvv]["location"])
        if None in info.values(): return ("?", "vt binding")
        return ("vt", info, c)

    def tex_sample(self, img, uve, c):
        mi = re.fullmatch(r"sampler2D\((_\d+), _\d+\)", img)
        if not mi: return ("?", "tex shape")
        tb = self.binding(mi.group(1))
        uve = outer_parens(uve.strip())
        scale = None
        mu = re.fullmatch(r"(_\d+)(?: \* (?:(_\d+)\.(_m\d+)|vec2\(([0-9.]+)\)))?", uve)
        if not mu or mu.group(1) not in self.inputs or self.inputs[mu.group(1)]["type"] != "vec2":
            return ("?", "tex uv")
        if mu.group(2):
            mm = self.member(mu.group(2), mu.group(3))
            ub = self.binding(mu.group(2))
            if not mm or mm["type"] != "vec2" or not ub: return ("?", "tex uv scale")
            scale = "u:%d.%d:%d" % (*ub, mm["offset"])
        elif mu.group(4):
            scale = "c:%s" % mu.group(4)
        if not tb: return ("?", "tex binding")
        return ("tex", dict(tex=tb, uv=self.inputs[mu.group(1)]["location"], uvs=scale), c)

    def slice(self, expr, depth=14):
        seen, out, todo = set(), [expr], [expr]
        for _ in range(depth):
            nxt = []
            for t in todo:
                for v in re.findall(r"\b(_\d+)\b", t):
                    if v in seen or v not in self.full: continue
                    seen.add(v)
                    for d, _ in self.full[v]:
                        out.append(d)
                        nxt.append(d)
            todo = nxt
        return "\n".join(out)

    def output(self, loc, c):
        o = self.outputs.get(loc)
        if not o: return ("?", "no output")
        return self.resolve(o, c)


def fmt_factor(f):
    if f[0] == "c": return "c:%g" % f[1]
    if f[0] == "u": return "u:%d.%d:%d" % f[1:]
    return None


def classify(h, s, refl):
    sh = Shader(h, s, refl)
    if sh.branchy: return None, {"alb": "loop", "rough": "loop", "metal": "loop", "refl": "loop"}
    srcs, why = {}, {}
    albk = None
    # base colour: three components of one sample (same texture, components in order)
    alb = [sh.output(1, k) for k in range(3)]
    fac = [a[2] if a[0] == "mul" else None for a in alb]
    base = [a[1] if a[0] == "mul" else a for a in alb]
    if all(b[0] in ("vt", "tex") for b in base) and len({json.dumps(b[1], sort_keys=True) for b in base}) == 1:
        fk = {json.dumps(f) for f in fac}
        if len(fk) == 1 and (fac[0] is None or fac[0][0] in ("c", "u")):
            # one factor for all three: a scalar (same address) or a vec3 (consecutive floats)
            if fac[0] is None or all(fac[k] == fac[0] for k in range(3)):
                srcs["alb"] = (base[0][0], base[0][1], "".join(SW[b[2]] for b in base))
                albk = fmt_factor(fac[0]) + ":1" if fac[0] else None
            else:
                why["alb"] = "factor"
        elif all(f and f[0] == "u" for f in fac) and fac[1][3] == fac[0][3] + 4 and fac[2][3] == fac[0][3] + 8 and \
                fac[0][1:3] == fac[1][1:3] == fac[2][1:3]:
            srcs["alb"] = (base[0][0], base[0][1], "".join(SW[b[2]] for b in base))
            albk = "u:%d.%d:%d:3" % fac[0][1:]
        else:
            why["alb"] = "factor"
    else:
        bad = [b for b in base if b[0] == "?"]
        why["alb"] = bad[0][1] if bad else "mixed sources"
    for name, comp in (("rough", 0), ("refl", 1), ("metal", 2)):
        r = sh.output(2, comp)
        if r[0] in ("vt", "tex"): srcs[name] = (r[0], r[1], SW[r[2]])
        elif r[0] in ("c", "u"): srcs[name] = r
        else: why[name] = r[1] if r[0] == "?" else "factor"
    samples = [v for v in srcs.values() if v[0] in ("vt", "tex")]
    if not srcs: return None, why
    # one family per shader: every sample from the same tile set / the same uv
    kinds = {v[0] for v in samples}
    if len(kinds) > 1:
        return None, dict(why, alb="vt and tex in one shader")
    kind = kinds.pop() if kinds else "c"
    if kind == "vt":
        keys = {(v[1]["ind"], v[1]["ubo"], v[1]["p0"], v[1]["idx"], v[1]["ssbo"], v[1]["uv"]) for v in samples}
        if len(keys) != 1: return None, dict(why, alb="several tile sets")
        ind, ubo, p0, idx, ssbo, uv = keys.pop()
        head = "vt uv=%d ubo=%d.%d p0=%d idx=%d ssbo=%d.%d ind=%d.%d" % (uv, *ubo, p0, idx, *ssbo, *ind)
    elif kind == "tex":
        keys = {(v[1]["uv"], v[1]["uvs"]) for v in samples}
        if len(keys) != 1: return None, dict(why, alb="several uvs")
        uv, uvs = keys.pop()
        head = "tex uv=%d" % uv + (" uvs=%s" % uvs if uvs else "")
    else:
        head = "const"

    def src(name):
        v = srcs.get(name)
        if not v: return "-"
        if v[0] == "vt": return "vt:%d.%d:%d:%d:%s" % (*v[1]["cache"], v[1]["ich"], v[1]["col"], v[2])
        if v[0] == "tex": return "tex:%d.%d:%s" % (*v[1]["tex"], v[2])
        if v[0] == "c": return "c:%g" % v[1]
        if v[0] == "u": return "u:%d.%d:%d" % v[1:]
        return "-"

    line = "%s %s alb=%s rough=%s metal=%s refl=%s" % (h, head, src("alb"), src("rough"), src("metal"), src("refl"))
    if albk: line += " albk=" + albk
    return line, why


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", default=str(MAIN / "captures" / "shaders"))
    ap.add_argument("--cache", default=str(MAIN / "captures" / "alpha_cache"))
    ap.add_argument("--out", default=str(ROOT / "shaders" / "rt" / "material_fs.txt"))
    ap.add_argument("--frame", help="frame.json of a trace: G-buffer draws per outcome")
    ap.add_argument("--why", action="store_true", help="print the reasons for unknown fields")
    ap.add_argument("--db", default=str(ROOT / "data" / "assetdb" / "bg3rtx_assetdb.bin"),
                    help="asset DB: family names (A1 join) and the cross-check of the VT albedo binding")
    a = ap.parse_args()
    cache = pathlib.Path(a.cache)
    cache.mkdir(parents=True, exist_ok=True)
    tool = spirv_cross()
    spvs = sorted(pathlib.Path(a.dump).glob("*.spv"))
    with cf.ThreadPoolExecutor(16) as ex:
        dec = list(ex.map(lambda p: (p.stem, decompile(tool, p, cache)), spvs))
    rows, n = [], 0
    lines = {}
    known = collections.Counter()
    reasons = {k: collections.Counter() for k in ("alb", "rough", "metal", "refl")}
    outcome = {}
    for h, d in dec:
        if not d: continue
        n += 1
        line, why = classify(h, *d)
        for k in reasons:
            if k in why: reasons[k][why[k]] += 1
        if line:
            rows.append(line)
            lines[h] = line
            for k in ("alb", "rough", "metal", "refl"):
                if " %s=-" % k not in line: known[k] += 1
            outcome[h] = ("vt" if " vt " in line else "tex" if " tex " in line else "const") + \
                         ("+alb" if " alb=-" not in line else "")
        else:
            outcome[h] = "unknown"
    pathlib.Path(a.out).write_text(
        "# Generated by scripts/material_table.py from the shader dump: what each G-buffer fragment\n"
        "# shader writes as base colour, roughness, metalness and reflectance (EN2, TRACK_D_RT.md 13).\n"
        + "\n".join(sorted(rows)) + "\n")
    print("%d G-buffer fragment shaders, %d with a known field; known: %s -> %s" % (n, len(rows), dict(known), a.out))
    print("outcomes: %s" % dict(collections.Counter(outcome.values())))
    if a.why:
        for k, c in reasons.items(): print("  %s unknown: %s" % (k, dict(c.most_common(12))))
    if a.frame:
        f = json.load(open(a.frame))
        per = collections.Counter()
        tot = 0
        for sub in f["submits"]:
            for c in sub["cmds"]:
                cur = None
                for e in c["events"]:
                    if e["op"] == "pass":
                        cur = [] if e["color"] and len(e["color"]) == 5 else None
                    elif e["op"] == "draw" and cur is not None:
                        per[outcome.get(e.get("fs", ""), "not a G-buffer FS")] += e.get("n", 1)
                        tot += e.get("n", 1)
                    elif e["op"] == "end":
                        cur = None
        print("G-buffer draws of %s: %d: %s" % (a.frame, tot, dict(per)))
    if a.db and pathlib.Path(a.db).exists():
        families(a.db, outcome, lines, a.frame)


def families(db_path, outcome, lines, frame=None):
    """Per material template (asset DB, A1 SPIR-V join): G-buffer shaders and how many have a
    known base colour; and the cross-check of the table's VT base-colour source against the
    template's own VT albedo slot (cache set/binding and layer)."""
    sys.path.insert(0, str(ROOT))
    from tools.assets import cli as acli
    from tools.assets.extract.materials import ROLES
    r = acli._db(db_path)
    shd, _ = acli._shader_index(r)
    tpl, var, prm, sref = r.rows("MTPL"), r.rows("MVAR"), r.rows("MPRM"), r.rows("SREF")
    role = {name: i for i, name in enumerate(ROLES)}
    fam = collections.defaultdict(lambda: collections.Counter())
    agree = collections.Counter()
    draws = collections.Counter()
    if frame:
        f = json.load(open(frame))
        for sub in f["submits"]:
            for c in sub["cmds"]:
                cur = None
                for e in c["events"]:
                    if e["op"] == "pass": cur = [] if e["color"] and len(e["color"]) == 5 else None
                    elif e["op"] == "draw" and cur is not None: draws[e.get("fs", "")] += e.get("n", 1)
                    elif e["op"] == "end": cur = None
    for h, out in outcome.items():
        k = int(h, 16)
        name = "(not in the asset DB)"
        slots = {}
        if k in shd:
            _h, first, cnt, *_ = shd[k]
            v = var[sref[first][0]]
            name = r.string(tpl[v[0]][0])
            for pr in prm[v[10]:v[10] + v[7]]:
                slots.setdefault(pr[0], pr)
        w = draws.get(h, 0) if frame else 1
        fam[name]["shaders"] += 1
        fam[name]["weight"] += w
        if out.endswith("+alb"): fam[name]["alb"] += w
        line = lines.get(h, "")
        m = re.search(r" alb=vt:(\d+)\.(\d+):(\d):(\d):", line)
        va = slots.get(role.get("vt_albedo", -1))
        if m and va:
            ok = (int(m.group(1)), int(m.group(2))) == (va[2], va[3])
            agree["vt albedo binding agrees" if ok else "vt albedo binding DIFFERS"] += 1
        elif m:
            agree["vt albedo, template has no VT albedo slot"] += 1
    print("cross-check against the asset DB templates: %s" % dict(agree))
    print("%-48s %7s %8s %s" % ("family (template)", "shaders", "draws" if frame else "", "base colour known"))
    for name, c in sorted(fam.items(), key=lambda kv: -kv[1]["weight"])[:40]:
        print("%-48s %7d %8s %5.1f%%" % (name[:48], c["shaders"], c["weight"] if frame else "",
                                         100.0 * c["alb"] / max(c["weight"], 1)))


if __name__ == "__main__":
    main()
