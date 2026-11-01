"""Structural normalizer and classifier for SPIRV-Cross GLSL fragment shaders.

The audited hit-material recipes (common/native_vt_surface.h) were found by renaming the
dumped GLSL (captures/alpha_cache/<hash>.glsl, `spirv-cross -V`) and diffing it by hand
(PLAN_M4 "next families"). This module does the same mechanically:

- Resources get canonical names from their kind and set, ordinal by binding within that
  (kind, set): `ubo1_0`, `texture2DArray1_2`, ... Binding numbers and block type names drop
  out; block members keep their declared types (a different layout is a different decode).
- Inputs and outputs are named by location (`in0`, `out2`), structs by their member list.
- Temporaries are value-numbered: a temporary's name is a hash of its type and its
  (already renamed) initializer. Inserting or removing one statement therefore does not
  rename every later temporary, and the statement multisets of two shaders can be diffed.
- A temporary that is written again later (`_219.z = ...`) also gets its occurrence count,
  so two equal initializers with different later writes never merge.

Classification of a shader X against a recipe R (both normalized):
  SUPPORTED  X is R.
  COPY       X equals R modulo names/bindings, or modulo float constants, or every
             statement that differs reaches exactly one G-buffer output (one term, e.g.
             RT2.y = 0.5 instead of the material UBO's reflectance).
  VARIANT    same sampling structure (per sampled image: its type and the sampling
             functions; same number of outputs), more than one output differs (different
             field decode, e.g. other UBO offsets).
  NEW        anything else.
"""
from __future__ import annotations

import difflib
import hashlib
import re
from collections import Counter, defaultdict
from dataclasses import dataclass, field

_ID = re.compile(r"\b_\d+(?:_\d+)?\b")
_FLOAT = re.compile(r"(?<![\w.])(?:\d+\.\d*|\.\d+)(?:[eE][+-]?\d+)?f?(?![\w.])")
_TOKEN = re.compile(r"[A-Za-z_]\w*|\d+\.\d*(?:[eE][+-]?\d+)?|\.\d+|\d+u?|<<|>>|[<>=!]=|&&|\|\||\S")
_STRUCT = re.compile(r"^struct\s+(_\d+)\s*\{(.*?)\};", re.S | re.M)
_BLOCK = re.compile(r"^layout\(([^)]*)\)\s*((?:readonly\s+|writeonly\s+|restrict\s+|coherent\s+)*(?:uniform|buffer))"
                    r"\s+(_\d+(?:_\d+)?)\s*\{(.*?)\}\s*(_\d+)\s*;", re.S | re.M)
_OPAQUE = re.compile(r"^layout\(([^)]*)\)\s*uniform\s+(\w+)\s+(_\d+)(\[\d*\])?\s*;", re.M)
_IO = re.compile(r"^layout\(location\s*=\s*(\d+)(?:,[^)]*)?\)\s*((?:flat\s+|noperspective\s+|centroid\s+)*)(in|out)\s+(\w+)\s+(_\d+)\s*;", re.M)
_GLOBAL = re.compile(r"^(?:const\s+)?(\w+)\s+(_\d+)(\[\d*\])?\s*(?:=\s*(.*?))?;\s*$", re.M)
_SAMPLE = re.compile(r"\b(texture\w*|texelFetch\w*|imageLoad|imageStore|subpassLoad)\s*\(\s*(?:\w+\(\s*)?(\w+)")
_DECL = re.compile(r"^(?:(?:precise|highp|mediump|lowp|const)\s+)*([A-Za-z_]\w*)\s+(_\d+)(\[\d+\])?\s*(?:=\s*(.*))?$", re.S)
_ASSIGN = re.compile(r"^(_\d+|\w+)((?:\.\w+|\[[^\]]*\])*)\s*(?:[-+*/%&|^]|<<|>>)?=(?!=)\s*(.*)$", re.S)
_TYPES = {"float", "int", "uint", "bool", "double"} | {f"{p}vec{n}" for p in ("", "i", "u", "b", "d") for n in (2, 3, 4)} \
    | {f"mat{a}" for a in ("2", "3", "4", "2x3", "2x4", "3x2", "3x4", "4x2", "4x3")}


