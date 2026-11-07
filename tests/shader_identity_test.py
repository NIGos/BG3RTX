"""tools/shader_identity.py: cache keys, cache hits/misses and identity, without DXC.

An include change invalidates exactly the requests that can reach it; a DXC change invalidates
all; a new file at a missing include candidate invalidates its includers; a warm run compiles
nothing; a corrupt blob is a miss; equal input keys are identical without compiling.
"""
import contextlib
import io
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import tools.shader_identity as si  # noqa: E402
original_run_dxc = si._run_dxc
original_run_pp = si._run_pp

checks = 0


def check(cond, what):
    global checks
    checks += 1
    if not cond:
        print("FAIL:", what)
        sys.exit(1)


FILES = {
    "shaders/a.hlsl": '#include "inc/x.hlsli"\n[numthreads(8,1,1)] void main() {}\n',
    "shaders/b.hlsl": '#include "common.hlsli"\n[numthreads(8,1,1)] void main() {}\n',
    "shaders/c.hlsl": '// dxc: -enable-16bit-types\n#include <inc/x.hlsli>\n#include "SharcCommon.h"\n'
                      '[numthreads(8,1,1)] void one() {}\n[numthreads(8,1,1)] void two() {}\n',
    "shaders/d.hlsl": '[numthreads(8,1,1)] void main() {}\n',
    "shaders/inc/x.hlsli": '#include "../../common/h.h"\n',
    "shaders/common.hlsli": "float f;\n",
    "common/h.h": "#pragma once\n#if !defined(__HLSL_VERSION)\n#include <vector>\n#endif\n",
    "sharc/SharcCommon.h": "// sharc\n",
    "shaders/variants.txt": "# generated\nd.hlsl?FOO=1\n",
}
REQUESTS = ["a.hlsl", "b.hlsl", "c.hlsl", "d.hlsl", "d.hlsl?FOO=1"]


def keys(files, tool_id="tool-1"):
    tree = si.SourceTree.memory(files)
    return {r: si.request_keys(tree, r, tool_id) for r in REQUESTS}


def changed(k0, k1):
    return {r for r in REQUESTS if k0[r] != k1[r]}


base = keys(FILES)
check(base == keys(dict(FILES)), "keys are deterministic")
check([e for e, _ in base["c.hlsl"]] == ["one", "two"], "every [numthreads] entry is keyed")
check(len({k for v in base.values() for _, k in v}) == 6, "every entry of every request has its own key")
check(si.SourceTree.memory(FILES).requests() == ["a.hlsl", "b.hlsl", "c.hlsl", "d.hlsl", "d.hlsl?FOO=1"],
      "requests = every .hlsl plus variants.txt")

# An include change invalidates exactly the requests that can reach it.
check(changed(base, keys({**FILES, "common/h.h": FILES["common/h.h"] + "// edit\n"})) == {"a.hlsl", "c.hlsl"},
      "common/h.h edit -> a and c (through inc/x.hlsli), not b or d")
check(changed(base, keys({**FILES, "shaders/common.hlsli": "float g;\n"})) == {"b.hlsl"}, "common.hlsli edit -> b only")
check(changed(base, keys({**FILES, "sharc/SharcCommon.h": "// sharc 2\n"})) == {"c.hlsl"}, "SHaRC header edit -> c only")
check(changed(base, keys({**FILES, "shaders/d.hlsl": FILES["shaders/d.hlsl"] + "\n"})) == {"d.hlsl", "d.hlsl?FOO=1"},
      "source edit -> every request of that file")
check(changed(base, keys({**FILES, "shaders/unused.hlsli": "x"})) == set(), "an unrelated new file changes nothing")
check(changed(base, keys({**FILES, "shaders/variants.txt": "d.hlsl?FOO=1\n"})) == set(), "the manifest is not an input")
# A new file at an earlier search candidate of an include shadows the old one: its includers change.
check(changed(base, keys({**FILES, "shaders/inc/common.hlsli": "x"})) == set(), "a candidate nobody searches")
check(changed(base, keys({**FILES, "shaders/inc/SharcCommon.h": "x"})) == set(), "c includes SharcCommon.h from shaders/")
check(changed(base, keys({**FILES, "shaders/SharcCommon.h": "x"})) == {"c.hlsl"},
      "a file at a missing candidate (shaders/SharcCommon.h) invalidates its includer")
# Conditional includes count (conservative): <vector> candidates are recorded but never exist.
check(changed(base, keys({**FILES, "shaders/vector": "x"})) == {"a.hlsl", "c.hlsl"}, "includes under #if count")
# A DXC (toolchain) change invalidates all.
other = keys(FILES, tool_id="tool-2")
check(changed(base, other) == set(REQUESTS) and
      not {k for v in base.values() for _, k in v} & {k for v in other.values() for _, k in v}, "a DXC change invalidates all")
# A macro include depends on every file.
macro = {**FILES, "shaders/d.hlsl": "#include FOO_H\n" + FILES["shaders/d.hlsl"]}
check(changed(keys(macro), keys({**macro, "shaders/unused.hlsli": "x"})) == {"d.hlsl", "d.hlsl?FOO=1"},
      "a macro include depends on the whole tree")
check(si.entry_key("t", "d.hlsl", "main", []) != si.entry_key("t", "d.hlsl", "main", [], profile="cs_6_5"), "profile is keyed")


# ---- the compile engine with a fake DXC -------------------------------------------------------
class FakeTool(si.Toolchain):
    def __init__(self, tid="fake-1"):
        super().__init__(dxc="fake-dxc.exe", sharc=None)
        self._id, self._val_id = tid, "val-" + tid
        self.sharc = None


