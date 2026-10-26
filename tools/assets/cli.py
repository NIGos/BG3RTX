"""Command line: python -m tools.assets <command>

  build  --out data/assetdb [--game DIR] [--json] [--jobs N] [--no-effects] [--no-names] [--honour]
  info   [--db data/assetdb/bg3rtx_assetdb.bin]      counts and section sizes of a built database
  ls     PATTERN [--game DIR]                         list package files (fnmatch pattern)
  dump   PATH [--game DIR] [--depth N]                print an LSF/LSX resource as JSON
  shader HASH [--db ...]                              look a shader hash up (material or engine)
  join   --db ... --dump captures/shaders [--frame captures/<ts>/frame.json]
         offline shader join: runtime shader dump (and a trace's draws) against the database
"""
from __future__ import annotations

import argparse
import json
import os
import sys

from . import dbformat
from .build import DEFAULT_GAME, build, find_data_dir


def _db(path):
    with open(path, "rb") as f:
        return dbformat.Reader(f.read())


def cmd_info(a):
    r = _db(a.db)
    print(f"{a.db}: {len(r.data)} bytes")
    for tag, (off, size, count, rsize) in r.toc.items():
        print(f"  {tag}  {count:8d} x {rsize:4d} B = {size:9d} B")


def _shader_index(r):
    shd = {row[0]: row for row in r.rows("SHDR")}
    eng = {row[0]: row for row in r.rows("ESHD")}
    return shd, eng


def cmd_shader(a):
    from .extract.materials import CLASSES, FLAG_NAMES, KINDS, ROLES, VARIANTS
    r = _db(a.db)
    h = int(a.hash, 16)
    shd, eng = _shader_index(r)
    if h in eng:
        print("engine shader", r.string(eng[h][1]))
        return
    if h not in shd:
        cand = [k for k in list(shd) + list(eng) if f"{k:016x}".startswith(a.hash.lower())]
        if len(cand) == 1:
            h = cand[0]
        else:
            print("not found" if not cand else f"ambiguous prefix: {len(cand)} matches")
            return
        if h in eng:
            print("engine shader", r.string(eng[h][1]))
            return
    tpl, var, prm, sref = r.rows("MTPL"), r.rows("MVAR"), r.rows("MPRM"), r.rows("SREF")
    _h, first, cnt, stage, pss, canon, _ = shd[h]
    for k in range(first, first + cnt):
        v = var[sref[k][0]]
        t = tpl[v[0]]
        print(f"{r.string(t[0])}:{VARIANTS[v[1]]}  class={CLASSES[t[2]]} shading={t[3]} "
              f"flags={','.join(n for b, n in enumerate(FLAG_NAMES) if t[1] >> b & 1)}")
        if k == first:
            print(f"  engine cb s{v[2]}b{v[3]} ({v[8]} B), material cb s{v[4]}b{v[5]} ({v[9]} B)")
            for p in prm[v[10]:v[10] + v[7]]:
                where = f"s{p[2]}b{p[3]}" + (f"+{p[4]}" if p[4] != 0xFFFF else "")
                print(f"    {ROLES[p[0]]:24s} {KINDS[p[1]]:9s} {where:12s} {r.string(p[7])}")


def cmd_join(a):
    import collections
    r = _db(a.db)
    shd, eng = _shader_index(r)
    names = [f[:16] for f in os.listdir(a.dump) if f.endswith(".spv")]
    hs = [int(n, 16) for n in names]
    m = sum(1 for h in hs if h in shd)
    e = sum(1 for h in hs if h in eng)
    print(f"runtime shader dump: {len(hs)} modules, {m} material ({100 * m / max(len(hs), 1):.1f}%), "
          f"{e} engine ({100 * e / max(len(hs), 1):.1f}%), unmatched {len(hs) - m - e}")
    for fr in a.frame or []:
        d = json.load(open(fr, encoding="utf-8"))
        draws = collections.Counter()
        for sub in d.get("submits", []):
            for c in sub.get("cmds", []):
                for ev in c.get("events", []):
                    if ev.get("op", "").startswith("draw") or "fs" in ev:
                        n = ev.get("n", 1)
                        for st in ("vs", "fs"):
                            if ev.get(st):
                                hv = int(ev[st], 16)
                                k = "material" if hv in shd else "engine" if hv in eng else "none"
                                draws[(st, k)] += n
        tot = {st: sum(v for (s, _), v in draws.items() if s == st) for st in ("vs", "fs")}
        print(f"{fr}: " + ", ".join(
            f"{st} draws {tot[st]}: material {draws[(st, 'material')]} ({100 * draws[(st, 'material')] / max(tot[st], 1):.1f}%), "
            f"engine {draws[(st, 'engine')]}, none {draws[(st, 'none')]}" for st in ("vs", "fs")))


def cmd_ls(a):
    from .lspk import PackageSet
    ps = PackageSet(find_data_dir(a.game))
    for n in sorted(ps.names(a.pattern)):
        p, e = ps.index[n]
        print(f"{os.path.basename(p.path):24s} {e.size:10d}  {n}")


def cmd_dump(a):
    from . import lsf, lsx
    from .lspk import PackageSet
    ps = PackageSet(find_data_dir(a.game))
    data = ps.read(a.path)
    res = lsf.parse(data) if data[:4] == b"LSOF" else lsx.parse(data)
    json.dump([n.to_dict(a.depth) for n in res.regions], sys.stdout, indent=1, default=str)
    print()


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python -m tools.assets", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build", help="build data/assetdb from the game install (read-only on the game)")
    b.add_argument("--out", default="data/assetdb")
    b.add_argument("--game", default=os.environ.get("BG3_DIR", DEFAULT_GAME))
    b.add_argument("--jobs", type=int, default=0, help="worker processes (default: CPU count, max 16)")
    b.add_argument("--json", action="store_true", help="also write readable JSON under <out>/json")
    b.add_argument("--no-effects", action="store_true", help="skip the .lsfx effect lights (saves ~10 s)")
    b.add_argument("--no-names", action="store_true", help="drop light/trigger/preset names and probe paths")
    b.add_argument("--honour", action="store_true", help="include Honour-mode level additions")
    i = sub.add_parser("info")
    i.add_argument("--db", default="data/assetdb/bg3rtx_assetdb.bin")
    s = sub.add_parser("shader")
    s.add_argument("hash")
    s.add_argument("--db", default="data/assetdb/bg3rtx_assetdb.bin")
    j = sub.add_parser("join")
    j.add_argument("--db", default="data/assetdb/bg3rtx_assetdb.bin")
    j.add_argument("--dump", required=True)
    j.add_argument("--frame", nargs="*")
    l = sub.add_parser("ls")
    l.add_argument("pattern")
    l.add_argument("--game", default=os.environ.get("BG3_DIR", DEFAULT_GAME))
    d = sub.add_parser("dump")
    d.add_argument("path")
    d.add_argument("--game", default=os.environ.get("BG3_DIR", DEFAULT_GAME))
    d.add_argument("--depth", type=int, default=64)
    a = ap.parse_args(argv)
    if a.cmd == "build":
        build(a.game, a.out, a.jobs, a.json, a.honour, a.no_effects, not a.no_names)
    else:
        {"info": cmd_info, "shader": cmd_shader, "join": cmd_join, "ls": cmd_ls, "dump": cmd_dump}[a.cmd](a)
