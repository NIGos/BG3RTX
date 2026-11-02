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

# A commit's blob ids equal the working tree's for unmodified files (cache shared across trees).
dirty = subprocess.run(["git", "status", "--porcelain", "--", "shaders", "common"], cwd=ROOT, capture_output=True,
                       text=True).stdout
if subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True).returncode == 0:
    head, work = si.SourceTree.commit("HEAD"), si.SourceTree.worktree(ROOT)
    same = [p for p in head.ids if p not in dirty]
    check(same and all(head.ids[p] == work.ids.get(p) for p in same), "commit and worktree blob ids agree")
    check(all(si.git_blob_id(head.read(p)) == head.ids[p] for p in same[:50]), "cat-file contents match their ids")
print(f"shader_identity_test: {checks} checks passed")