calls = []


def fake_dxc(tool, tree, request, entry, out):
    calls.append((tree.label, request, entry))
    file, defines = si.split_request(request)
    # Output depends on the source file only (as if comments/includes compiled away), plus defines.
    return (tree.read("shaders/" + file) + ";".join(defines).encode() + entry.encode()), ""


si._run_dxc = fake_dxc
quiet = contextlib.redirect_stdout(io.StringIO())
cache = si.MemoryCache()
tool = FakeTool()
t0 = si.SourceTree.memory(FILES, "t0")
with quiet:
    r0 = si.compile_requests(t0, REQUESTS, tool=tool, cache=cache, jobs=2)
check(len(calls) == 6 and all(r.ok and not r.cached for r in r0.values()), "cold run compiles every entry")
calls.clear()
with quiet:
    r1 = si.compile_requests(si.SourceTree.memory(FILES, "t0b"), REQUESTS, tool=tool, cache=cache, jobs=2)
check(not calls and all(r.cached for r in r1.values()), "warm run compiles nothing")
check(all(r0[r].digest == r1[r].digest for r in REQUESTS), "cached digests equal fresh ones")
check(r1["c.hlsl"].blob("two") == FILES["shaders/c.hlsl"].encode() + b"two", "cached blob readable")
t1 = si.SourceTree.memory({**FILES, "common/h.h": "// edit\n"}, "t1")
with quiet:
    si.compile_requests(t1, REQUESTS, tool=tool, cache=cache, jobs=2)
check(sorted(c[1:] for c in calls) == [("a.hlsl", "main"), ("c.hlsl", "one"), ("c.hlsl", "two")],
      "an include change recompiles exactly its dependents")
calls.clear()
with quiet:
    si.compile_requests(t0, REQUESTS, tool=FakeTool("fake-2"), cache=cache, jobs=2)
check(len(calls) == 6, "a DXC change recompiles all")
# A corrupt blob is a miss.
calls.clear()
sha = r1["d.hlsl"].entries[0].sha
cache._verified.clear()
cache.blob_path(sha).write_bytes(b"corrupt")
with quiet:
    r2 = si.compile_requests(si.SourceTree.memory(FILES, "t0c"), ["d.hlsl"], tool=tool, cache=cache, jobs=1)
check(calls == [("t0c", "d.hlsl", "main")] and r2["d.hlsl"].digest == r0["d.hlsl"].digest, "corrupt blob -> recompiled")
check(r2["d.hlsl"].blob() == FILES["shaders/d.hlsl"].encode() + b"main",
      "recompiled blob replaces corrupt stored bytes")
reopened = si.Cache(cache.dir)
with quiet:
    repaired = si.compile_requests(si.SourceTree.memory(FILES, "repaired"), ["d.hlsl"], tool=tool,
                                   cache=reopened, jobs=1)
check(repaired["d.hlsl"].cached and repaired["d.hlsl"].blob() == r0["d.hlsl"].blob(),
      "repaired blob verifies after reopening cache")
reopened.db.close()
# Failures are reported and never cached.
calls.clear()
si._run_dxc = lambda *a: (calls.append(a[3]), (None, "[main] error: boom"))[1]
with quiet:
    bad = si.compile_requests(si.SourceTree.memory({"shaders/z.hlsl": "x"}, "tz"), ["z.hlsl", "nope.hlsl"],
                              tool=tool, cache=cache, jobs=1)
    si.compile_requests(si.SourceTree.memory({"shaders/z.hlsl": "x"}, "tz2"), ["z.hlsl"], tool=tool, cache=cache, jobs=1)
check(not bad["z.hlsl"].ok and "boom" in bad["z.hlsl"].log and not bad["nope.hlsl"].ok and calls == ["main", "main"],
      "failures reported, missing files reported, failures not cached")
si._run_dxc = fake_dxc

# Identity: equal input keys are identical without compiling; the rest are compiled and compared.
calls.clear()
cache2 = si.MemoryCache()
new = si.SourceTree.memory({**FILES, "common/h.h": "// comment only\n",
                            "shaders/d.hlsl": "[numthreads(8,1,1)] void main() { x(); }\n"}, "new")
with quiet:
    res = dict((r, s) for r, s, _ in si.identity(si.SourceTree.memory(FILES, "old"), new, REQUESTS + ["e.hlsl"],
                                                 tool=tool, cache=cache2, jobs=2))
check(res == {"a.hlsl": si.SAME, "b.hlsl": si.IDENTICAL_INPUTS, "c.hlsl": si.SAME, "d.hlsl": "DIFFERENT",
              "d.hlsl?FOO=1": "DIFFERENT", "e.hlsl": "missing"}, f"identity states {res}")
check(not any(c[1] == "b.hlsl" for c in calls), "identical inputs are not compiled")

# In-flight lock: two runs (own Cache objects, one shared cache directory: two processes' view) that miss the same
# entries at the same time compile each entry once; the other run takes the result (identical digests).
import tempfile  # noqa: E402
import threading  # noqa: E402
import time  # noqa: E402
with tempfile.TemporaryDirectory(prefix="b3k-", ignore_cleanup_errors=True) as shared_dir:
    calls.clear()

    def slow_dxc(tool_, tree, request, entry, out):
        time.sleep(0.3)   # both runs are inside their compile window together
        return fake_dxc(tool_, tree, request, entry, out)
    si._run_dxc = slow_dxc
    runs = {}

    def run(name):
        c = si.Cache(shared_dir)
        runs[name] = si.compile_requests(si.SourceTree.memory(FILES, name), ["c.hlsl", "d.hlsl"], tool=tool, cache=c, jobs=2)
        c.db.close()
    th = [threading.Thread(target=run, args=(n,)) for n in ("p1", "p2")]
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):   # once: sys.stdout is global
        for t in th:
            t.start()
        for t in th:
            t.join()
    si._run_dxc = fake_dxc
    check(len(calls) == 3, f"two concurrent runs of one key compile it once ({len(calls)} compiles for 3 entries)")
    check(all(runs["p1"][r].ok and runs["p1"][r].digest == runs["p2"][r].digest for r in ("c.hlsl", "d.hlsl")),
          "the waiting run gets the holder's result")

