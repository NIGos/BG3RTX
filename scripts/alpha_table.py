"""Track D phase 3: classify BG3's depth-prepass fragment shaders for ray-query alpha testing.

Reads the shader dump (captures/shaders/*.spv, written by dump_shaders=1), decompiles every
depth-only fragment shader that discards (spirv-cross -V, plus --reflect for bindings and
std140 offsets) and recognises the two families of alpha test that cover BG3's cutouts:

  vt   virtual texture (Granite): the albedo tile's alpha, read through an indirection
       texture (texelFetch, page index in the bits of .x) from the tile cache array; per-draw
       parameters in a UBO (uv transform vec4[2] and an index) and an SSBO entry (two row-major
       mat4 of tile-set constants) selected by that index.
  tex  a plain texture: texture(sampler2D(T, s), uv [* ubo scale | * const]).w (or .x).

Writes shaders/rt/alpha_fs.txt, one line per recognised shader:

  <fs hash> vt  uv=L cut=C ubo=S.B p0=OFF idx=OFF ssbo=S.B ind=S.B cache=S.B col=K
  <fs hash> tex uv=L cut=C tex=S.B ch=K [ubo=S.B scale=OFF | k=CONST]

The layer (layer/alphatest.cpp) loads it at startup. Anything else (no texture, dithering
only, the camera cut-away fade, unrecognised shapes) is left out and stays opaque. Fade and
vertex-colour factors in the condition are ignored: the test is "alpha >= cut".

  py -3 scripts\\alpha_table.py [--dump captures\\shaders] [--frame captures\\<ts>\\frame.json]

With --frame the draw counts of that trace's depth prepass are reported per family.
"""
import argparse, concurrent.futures as cf, json, os, pathlib, re, shutil, subprocess, sys, collections

ROOT = pathlib.Path(__file__).resolve().parent.parent


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
    if "gl_Position" in s or "local_size_x" in s or "discard" not in s: return None
    if re.search(r"layout\(location = \d+\) out ", s): return None   # has colour outputs
    j = cache / (spv.stem + ".json")
    if not j.exists():
        r = subprocess.run([tool, str(spv), "--reflect"], capture_output=True)
        if r.returncode: return None
        j.write_bytes(r.stdout)
    return s, json.loads(j.read_text())


def slice_of(body, expr, depth=12):
    """Text of `expr` plus the definitions of every _N it references, transitively."""
    defs = {}
    for m in re.finditer(r"^\s*(?:[\w]+\s+)?(_\d+)\s*=\s*(.*?);\s*$", body, re.M):
        defs.setdefault(m.group(1), m.group(2))
    seen, out, todo = set(), [expr], [expr]
    for _ in range(depth):
        nxt = []
        for t in todo:
            for v in re.findall(r"\b(_\d+)\b", t):
                if v in seen or v not in defs: continue
                seen.add(v)
                out.append(defs[v])
                nxt.append(defs[v])
        todo = nxt
    return "\n".join(out)


def cutoff(cond):
    m = re.search(r"step\(([0-9.]+),", cond)
    if m: return float(m.group(1))
    m = re.findall(r"-\s*([0-9]*\.[0-9]+)\)\s*<\s*0\.0", cond)
    if m: return float(m[-1])
    return 0.5


