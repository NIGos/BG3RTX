"""rt_spec_cascade equivalence gate (docs/BUILD_AND_PACKAGE_EFFICIENCY.md 1, step S1). Offline, no game.

    py -3 tools/spec_cascade_check.py [--all | --sample N] [--seed S] [--jobs J] [--replay <pack dir>]
    python tools/spec_cascade_check.py --key gpuopt --all --baseline HEAD

GPU optimization candidates: cached compile/validation of every rt_common consumer and water compose;
frozen key-off compares directly against baseline. Unused interface variables and capabilities are
removed symmetrically before canonical comparison; executable instructions must still match.

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


def old_requests_watercasc(spec_request):
    """Water cascade off/on: sun-hit is a fixed pass marker in the union only."""
    file, defs = split_request(spec_request)
    plain = [d for d in defs if d not in ("WATER_SPEC_CASCADE=1", "RT_CASCADE_SUN_HIT=1")]
    on = ["RT_CASCADE_MASKS=1"] + (["RT_CASCADE_SUN_HIT=1"] if "WATER_SHADOWS=1" in defs else [])
    join = lambda ds: file + ("?" + ";".join(ds) if ds else "")
    return join(plain), join(plain + on)


GUARDS = "GI_SPEC_GUARDS=1"


def old_requests_guards(spec_request):
    """(guards off, guard on) of a GI_SPEC_GUARDS request (step G): the define of gi-reproj-guards in its place."""
    file, defs = split_request(spec_request)
    on = "GI_TEMPORAL_MV_SAMPLE=1" if file == "gi/gi_temporal.hlsl" else "GI_PREV_RADIANCE_GUARD=1"
    join = lambda ds: file + ("?" + ";".join(ds) if ds else "")  # noqa: E731
    return join([d for d in defs if d != GUARDS]), join([on if d == GUARDS else d for d in defs])


def guard_id(file):
    return 1001 if file == "gi/gi_temporal.hlsl" else 1002


FAMILY = "RT_SPEC_HIT_FAMILY=1"


def old_requests_family(spec_request):
    """Step S2a: [({1004: w, 1005: g}, old request)] for the four wind RGB / IP grass combinations."""
    file, defs = split_request(spec_request)
    out = []
    for w, g in ((0, 0), (1, 0), (0, 1), (1, 1)):
        add = (["RT_HIT_WIND_RGB=1"] if w else []) + (["RT_HIT_IP_GRASS=1"] if g else [])
        ds = []
        for d in defs:
            ds += add if d == FAMILY else [d]
        out.append(({1004: w, 1005: g}, file + ("?" + ";".join(ds) if ds else "")))
    return out


INST = "RT_SPEC_HIT_INST=1"


def old_requests_inst(spec_request):
    """Step S2c: [({1003: i, 1004: w, 1005: g}, old request)]: off, and instanced with each wind / IP grass pair."""
    file, defs = split_request(spec_request)
    out = []
    for i, w, g in ((0, 0, 0), (1, 0, 0), (1, 1, 0), (1, 0, 1), (1, 1, 1)):
        add = (["RT_HIT_INSTANCED=1"] if i else []) + (["RT_HIT_WIND_RGB=1"] if w else []) + \
              (["RT_HIT_IP_GRASS=1"] if g else [])
        ds = []
        for d in defs:
            ds += add if d == INST else [d]
        out.append(({1003: i, 1004: w, 1005: g}, file + ("?" + ";".join(ds) if ds else "")))
    return out


def old_requests_native(spec_request):
    """S3: both tileset values crossed with plain, native hair/scalp, physical hair origins."""
    file, defs = split_request(spec_request)
    physical = "RT_SPEC_HAIR_ORIGIN=1" in defs
    out = []
    for t in (0, 1):
        for h, p in ((0, 0), (1, 0), (1, 1)) if physical else ((0, 0), (1, 0)):
            ds = []
            for d in defs:
                if d == "RT_SPEC_NATIVE=1":
                    ds += (["RT_NATIVE_TILESET=1"] if t else [])
                    ds += (["RT_HAIR_PHYSICAL_ORIGIN=1"] if p else [])
                    ds += (["RT_NATIVE_SCALP=1", "RT_NATIVE_HAIR=1"] if h else [])
                elif d != "RT_SPEC_HAIR_ORIGIN=1":
                    ds.append(d)
            assign = {1008: t, 1010: h}
            if physical:
                assign[1009] = p
            out.append((assign, file + ("?" + ";".join(ds) if ds else "")))
    return out


def old_requests_swap(spec_request, marker, define):
    """(without the marker, with the old define in its place): a key whose spec build replaces one define."""
    file, defs = split_request(spec_request)
    join = lambda ds: file + ("?" + ";".join(ds) if ds else "")  # noqa: E731
    return join([d for d in defs if d != marker]), join([define if d == marker else d for d in defs])


def two(olds, id_of):
    """A key with one constant: [({id: 0}, old0), ({id: 1}, old1)]."""
    return lambda r: [({id_of(split_request(r)[0]): v}, o) for v, o in enumerate(olds(r))]


# --key: the spec define marking a request and its [(constant assignment, old request)] list.
# OMM remains a layout define (RT_OMM_SCENE), not constant 1012: S4 was rejected
# in BUILD_AND_PACKAGE_EFFICIENCY.md. It has no specialization request to check.
def old_requests_waterhair(spec_request):
    # Water routes both tileset coverage (1008) and scalp/hair (1010).
    return old_requests_native(spec_request)


def old_requests_waterhit(spec_request):
    # Cross compose shift and hit shading: the combined path must preserve TLAS-space H.
    compose = split_request(spec_request)[0] == "rt/water_compose.hlsl"
    out = []
    for shift in (0, 1) if compose else (0,):
        for value in (0, 1, 2):
            assign = {1016: value}
            reference = spec_request + f";WATER_HIT_SHADE_REFERENCE={value}"
            if compose:
                assign[1015] = shift
                reference += f";WATER_COMPOSE_SHIFT_REFERENCE={shift}"
            out.append((assign, reference))
    return out


def old_requests_watercost(spec_request):
    return [(a | {1021: cost}, r + f";WATER_HIT_COST_REFERENCE={cost}")
            for a, r in old_requests_waterhit(spec_request) if a[1016] in (1, 2)
            for cost in (0, 1, 2, 4, 7)]


def old_requests_hairgi(spec_request):
    """Hair absent/present crossed with GI culling off/on; native coverage stays literal in references."""
    out = []
    for assign, request in old_requests_native(spec_request):
        if assign[1008] or assign.get(1009, 0):
            continue
        for cull in (0, 1):
            out.append((assign | {1017: cull}, request +
                        (";" if "?" in request else "?") + f"RT_HAIR_GI_CULL_REFERENCE={cull}"))
    return out


def old_requests_groom(spec_request):
    """rt_hair_groom (1020) off: the program without the groom code, hair absent or present (the literal native
    references have none). On is a different program by design: --baseline <main> covers off against main."""
    return [(assign | {1020: 0}, request) for assign, request in old_requests_native(spec_request)
            if not assign[1008] and not assign.get(1009, 0)]


def old_requests_haircost(spec_request):
    out = []
    for assign, request in old_requests_native(spec_request):
        if assign[1008] or assign.get(1009, 0):
            continue
        for cheap, mean in ((0, 0), (1, 0), (0, 1), (1, 1)):
            out.append((assign | {1018: cheap, 1019: mean}, request +
                        (";" if "?" in request else "?") + f"RT_HAIR_CHEAP_REFERENCE={cheap};RT_HAIR_GI_MEAN_REFERENCE={mean}"))
    return out



def gpu_opt_full_trace_request(request):
    """Freeze trace scaling off too: main predates the half-trace layout variant."""
    file, defines = split_request(request)
    defines = [d for d in defines if not d.startswith("RT_TRACE_HALF=")]
    return file + ("?" + ";".join(defines) if defines else "")


def gpu_opt_requests(request):
    """Key-off reference; output exactness of ON is checked by numeric fixtures, separately."""
    return [({1024: 0, 1022: 0, 1023: 0}, request +
             (";" if "?" in request else "?") + "BG3RTX_GPU_OPT_LEGACY=1")]


KEYS = {"gpuopt": ("gpuopt", gpu_opt_requests), "haircost": ("RT_SPEC_NATIVE=1", old_requests_haircost), "hairgi": ("RT_SPEC_NATIVE=1", old_requests_hairgi),
        "groom": ("RT_SPEC_NATIVE=1", old_requests_groom),
        "waterhair": ("RT_SPEC_NATIVE=1", old_requests_waterhair),
        "waterhit": ("WATER_HIT_SHADE_LAYOUT=1", old_requests_waterhit),
        "watercost": ("WATER_HIT_SHADE_LAYOUT=1", old_requests_watercost),
        "watershift": ("WATER_SPEC_CASCADE=1", lambda r: [({1015: v}, r + f";WATER_COMPOSE_SHIFT_REFERENCE={v}") for v in (0, 1)]),
        "watercasc": ("WATER_SPEC_CASCADE=1", two(old_requests_watercasc, lambda f: SPEC_ID)),
        "occ": ("RT_SPEC_OCC_STATS=1", two(lambda r: old_requests_swap(r, "RT_SPEC_OCC_STATS=1", "RT_OCC_STATS=1"), lambda f: 1011)),
        "rfgi": ("RF_SPEC_HIT_GI=1", two(lambda r: old_requests_swap(r, "RF_SPEC_HIT_GI=1", "RF_HIT_GI=1"), lambda f: 1013)),
        "wateredge": ("WATER_SPEC_RF_EDGE=1", two(lambda r: old_requests_swap(r, "WATER_SPEC_RF_EDGE=1", "WATER_RF_EDGE=1"), lambda f: 1014)),
        "native": ("RT_SPEC_NATIVE=1", old_requests_native),
        "cascade": (SPEC, two(old_requests, lambda f: SPEC_ID)),
        "guards": (GUARDS, two(old_requests_guards, guard_id)),
        "hitfam": (INST, lambda r: old_requests_inst(r)[1:]),   # S2a regression on the S2c union, instanced = 1
        "hitinst": (INST, old_requests_inst),
        "gifar": ("RT_SPEC_GI_FAR=1", two(lambda r: old_requests_swap(r, "RT_SPEC_GI_FAR=1", "RT_GI_FAR=1"),
                                          lambda f: 1007))}


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
                # Phi predecessors are dataflow inputs, not successor/control references.
                if " OpPhi " in l:
                    continue
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


def phi_order(body, global_names):
    """Order parallel phi declarations and their intact predecessor/value pairs.

    SPIR-V phis at the start of a block execute simultaneously. DXC's SROA can
    emit their types in a different order after specialization. Like global
    declaration order, that order is not dataflow. Equal-type phis retain their
    order; no operation, operand, incoming edge or result ID is discarded.
    Incoming pairs follow the control-flow block order, never raw ID numbering.
    """
    out, phis = [], []
    labels = {m.group(1): i for i, line in enumerate(body) if (m := LABEL.match(line))}

    def flush():
        out.extend(sorted(phis, key=lambda line: global_names.get(DEF.match(line).group(3).split()[0],
                                                            DEF.match(line).group(3).split()[0])))
        phis.clear()
    for line in body:
        match = DEF.match(line)
        if match and match.group(2) == "OpPhi":
            args = match.group(3).split()
            # A phi chooses a value by predecessor identity. Permute complete pairs,
            # never values or predecessor edges independently.
            pairs = list(zip(args[1::2], args[2::2]))
            assert len(args) == 1 + 2 * len(pairs), "malformed phi"
            pairs.sort(key=lambda pair: labels.get(pair[1], len(body)))
            line = line[:match.start(3)] + " " + " ".join([args[0], *[x for pair in pairs for x in pair]])
            phis.append(line)
        else:
            flush()
            out.append(line)
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
    body = phi_order(block_order(body), canon)
    ren = {}
    b = [RX.sub(lambda m: canon.get(m.group(0)) or ren.setdefault(m.group(0), f"%v{len(ren)}"), l).strip() for l in body]
    g = sorted(RX.sub(lambda m: canon.get(m.group(0)) or ren.get(m.group(0), m.group(0)), l).strip() for l in glob)
    return g, b


def differ(a, b):
    (ga, ba), (gb, bb) = a, b
    return len(set(ga) ^ set(gb)) + abs(len(ga) - len(gb)) + sum(x != y for x, y in zip(ba, bb)) + abs(len(ba) - len(bb))


def check_one(tool, tmp, i, spec_path, old_path, assign):
    """The other constants of both modules (e.g. 1000 in a guards request) stay at their default 0 on both sides."""
    d = Path(tmp) / f"{i}_{'_'.join(f'{k}-{v}' for k, v in sorted(assign.items()))}"
    d.mkdir()
    s = opt(tool, spec_path, d / "s.spv",
            "--set-spec-const-default-value=" + " ".join(f"{k}:{v}" for k, v in sorted(assign.items())))
    s = opt(tool, s, d / "f.spv", "--freeze-spec-const", "--fold-spec-const-op-composite")
    # Unused capabilities (e.g. vote in gpuopt empty tiles) describe no live operation after folding.
    # Trim them symmetrically, without changing any executable instruction.
    # S2b: the far main() declares SV_GroupID / SV_GroupIndex even with the constant 0; dropping unused interface
    # variables on both sides compares the programs, not the declarations.
    s = opt(tool, s, d / "sO.spv", "-O", "--remove-unused-interface-variables", "--trim-capabilities")
    o = opt(tool, old_path, d / "of.spv", "--freeze-spec-const", "--fold-spec-const-op-composite")
    o = opt(tool, o, d / "oO.spv", "-O", "--remove-unused-interface-variables", "--trim-capabilities")
    return differ(canonical(tool, s), canonical(tool, o))


def check_baseline(tool, tmp, i, spec_path, old_path, assign, assign_reference):
    """One independent baseline comparison; each worker owns its reference specialization file."""
    reference = old_path
    if assign_reference:
        reference = opt(tool, old_path, Path(tmp) / f"{i}_baseline-assigned.spv",
                        "--set-spec-const-default-value=" + " ".join(
                            f"{k}:{v}" for k, v in sorted(assign.items()) if k not in (1017, 1018, 1019, 1020)))
    return check_one(tool, tmp, i, spec_path, reference, assign)


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()[:16]


def bin_dir():
    for c in (ROOT / "bin", ROOT.parents[2] / "bin"):
        if (c / "frame_replay.exe").exists():
            return c
    return ROOT / "bin"


def replay(tool, P, pack, tmp, spec, olds):
    (a0, p0), (a1, p1) = olds(spec)[:2]
    spec_id = next(iter(a1))
    t = Path(tmp)
    mods = {"old0": P(p0), "old1": P(p1)}
    for v in (0, 1):
        mods[f"new{v}"] = opt(tool, P(spec), t / f"replay_new{v}.spv", f"--set-spec-const-default-value={spec_id}:{v}")
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


def gpu_opt_gate(args, tree, tool, pick):
    """Compare frozen key-off directly to baseline, without a redundant legacy compile set."""
    if not args.baseline:
        raise SystemExit("--key gpuopt requires --baseline")
    base = SourceTree.commit(args.baseline).with_sharc(tool.sharc)
    try:
        new = compile_requests(tree, pick, tool=tool, cache=Cache(), jobs=args.jobs,
                               validate=True, label="gpuopt union")
        old = compile_requests(base, pick, tool=tool, cache=Cache(), jobs=args.jobs,
                               label="gpuopt baseline")
        bad = [r for r in pick if not new[r].ok or new[r].valid is False or not old[r].ok]
        if bad:
            for r in bad: say(f"FAIL {r}: {new[r].log} {old[r].log}")
            return 1
        failures = []
        with tempfile.TemporaryDirectory(prefix="b3gpuopt-") as tmp:
            work = []
            byte_equal = 0
            for i, r in enumerate(pick):
                for e in new[r].entries:
                    if new[r].blob(e.name) == old[r].blob(e.name):
                        byte_equal += 1
                        continue
                    n, o = Path(tmp)/f"n{i}_{e.name}.spv", Path(tmp)/f"o{i}_{e.name}.spv"
                    n.write_bytes(new[r].blob(e.name)); o.write_bytes(old[r].blob(e.name))
                    work.append((f"{i}_{e.name}", r, n, o))
            say(f"gpuopt key-off: {byte_equal} entries byte-identical before specialization")
            # Positive control uses a real changed function, with the same comparator.
            cr = next(r for r in pick if split_request(r)[0] == "gi/gi_trace_rt_hit_direct.hlsl")
            cn = Path(tmp)/"control.spv"; cn.write_bytes(new[cr].blob("main"))
            control = check_one(tool, tmp, "control", cn, cn, {1024: 1, 1022: 1, 1023: 0})
            say(f"gpuopt enabled positive control: {control} differing lines (must be > 0)")
            with Progress("gpuopt key-off baseline", len(work), every=30, verb="compared") as prog, \
                    concurrent.futures.ProcessPoolExecutor(args.jobs, initializer=lower_priority) as pool:
                futs = {pool.submit(check_one, tool, tmp, i, n, o, {1024: 0, 1022: 0, 1023: 0}): r
                        for i, r, n, o in work}
                for f in concurrent.futures.as_completed(futs):
                    r = futs[f]
                    try: diff = f.result()
                    except Exception as ex: diff = repr(ex)
                    if diff:
                        failures.append((r, diff)); say(f"KEY-OFF DIFFERENT {r}: {diff}")
                    prog.step(failed=bool(diff))
        say(f"gpuopt key-off {len(pick)} requests/{len(work) + byte_equal} entries: "
            f"{'PASS' if not failures and control else 'FAIL'}; baseline {args.baseline}")
        return int(bool(failures) or not control)
    finally:
        base.cleanup()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--sample", type=int, default=24)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--jobs", type=int, default=default_jobs())
    ap.add_argument("--replay", help="replay pack directory (read only)")
    ap.add_argument("--key", choices=sorted(KEYS), default="cascade", help="which migration step's constants")
    ap.add_argument("--baseline", help="also compare key-off programs against this revision")
    args = ap.parse_args()
    marker, olds = KEYS[args.key]
    lower_priority()
    tool = Toolchain()
    tree = SourceTree.worktree(ROOT).with_sharc(tool.sharc)
    known = set(tree.requests())
    spec = sorted(r for r in known if marker in split_request(r)[1] and
                  (args.key != "waterhair" or split_request(r)[0].startswith("rt/water_")))
    if args.key == "watershift":
        spec = [r for r in spec if split_request(r)[0] == "rt/water_compose.hlsl"]
    if args.key == "hairgi":
        spec = [r for r in spec if split_request(r)[0].startswith("gi/gi_trace_rt")]
    if not spec and args.key != "gpuopt":
        say(f"no {marker} request in shaders/variants.txt")
        return 1
    if args.key == "gpuopt":
        # Each rt_common consumer plus compose, including raw (no variant defines) requests.
        spec = sorted(r for r in known if "shaders/rt/rt_common.hlsli" in dict(tree.closure("shaders/" + split_request(r)[0]))
                      or split_request(r)[0] == "rt/water_compose.hlsl")
    if args.key == "gpuopt":
        # Compare the bundle with every optimization key off, including rt_trace_scale=1.
        spec = sorted({gpu_opt_full_trace_request(r) for r in spec})
    by_file = {}
    for r in spec:
        f = split_request(r)[0]
        if f not in by_file or len(split_request(r)[1]) < len(split_request(by_file[f])[1]):
            by_file[f] = r
    pick = spec if args.all else sorted(set(by_file.values()) | set(
        random.Random(args.seed).sample([r for r in spec if r not in by_file.values()],
                                        min(args.sample, len(spec) - len(by_file)))))
    if args.replay:
        # gi_hit_direct's spec request with the fewest defines (the pack's variant is the plain one)
        replay_spec = min((r for r in spec if split_request(r)[0] == "gi/gi_trace_rt_hit_direct.hlsl"),
                          key=lambda r: len(split_request(r)[1]))
        pick = sorted(set(pick) | {replay_spec})
    if args.key == "gpuopt":
        return gpu_opt_gate(args, tree, tool, pick)
    pairs = {r: olds(r) for r in pick}
    # Once the spec build is the only cascade path the old requests are no longer listed; they still compile
    # from the same sources (the reference programs), so they are built here on demand.
    unlisted = sorted({o for r in pick for _, o in pairs[r] if o not in known})
    if unlisted:
        say(f"[spec cascade] {len(unlisted)} old reference requests not in shaders/variants.txt: compiled on demand")
    say(f"[spec {args.key}] {len(spec)} {marker} requests over {len(by_file)} files; checking {len(pick)} "
        f"({'all' if args.all else f'each file once + {len(pick) - len(by_file)} random, seed {args.seed}'})")
    need = sorted(set(pick) | {o for r in pick for _, o in pairs[r]})
    compiled = compile_requests(tree, need, tool=tool, cache=Cache(), jobs=args.jobs, label="spec cascade")
    bad = [r for r in need if not compiled[r].ok]
    for r in bad:
        say(f"FAIL compile {r}\n{compiled[r].log[:3000]}")
    if bad:
        return 1
    baseline_ok = True
    if args.baseline:
        base = SourceTree.commit(args.baseline).with_sharc(tool.sharc)
        try:
            refs = compile_requests(base, pick, tool=tool, cache=Cache(), jobs=args.jobs, label="spec baseline")
            with tempfile.TemporaryDirectory(prefix="b3spec-base-") as bt:
                baseline_jobs = []
                for i, r in enumerate(pick):
                    for e in compiled[r].entries:
                        newp, oldp = Path(bt) / f"new_{i}_{e.name}.spv", Path(bt) / f"old_{i}_{e.name}.spv"
                        newp.write_bytes(compiled[r].blob(e.name))
                        oldp.write_bytes(refs[r].blob(e.name))
                        assignments = ([a for a, _ in pairs[r] if a[1017] == 0]
                                       if args.key == "hairgi" else
                                       [a for a, _ in pairs[r] if a[1018] == 0 and a[1019] == 0]
                                       if args.key == "haircost" else
                                       [a for a, _ in pairs[r]] if args.key == "groom" else [pairs[r][0][0]])
                        for j, assign in enumerate(assignments):
                            baseline_jobs.append((f"base_{i}_{e.name}_{j}", r, e.name, newp, oldp, assign))
                with Progress("spec baseline equivalence", len(baseline_jobs), every=30, verb="compared") as prog, \
                        concurrent.futures.ProcessPoolExecutor(args.jobs, initializer=lower_priority) as pool:
                    futs = {pool.submit(check_baseline, tool, bt, i, newp, oldp, assign, args.key in ("hairgi", "haircost", "groom")):
                            (r, entry, assign) for i, r, entry, newp, oldp, assign in baseline_jobs}
                    for f in concurrent.futures.as_completed(futs):
                        r, entry, assign = futs[f]
                        try:
                            n = f.result()
                        except Exception as ex:  # noqa: BLE001
                            n = f"error {ex!r}"
                        baseline_ok &= n == 0
                        if n: say(f"BASELINE DIFFERENT: {r} [{entry}] {assign}: {n}")
                        prog.step(failed=bool(n))
            say(f"key-off baseline {args.baseline}: {'PASS' if baseline_ok else 'FAIL'} ({len(pick)} requests)")
        finally:
            base.cleanup()
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
        control_request = (next((r for r in pick if split_request(r)[0] == "gi/gi_trace_rt_hit_direct.hlsl"), pick[0])
                           if args.key in ("native", "omm", "haircost") else
                           next((r for r in pick if split_request(r)[0].startswith("gi/gi_trace_rt")), pick[0])
                           if args.key == "groom" else pick[0])
        control_pairs = pairs[control_request]
        if args.key == "gpuopt":
            control_request = next(r for r in pick if split_request(r)[0] == "gi/gi_trace_rt_hit_direct.hlsl")
            reference = pairs[control_request][0][1]
            ctrl = check_one(tool, tmp, "gpuopt_control", P(control_request), P(reference),
                             {1024: 1, 1022: 1, 1023: 0})
            say(f"gpuopt positive control: enabled versus legacy differs in {ctrl} lines (must be > 0)")
        else:
            (_, c0), (_, c1) = ((control_pairs[0], control_pairs[2]) if args.key == "watercost" else
                               (control_pairs[4], control_pairs[6]) if args.key == "haircost" else
                               control_pairs[-2:] if args.key == "hairgi" else
                               (control_pairs[0], control_pairs[-1]) if args.key in ("native", "groom") else control_pairs[:2])
            # Water's copy entry is invariant; the compose entry must distinguish edge off/on.
            control_entry = ("water_compose" if args.key in ("wateredge", "watercasc", "watershift") else
                             "water_reflect" if args.key in ("waterhair", "waterhit", "watercost") and "water_reflect.hlsl" in control_request else
                             "water_compose" if args.key in ("waterhair", "waterhit", "watercost") and "water_compose.hlsl" in control_request else "main")
            ctrl = differ(canonical(tool, opt(tool, P(c0, control_entry), Path(tmp) / "c0.spv", "-O")),
                          canonical(tool, opt(tool, P(c1, control_entry), Path(tmp) / "c1.spv", "-O")))
            say(f"control: {c0} vs its define variant differ in {ctrl} lines (must be > 0)")
        jobs = [(i, r, e.name, v) for i, r in enumerate(pick) for e in compiled[r].entries for v in range(len(pairs[r]))]
        with Progress("spec cascade equivalence", len(jobs), every=30, verb="compared") as prog, \
                concurrent.futures.ProcessPoolExecutor(args.jobs, initializer=lower_priority) as pool:
            futs = {pool.submit(check_one, tool, tmp, f"{i}_{e}", P(r, e), P(pairs[r][v][1], e), pairs[r][v][0]):
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
                    say(f"DIFFERENT: {r} [{e}] {pairs[r][v][0]} vs {pairs[r][v][1]}: {n}")
                prog.step(failed=bool(n))
        ok_replay = replay(tool, P, args.replay, tmp, replay_spec, olds) if args.replay else True
    for r, e, v, n in sorted(fails):
        say(f"DIFFERENT: {r} [{e}] {pairs[r][v][0]} vs {pairs[r][v][1]}: {n}")
    say(f"spec {args.key}: {len(pick) - len({r for r, _, _, _ in fails})}/{len(pick)} {marker} requests are, "
        f"specialized to every checked assignment, the same program as their define variants"
        f"{'' if ctrl else '; CONTROL BLIND'}{'' if ok_replay else '; REPLAY DIFFERS'}")
    return 1 if fails or not ctrl or not ok_replay or not baseline_ok else 0


if __name__ == "__main__":
    sys.exit(main())