# tools/low_priority.py really lowers the process on Windows (the ctypes handle was truncated before: still Normal).
if sys.platform == "win32":
    probe = ("import ctypes,sys; sys.path.insert(0, sys.argv[1]); from tools.low_priority import lower_priority; "
             "lower_priority(); k=ctypes.windll.kernel32; k.GetCurrentProcess.restype=ctypes.c_void_p; "
             "k.GetPriorityClass.argtypes=[ctypes.c_void_p]; print(k.GetPriorityClass(k.GetCurrentProcess()))")
    env = {k: v for k, v in __import__("os").environ.items() if k != "BG3RTX_NORMAL_PRIORITY"}
    out = subprocess.run([sys.executable, "-c", probe, str(ROOT)], capture_output=True, text=True, env=env).stdout.strip()
    check(out == str(0x4000), f"lower_priority sets BELOW_NORMAL ({out})")

# Pruning: age, protection, size cap (oldest first), shared blobs, unreferenced blobs deleted.
pc = si.MemoryCache()
now = 1_000_000_000.0
day = 86400
for key, data, age in (("old", b"A" * 100, 30), ("old-protected", b"B" * 100, 30), ("old-shared", b"C" * 100, 30),
                       ("new-shared", b"C" * 100, 1), ("mid", b"D" * 300, 5), ("new", b"E" * 200, 1)):
    pc.put(key, data, key, "main")
    pc.db.execute("UPDATE compiled SET at=? WHERE key=?", (now - age * day, key))
pc.commit()
sha = {k: pc.db.execute("SELECT sha FROM compiled WHERE key=?", (k,)).fetchone()[0]
       for k in ("old", "old-protected", "old-shared", "mid", "new")}
with quiet:
    r = si.prune(pc, days=14, cap=10_000, protect={"old-protected"}, now=now)
left = {k for (k,) in pc.db.execute("SELECT key FROM compiled")}
check(left == {"old-protected", "new-shared", "mid", "new"} and r["entries"] == 2 and r["blobs"] == 1,
      f"age prune drops old unprotected entries only ({left}, {r})")
check(not pc.blob_path(sha["old"]).exists() and pc.blob_path(sha["old-shared"]).exists(),
      "the blob of a dropped entry goes; a blob still referenced stays")
with quiet:
    r = si.prune(pc, days=14, cap=350, protect={"old-protected"}, now=now)
left = {k for (k,) in pc.db.execute("SELECT key FROM compiled")}
check(left == {"old-protected", "new"} and r["kept"] == 300 and not pc.blob_path(sha["mid"]).exists(),
      f"cap evicts the oldest unprotected entries until under the cap ({left}, {r})")
# A cap below the protected SPIR-V is raised to 1.25x it (protected 300 B -> 375 B: a recent 50 B entry still fits).
pc.put("small", b"F" * 50, "small", "main")
with quiet:
    r = si.prune(pc, days=14, cap=10, protect={"old-protected", "new"}, now=now)
left = {k for (k,) in pc.db.execute("SELECT key FROM compiled")}
check(left == {"old-protected", "new", "small"}, f"derived cap keeps the protected set and fits recent work ({left}, {r})")

# Progress prints while running.
buf = io.StringIO()
with contextlib.redirect_stdout(buf):
    with si.Progress("p", 3, every=0.02) as p:
        p.step()
        import time
        time.sleep(0.1)
        p.step(failed=True)
out = buf.getvalue()
check(out.count("[p]") >= 2 and "2/3 compiled" in out and "1 failed" in out and "ETA" in out, "progress lines")

# Preprocessed digest (--pp-diff): #line directives (tree paths, comment line shifts), CRLF, trailing
# spaces and blank lines do not count; any token does.
pp = b'#line 1 "C:\\\\a\\\\x.hlsl"\nfloat f;\n\n#line 9 "C:\\\\a\\\\x.hlsl"\nvoid main() { f = 1; }\n'
check(si.pp_digest(pp) == si.pp_digest(b'#line 1 "D:\\\\tmp\\\\b3s-1\\\\x.hlsl"\r\nfloat f;  \r\n#line 40 "y"\r\n\r\n'
                                       b'void main() { f = 1; }'), "pp digest ignores #line, CRLF, blank lines")
check(si.pp_digest(pp) != si.pp_digest(pp.replace(b"f = 1", b"f = 2")), "pp digest sees a token change")
check(si.pp_digest(pp) != si.pp_digest(pp.replace(b"float f;", b"float  f;")), "pp digest keeps inner spacing")

# Preprocessed-text key level with a fake preprocessor (the closure's text without // comment lines, plus
# the defines): a comment edit in a header compiles nothing, a code edit compiles its dependents, the
# self-check sees a preprocessor that hides a change.
def fake_pp(tool, tree, request, out):
    file, defines = si.split_request(request)
    text = b"".join(tree.read(p) for p, bid in tree.closure("shaders/" + file) if bid)
    return b"\n".join(l for l in text.split(b"\n") if not l.lstrip().startswith(b"//")) + ";".join(defines).encode()


