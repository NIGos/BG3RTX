"""Scene handout key-off equivalence (docs/SCENE_HANDOUT_DESIGN.md 3.4, constant 1051). Offline, no game.

    py -3 tools/scene_handout_spec_check.py --baseline <rev> --json <shader_identity --json report> [--jobs J]

shader_identity.py --baseline proves byte identity of every request whose inputs did not change. The requests that
include rt/rt_common.hlsli now declare constant 1051, so their raw SPIR-V differs. For each such request listed as
DIFFERENT in the identity report, every entry is specialized with 1051 = 0 (the key off), every other
specialization constant kept symbolic on both sides (spirv_equivalence.specialize), optimized, and compared up to
id numbering with the baseline module (canonical()): equal = the same program the driver compiles with the key off.
Control (non-blind): 1051 = 1 must differ from the baseline in at least one request.
Compiles go through tools/shader_identity.py (shared cache, DXC slots); progress at least every 30 s.
Exit code 1 on any difference, compile failure or a blind control.
"""
import argparse
import concurrent.futures
import json
import struct
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.shader_identity import (Cache, Progress, SourceTree, Toolchain, compile_requests, default_jobs,  # noqa: E402
                                   lower_priority)
from tools.spirv_equivalence import canonical, first_difference, specialize  # noqa: E402

SPEC_ID = 1051


def spec_ids(path):
    """The SpecId decorations of a module (OpDecorate <id> SpecId <n>)."""
    data = Path(path).read_bytes()
    words = struct.unpack("<" + "I" * (len(data) // 4), data)
    out, i = set(), 5
    while i < len(words):
        n, op = words[i] >> 16, words[i] & 65535
        if op == 71 and n == 4 and words[i + 2] == 1:
            out.add(words[i + 3])
        i += n or 1
    return out


def compare(tool, new_spv, old_spv, work):
    """(key-off equal, first difference, key-on differs) of one entry."""
    work = Path(work)
    ids = spec_ids(new_spv) | spec_ids(old_spv)
    keep = ids - {SPEC_ID}
    off = canonical(tool, specialize(tool, new_spv, work / "new0.spv", {SPEC_ID: 0}, preserve=keep))
    old = canonical(tool, specialize(tool, old_spv, work / "old.spv", {}, preserve=keep))
    on = canonical(tool, specialize(tool, new_spv, work / "new1.spv", {SPEC_ID: 1}, preserve=keep)) \
        if SPEC_ID in ids else old
    diff = first_difference(old, off)
    return not diff, diff, on != old


def job(args):
    tool, name, new_spv, old_spv, tmp = args
    with tempfile.TemporaryDirectory(prefix="b3handout-", dir=tmp) as work:
        return name, *compare(tool, new_spv, old_spv, work)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--json", required=True, help="shader_identity.py --baseline ... --json report")
    ap.add_argument("--jobs", type=int, default=default_jobs())
    args = ap.parse_args()
    report = json.loads(Path(args.json).read_text())
    changed = sorted(r for r, s in report.items() if s == "DIFFERENT")
    other = sorted(r for r, s in report.items() if s not in ("DIFFERENT", "same", "identical inputs"))
    if other:
        print(f"FAIL {len(other)} requests neither identical nor DIFFERENT (compile/missing): {other[:5]}")
        return 1
    print(f"[handout spec] {len(changed)} requests differ in raw SPIR-V; checking key off (1051 = 0) against {args.baseline}",
          flush=True)
    tool = Toolchain()
    new = SourceTree.worktree(ROOT).with_sharc(tool.sharc)
    base = SourceTree.commit(args.baseline).with_sharc(tool.sharc)
    try:
        cur = compile_requests(new, changed, tool=tool, cache=Cache(), jobs=args.jobs, label="handout new")
        ref = compile_requests(base, changed, tool=tool, cache=Cache(), jobs=args.jobs, label="handout baseline")
        bad = [r for r in changed if not cur[r].ok or not ref[r].ok]
        for r in bad:
            print(f"FAIL compile {r}")
        if bad:
            return 1
        with tempfile.TemporaryDirectory(prefix="b3handout-") as tmp:
            jobs = []
            for i, r in enumerate(changed):
                for e in cur[r].entries:
                    n, o = Path(tmp) / f"n{i}_{e.name}.spv", Path(tmp) / f"o{i}_{e.name}.spv"
                    n.write_bytes(cur[r].blob(e.name))
                    o.write_bytes(ref[r].blob(e.name))
                    jobs.append((tool, f"{r} [{e.name}]", n, o, tmp))
            fails, on_differs = [], 0
            with Progress("handout key-off equivalence", len(jobs), every=30, verb="compared") as prog, \
                    concurrent.futures.ProcessPoolExecutor(args.jobs, initializer=lower_priority) as pool:
                for name, equal, diff, on in pool.map(job, jobs):
                    if not equal:
                        fails.append((name, diff))
                    on_differs += on
                    prog.step(failed=not equal)
    finally:
        base.cleanup()
    for name, diff in fails[:20]:
        print(f"DIFFERENT with 1051 = 0: {name}\n  {diff}")
    print(f"[handout spec] key off: {len(jobs) - len(fails)}/{len(jobs)} entries the same program as {args.baseline}; "
          f"key on differs in {on_differs} (control)", flush=True)
    if not on_differs:
        print("FAIL blind control: 1051 = 1 never changed a program")
    return 1 if fails or not on_differs else 0


if __name__ == "__main__":
    sys.exit(main())