def _h(s: str) -> str:
    return hashlib.sha1(s.encode()).hexdigest()[:10]


def _layout_kv(s: str) -> dict:
    out = {}
    for part in s.split(","):
        k, _, v = part.strip().partition("=")
        out[k.strip()] = v.strip()
    return out


def _members(body: str) -> list[str]:
    return [" ".join(m.split()) for m in body.split(";") if m.strip()]


@dataclass
class Shader:
    """A normalized shader: declarations, statements and helpers for the diff."""
    decls: dict = field(default_factory=dict)        # canonical name -> signature
    kinds: dict = field(default_factory=dict)        # canonical resource name -> type (texture2D, ubo, ...)
    stmts: list = field(default_factory=list)        # renamed main() statements, in order
    outputs: list = field(default_factory=list)      # canonical output names
    inputs: list = field(default_factory=list)
    funcs: list = field(default_factory=list)        # other functions (normalized text)
    defs: dict = field(default_factory=dict)         # temp -> its defining statement
    blocks: dict = field(default_factory=dict)       # canonical block name -> member declarations
    refs: dict = field(default_factory=dict)         # erase mode: statement -> its (block, member) refs

    def sampling(self) -> tuple:
        """Per sampled image: (type, sorted sampling functions); plus the output count."""
        per = defaultdict(set)
        for s in self.stmts + self.funcs:
            for fn, res in _SAMPLE.findall(s):
                if res in self.kinds:
                    per[res].add(fn)
        sig = sorted((self.kinds[r], tuple(sorted(f))) for r, f in per.items())
        return tuple(sig), len(self.outputs)


def _split_body(body: str) -> list[str]:
    out, cur, depth = [], [], 0
    for ch in body:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if depth == 0 and ch in ";{}":
            s = "".join(cur).strip()
            if ch == ";":
                if s:
                    out.append(s)
            else:
                out.append((s + " " + ch).strip())
            cur = []
            continue
        cur.append(ch)
    s = "".join(cur).strip()
    if s:
        out.append(s)
    return out


def _main_body(text: str) -> tuple[str, str]:
    """(text before main, body of main without its braces)."""
    m = re.search(r"\bvoid\s+main\s*\(\s*\)\s*\{", text)
    if not m:
        return text, ""
    i, depth = m.end(), 1
    j = i
    while j < len(text) and depth:
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
        j += 1
    return text[:m.start()], text[i:j - 1]


_MREF = re.compile(r"\b((?:ubo|ssbo|pc)\d+_\d+)\._m(\d+)\b")


