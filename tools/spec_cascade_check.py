"""rt_spec_cascade equivalence gate (docs/BUILD_AND_PACKAGE_EFFICIENCY.md 1, step S1). Offline, no game.

    py -3 tools/spec_cascade_check.py [--all | --sample N] [--seed S] [--jobs J] [--replay <pack dir>]

Every RT_SPEC_CASCADE request of shaders/variants.txt stands for two old requests: constant 1000 = 0 is the
request without the spec defines (the plain variant), = 1 the same request with RT_CASCADE_MASKS=1 (and
RT_CASCADE_SUN=1 for rt_shadow.hlsl) in their place. For each checked request and value, the spec module is
specialized (spirv-opt --set-spec-const-default-value), frozen and folded (--freeze-spec-const
--fold-spec-const-op-composite) and optimized (-O), the old module goes through the same -O, and the two are
compared up to id numbering and declaration order (canonical()): equal = the same program the driver compiles.
A control pair (old plain vs old cascade of the first file) must differ, or the comparison is blind.
Default sample: each file's request with the fewest defines plus N (24) random others; --all checks every one.
--replay: gi_hit_direct on the pack (bin/frame_replay.exe --spv) with the old plain, old cascade and spec 0 / 1
modules: giRaw, giRawDepth and rtStats must be bit-identical per value (outputs to a temp dir, never the pack).
Compiles go through tools/shader_identity.py (shared cache, DXC slots); progress at least every 30 s.
Exit code 1 on any difference or failure.
"""
import argparse
import concurrent.futures
import hashlib
import random
import re
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from tools.shader_identity import (Cache, MemoryCache, Progress, SourceTree, Toolchain, compile_requests, default_jobs,  # noqa: E402
                                   lower_priority, split_request)

ROOT = Path(__file__).resolve().parents[1]
SPEC_ID = 1000
SPEC, SPEC_SUN = "RT_SPEC_CASCADE=1", "RT_SPEC_CASCADE_SUN=1"
RX = re.compile(r"%\d+")
DEF = re.compile(r"\s*(%\d+) = (Op\w+)(.*)$")


def say(msg):
    print(msg, flush=True)


def old_requests(spec_request):
    """(old plain, old cascade) request strings of a spec request, in common/shader_variant.h's order."""
    file, defs = split_request(spec_request)
    sun = SPEC_SUN in defs
    plain = [d for d in defs if d not in (SPEC, SPEC_SUN)]
    casc = []
    for d in defs:
        if d == SPEC:
            casc += ["RT_CASCADE_MASKS=1"] + (["RT_CASCADE_SUN=1"] if sun else [])
        elif d != SPEC_SUN:
            casc.append(d)
    join = lambda ds: file + ("?" + ";".join(ds) if ds else "")  # noqa: E731
    return join(plain), join(casc)