si._run_pp = fake_pp
pc2 = si.MemoryCache()
tp0 = si.SourceTree.memory(FILES, "tp0")
with quiet:
    rp0 = si.compile_requests(tp0, REQUESTS, tool=tool, cache=pc2, jobs=2)
calls.clear()
tp1 = si.SourceTree.memory({**FILES, "common/h.h": FILES["common/h.h"] + "// comment only\n"}, "tp1")
with quiet:
    rp1 = si.compile_requests(tp1, REQUESTS, tool=tool, cache=pc2, jobs=2)
check(not calls and all(rp1[r].digest == rp0[r].digest for r in REQUESTS)
      and sum(e.by_pp for r in REQUESTS for e in rp1[r].entries) == 3, "a comment-only header edit compiles nothing")
calls.clear()
with quiet:
    si.compile_requests(si.SourceTree.memory({**FILES, "common/h.h": FILES["common/h.h"] + "// again\n"}, "tp1b"),
                        REQUESTS, tool=tool, cache=pc2, jobs=2)
    si.compile_requests(tp1, REQUESTS, tool=tool, cache=pc2, jobs=2)
check(not calls, "linked closure keys hit directly next time")
tp2 = si.SourceTree.memory({**FILES, "common/h.h": FILES["common/h.h"] + "float g;\n"}, "tp2")
with quiet:
    si.compile_requests(tp2, REQUESTS, tool=tool, cache=pc2, jobs=2)
check(sorted(c[1:] for c in calls) == [("a.hlsl", "main"), ("c.hlsl", "one"), ("c.hlsl", "two")],
      "a code edit in a header compiles exactly its dependents")
with quiet:
    check(si.verify_pp(tp2, REQUESTS, 5, tool, pc2, jobs=2, seed=1) == [], "self-check passes on a sound key")
si._run_pp = lambda tool, tree, request, out: b"blind"   # hides every change
pc3 = si.MemoryCache()
with quiet:
    si.compile_requests(tp0, ["d.hlsl"], tool=tool, cache=pc3, jobs=2)
    tp3 = si.SourceTree.memory({**FILES, "shaders/d.hlsl": "[numthreads(4,1,1)] void main() {}\n"}, "tp3")
    bad = si.verify_pp(tp3, ["d.hlsl"], 1, tool, pc3, jobs=2, seed=1)
check(bad == ["d.hlsl"], "self-check catches a key that hides a change")
si._run_pp = lambda tool, tree, request, out: None        # preprocess failure: compile under the closure key
calls.clear()
with quiet:
    si.compile_requests(si.SourceTree.memory({**FILES, "common/h.h": "// x\n"}, "tp4"), REQUESTS, tool=tool,
                        cache=si.MemoryCache(), jobs=2)
check(len(calls) == 6, "a failed preprocess falls back to compiling")
# Backfill: a cache filled before the second level (closure keys only) learns the preprocessed keys once.
si._run_pp = fake_pp
pc4 = si.MemoryCache()
with quiet:
    si.compile_requests(tp0, REQUESTS, tool=tool, cache=pc4, jobs=2, pp=False)
    si.compile_requests(tp0, REQUESTS, tool=tool, cache=pc4, jobs=2, backfill=True)
calls.clear()
with quiet:
    si.compile_requests(tp1, REQUESTS, tool=tool, cache=pc4, jobs=2)
check(not calls, "after a backfill a comment-only header edit compiles nothing")

# Cache ids describe raw source bytes. Git checkout filters (for example LF -> CRLF)
# can change a clean file's bytes, so a clean status alone does not imply equal ids.
if subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True).returncode == 0:
    head, work = si.SourceTree.commit("HEAD"), si.SourceTree.worktree(ROOT)
    shared = sorted(head.ids.keys() & work.ids.keys())
    same = [p for p in shared if head.read(p) == work.read(p)]
    check(same and all(head.ids[p] == work.ids[p] for p in same),
          "byte-identical commit and worktree sources share blob ids")
    check(shared and all(si.git_blob_id(work.read(p)) == work.ids[p] for p in shared),
          "worktree ids hash raw checkout bytes, including filtered files")
    check(all(si.git_blob_id(head.read(p)) == head.ids[p] for p in shared[:50]),
          "cat-file contents match their ids")
# Retirement fallback must not hide compile errors, changed arithmetic, invalid
# modules or entry mismatches; only the explicitly retired constants may be frozen.
import tempfile
from types import SimpleNamespace
from unittest.mock import patch
import tools.hair_retirement_check as hr

