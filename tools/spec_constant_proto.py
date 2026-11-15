"""Specialization-constant prototype check (docs/BUILD_AND_PACKAGE_EFFICIENCY.md 2). Offline, no game.

    py -3 tools/spec_constant_proto.py [--file gi/gi_trace_rt_hit_direct.hlsl] [--replay <pack dir>] [--driver]

RT_SPEC_CASCADE=1 (shaders/rt/rt_common.hlsli, off: no request sets it) compiles the plain and the
RT_CASCADE_MASKS variant of a pass as one module with specialization constant 1000. This compares:
1. DXC time of the two old requests vs the one new module (no cache: a cold compile each).
2. Static: the new module with constant 1000 set to 0 / 1 (spirv-opt --set-spec-const-default-value),
   frozen and folded (--freeze-spec-const --fold-spec-const-op-composite) and then -O, against the old
   plain / cascade module through the same -O, compared up to id numbering and declaration order
   (canonical()): the same program = what the driver compiles after folding the constant. A control
   compares old plain with old cascade (they differ in the ray masks only).
3. --driver: bin/pipeline_stats.exe on the four modules (old plain, old cascade, new spec 0, new spec 1,
   the spec value written as the module default): registers, shared memory, binary size, wall time.
4. --replay: bin/frame_replay.exe --spv on the pack for the four modules; giRaw, giRawDepth and the
   rtStats counters compared byte for byte (new spec 0 == old plain, new spec 1 == old cascade).
Compiles go through tools/shader_identity.py; replay outputs go to a temp dir (never into the pack).
Progress lines at every step (each step is well under 30 s except the replays, ~10-30 s each).
"""
import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from tools.shader_identity import MemoryCache, SourceTree, Toolchain, compile_requests, lower_priority  # noqa: E402
from tools.spirv_equivalence import canonical, opt  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
SPEC_ID = 1000
KIND = {"separate_images": 2, "separate_samplers": 0, "images": 3, "ssbos": 7, "ubos": 6,
        "acceleration_structures": 1000150000, "textures": 1}


def bin_dir():
    for c in (ROOT / "bin", ROOT.parents[2] / "bin"):
        if (c / "frame_replay.exe").exists():
            return c
    return ROOT / "bin"


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()[:16]


def say(msg):
    print(msg, flush=True)