def classify(h, s, refl):
    body = s[s.find("void main"):]
    conds = [m.group(1) for m in re.finditer(r"if \((.*)\)\s*\n\s*\{\s*\n\s*discard;", body)]
    if not conds: return None, "no condition"
    # bindings by GLSL name
    res = {}
    for kind in ("separate_images", "separate_samplers", "ubos", "ssbos", "textures"):
        for r in refl.get(kind, []):
            name = r["name"].split("_")[-1] if kind in ("ubos", "ssbos") else r["name"]
            res["_" + name if kind in ("ubos", "ssbos") else name] = (kind, r)
    inputs = {i["name"]: i for i in refl.get("inputs", [])}
    types = refl.get("types", {})

    def binding(var):
        k = res.get(var)
        return (k[1]["set"], k[1]["binding"]) if k else None

    def member_offset(var, member):
        k = res.get(var)
        if not k: return None
        t = types.get(k[1]["type"], {})
        for m in t.get("members", []):
            if m["name"] == member: return m["offset"]
        return None

    for cond in conds:
        sl = slice_of(body, cond)
        cut = cutoff(cond)
        # --- virtual texture: the 2202 sentinel selects the real sample
        m = re.search(r"textureGrad\(sampler2DArray\((_\d+), _\d+\), vec3\(.*?_m0\[1\]\[2\]\.([xyzw])", sl)
        if m and "2202" in sl and re.search(r"2202\.0\)\)\.w", sl):
            cache, col = m.group(1), "xyzw".index(m.group(2))
            ind = re.search(r"texelFetch\((_\d+),", sl)
            uv = re.search(r"fma\(fract\((_\d+)\), (_\d+)\.(_m\d+)\[0\]\.zw, \2\.\3\[0\]\.xy\)", sl)
            idx = re.search(r"(_\d+)\._m0\[(_\d+)\.(_m\d+)\]\._m0\[", sl)
            if not (ind and uv and idx): return None, "vt: shape"
            uvv, ubo = uv.group(1), uv.group(2)
            if uvv not in inputs or inputs[uvv]["type"] != "vec2": return None, "vt: uv not a vec2 input"
            if idx.group(2) != ubo: return None, "vt: index from another block"
            p0 = member_offset(ubo, uv.group(3))
            io = member_offset(ubo, idx.group(3))
            b = {n: binding(v) for n, v in (("ubo", ubo), ("ssbo", idx.group(1)), ("ind", ind.group(1)), ("cache", cache))}
            if None in b.values() or p0 is None or io is None: return None, "vt: binding"
            return ("vt", inputs[uvv]["location"], cut,
                    "ubo=%d.%d p0=%d idx=%d ssbo=%d.%d ind=%d.%d cache=%d.%d col=%d" % (
                        *b["ubo"], p0, io, *b["ssbo"], *b["ind"], *b["cache"], col)), "ok"
        # --- plain texture alpha. Only where the ray query's test is conservative
        # (never lets a ray through where the game keeps the pixel): the
        # condition reads the sample's .w/.x directly (inline, or through a
        # variable assigned once), possibly times other factors (fade, a second
        # mask), never blended (mix) or chosen by a branch.
        if "mix(" in cond:
            continue
        tex_re = r"texture\(sampler2D\((_\d+), _\d+\), (_\d+)(?: \* (?:(_\d+)\.(_m\d+)|vec2\(([0-9.]+)\)))?\)"
        cands = [(m, None) for m in re.finditer(tex_re + r"\.([wx])", cond)]
        for v, ch in re.findall(r"\b(_\d+)\.([wx])\b", cond):
            defs = re.findall(r"^\s*(?:vec4\s+)?%s = (.*?);\s*$" % v, body, re.M)
            if len(defs) != 1: continue   # assigned in branches
            m = re.fullmatch(tex_re, defs[0].strip())
            if m: cands.append((m, ch))
        for t, ch_var in cands:
            tex, uvv, ubo, mem, k = t.groups()[:5]
            ch = ch_var or t.group(6)
            # the cut-away fade samples noise as fma(texture(..., uv * 2).x, 2, -1)
            if ch == "x" and k == "2.0": continue
            if uvv not in inputs or inputs[uvv]["type"] != "vec2": continue
            tb = binding(tex)
            if not tb: continue
            extra = ""
            if ubo:
                off = member_offset(ubo, mem)
                ub = binding(ubo)
                if off is None or not ub: continue
                extra = " ubo=%d.%d scale=%d" % (*ub, off)
            elif k:
                extra = " k=%s" % k
            return ("tex", inputs[uvv]["location"], cut, "tex=%d.%d ch=%d%s" % (*tb, "xyzw".index(ch), extra)), "ok"
    return None, "no texture alpha"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", default=str(ROOT / "captures" / "shaders"))
    ap.add_argument("--cache", default=str(ROOT / "captures" / "alpha_cache"))
    ap.add_argument("--out", default=str(ROOT / "shaders" / "rt" / "alpha_fs.txt"))
    ap.add_argument("--frame", help="frame.json of a trace: report the depth prepass's draws per family")
    a = ap.parse_args()
    cache = pathlib.Path(a.cache)
    cache.mkdir(parents=True, exist_ok=True)
    tool = spirv_cross()
    spvs = sorted(pathlib.Path(a.dump).glob("*.spv"))
    with cf.ThreadPoolExecutor(16) as ex:
        dec = list(ex.map(lambda p: (p.stem, decompile(tool, p, cache)), spvs))
    rows, why = [], collections.Counter()
    fam = {}
    for h, d in dec:
        if not d: continue
        r, reason = classify(h, *d)
        why[r[0] if r else reason] += 1
        fam[h] = r[0] if r else reason
        if r: rows.append("%s %s uv=%d cut=%g %s" % (h, r[0], r[1], r[2], r[3]))
    pathlib.Path(a.out).write_text(
        "# Generated by scripts/alpha_table.py from the shader dump: depth-prepass fragment shaders\n"
        "# whose alpha test the ray queries reproduce (docs/TRACK_D_RT.md, phase 3).\n" + "\n".join(rows) + "\n")
    print("%d depth-only discarding fragment shaders: %s -> %s" % (sum(why.values()), dict(why), a.out))
    if a.frame:
        f = json.load(open(a.frame))
        best = None
        for sub in f["submits"]:
            for c in sub["cmds"]:
                cur = None
                for e in c["events"]:
                    if e["op"] == "pass":
                        cur = {"color": e["color"], "draws": collections.Counter()} if not e["color"] else None
                    elif e["op"] == "draw" and cur is not None:
                        cur["draws"][e.get("fs", "")] += e.get("n", 1)
                    elif e["op"] == "end" and cur is not None:
                        if not best or sum(cur["draws"].values()) > sum(best["draws"].values()): best = cur
                        cur = None
        if best:
            per = collections.Counter()
            for fs, n in best["draws"].items(): per[fam.get(fs, "no discard / not depth-only")] += n
            print("depth prepass of %s: %d draws: %s" % (a.frame, sum(best["draws"].values()), dict(per)))


if __name__ == "__main__":
    main()