asm = '''OpDecorate %1 SpecId 1017
OpDecorate %2 SpecId 1018
OpDecorate %3 SpecId 1019
OpDecorate %4 SpecId 1025
%1 = OpSpecConstant %9 0
%2 = OpSpecConstant %9 0
%3 = OpSpecConstant %9 0
%4 = OpSpecConstant %9 0
'''
extra_retired = (1001, 1002, 1005, 1006, 1016, 1021, 1022, 1023, 1024)
asm += ''.join(f'OpDecorate %{i+10} SpecId {spec}\n%{i+10} = OpSpecConstant %9 0\n' for i,spec in enumerate(extra_retired))
with tempfile.TemporaryDirectory() as td:
    tmp = Path(td)
    fake_tool = SimpleNamespace(dxc=tmp / 'dxc.exe')
    with patch.object(hr.subprocess, 'run', return_value=SimpleNamespace(stdout=asm)):
        hr.retired_off(fake_tool, tmp / 'in.spv', tmp / 'out.spv')
    frozen = (tmp / 'out.spvasm').read_text()
    check(all(f'%{i} = OpConstant %9 0' in frozen for i in (1, 2, 3)), 'retired IDs frozen at zero')
    check(all(f'%{i+10} = OpConstant %9 0' in frozen for i in range(len(extra_retired))), 'consolidation retired IDs frozen at zero')
    check('OpDecorate %4 SpecId 1025' in frozen and '%4 = OpSpecConstant %9 0' in frozen,
          'surviving specialization remains variable')
    with patch.object(hr.subprocess, 'run', return_value=SimpleNamespace(stdout=asm.replace('%1 = OpSpecConstant %9 0', '%1 = OpSpecConstant %9 1'))):
        try:
            hr.retired_off(fake_tool, tmp / 'in.spv', tmp / 'out.spv')
            check(False, 'nonzero retired default must fail')
        except RuntimeError:
            check(True, 'nonzero retired default rejected')
    shadow_asm = asm + 'OpDecorate %5 SpecId 1026\n%5 = OpSpecConstant %9 0\n'
    with patch.object(hr.subprocess, 'run', return_value=SimpleNamespace(stdout=shadow_asm)):
        hr.shadow_off(fake_tool, tmp / 'in.spv', tmp / 'shadow.spv')
    frozen = (tmp / 'shadow.spvasm').read_text()
    check('%5 = OpConstant %9 0' in frozen and 'OpDecorate %5 SpecId' not in frozen, 'C2 default frozen')
    check(all(f'OpDecorate %{i} SpecId' in frozen for i in (1, 2, 3, 4)), 'C2 preserves all other specializations')
    with patch.object(hr.subprocess, 'run', return_value=SimpleNamespace(stdout=shadow_asm.replace('%5 = OpSpecConstant %9 0', '%5 = OpSpecConstant %9 1'))):
        try:
            hr.shadow_off(fake_tool, tmp / 'in.spv', tmp / 'bad.spv')
            check(False, 'nonzero C2 default must fail')
        except RuntimeError:
            check(True, 'nonzero C2 default rejected')
    module = SimpleNamespace(ok=True, valid=True, entries=[SimpleNamespace(name='main')], blob=lambda e: b'module')
    check(bool(hr.compare_retired(fake_tool, module, SimpleNamespace(ok=False), tmp)), 'compile failure rejected')
    check(bool(hr.compare_retired(fake_tool, module, SimpleNamespace(ok=True, valid=False), tmp)), 'validation failure rejected')
    check(bool(hr.compare_retired(fake_tool, module, SimpleNamespace(ok=True, valid=True, entries=[]), tmp)), 'missing entry rejected')
    with patch.object(hr, 'retired_off', side_effect=lambda t, s, d: s), \
         patch.object(hr, 'shadow_off', side_effect=lambda t, s, d: s), \
         patch.object(hr, 'opt', side_effect=lambda t, s, d, *p: s), \
         patch.object(hr, 'canonical', side_effect=[([], ['same']), ([], ['same']),
                                                   ([], ['old arithmetic']), ([], ['new arithmetic'])]):
        check(not hr.compare_retired(fake_tool, module, module, tmp), 'equivalent modules accepted')
        check(bool(hr.compare_retired(fake_tool, module, module, tmp)), 'changed arithmetic rejected')
    rows = [('same', si.SAME, ''), ('changed', 'DIFFERENT', ''), ('broken', 'compile', 'error')]
    with patch.object(hr, 'compile_requests', return_value={'changed': module}) as compile_mock, \
         patch.object(hr, 'compare_retired', return_value='') as compare_mock, quiet:
        proofs = hr.retirement_fallback(rows, SimpleNamespace(label='base'), SimpleNamespace(label='new'), None, None, 'test')
    check([p['path'] for p in proofs] == ['raw identity', 'retirement/default equivalence (1001/1002/1005/1006/1016-1019/1021-1024=0 baseline; 1026/1044=0 both)', 'failed'],
          'only DIFFERENT uses fallback, raw identity and failures retain their paths')
    check(compare_mock.call_count == 1 and all(c.args[1] == ['changed'] for c in compile_mock.call_args_list),
          'fallback compiles only differing identical request spellings')
    with patch.object(hr, 'compile_requests', return_value={'changed': module}), \
         patch.object(hr, 'compare_retired', return_value='arithmetic differs'), quiet:
        proofs = hr.retirement_fallback(rows, SimpleNamespace(label='base'), SimpleNamespace(label='new'), None, None, 'test')
    check(proofs[1]['path'] == 'failed', 'failed retirement proof remains a gate failure')
# Verification must fail if either side failed, rather than counting it as identical.
for cached_ok, fresh_ok in ((False, True), (True, False), (False, False)):
    sampled = [{(tp0.label, "d.hlsl"): SimpleNamespace(ok=ok, digest="same")}
               for ok in (cached_ok, fresh_ok)]
    with patch.object(si, "compile_many", side_effect=sampled), quiet:
        failed = si.verify_pp(tp0, ["d.hlsl"], 1, tool, pc4)
    check(failed == ["d.hlsl"], f"pp verification rejects compile failures ({cached_ok}, {fresh_ok})")