def driver_stats(tool, spv, tmp):
    refl = json.loads(subprocess.run([str(tool.dxc.with_name("spirv-cross.exe")), str(spv), "--reflect"],
                                     capture_output=True, text=True, check=True).stdout)
    lines = [f"{v['set']} {v['binding']} {t} {(v.get('array') or [1])[0]}" for k, t in KIND.items() for v in refl.get(k, [])]
    lay = Path(tmp) / (Path(spv).stem + ".layout.txt")
    lay.write_text("\n".join(lines) + "\n")
    t0 = time.perf_counter()
    r = subprocess.run([str(bin_dir() / "pipeline_stats.exe"), str(spv), str(lay)], capture_output=True, text=True)
    return time.perf_counter() - t0, (r.stdout + r.stderr).strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--file", default="gi/gi_trace_rt_hit_direct.hlsl")
    ap.add_argument("--replay", help="replay pack directory (read only)")
    ap.add_argument("--driver", action="store_true", help="also bin/pipeline_stats.exe on the four modules")
    ap.add_argument("--repeat", type=int, default=1, help="cold compiles per request (the minimum is reported)")
    ap.add_argument("--keep", help="write the modules here (default: a temp dir)")
    args = ap.parse_args()
    lower_priority()
    tool = Toolchain()
    tree = SourceTree.worktree(ROOT).with_sharc(tool.sharc)
    old_plain, old_on, new = args.file, args.file + "?RT_CASCADE_MASKS=1", args.file + "?RT_SPEC_CASCADE=1"
    times = {}
    res = {}
    for r in (old_plain, old_on, new):   # cold compiles, timed (the minimum: other runs share the DXC slots)
        for _ in range(args.repeat):
            t0 = time.perf_counter()
            res.update(compile_requests(tree, [r], tool=tool, cache=MemoryCache(), jobs=1, label="spec proto"))
            times[r] = min(times.get(r, 1e9), time.perf_counter() - t0)
        if not res[r].ok:
            say(f"FAIL compile {r}\n{res[r].log}")
            return 1
        say(f"[spec proto] compiled {r}: {times[r]:.1f} s, {res[r].path().stat().st_size} bytes")
    say(f"DXC: old plain + old cascade {times[old_plain] + times[old_on]:.1f} s for 2 modules; "
        f"new {times[new]:.1f} s for 1 module")
    with tempfile.TemporaryDirectory(prefix="b3spec-") as tmp:
        if args.keep:
            tmp = args.keep
            Path(tmp).mkdir(parents=True, exist_ok=True)
        t = Path(tmp)
        mods = {"old0": t / "old0.spv", "old1": t / "old1.spv"}
        mods["old0"].write_bytes(res[old_plain].blob())
        mods["old1"].write_bytes(res[old_on].blob())
        base_new = t / "new.spv"
        base_new.write_bytes(res[new].blob())
        for v in (0, 1):   # the module default = the value a VkSpecializationInfo would give
            mods[f"new{v}"] = opt(tool, base_new, t / f"new{v}.spv", f"--set-spec-const-default-value={SPEC_ID}:{v}")
        # 2. static: frozen + folded + the same -O on both sides, compared up to id numbering
        for v in (0, 1):
            # the old modules carry the later spec constants (1001+) too: freeze them at their defaults on both sides
            fa = opt(tool, mods[f"old{v}"], t / f"old{v}.f.spv", "--freeze-spec-const", "--fold-spec-const-op-composite")
            a = opt(tool, fa, t / f"old{v}.O.spv", "-O")
            f = opt(tool, mods[f"new{v}"], t / f"new{v}.f.spv", "--freeze-spec-const", "--fold-spec-const-op-composite")
            b = opt(tool, f, t / f"new{v}.O.spv", "-O")
            ga, ba = canonical(tool, a)
            gb, bb = canonical(tool, b)
            dg = len(set(ga) ^ set(gb)) + abs(len(ga) - len(gb))
            db = sum(1 for x, y in zip(ba, bb) if x != y) + abs(len(ba) - len(bb))
            say(f"static spec {v}: {'SAME program' if not dg and not db else 'DIFFERENT'} as the old "
                f"{'cascade' if v else 'plain'} module ({len(gb)} globals, {dg} differ; {len(bb)} body lines, {db} differ; "
                f"bytes equal {sha(a) == sha(b)}: id numbering only)")
        ga, ba = canonical(tool, t / "old0.O.spv")
        gb, bb = canonical(tool, t / "old1.O.spv")
        say(f"control: old plain vs old cascade differ in {sum(1 for x, y in zip(ba, bb) if x != y)} body lines "
            f"(the ray masks): the comparison sees a one-constant change")
        if args.driver:
            for k in ("old0", "old1", "new0", "new1"):
                dt, out = driver_stats(tool, mods[k], tmp)
                say(f"driver {k}: {dt * 1000:.0f} ms wall (device + pipeline)\n  " + out.replace("\n", "\n  "))
        if args.replay:
            outs = {}
            for k in ("old0", "old1", "new0", "new1"):
                o = t / f"replay_{k}"
                t0 = time.perf_counter()
                r = subprocess.run([str(bin_dir() / "frame_replay.exe"), "--pack", args.replay, "--spv", str(mods[k]),
                                    "--out", str(o)], capture_output=True, text=True)
                if r.returncode:
                    say(f"FAIL replay {k}\n{r.stdout[-3000:]}{r.stderr[-2000:]}")
                    return 1
                gpu = [l for l in r.stdout.splitlines() if "dispatch" in l.lower() and "ms" in l]
                outs[k] = {f: sha(o / f) for f in ("giRaw.raw", "giRawDepth.raw", "rtstats.bin") if (o / f).exists()}
                say(f"[spec proto] replay {k}: {time.perf_counter() - t0:.0f} s; {gpu[:1]}; {outs[k]}")
            for v in (0, 1):
                eq = outs[f"old{v}"] == outs[f"new{v}"]
                say(f"replay spec {v}: outputs bit-identical to the old {'cascade' if v else 'plain'} module: {eq}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
