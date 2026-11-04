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
# modules or entry mismatches; only the three retired constants may be frozen.
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
with tempfile.TemporaryDirectory() as td:
    tmp = Path(td)
    fake_tool = SimpleNamespace(dxc=tmp / 'dxc.exe')
    with patch.object(hr.subprocess, 'run', return_value=SimpleNamespace(stdout=asm)):
        hr.retired_off(fake_tool, tmp / 'in.spv', tmp / 'out.spv')
    frozen = (tmp / 'out.spvasm').read_text()
    check(all(f'%{i} = OpConstant %9 0' in frozen for i in (1, 2, 3)), 'retired IDs frozen at zero')
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
    check([p['path'] for p in proofs] == ['raw identity', 'retirement/default equivalence (1017-1019=0 baseline; 1026=0 both)', 'failed'],
          'only DIFFERENT uses fallback, raw identity and failures retain their paths')
    check(compare_mock.call_count == 1 and all(c.args[1] == ['changed'] for c in compile_mock.call_args_list),
          'fallback compiles only differing identical request spellings')
    with patch.object(hr, 'compile_requests', return_value={'changed': module}), \
         patch.object(hr, 'compare_retired', return_value='arithmetic differs'), quiet:
        proofs = hr.retirement_fallback(rows, SimpleNamespace(label='base'), SimpleNamespace(label='new'), None, None, 'test')
    check(proofs[1]['path'] == 'failed', 'failed retirement proof remains a gate failure')
print(f"shader_identity_test: {checks} checks passed")