# DXC and the preprocessor must read captured source AND SDK bytes after an edit.
with tempfile.TemporaryDirectory() as td:
    tmp = Path(td)
    (tmp / "shaders").mkdir()
    (tmp / "shaders/x.hlsl").write_bytes(b"old source")
    (tmp / "sdk").mkdir()
    (tmp / "sdk/h.h").write_bytes(b"old SDK")
    snapshot = si.SourceTree.worktree(tmp).with_sharc(tmp / "sdk")
    snapshot_tool = FakeTool()
    snapshot_tool.sharc = tmp / "sdk"
    (tmp / "shaders/x.hlsl").write_bytes(b"new source")
    (tmp / "sdk/h.h").write_bytes(b"new SDK")

    def snapshot_dxc(cmd, **kwargs):
        source = Path(cmd[1])
        include = Path(cmd[cmd.index("BG3RTX=1") + 2])
        check(source.read_bytes() == b"old source", "compiler reads captured source bytes")
        check((include / "h.h").read_bytes() == b"old SDK", "compiler reads captured SDK bytes")
        output = Path(cmd[cmd.index("-Fi" if "-Fi" in cmd else "-Fo") + 1])
        output.write_bytes(source.read_bytes())
        return SimpleNamespace(returncode=0, stdout="", stderr="")

    with patch.object(si.subprocess, "run", side_effect=snapshot_dxc), \
         patch.object(si, "_DxcSlot", return_value=contextlib.nullcontext()):
        check(original_run_dxc(snapshot_tool, snapshot, "x.hlsl", "main", tmp / "out.spv")[0] == b"old source",
              "compile uses immutable snapshot")
        check(original_run_pp(snapshot_tool, snapshot, "x.hlsl", tmp / "out.pp") == b"old source",
              "preprocess uses immutable snapshot")
    check(si.git_blob_id((snapshot.materialize() / "shaders/x.hlsl").read_bytes()) == snapshot.ids["shaders/x.hlsl"],
          "compiled snapshot bytes match cache input id")
    snapshot.cleanup()

# Exercise the qualification CLI with successful fake compiles and absent checkers.
import runpy
for request in ("gi/gi_trace_rt_hit_direct.hlsl", "gi/gi_trace_rt_hit_direct_em.hlsl",
                "gi/rt_reflections_hit_shade.hlsl"):
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        (tmp / "dxc.exe").touch()
        (tmp / "module.spv").write_bytes(b"module")
        cli_tool = SimpleNamespace(dxc=tmp / "dxc.exe", sharc=None)
        cli_tree = SimpleNamespace(with_sharc=lambda sharc: cli_tree)
        cli_module = SimpleNamespace(ok=True, log="", cached=False, path=lambda entry: tmp / "module.spv")
        exists = Path.exists

        def missing_checker(path):
            return False if path.name in ("native_hit_gi_bindings_test.exe", "native_hit_rf_bindings_test.exe") else exists(path)

        with patch.object(si, "Toolchain", return_value=cli_tool), \
             patch.object(si.SourceTree, "worktree", return_value=cli_tree), \
             patch.object(si, "compile_requests", return_value={request: cli_module}), \
             patch.object(si, "lower_priority"), patch.object(Path, "exists", missing_checker), \
             patch.object(sys, "argv", ["compile_shaders.py", "--no-cache", request]), quiet:
            try:
                runpy.run_path(str(ROOT / "tools/compile_shaders.py"), run_name="__main__")
                check(False, "qualification must exit")
            except SystemExit as exc:
                check(exc.code == 1, f"missing checker fails qualification for {request}")
# Shared specialization proof: ID/declaration/block order is irrelevant, live operands are not.
from tools import spirv_equivalence as se
from tools import spec_cascade_check as sc
check(sc.canonical is se.canonical and sc.opt is se.opt and sc.differ is se.differ,
      "cascade gate uses shared SPIR-V implementation")
assembly = """OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %10 "main"
OpExecutionMode %10 LocalSize 1 1 1
OpDecorate %8 DescriptorSet 0
%1 = OpTypeVoid
%2 = OpTypeBool
%3 = OpTypeInt 32 0
%4 = OpTypeFunction %1
%5 = OpConstantTrue %2
%6 = OpConstant %3 1
%7 = OpConstant %3 2
%8 = OpVariable %3 Private
%10 = OpFunction %1 None %4
%11 = OpLabel
OpSelectionMerge %14 None
OpBranchConditional %5 %12 %13
%12 = OpLabel
OpBranch %14
%13 = OpLabel
OpBranch %14
%14 = OpLabel
%15 = OpPhi %3 %6 %12 %7 %13
OpStore %8 %15
OpReturn
OpFunctionEnd
"""
normalized = se.canonical_text(assembly)
# Reverse global type/constant declaration layout and exchange the two branch blocks.
reordered = assembly.replace("%6 = OpConstant %3 1\n%7 = OpConstant %3 2", "%7 = OpConstant %3 2\n%6 = OpConstant %3 1")
reordered = reordered.replace("%12 = OpLabel\nOpBranch %14\n%13 = OpLabel\nOpBranch %14",
                              "%13 = OpLabel\nOpBranch %14\n%12 = OpLabel\nOpBranch %14")
reordered = reordered.replace("%15 = OpPhi %3 %6 %12 %7 %13", "%15 = OpPhi %3 %7 %13 %6 %12")
reordered = se.RX.sub(lambda m: "%" + str(int(m.group()[1:]) + 100), reordered)
check(normalized == se.canonical_text(reordered), "canonical IDs, globals, blocks and phi pairs")
check(not se.first_difference(normalized, se.canonical_text(reordered)), "equal modules have no first difference")
for old, new in (("OpConstant %3 1", "OpConstant %3 3"),
                 ("OpBranchConditional %5 %12 %13", "OpBranchConditional %5 %13 %12"),
                 ("OpPhi %3 %6 %12 %7 %13", "OpPhi %3 %7 %12 %6 %13"),
                 ("DescriptorSet 0", "DescriptorSet 1"),
                 ("OpStore %8 %15", "OpStore %8 %6"),
                 ("OpStore %8 %15", "OpLoad %8 %15")):
    changed = se.canonical_text(assembly.replace(old, new))
    check(se.differ(normalized, changed) > 0 and bool(se.first_difference(normalized, changed)),
          "equivalence control retains " + old)