def opt(tool, src, dst, *passes):
    r = subprocess.run([str(tool.dxc.with_name("spirv-opt.exe")), *passes, str(src), "-o", str(dst)],
                       capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(f"spirv-opt {passes}: {r.stdout}{r.stderr}")
    return dst


LABEL = re.compile(r"\s*(%\d+) = OpLabel")


def block_order(body):
    """The function bodies with each function's blocks in the order they are first referenced from the entry
    block (breadth first over the instruction text), not DXC's layout order: two layouts of the same control flow
    (seen: an if/else whose arms DXC emitted in the other order in rt_shadow) then compare equal."""
    out, fn = [], []

    def flush():
        if not fn:
            return
        head, blocks, cur = [], {}, None
        order = []
        for l in fn:
            m = LABEL.match(l)
            if m:
                cur = m.group(1)
                blocks[cur] = [l]
                order.append(cur)
            elif cur is None:
                head.append(l)
            else:
                blocks[cur].append(l)
        tail = []
        if order and blocks[order[-1]] and "OpFunctionEnd" in blocks[order[-1]][-1]:
            tail = [blocks[order[-1]].pop()]
        seen, queue = [], order[:1]
        while queue:
            b = queue.pop(0)
            if b in seen:
                continue
            seen.append(b)
            for l in blocks[b][1:]:
                for x in RX.findall(l):
                    if x in blocks and x not in seen and x not in queue:
                        queue.append(x)
        seen += [b for b in order if b not in seen]   # unreachable blocks keep their order
        out.extend(head + [l for b in seen for l in blocks[b]] + tail)
        fn.clear()
    for l in body:
        fn.append(l)
        if "OpFunctionEnd" in l:
            flush()
    flush()
    return out


def canonical(tool, spv):
    """(sorted global lines, body lines) of a module up to id numbering and declaration order: globals
    (types, constants, undefs, variables) are named by their hash-consed definition, function-body ids by
    order of appearance; debug names dropped."""
    t = subprocess.run([str(tool.dxc.with_name("spirv-dis.exe")), "--raw-id", "--no-header", str(spv)],
                       capture_output=True, text=True, check=True).stdout
    lines = [l for l in t.splitlines() if l.strip() and "OpName" not in l and "OpMemberName" not in l]
    first = next(i for i, l in enumerate(lines) if " OpFunction " in l)
    glob, body = lines[:first], lines[first:]
    defs = {m.group(1): (m.group(2), m.group(3)) for m in map(DEF.match, glob) if m}
    canon = {}

    def name(x, depth=0):
        if x not in defs or depth > 64:
            return x
        if x not in canon:
            canon[x] = "rec"
            op, rest = defs[x]
            canon[x] = "g" + hashlib.sha1((op + RX.sub(lambda m: name(m.group(0), depth + 1), rest)).encode()).hexdigest()[:12]
        return canon[x]
    for x in defs:
        name(x)
    body = block_order(body)
    ren = {}
    b = [RX.sub(lambda m: canon.get(m.group(0)) or ren.setdefault(m.group(0), f"%v{len(ren)}"), l).strip() for l in body]
    g = sorted(RX.sub(lambda m: canon.get(m.group(0)) or ren.get(m.group(0), m.group(0)), l).strip() for l in glob)
    return g, b


def differ(a, b):
    (ga, ba), (gb, bb) = a, b
    return len(set(ga) ^ set(gb)) + abs(len(ga) - len(gb)) + sum(x != y for x, y in zip(ba, bb)) + abs(len(ba) - len(bb))


def check_one(tool, tmp, i, spec_path, old_path, value):
    d = Path(tmp) / f"{i}_{value}"
    d.mkdir()
    s = opt(tool, spec_path, d / "s.spv", f"--set-spec-const-default-value={SPEC_ID}:{value}")
    s = opt(tool, s, d / "f.spv", "--freeze-spec-const", "--fold-spec-const-op-composite")
    s = opt(tool, s, d / "sO.spv", "-O")
    o = opt(tool, old_path, d / "oO.spv", "-O")
    return differ(canonical(tool, s), canonical(tool, o))


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()[:16]


def bin_dir():
    for c in (ROOT / "bin", ROOT.parents[2] / "bin"):
        if (c / "frame_replay.exe").exists():
            return c
    return ROOT / "bin"


def replay(tool, P, pack, tmp):
    spec = "gi/gi_trace_rt_hit_direct.hlsl?" + SPEC
    p0, p1 = old_requests(spec)
    t = Path(tmp)
    mods = {"old0": P(p0), "old1": P(p1)}
    for v in (0, 1):
        mods[f"new{v}"] = opt(tool, P(spec), t / f"replay_new{v}.spv", f"--set-spec-const-default-value={SPEC_ID}:{v}")
    outs = {}
    for k, m in mods.items():
        o = t / f"replay_{k}"
        t0 = time.perf_counter()
        r = subprocess.run([str(bin_dir() / "frame_replay.exe"), "--pack", str(pack), "--spv", str(m), "--out", str(o)],
                           capture_output=True, text=True)
        if r.returncode:
            say(f"FAIL replay {k}\n{r.stdout[-3000:]}{r.stderr[-2000:]}")
            return False
        gpu = next((l.strip() for l in r.stdout.splitlines() if "GPU" in l and "ms" in l), "")
        outs[k] = {f: sha(o / f) for f in ("giRaw.raw", "giRawDepth.raw", "rtstats.bin") if (o / f).exists()}
        say(f"[spec cascade] replay {k}: {time.perf_counter() - t0:.0f} s; {gpu}")
    ok = True
    for v in (0, 1):
        eq = outs[f"old{v}"] == outs[f"new{v}"] and len(outs[f"new{v}"]) == 3
        ok &= eq
        say(f"replay gi_hit_direct spec {v}: giRaw, giRawDepth, rtStats bit-identical to the old "
            f"{'cascade' if v else 'plain'} module: {eq}")
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--sample", type=int, default=24)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--jobs", type=int, default=default_jobs())
    ap.add_argument("--replay", help="replay pack directory (read only)")
    args = ap.parse_args()
    lower_priority()
    tool = Toolchain()
    tree = SourceTree.worktree(ROOT).with_sharc(tool.sharc)
    known = set(tree.requests())
    spec = sorted(r for r in known if SPEC in split_request(r)[1])
    if not spec:
        say("no RT_SPEC_CASCADE request in shaders/variants.txt")
        return 1
    by_file = {}
    for r in spec:
        f = split_request(r)[0]
        if f not in by_file or len(split_request(r)[1]) < len(split_request(by_file[f])[1]):
            by_file[f] = r
    pick = spec if args.all else sorted(set(by_file.values()) | set(
        random.Random(args.seed).sample([r for r in spec if r not in by_file.values()],
                                        min(args.sample, len(spec) - len(by_file)))))
    if args.replay:
        pick = sorted(set(pick) | {"gi/gi_trace_rt_hit_direct.hlsl?" + SPEC})
    pairs = {r: old_requests(r) for r in pick}
    # Once the spec build is the only cascade path the old requests are no longer listed; they still compile
    # from the same sources (the reference programs), so they are built here on demand.
    unlisted = sorted({o for r in pick for o in pairs[r] if o not in known})
    if unlisted:
        say(f"[spec cascade] {len(unlisted)} old reference requests not in shaders/variants.txt: compiled on demand")
    say(f"[spec cascade] {len(spec)} RT_SPEC_CASCADE requests over {len(by_file)} files; checking {len(pick)} "
        f"({'all' if args.all else f'each file once + {len(pick) - len(by_file)} random, seed {args.seed}'})")
    need = sorted(set(pick) | {o for r in pick for o in pairs[r]})
    compiled = compile_requests(tree, need, tool=tool, cache=Cache(), jobs=args.jobs, label="spec cascade")
    bad = [r for r in need if not compiled[r].ok]
    for r in bad:
        say(f"FAIL compile {r}\n{compiled[r].log[:3000]}")
    if bad:
        return 1
    fails, lock = [], threading.Lock()
    with tempfile.TemporaryDirectory(prefix="b3sc-") as tmp:
        # Private copies: another run's prune may evict the shared blobs while this one works (seen 2026-11-02).
        local, lost = {}, []
        for i, r in enumerate(need):
            for e in compiled[r].entries:
                dst = Path(tmp) / f"m{i}_{e.name}.spv"
                try:
                    dst.write_bytes(compiled[r].blob(e.name))
                    local[(r, e.name)] = dst
                except (OSError, TypeError):
                    lost.append(r)
        if lost:
            again = compile_requests(tree, sorted(set(lost)), tool=tool, cache=MemoryCache(), jobs=args.jobs,
                                     label="spec cascade (evicted, recompiled)")
            for i, r in enumerate(sorted(set(lost))):
                for e in again[r].entries:
                    dst = Path(tmp) / f"r{i}_{e.name}.spv"
                    dst.write_bytes(again[r].blob(e.name))
                    local[(r, e.name)] = dst
        P = lambda r, e="main": local[(r, e)]  # noqa: E731
        c0, c1 = pairs[pick[0]]
        ctrl = differ(canonical(tool, opt(tool, P(c0), Path(tmp) / "c0.spv", "-O")),
                      canonical(tool, opt(tool, P(c1), Path(tmp) / "c1.spv", "-O")))
        say(f"control: {c0} vs its cascade variant differ in {ctrl} lines (must be > 0)")
        jobs = [(i, r, e.name, v) for i, r in enumerate(pick) for e in compiled[r].entries for v in (0, 1)]
        with Progress("spec cascade equivalence", len(jobs), every=30, verb="compared") as prog, \
                concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            futs = {pool.submit(check_one, tool, tmp, f"{i}_{e}", P(r, e), P(pairs[r][v], e), v):
                    (r, e, v) for i, r, e, v in jobs}
            for f in concurrent.futures.as_completed(futs):
                r, e, v = futs[f]
                try:
                    n = f.result()
                except Exception as ex:  # noqa: BLE001
                    n = f"error {ex!r}"
                if n:
                    with lock:
                        fails.append((r, e, v, n))
                prog.step(failed=bool(n))
        ok_replay = replay(tool, P, args.replay, tmp) if args.replay else True
    for r, e, v, n in sorted(fails):
        say(f"DIFFERENT: {r} [{e}] value {v} vs {pairs[r][v]}: {n}")
    say(f"spec cascade: {len(pick) - len({r for r, _, _, _ in fails})}/{len(pick)} RT_SPEC_CASCADE requests are, "
        f"specialized to 0 and 1, the same program as their plain and RT_CASCADE_MASKS variants"
        f"{'' if ctrl else '; CONTROL BLIND'}{'' if ok_replay else '; REPLAY DIFFERS'}")
    return 1 if fails or not ctrl or not ok_replay else 0


if __name__ == "__main__":
    sys.exit(main())