def normalize(text: str, fold_floats: bool = False, erase_members: bool = False) -> Shader:
    """Parses SPIRV-Cross GLSL into a Shader with canonical names (see module doc).

    erase_members: block member indices become `_m?` (and block layouts drop out of the
    declarations); each statement's original (block, member) refs are kept in `refs`. Two
    shaders equal in this form differ only in which UBO/SSBO fields they read."""
    text = re.sub(r"//[^\n]*", "", text.replace("\r\n", "\n"))
    if fold_floats:
        text = _FLOAT.sub("K", text)
    head, body = _main_body(text)
    sh = Shader()
    ren: dict[str, str] = {}

    def rn(s: str) -> str:
        return _ID.sub(lambda m: ren.get(m.group(0), m.group(0)), s)

    for m in _STRUCT.finditer(head):
        mem = ";".join(rn(x) for x in _members(m.group(2)))
        name = "S" + _h(mem)
        ren[m.group(1)] = name
        sh.decls[name] = f"struct {{{mem}}}"
    res = []
    for m in _BLOCK.finditer(head):
        lay = _layout_kv(m.group(1))
        kind = "ssbo" if "buffer" in m.group(2) else ("pc" if "push_constant" in lay else "ubo")
        st = int(lay.get("set", 0) or 0)
        b = int(lay.get("binding", 0) or 0)
        q = ",".join(k for k in lay if k not in ("set", "binding"))
        mem = _members(m.group(4))
        res.append((kind, st, b, m.group(5), f"{m.group(2)} {q} " + ("{...}" if erase_members else f"{{{';'.join(mem)}}}"),
                    kind, mem))
    for m in _OPAQUE.finditer(head):
        lay = _layout_kv(m.group(1))
        res.append((m.group(2), int(lay.get("set", 0) or 0), int(lay.get("binding", 0) or 0), m.group(3),
                    f"{m.group(2)}{m.group(4) or ''}", m.group(2), None))
    ordinal = Counter()
    for kind, st, b, ident, sig, typ, mem in sorted(res, key=lambda r: (r[0], r[1], r[2])):
        name = f"{kind}{st}_{ordinal[(kind, st)]}"
        ordinal[(kind, st)] += 1
        ren[ident] = name
        sh.kinds[name] = typ
        if mem is not None:
            sh.blocks[name] = mem
    for kind, st, b, ident, sig, typ, mem in res:
        sh.decls[ren[ident]] = rn(sig)
    for m in _IO.finditer(head):
        name = f"{m.group(3)}{m.group(1)}"
        ren[m.group(5)] = name
        sh.decls[name] = f"{m.group(2).strip()} {m.group(4)}".strip()
        (sh.inputs if m.group(3) == "in" else sh.outputs).append(name)
    gcount = Counter()
    for m in _GLOBAL.finditer(head):
        if m.group(2) in ren:
            continue
        key = f"{m.group(1)}{m.group(3) or ''}={rn(m.group(4) or '')}"
        name = "g" + _h(key) + (f"#{gcount[key]}" if gcount[key] else "")
        gcount[key] += 1
        ren[m.group(2)] = name
        sh.decls[name] = key
    # other functions: their bodies, renamed, as opaque blobs
    for m in re.finditer(r"^(\w+)\s+(\w+)\s*\(([^)]*)\)\s*\{", head, re.M):
        if m.group(2) == "main":
            continue
        i, depth, j = m.end(), 1, m.end()
        while j < len(head) and depth:
            depth += {"{": 1, "}": -1}.get(head[j], 0)
            j += 1
        sh.funcs.append(" ".join(rn(head[m.start():j]).split()))

    raw = [" ".join(s.split()) for s in _split_body(body)]
    # temps written after their declaration
    declared, mutated = set(), set()
    for s in raw:
        d = _DECL.match(s)
        if d and (d.group(1) in _TYPES or d.group(1).startswith("_") or d.group(1) in ("sampler2D",)):
            declared.add(d.group(2))
            continue
        a = _ASSIGN.match(s)
        if a and a.group(1) in declared:
            mutated.add(a.group(1))
        for v in re.findall(r"(_\d+)\s*(?:\+\+|--)|(?:\+\+|--)\s*(_\d+)", s):
            mutated.update(x for x in v if x)
    def rb(s: str) -> tuple[str, list]:
        r = rn(s)
        if not erase_members:
            return r, []
        return _MREF.sub(r"\1._m?", r), [(a, int(b)) for a, b in _MREF.findall(r)]

    occ = Counter()
    for s in raw:
        d = _DECL.match(s)
        if d and (d.group(1) in _TYPES or d.group(1).startswith("_")):
            typ = rn(d.group(1)) + (d.group(3) or "")
            init, refs = rb(d.group(4)) if d.group(4) is not None else (None, [])
            key = f"{typ}={init}" if init is not None else f"{typ}#decl"
            if init is None or d.group(2) in mutated:
                name = ("m" if init is not None else "u") + _h(key) + f"#{occ[key]}"
                occ[key] += 1
            else:
                name = "t" + _h(key)
            ren[d.group(2)] = name
            if name in sh.defs:
                continue   # the same pure value again (SPIRV-Cross repeats inlined chains): CSE
            stmt = f"{typ} {name}" + (f" = {init}" if init is not None else "")
            sh.defs[name] = stmt
            sh.stmts.append(stmt)
            sh.refs.setdefault(stmt, refs)
        else:
            stmt, refs = rb(s)
            sh.stmts.append(stmt)
            sh.refs.setdefault(stmt, refs)
    return sh