two_objects = assembly.replace("OpDecorate %8 DescriptorSet 0", "OpDecorate %8 Binding 0\nOpDecorate %9 Binding 1")
two_objects = two_objects.replace("%8 = OpVariable %3 Private", "%8 = OpVariable %3 Private\n%9 = OpVariable %3 Private")
two_objects = two_objects.replace("OpStore %8 %15", "OpStore %8 %15\nOpStore %9 %6")
check(se.differ(se.canonical_text(two_objects), se.canonical_text(two_objects.replace(
    "OpStore %8 %15\nOpStore %9 %6", "OpStore %9 %15\nOpStore %8 %6"))) > 0,
      "same-type descriptors preserve binding identity when uses swap")
private_objects = two_objects.replace("OpDecorate %8 Binding 0\nOpDecorate %9 Binding 1\n", "")
check(se.differ(se.canonical_text(private_objects), se.canonical_text(private_objects.replace(
    "OpStore %9 %6", "OpStore %8 %6"))) > 0,
      "undecorated mutable global objects retain distinct identities")
reordered_objects = two_objects.replace("%8 = OpVariable %3 Private\n%9 = OpVariable %3 Private",
                                        "%9 = OpVariable %3 Private\n%8 = OpVariable %3 Private")
check(se.canonical_text(two_objects) == se.canonical_text(reordered_objects),
      "mutable global declaration order remains irrelevant")
check(se.differ((["A", "A", "B"], []), (["A", "B", "B"], [])) > 0,
      "global instruction multiplicity is preserved")
check("<absent>" in se.first_difference(([], ["OpReturn"]), ([], [])), "first difference reports removed instruction")
with tempfile.TemporaryDirectory() as td:
    target = Path(td) / "optimized.spv"
    with patch.object(se, "opt", side_effect=lambda tool, src, dst, *passes: dst) as optimize:
        check(se.specialize(None, Path(td) / "input.spv", target, {1025: 0, 1000: 1}) == target,
              "shared specializer returns optimized destination")
        calls = optimize.call_args_list
        check(calls[0].args[3:] == ("--set-spec-const-default-value=1000:1 1025:0",),
              "specialization assigns explicit values in stable order")
        check(calls[1].args[3:] == ("--freeze-spec-const", "--fold-spec-const-op-composite"),
              "specialization freezes all constants and folds composites before optimization")
        check(calls[2].args[3:] == ("-O", "--remove-unused-interface-variables", "--trim-capabilities"),
              "specialization optimizes and trims inactive declarations symmetrically")
        check(calls[3].args[1] == calls[2].args[2] and calls[3].args[2] == target and
              calls[3].args[3:] == calls[2].args[3:],
              "second symmetric optimizer schedule cleans branches exposed by specialization")
# Release proof domains and old request mapping must be exhaustive and explicit.
from tools import release_shader_equivalence as rse
import struct
retired = {1001, 1021}
values = rse.assignments({1000, 1001, 1008, 1025}, retired)
check(len(values) == 4 and {(a[1000], a[1025]) for a in values} == {(0, 0), (0, 1), (1, 0), (1, 1)},
      "release proof crosses both values of every retained Boolean constant")
check(all(a[1008] == 0 and a[1001] == 0 and a[1021] == 0 for a in values),
      "release proof fixes hidden tilesets and all removed constants off, including absent ones")
fixed_values = rse.assignments({1000, 1003}, set(), {1003: 0})
check(fixed_values == [{1000: 0, 1003: 0}, {1000: 1, 1003: 0}],
      "candidate OFF proof fixes only the explicit key while crossing retained constants")
check(rse.baseline_request("a.hlsl?X=1", {"a.hlsl?X=1;GI_SPEC_GUARDS=1"}) == "a.hlsl?X=1;GI_SPEC_GUARDS=1",
      "baseline mapping restores removed guard marker")
check(rse.baseline_request("a.hlsl", {"a.hlsl?GI_SPEC_GUARDS=1"}) == "a.hlsl?GI_SPEC_GUARDS=1",
      "baseline mapping inserts first define separator")
check(rse.baseline_request("a.hlsl", {"a.hlsl", "a.hlsl?GI_SPEC_GUARDS=1"}) == "a.hlsl",
      "baseline mapping prefers exact request")
words = [0x07230203, 0x10000, 0, 100, 0, (4 << 16) | 71, 10, 1, 1025,
         (4 << 16) | 71, 11, 33, 7, (4 << 16) | 71, 12, 1, 1000]
check(rse.live_ids(struct.pack("<" + "I" * len(words), *words)) == {1000, 1025},
      "binary domain discovery reads only SpecId decorations")
registry_tree = SimpleNamespace(read=lambda path: b"enum SpecId : unsigned {\n kSpecIdA = 1001,\n // retired 1002\n kSpecWaterB = 1021,\n};")
check(rse.registry_ids(registry_tree) == {1000, 1001, 1021}, "registry discovery ignores retired comments")
# Non-Boolean address specialization inputs must remain symbolic, never freeze to zero.
with tempfile.TemporaryDirectory() as td:
    source, dest = Path(td) / "input.spv", Path(td) / "frozen.spv"
    source_words = [0x07230203, 0x10000, 0, 100, 0,
                    (4 << 16) | 71, 10, 1, 1100,
                    (4 << 16) | 71, 11, 1, 1000,
                    (4 << 16) | 71, 12, 1, 1101,
                    (4 << 16) | 50, 3, 10, 0x1234,
                    (4 << 16) | 50, 3, 11, 1,
                    (4 << 16) | 50, 3, 12, 0x5678,
                    (3 << 16) | 48, 2, 13,
                    (3 << 16) | 49, 2, 14,
                    (6 << 16) | 52, 3, 15, 128, 10, 11]
    source.write_bytes(struct.pack("<" + "I" * len(source_words), *source_words))
    se.freeze_except(source, dest, {1100, 1101})
    frozen = struct.unpack("<" + "I" * (dest.stat().st_size // 4), dest.read_bytes())
    decoded, i = [], 5
    while i < len(frozen):
        count = frozen[i] >> 16
        decoded.append(tuple(frozen[i:i + count]))
        i += count
    check(rse.live_ids(dest.read_bytes()) == {1100, 1101}, "symbolic address SpecId decorations preserved")
    check(((4 << 16) | 50, 3, 10, 0x1234) in decoded and
          ((4 << 16) | 50, 3, 12, 0x5678) in decoded,
          "symbolic scalar instructions and full default literals preserved")
    check(((4 << 16) | 43, 3, 11, 1) in decoded and
          ((3 << 16) | 41, 2, 13) in decoded and ((3 << 16) | 42, 2, 14) in decoded,
          "selective freeze converts all other numeric and Boolean scalars")
    check(((6 << 16) | 52, 3, 15, 128, 10, 11) in decoded,
          "derived specialization operation remains for dependency-aware folding")
    with patch.object(se, "opt", side_effect=lambda tool, src, dst, *passes: dst) as optimize:
        se.specialize(None, source, Path(td) / "out.spv", {}, preserve={1100, 1101})
        check(all("--freeze-spec-const" not in c.args for c in optimize.call_args_list),
              "symbolic normalization never invokes blanket freeze pass")
        check(optimize.call_args_list[0].args[3:] == ("--fold-spec-const-op-composite",),
              "symbolic normalization folds after selective freeze")
    try:
        se.specialize(None, source, dest, {1100: 0}, preserve={1100})
        check(False, "symbolic constants cannot also be assigned")
    except ValueError:
        check(True, "symbolic constants cannot also be assigned")
    source.write_bytes(b"bad module")
    try:
        se.freeze_except(source, dest, {1100})
        check(False, "malformed selective-freeze input rejected")
    except ValueError:
        check(True, "malformed selective-freeze input rejected")
try:
    rse.validate_policy(b"unreviewed policy")
    check(False, "changed release policy must be reviewed")
except RuntimeError:
    check(True, "changed release policy fails closed")
# A universal symbolic certificate can replace enumeration; sampled equality cannot.
from queue import SimpleQueue
accelerator_values = rse.assignments({1003, 1004}, set())
check(rse.certificate_values(accelerator_values, False) == accelerator_values,
      "symbolic mismatch falls back to every Cartesian assignment")
endpoints = rse.certificate_values(accelerator_values, True)
check(len(endpoints) == 2 and all({a[i] for a in endpoints} == {0, 1} for i in (1003, 1004)),
      "universal equality endpoints explicitly cover both retained Boolean values")
check(rse.certificate_values(accelerator_values, True, exhaustive=True) == accelerator_values,
      "exhaustive mode retains every Cartesian assignment despite universal equality")
for universal in (True, False):
    normalization_calls = []
    def accelerator_get(blob, assign, temp, name, full=False):
        normalization_calls.append((blob, dict(assign), full))
        if 1003 not in assign and not universal:
            return blob
        return "equivalent"
    progress_queue = SimpleQueue()
    with patch.object(rse, "_WORKER_NORMALIZER", SimpleNamespace(get=accelerator_get)), \
         patch.object(rse, "_WORKER_PROGRESS", progress_queue):
        request, row = rse.check_request(("test", [("main", b"old", b"new", accelerator_values)], 4, []))
    check(request == "test" and row["state"] == "equivalent", "certificate worker equivalent result")
    check(len(normalization_calls) == (6 if universal else 10),
          "worker performs symbolic pair plus certified endpoints or full fallback")
    check(row["certificates"][0]["universal_certificate"] == universal and
          row["actual_frozen_checks"] == (2 if universal else 4),
          "worker records universal proof and actual frozen comparison count")
    events = []
    while not progress_queue.empty():
        events.append(progress_queue.get_nowait())
    check(events == [False] * 4, "worker reports exact Cartesian coverage without false failures")
# A failed symbolic proof must expose an interior Cartesian difference, not only endpoints.
def interior_difference_get(blob, assign, temp, name, full=False):
    if 1003 not in assign:
        return blob
    different = assign[1003] == 1 and assign[1004] == 0
    if full:
        return ([], ["OpStore old" if blob == b"old" else "OpStore changed"])
    return blob if different else "equivalent"
with patch.object(rse, "_WORKER_NORMALIZER", SimpleNamespace(get=interior_difference_get)), \
     patch.object(rse, "_WORKER_PROGRESS", SimpleQueue()):
    _, row = rse.check_request(("test", [("main", b"old", b"new", accelerator_values)], 4, []))
check(row["state"] == "DIFFERENT" and "OpStore changed" in row["detail"] and row["actual_frozen_checks"] == 4,
      "failed symbolic proof detects an interior differing assignment with instruction detail")
print(f"shader_identity_test: {checks} checks passed")