_SIZES = {"float": (4, 4), "int": (4, 4), "uint": (4, 4), "bool": (4, 4),
          "vec2": (8, 8), "ivec2": (8, 8), "uvec2": (8, 8), "vec3": (16, 12), "ivec3": (16, 12),
          "uvec3": (16, 12), "vec4": (16, 16), "ivec4": (16, 16), "uvec4": (16, 16),
          "mat3": (16, 48), "mat4": (16, 64), "mat3x4": (16, 48), "mat4x3": (16, 64)}


def std140_offsets(members: list[str]) -> list:
    """Byte offset of each member (None from the first type this does not know)."""
    out, off = [], 0
    for m in members:
        m = re.sub(r"layout\([^)]*\)\s*", "", m)
        t = re.match(r"(\w+)\s+_m\d+(\[(\d*)\])?", m)
        if off is None or not t or t.group(1) not in _SIZES:
            out.append(None)
            off = None
            continue
        al, sz = _SIZES[t.group(1)]
        if t.group(2) is not None:
            n = int(t.group(3) or 0)
            al, sz = 16, ((sz + 15) // 16) * 16 * n
        off = (off + al - 1) // al * al
        out.append(off)
        off += sz
    return out


# ---- dependency reach -------------------------------------------------------------------



def _vars(s: str) -> list[str]:
    return re.findall(r"\b[tmug][0-9a-f]{10}(?:#\d+)?|\bout\d+\b", s)


def _lhs(s: str) -> str | None:
    d = re.match(r"^\S+\s+([tmu][0-9a-f]{10}(?:#\d+)?)(?:\s*=|$)", s)
    if d:
        return d.group(1)
    a = re.match(r"^([tmu][0-9a-f]{10}(?:#\d+)?|out\d+)((?:\.\w+|\[[^\]]*\])*)\s*(?:[-+*/%&|^]|<<|>>)?=(?!=)", s)
    return a.group(1) if a else None


def reach(sh: Shader) -> dict[str, set]:
    """Variable -> the outputs its value flows into (control flow ignored)."""
    users = defaultdict(set)
    for s in sh.stmts:
        lhs = _lhs(s)
        if lhs is None:
            continue
        for v in _vars(s):
            if v != lhs or not re.match(r"^\S+\s+" + re.escape(lhs), s):
                users[v].add(lhs)
    out: dict[str, set] = {}

    def go(v, stack=()):
        if v in out:
            return out[v]
        if v in stack:
            return set()
        r = {v} if v.startswith("out") else set()
        for u in users.get(v, ()):
            if u != v:
                r |= go(u, stack + (v,))
        out[v] = r
        return r

    for v in list(users) + list(sh.outputs):
        go(v)
    return out


def _affected(sh: Shader, stmts) -> set:
    r = reach(sh)
    aff = set()
    for s in stmts:
        lhs = _lhs(s)
        if lhs is None:
            return {"*control*"}
        aff |= r.get(lhs, set()) if not lhs.startswith("out") else {lhs}
    return aff


# ---- comparison -------------------------------------------------------------------------

CLASS_ORDER = {"SUPPORTED": 0, "COPY": 1, "VARIANT": 2, "NEW": 3}


@dataclass
class Verdict:
    cls: str
    recipe: str
    term: str
    diff_stmts: int
    outputs: tuple
    fields: tuple = ()   # VARIANT "fields only": (block, recipe offset, shader offset) per moved field


def _short(s: str, n: int = 70) -> str:
    s = " ".join(s.split())
    return s if len(s) <= n else s[:n - 3] + "..."


def _term(a: Shader, b: Shader, out: str) -> str:
    """The replaced token spans of output `out`'s statement, recipe (a) -> shader (b)."""
    sa = [s for s in a.stmts if _lhs(s) == out]
    sb = [s for s in b.stmts if _lhs(s) == out]
    if not sa or not sb:
        return f"{out} assignment differs"
    ta, tb = _TOKEN.findall(sa[-1]), _TOKEN.findall(sb[-1])
    ops = [o for o in difflib.SequenceMatcher(None, ta, tb, autojunk=False).get_opcodes() if o[0] != "equal"]
    if not ops:
        return f"{out}: same expression, its inputs differ"
    i1, i2, j1, j2 = ops[0][1], ops[-1][2], ops[0][3], ops[-1][4]
    # widen an empty side to the enclosing token so the term reads as a replacement
    if i1 == i2 and i1 < len(ta):
        i2 += 1
        j2 = max(j2, min(len(tb), j1 + 1 + (j2 - j1)))
    return f"{out}: `{_compact(ta[i1:i2])}` -> `{_compact(tb[j1:j2])}`"


def _compact(tokens: list[str], n: int = 64) -> str:
    s = re.sub(r"\s*([(),.\[\]])\s*", r"\1", " ".join(tokens)).replace(",", ", ") or "(none)"
    return s if len(s) <= n else s[:n // 2 - 2] + " ... " + s[-(n // 2 - 3):]


def _one_term(recipe: Shader, x: Shader, name: str) -> Verdict | None:
    """COPY when the shaders are equal or every differing statement reaches one output."""
    ca, cb = Counter(recipe.stmts), Counter(x.stmts)
    only_a, only_b = ca - cb, cb - ca
    ndiff = sum(only_a.values()) + sum(only_b.values())
    dec_names = sorted({k for k, _ in set(recipe.decls.items()) ^ set(x.decls.items())})
    if not ndiff and not dec_names and recipe.funcs == x.funcs:
        return Verdict("COPY", name, "identical modulo names and bindings", 0, ())
    aff = _affected(recipe, only_a) | _affected(x, only_b)
    same_io = recipe.outputs == x.outputs and recipe.inputs == x.inputs
    if not (same_io and "*control*" not in aff and len(aff) <= 1 and recipe.funcs == x.funcs):
        return None
    out = next(iter(aff)) if aff else ""
    t = _term(recipe, x, out) if out else "dead code only"
    feed = [st for st in only_b if _lhs(st) != out]
    if feed:
        lhs = {_lhs(st) for st in feed}
        roots = [st for st in feed if not (set(_vars(st.split("=", 1)[-1])) & lhs)] or feed
        root = min(roots, key=len)
        t += f" (+{len(feed)} statements feeding it, e.g. `{_compact(_TOKEN.findall(root.split('=', 1)[-1]), 72)}`)"
    if dec_names:
        t += "; decls: " + ", ".join(("-" if k in recipe.decls and k not in x.decls else
                                      "+" if k not in recipe.decls else "~") + k for k in dec_names)
    return Verdict("COPY", name, t, ndiff, tuple(sorted(aff)))


def member_map(recipe_e: Shader, x_e: Shader, recipe: Shader, x: Shader) -> tuple[list[str], tuple]:
    """Field remapping recipe -> x (erase-mode forms), with std140 byte offsets when known."""
    votes = defaultdict(Counter)
    for st, ra in recipe_e.refs.items():
        rx = x_e.refs.get(st)
        if rx is None or len(rx) != len(ra):
            continue
        for (ba, ma), (bx, mx) in zip(ra, rx):
            if ba == bx:
                votes[(ba, ma)][mx] += 1
    out, fields = [], []
    for (blk, ma), c in sorted(votes.items()):
        mx = c.most_common(1)[0][0]
        if mx == ma:
            continue
        oa = std140_offsets(recipe.blocks.get(blk, []))
        ox = std140_offsets(x.blocks.get(blk, []))
        fa = f"@{oa[ma]}" if ma < len(oa) and oa[ma] is not None else ""
        fx = f"@{ox[mx]}" if mx < len(ox) and ox[mx] is not None else ""
        out.append(f"{blk}._m{ma}{fa}->_m{mx}{fx}")
        if fa and fx:
            fields.append((blk, oa[ma], ox[mx]))
    return out, tuple(fields)


@dataclass
class Forms:
    plain: Shader
    folded: Shader
    erased: Shader

    @classmethod
    def of(cls, text: str) -> "Forms":
        return cls(normalize(text), normalize(text, fold_floats=True), normalize(text, erase_members=True))


def compare(recipe: Forms, x: Forms, recipe_name: str = "") -> Verdict:
    """Classifies x against one recipe (see module doc)."""
    v = _one_term(recipe.plain, x.plain, recipe_name)
    if v:
        return v
    a, b = recipe.folded, x.folded
    if Counter(a.stmts) == Counter(b.stmts) and a.decls == b.decls:
        only_a = Counter(recipe.plain.stmts) - Counter(x.plain.stmts)
        only_b = Counter(x.plain.stmts) - Counter(recipe.plain.stmts)
        fa, fb = Counter(_FLOAT.findall(" ".join(only_a))), Counter(_FLOAT.findall(" ".join(only_b)))
        return Verdict("COPY", recipe_name, "constants only: " + _short(
            ", ".join(f"{k}->" for k in (fa - fb)) + " " + ", ".join(f"->{k}" for k in (fb - fa)), 60),
            sum(only_a.values()) + sum(only_b.values()), ())
    pa, pb = recipe.plain, x.plain
    only_a, only_b = Counter(pa.stmts) - Counter(pb.stmts), Counter(pb.stmts) - Counter(pa.stmts)
    ndiff = sum(only_a.values()) + sum(only_b.values())
    if pa.sampling() != pb.sampling():
        sig, nout = pb.sampling()
        desc = ", ".join(f"{t}:{'/'.join(f)}" + (f" x{n}" if n > 1 else "")
                         for (t, f), n in sorted(Counter(sig).items()))
        return Verdict("NEW", recipe_name, f"sampling {desc or 'none'}; {nout} outputs", ndiff, ())
    e = _one_term(recipe.erased, x.erased, recipe_name)
    if e:
        m, fields = member_map(recipe.erased, x.erased, pa, pb)
        t = "fields only: " + (", ".join(m) or "same members")
        if e.diff_stmts:
            t += "; plus " + e.term
        return Verdict("VARIANT", recipe_name, t, e.diff_stmts, e.outputs, fields)
    aff = tuple(sorted(_affected(pa, only_a) | _affected(pb, only_b)))
    return Verdict("VARIANT", recipe_name,
                   f"same sampling, {ndiff} statements differ, outputs {','.join(aff) or '-'}"
                   + ("" if pa.inputs == pb.inputs else ", other VS inputs"), ndiff + 1000, aff)


_FORMS: dict[str, Forms] = {}


def classify(x_hash: str, x_text: str, recipes: dict[str, str]) -> Verdict:
    """Best verdict of shader x over the recipes {hash: glsl}."""
    if x_hash in recipes:
        return Verdict("SUPPORTED", x_hash, "audited recipe", 0, ())
    x = Forms.of(x_text)
    best = None
    for h, t in recipes.items():
        if h not in _FORMS:
            _FORMS[h] = Forms.of(t)
        v = compare(_FORMS[h], x, h)
        if best is None or (CLASS_ORDER[v.cls], v.diff_stmts) < (CLASS_ORDER[best.cls], best.diff_stmts):
            best = v
    return best
