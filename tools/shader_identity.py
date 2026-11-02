"""Offline compile of runtime shader requests with a content-addressed cache (one shared helper).

Every offline shader gate (tools/compile_shaders.py for merge-green, the tests/*_shader_test.py
byte-identity gates) compiles through this module:

- A request is "<file>?NAME=VALUE;..." (common/shader_variant.h, shaders/variants.txt). It compiles
  as layer/shaders.cpp does at run time: DXC -spirv vulkan1.3, HLSL 2021, cs_6_6, -I shaders,
  -D BG3RTX=1, the SHaRC headers when found, the request's defines, the file's "// dxc:" line, -O3;
  one module per [numthreads] entry (else main).
- Cache key of one entry = sha256 of (schema, toolchain id = the DXC binaries + `dxc --version` +
  the argument template, request string, entry, profile, and the git blob id of the source and of
  every file it can include, transitively, plus every include candidate that does not exist).
  An include change invalidates exactly the requests that can reach it; a DXC change invalidates
  all. Cached value: the SPIR-V hash; the SPIR-V itself lives in <cache>/spv/<sha>.spv and is
  re-hashed on every hit (a corrupt or missing blob is a miss). Failed compiles are never cached.
- A baseline commit's sources come from `git ls-tree` + `git cat-file --batch` (raw blobs, the same
  ids as the working tree's files); they are written to a short temp tree only when some request
  misses the cache. A second run against the same baseline compiles nothing.
- Byte identity of two trees: requests whose input keys are equal are identical by construction
  (reported, not compiled); the others are compiled (or fetched) on both sides and their SPIR-V
  hashes compared.
- Progress: a line at least every 15 s (BG3RTX_PROGRESS_SECONDS) with done/total, cached, rate,
  ETA and failures so far, flushed. The process runs at BelowNormal priority (tools/low_priority.py).

layer/shader_cache.cpp is the C++ twin of git_blob_id, SourceTree.closure and entry_key (the runtime
cache): change both together; tests/shader_cache_key_test.py (--write) and shader_cache_test check them.

The cache defaults to build/shader_cache of the main checkout (shared by every worktree; the
content addressing makes sharing safe); BG3RTX_SHADER_CACHE overrides it.

CLI:
    py -3 tools/shader_identity.py [--filter REGEX] [--validate]          compile every request
    py -3 tools/shader_identity.py --baseline <rev> [--filter REGEX]      byte identity vs <rev>
    py -3 tools/shader_identity.py --stats                                cache size
    py -3 tools/shader_identity.py --prune [--prune-days 14 --prune-baselines 3 --prune-cap-gb 2]
        keeps entries used in the last N days plus the working tree's and the last K baselines'
        entries, evicts the oldest others above the cap, deletes unreferenced blobs
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import posixpath
import re
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.low_priority import lower_priority  # noqa: E402

SCHEMA = 1
PROFILE = "cs_6_6"
SHARC_LOGICAL = "sharc"       # logical include root of the SHaRC headers (external/sharc-1.8.3/include)
SHADERS = "shaders"
# The fixed DXC arguments (layer/shaders.cpp, shader_debug=0). Part of the toolchain id.
ARGS_TEMPLATE = ("{src} -spirv -fspv-target-env=vulkan1.3 -HV 2021 -T {profile} -E {entry} -I {shaders} "
                 "-D BG3RTX=1 [-I {sharc} -D BG3RTX_SHARC_SDK=1] {defines} {first_line} -O3 -Fo {out}")
VAL_ARGS = ["--target-env", "vulkan1.3"]
ENTRY_RE = re.compile(rb"\[numthreads\([^)]*\)\]\s*void\s+(\w+)")
INCLUDE_RE = re.compile(rb'^[ \t]*#[ \t]*include[ \t]*([<"])([^>"\r\n]+)[>"]', re.M)
MACRO_INCLUDE_RE = re.compile(rb'^[ \t]*#[ \t]*include[ \t]+[^<"\s]', re.M)


def git_blob_id(data):
    """The id git gives a blob: the working tree and a commit hash the same content the same way."""
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def git(*args, cwd=ROOT, check=True, **kw):
    return subprocess.run(["git", *args], cwd=cwd, capture_output=True, check=check, **kw)


def default_baseline():
    """The merge-base with main (HEAD itself on main): the commit the work starts from."""
    base = git("merge-base", "HEAD", "main", check=False, text=True)
    if base.returncode == 0 and base.stdout.strip():
        return base.stdout.strip()
    return git("rev-parse", "HEAD", text=True).stdout.strip()


def split_request(request):
    file, _, defs = request.partition("?")
    return file, [d for d in defs.split(";") if d]


def first_line_args(data):
    first = data.split(b"\n", 1)[0].strip().decode("utf-8", "replace")
    return first[len("// dxc:"):].split() if first.startswith("// dxc:") else []


def entries_of(data):
    """Every [numthreads] entry once (a name under two #if branches is one module), else main."""
    return list(dict.fromkeys(e.decode() for e in ENTRY_RE.findall(data))) or ["main"]


# ---------------------------------------------------------------------------------------------
# Source trees
# ---------------------------------------------------------------------------------------------
class SourceTree:
    """Files by logical posix path ('shaders/gi/x.hlsl', 'common/y.h', 'sharc/SharcCommon.h')
    -> git blob id. `read` returns the bytes; `disk` is a directory holding the files at their
    logical paths (None until `materialize`)."""

    def __init__(self, label, ids, reader, disk=None):
        self.label, self.ids, self._reader, self.disk = label, ids, reader, disk
        self._data, self._closure, self._tmp = {}, {}, None
        self._lock = threading.Lock()

    def read(self, path):
        if path not in self._data:
            self._data[path] = self._reader(path)
        return self._data[path]

    def with_sharc(self, sharc):
        """Add the SHaRC headers under 'sharc/' (read from disk, the same for every tree)."""
        if sharc is None:
            return self
        sharc = Path(sharc)
        for p in sorted(sharc.rglob("*")):
            if p.is_file():
                logical = SHARC_LOGICAL + "/" + p.relative_to(sharc).as_posix()
                self._data[logical] = p.read_bytes()
                self.ids[logical] = git_blob_id(self._data[logical])
        self.sharc_dir = sharc
        return self

    sharc_dir = None

    @staticmethod
    def worktree(root=ROOT, folders=("shaders", "common")):
        root = Path(root)
        ids = {}
        data = {}
        for folder in folders:
            for p in (root / folder).rglob("*"):
                if p.is_file():
                    logical = p.relative_to(root).as_posix()
                    data[logical] = p.read_bytes()
                    ids[logical] = git_blob_id(data[logical])
        tree = SourceTree(f"tree {root.name}", ids, lambda p: (root / p).read_bytes(), disk=root)
        tree._data.update(data)
        return tree

    @staticmethod
    def commit(rev, repo=ROOT, folders=("shaders", "common")):
        """A commit's files as raw blobs (git ls-tree + one git cat-file --batch)."""
        full = git("rev-parse", "--verify", rev + "^{commit}", cwd=repo, text=True).stdout.strip()
        out = git("ls-tree", "-r", "-z", full, "--", *folders, cwd=repo).stdout
        ids = {}
        for item in out.split(b"\0"):
            if not item:
                continue
            meta, path = item.split(b"\t", 1)
            mode, kind, oid = meta.split()
            if kind == b"blob" and mode != b"120000":
                ids[path.decode()] = oid.decode()
        data = {}
        if ids:
            order = sorted(ids)
            raw = git("cat-file", "--batch", cwd=repo, input="\n".join(ids[p] for p in order).encode() + b"\n").stdout
            pos = 0
            for p in order:
                eol = raw.index(b"\n", pos)
                size = int(raw[pos:eol].split()[2])
                data[p] = raw[eol + 1:eol + 1 + size]
                pos = eol + 1 + size + 1
        tree = SourceTree(f"commit {full[:12]}", ids, data.__getitem__)
        tree._data.update(data)
        tree.rev = full
        return tree

    @staticmethod
    def memory(files, label="memory"):
        """Tests: {logical path: bytes}."""
        files = {k: (v.encode() if isinstance(v, str) else v) for k, v in files.items()}
        tree = SourceTree(label, {k: git_blob_id(v) for k, v in files.items()}, files.__getitem__)
        tree._data.update(files)
        return tree

    def requests(self):
        """Every shaders/**/*.hlsl plus every request of shaders/variants.txt."""
        files = sorted(p[len(SHADERS) + 1:] for p in self.ids if p.startswith(SHADERS + "/") and p.endswith(".hlsl"))
        manifest = SHADERS + "/variants.txt"
        lines = self.read(manifest).decode().splitlines() if manifest in self.ids else []
        return files + [l.strip() for l in lines if l.strip() and not l.startswith("#")]

    def materialize(self):
        """A directory with the files at their logical paths (a short temp dir: MAX_PATH)."""
        with self._lock:
            if self.disk is None:
                self._tmp = tempfile.TemporaryDirectory(prefix="b3s-", ignore_cleanup_errors=True)
                base = Path(self._tmp.name)
                for p in self.ids:
                    if p.startswith(SHARC_LOGICAL + "/"):
                        continue
                    dest = base / p
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    dest.write_bytes(self.read(p))
                self.disk = base
            return self.disk

    def cleanup(self):
        if self._tmp is not None:
            self._tmp.cleanup()
            self._tmp, self.disk = None, None

    # Dependencies --------------------------------------------------------------------------
    def includes(self):
        return [SHADERS] + ([SHARC_LOGICAL] if any(p.startswith(SHARC_LOGICAL + "/") for p in self.ids) else [])

    def closure(self, path):
        """[(logical path, blob id or None)] of `path` and everything it can include, sorted.
        Conservative: every #include line counts (also under #if), every existing search
        candidate is followed, every missing candidate is recorded (adding it changes the key)."""
        if path in self._closure:
            return self._closure[path]
        dirs = self.includes()
        seen, stack = {}, [path]
        while stack:
            p = stack.pop()
            if p in seen:
                continue
            bid = self.ids.get(p)
            seen[p] = bid
            if bid is None:
                continue
            data = self.read(p)
            if MACRO_INCLUDE_RE.search(data):
                # An include through a macro cannot be resolved here: depend on every file.
                seen["*all*"] = hashlib.sha256("".join(f"{k}={v}\n" for k, v in sorted(self.ids.items())).encode()).hexdigest()
            for kind, spec in INCLUDE_RE.findall(data):
                spec = spec.decode().strip().replace("\\", "/")
                cands = ([posixpath.dirname(p)] if kind == b'"' else []) + dirs
                for d in cands:
                    stack.append(posixpath.normpath(posixpath.join(d, spec)))
        self._closure[path] = sorted(seen.items())
        return self._closure[path]


# ---------------------------------------------------------------------------------------------
# Toolchain and keys
# ---------------------------------------------------------------------------------------------
def _file_digest(paths):
    h = hashlib.sha256()
    for p in paths:
        p = Path(p)
        h.update(p.name.encode() + b"\0")
        h.update(p.read_bytes() if p.exists() else b"<missing>")
    return h.hexdigest()


def find_sharc(root=ROOT):
    for c in (root / "external" / "sharc-1.8.3" / "include", root.parents[2] / "external" / "sharc-1.8.3" / "include"):
        if (c / "SharcCommon.h").exists():
            return c
    return None


class Toolchain:
    def __init__(self, dxc=None, spirv_val=None, sharc="auto"):
        sdk = Path(os.environ.get("VULKAN_SDK", r"C:\VulkanSDK\1.4.357.0"))
        self.dxc = Path(dxc) if dxc else sdk / "Bin" / "dxc.exe"
        self.spirv_val = Path(spirv_val) if spirv_val else self.dxc.with_name("spirv-val" + self.dxc.suffix)
        self.sharc = find_sharc() if sharc == "auto" else (Path(sharc) if sharc else None)
        self._id = self._val_id = None

    @property
    def id(self):
        """DXC binaries + its version line + the argument template + SHaRC presence."""
        if self._id is None:
            if not self.dxc.exists():
                raise FileNotFoundError(f"DXC not found: {self.dxc}")
            version = subprocess.run([str(self.dxc), "--version"], capture_output=True, text=True).stdout.strip()
            self._id = hashlib.sha256(json.dumps([
                _file_digest([self.dxc, self.dxc.with_name("dxcompiler.dll")]), version, ARGS_TEMPLATE,
                PROFILE, self.sharc is not None]).encode()).hexdigest()
        return self._id

    @property
    def val_id(self):
        if self._val_id is None:
            self._val_id = hashlib.sha256(json.dumps([_file_digest([self.spirv_val]), VAL_ARGS]).encode()).hexdigest()
        return self._val_id

    def command(self, src, shaders, entry, defines, extra, out):
        cmd = [str(self.dxc), str(src), "-spirv", "-fspv-target-env=vulkan1.3", "-HV", "2021", "-T", PROFILE,
               "-E", entry, "-I", str(shaders), "-D", "BG3RTX=1"]
        if self.sharc is not None:
            cmd += ["-I", str(self.sharc), "-D", "BG3RTX_SHARC_SDK=1"]
        for d in defines:
            cmd += ["-D", d]
        return cmd + list(extra) + ["-O3", "-Fo", str(out)]


def entry_key(tool_id, request, entry, deps, profile=PROFILE):
    """The cache key of one compiled entry (pure: unit-tested)."""
    return hashlib.sha256(json.dumps([SCHEMA, tool_id, request, entry, profile, deps]).encode()).hexdigest()


def request_keys(tree, request, tool_id):
    """[(entry, key)] of a request in `tree`, or None when its file is not there."""
    file, _ = split_request(request)
    path = SHADERS + "/" + file
    if path not in tree.ids:
        return None
    deps = tree.closure(path)
    return [(e, entry_key(tool_id, request, e, deps)) for e in entries_of(tree.read(path))]


# ---------------------------------------------------------------------------------------------
# Cache
# ---------------------------------------------------------------------------------------------
def default_cache_dir():
    if os.environ.get("BG3RTX_SHADER_CACHE"):
        return Path(os.environ["BG3RTX_SHADER_CACHE"])
    common = git("rev-parse", "--path-format=absolute", "--git-common-dir", check=False, text=True).stdout.strip()
    main = Path(common).parent if common and Path(common).name == ".git" else ROOT
    return main / "build" / "shader_cache"


class Cache:
    """sqlite index (key -> SPIR-V sha256; validated SPIR-V) + content-addressed blobs.
    Only the calling thread touches the database; blobs are written atomically."""

    def __init__(self, directory=None):
        self.dir = Path(directory) if directory else default_cache_dir()
        (self.dir / "spv").mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(str(self.dir / "index.sqlite3"), timeout=120, check_same_thread=False,
                                  isolation_level=None)  # autocommit: no write lock held across a run (parallel runs hit "database is locked")
        # Switching a fresh database to WAL ignores the busy timeout: two runs opening it at once got "database is
        # locked" (shader_identity_test's in-flight lock case, 21 of 60 in a repro). Retry for up to the timeout.
        for attempt in range(2400):
            try:
                self.db.execute("PRAGMA journal_mode=WAL")
                break
            except sqlite3.OperationalError:
                if attempt == 2399:
                    raise
                time.sleep(0.05)
        self.db.execute("CREATE TABLE IF NOT EXISTS compiled (key TEXT PRIMARY KEY, sha TEXT, size INTEGER, "
                        "request TEXT, entry TEXT, at REAL)")
        self.db.execute("CREATE TABLE IF NOT EXISTS validated (sha TEXT, val TEXT, PRIMARY KEY (sha, val))")
        self.db.execute("CREATE TABLE IF NOT EXISTS baselines (rev TEXT PRIMARY KEY, at REAL)")
        self.db.commit()
        self._verified = set()

    def blob_path(self, sha):
        return self.dir / "spv" / sha[:2] / (sha + ".spv")

    def get(self, key):
        """The SPIR-V sha256 of `key`, or None (a missing or corrupt blob is a miss)."""
        row = self.db.execute("SELECT sha, size FROM compiled WHERE key=?", (key,)).fetchone()
        if not row:
            return None
        sha, size = row
        # 'at' = last use: pruning keeps what was used recently.
        try:  # LRU touch is best effort: never fail a run because another process holds the lock
            self.db.execute("UPDATE compiled SET at=? WHERE key=?", (time.time(), key))
        except sqlite3.OperationalError:
            pass
        if sha in self._verified:
            return sha
        p = self.blob_path(sha)
        try:
            data = p.read_bytes()
        except OSError:
            return None
        if len(data) != size or hashlib.sha256(data).hexdigest() != sha:
            return None
        self._verified.add(sha)
        return sha

    def put_blob(self, data):
        sha = hashlib.sha256(data).hexdigest()
        p = self.blob_path(sha)
        if not p.exists():
            p.parent.mkdir(parents=True, exist_ok=True)
            tmp = p.with_name(f"{p.name}.{os.getpid()}.{threading.get_ident()}.tmp")
            tmp.write_bytes(data)
            try:
                os.replace(tmp, p)
            except OSError:
                if tmp.exists():
                    tmp.unlink()
        self._verified.add(sha)
        return sha

    def put(self, key, data, request, entry):
        sha = self.put_blob(data)
        try:  # best effort: the blob is on disk; a locked index only costs a recompile next time
            self.db.execute("INSERT OR REPLACE INTO compiled VALUES (?,?,?,?,?,?)",
                            (key, sha, len(data), request, entry, time.time()))
        except sqlite3.OperationalError:
            pass
        return sha

    def is_valid(self, sha, val_id):
        return self.db.execute("SELECT 1 FROM validated WHERE sha=? AND val=?", (sha, val_id)).fetchone() is not None

    def put_valid(self, sha, val_id):
        try:
            self.db.execute("INSERT OR IGNORE INTO validated VALUES (?,?)", (sha, val_id))
        except sqlite3.OperationalError:
            pass

    def commit(self):
        self.db.commit()

    def note_baseline(self, rev):
        self.db.execute("INSERT OR REPLACE INTO baselines VALUES (?,?)", (rev, time.time()))
        self.db.commit()

    def recent_baselines(self, k):
        return [r for (r,) in self.db.execute("SELECT rev FROM baselines ORDER BY at DESC LIMIT ?", (k,))]

    def stats(self):
        n = self.db.execute("SELECT COUNT(*) FROM compiled").fetchone()[0]
        v = self.db.execute("SELECT COUNT(*) FROM validated").fetchone()[0]
        blobs = list((self.dir / "spv").rglob("*.spv"))
        return {"dir": str(self.dir), "entries": n, "validated": v, "blobs": len(blobs),
                "bytes": sum(b.stat().st_size for b in blobs)}


class MemoryCache(Cache):
    """No persistence (--no-cache, tests): blobs still go to a temp dir so callers get paths."""

    def __init__(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="b3c-", ignore_cleanup_errors=True)
        super().__init__(self._tmp.name)


PRUNE_DAYS, PRUNE_BASELINES, PRUNE_CAP = 14, 3, 2 << 30


def protected_keys(cache, tool, k=PRUNE_BASELINES, repo=ROOT):
    """Every entry key of the working tree and of the last k baselines (current DXC)."""
    keys = set()
    trees = [SourceTree.worktree(repo)]
    for rev in cache.recent_baselines(k):
        try:
            trees.append(SourceTree.commit(rev, repo))
        except subprocess.CalledProcessError:
            continue                                  # the commit is gone
    for tree in trees:
        _prep(tree, tool)
        for r in tree.requests():
            keys.update(key for _, key in request_keys(tree, r, tool.id) or [])
    return keys


def prune(cache, days=PRUNE_DAYS, cap=PRUNE_CAP, protect=None, tool=None, baselines=PRUNE_BASELINES, now=None):
    """Drop entries unused for `days` that are not in `protect` (default: the working tree's and
    the last `baselines` baselines' keys), then the oldest unprotected ones while the referenced
    SPIR-V exceeds `cap` bytes; delete unreferenced blobs. Prints and returns what it freed."""
    if protect is None:
        protect = protected_keys(cache, tool or Toolchain(), baselines)
    now = time.time() if now is None else now
    rows = sorted(cache.db.execute("SELECT key, sha, size, at FROM compiled"), key=lambda r: r[3])
    drop = {k for k, _, _, at in rows if at < now - days * 86400 and k not in protect}
    refs, size_of = {}, {}
    for k, sha, size, _ in rows:
        size_of[sha] = size
        if k not in drop:
            refs[sha] = refs.get(sha, 0) + 1
    total = sum(size_of[x] for x in refs)
    for k, sha, size, _ in rows:                     # oldest first
        if total <= cap:
            break
        if k in drop or k in protect:
            continue
        drop.add(k)
        refs[sha] -= 1
        if not refs[sha]:
            del refs[sha]
            total -= size_of[sha]
    cache.db.executemany("DELETE FROM compiled WHERE key=?", [(k,) for k in drop])
    keep = {x for (x,) in cache.db.execute("SELECT DISTINCT sha FROM compiled")}
    stale = [x for (x,) in cache.db.execute("SELECT DISTINCT sha FROM validated") if x not in keep]
    cache.db.executemany("DELETE FROM validated WHERE sha=?", [(x,) for x in stale])
    cache.commit()
    blobs = freed = 0
    for p in (cache.dir / "spv").glob("*/*"):
        try:
            st = p.stat()
            if (p.suffix == ".spv" and p.stem not in keep) or (p.suffix == ".tmp" and st.st_mtime < now - 3600):
                p.unlink()
                freed += st.st_size
                blobs += 1
                cache._verified.discard(p.stem)
        except OSError:
            pass
    kept = sum(size_of.get(x, 0) for x in keep)
    print(f"shader cache prune: {len(drop)} entries and {blobs} blobs removed, {freed / 2**20:.0f} MB freed; "
          f"{len(keep)} blobs, {kept / 2**20:.0f} MB kept (cap {cap / 2**30:.1f} GB, {days:g} days, "
          f"{len(protect)} protected keys)", flush=True)
    return {"entries": len(drop), "blobs": blobs, "freed": freed, "kept": kept}


# ---------------------------------------------------------------------------------------------
# Progress
# ---------------------------------------------------------------------------------------------
def _fmt_s(s):
    s = int(s)
    return f"{s // 60}m{s % 60:02d}s" if s >= 60 else f"{s}s"


class Progress:
    """Prints `[label] done/total ...` at start, at least every `every` seconds, and at the end."""

    def __init__(self, label, total, cached=0, every=None, verb="compiled"):
        self.label, self.total, self.cached, self.verb = label, total, cached, verb
        self.every = float(every if every is not None else os.environ.get("BG3RTX_PROGRESS_SECONDS", 15))
        self.done = self.failed = 0
        self.t0 = time.time()
        self._stop = threading.Event()
        self._lock = threading.Lock()
        self._thread = None

    def __enter__(self):
        if self.total:
            self._thread = threading.Thread(target=self._loop, daemon=True)
            self._thread.start()
        return self

    def _loop(self):
        while not self._stop.wait(self.every):
            self.line()

    def step(self, failed=False):
        with self._lock:
            self.done += 1
            self.failed += bool(failed)

    def line(self, final=False):
        el = time.time() - self.t0
        rate = self.done / el if el > 0 else 0.0
        eta = (self.total - self.done) / rate if rate > 0 else 0
        tail = f"in {_fmt_s(el)}" if final else f"ETA {_fmt_s(eta) if rate > 0 else '?'}"
        print(f"[{self.label}] {self.done}/{self.total} {self.verb} ({self.cached} more cached), {rate:.1f}/s, {tail}, "
              f"{self.failed} failed", flush=True)

    def __exit__(self, *exc):
        self._stop.set()
        if self._thread:
            self._thread.join()
            self.line(final=True)


# ---------------------------------------------------------------------------------------------
# Compile
# ---------------------------------------------------------------------------------------------
class Entry:
    __slots__ = ("name", "key", "sha", "cached")

    def __init__(self, name, key, sha=None, cached=False):
        self.name, self.key, self.sha, self.cached = name, key, sha, cached


class Result:
    """One request in one tree. ok: every entry compiled; valid: every entry passed spirv-val
    (None when not asked); log: the DXC / spirv-val messages of a failure."""

    def __init__(self, request, entries, cache):
        self.request, self.entries, self._cache = request, entries, cache
        self.ok = entries is not None
        self.valid = None
        self.log = "" if self.ok else "no such file in this tree"

    @property
    def cached(self):
        return self.ok and all(e.cached for e in self.entries)

    @property
    def digest(self):
        """sha256 over the entries' SPIR-V hashes: the request's identity."""
        if not self.ok:
            return None
        return hashlib.sha256("".join(f"{e.name}:{e.sha}\n" for e in self.entries).encode()).hexdigest()

    def path(self, entry="main"):
        e = next((e for e in self.entries if e.name == entry), None) if self.ok else None
        return self._cache.blob_path(e.sha) if e and e.sha else None

    def paths(self):
        return [self._cache.blob_path(e.sha) for e in self.entries] if self.ok else []

    def blob(self, entry="main"):
        p = self.path(entry)
        return p.read_bytes() if p else None


# Child processes start at BelowNormal even if this process was not lowered.
_LOW = {"creationflags": 0x00004000} if sys.platform == "win32" else {}


class _DxcSlot:
    """Machine-wide cap on concurrent dxc processes, shared by every run (parallel agents, merge-green):
    max(2, cores / 2) lock files; a compile holds one while dxc runs. Each dxc takes ~0.5 GB, so two
    unshared 8-job runs made the PC unusable (2026-11-01)."""
    _dir = Path(os.environ.get("TEMP", ".")) / "bg3rtx_dxc_slots"
    _count = int(os.environ.get("BG3RTX_DXC_SLOTS", "0")) or max(2, (os.cpu_count() or 4) // 2)
    _waited_note = [False]

    def __enter__(self):
        self._dir.mkdir(parents=True, exist_ok=True)
        start = time.time()
        while True:
            for i in range(self._count):
                f = open(self._dir / f"slot{i}.lock", "a+b")
                try:
                    if sys.platform == "win32":
                        import msvcrt
                        msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
                    else:
                        import fcntl
                        fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    self._f = f
                    return self
                except OSError:
                    f.close()
            if time.time() - start > 10 and not self._waited_note[0]:
                self._waited_note[0] = True
                print(f"  waiting for a dxc slot ({self._count} machine-wide, shared with other runs)", flush=True)
            time.sleep(0.2)

    def __exit__(self, *exc):
        try:
            if sys.platform == "win32":
                import msvcrt
                self._f.seek(0)
                msvcrt.locking(self._f.fileno(), msvcrt.LK_UNLCK, 1)
        finally:
            self._f.close()


def _run_dxc(tool, tree, request, entry, out):
    file, defines = split_request(request)
    disk = tree.materialize()
    src = Path(disk) / SHADERS / file
    cmd = tool.command(src, Path(disk) / SHADERS, entry, defines, first_line_args(tree.read(SHADERS + "/" + file)), out)
    with _DxcSlot():
        r = subprocess.run(cmd, capture_output=True, text=True, **_LOW)
    if r.returncode or not out.exists():
        return None, f"[{entry}] " + (r.stdout + r.stderr).strip()[:4000]
    data = out.read_bytes()
    out.unlink()
    return data, ""


class _KeyLock:
    """Per-entry in-flight lock in the shared cache (<cache>/inflight/<key[:2]>/<key>.lock): parallel runs that miss
    the same entry (merge-green and two agents' identity runs on a new main, 2026-11-02: ~1950 entries compiled up to
    3 times) compile it once; the others wait for the lock and read the holder's result from <key>.done ("sha size",
    written after the blob, before the unlock). The lock is an OS byte lock (msvcrt / flock, as _DxcSlot): a dead
    holder's lock is released by the OS, so no stale lock survives a crash."""

    def __init__(self, cache_dir, key):
        d = Path(cache_dir) / "inflight" / key[:2]
        self.lock, self.done = d / f"{key}.lock", d / f"{key}.done"
        self._f = None

    def acquire(self):
        """Blocks until held; True when another holder had it first (it may have finished the entry)."""
        self.lock.parent.mkdir(parents=True, exist_ok=True)
        waited = False
        while True:
            f = open(self.lock, "a+b")
            try:
                f.seek(0)
                if sys.platform == "win32":
                    import msvcrt
                    msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
                self._f = f
                return waited
            except OSError:
                f.close()
                waited = True
                time.sleep(0.1)

    def release(self):
        if not self._f:
            return
        try:
            if sys.platform == "win32":
                import msvcrt
                self._f.seek(0)
                msvcrt.locking(self._f.fileno(), msvcrt.LK_UNLCK, 1)
        finally:
            self._f.close()
            self._f = None

    def finished(self):
        try:
            sha, size = self.done.read_text().split()
            return sha, int(size)
        except (OSError, ValueError):
            return None

    def mark_done(self, sha, size):
        tmp = self.done.with_name(f"{self.done.name}.{os.getpid()}.{threading.get_ident()}.tmp")
        tmp.write_text(f"{sha} {size}")
        os.replace(tmp, self.done)


def _compile_entry(tool, tree, request, entry, out, cache, key):
    """(data, err, shared): one entry through the in-flight lock; shared = taken from another run's compile."""
    lk = _KeyLock(cache.dir, key)
    lk.acquire()
    try:
        hit = lk.finished()
        if hit:
            try:
                data = cache.blob_path(hit[0]).read_bytes()
                if len(data) == hit[1] and hashlib.sha256(data).hexdigest() == hit[0]:
                    return data, "", True
            except OSError:
                pass
        data, err = _run_dxc(tool, tree, request, entry, out)
        if data is not None:
            lk.mark_done(cache.put_blob(data), len(data))
        return data, err, False
    finally:
        lk.release()


def _run_val(tool, path):
    r = subprocess.run([str(tool.spirv_val), *VAL_ARGS, str(path)], capture_output=True, text=True, **_LOW)
    return r.returncode == 0, (r.stdout + r.stderr).strip()[:4000]


def _prep(tree, tool):
    if tool.sharc is not None and tree.sharc_dir is None:
        tree.with_sharc(tool.sharc)


def default_jobs():
    return max(1, (os.cpu_count() or 2) // 2)


def compile_many(jobs_list, tool=None, cache=None, jobs=None, label="shaders", validate=False):
    """Compile [(tree, request)] through the cache. Returns {(tree.label, request): Result}."""
    lower_priority()
    tool = tool or Toolchain()
    cache = cache or Cache()
    jobs = jobs or default_jobs()
    results, todo, by_key, owners = {}, [], {}, {}
    for tree, request in jobs_list:
        _prep(tree, tool)
        keys = request_keys(tree, request, tool.id)
        if keys is None:
            results[(tree.label, request)] = Result(request, None, cache)
            continue
        entries = []
        for name, key in keys:
            sha = cache.get(key)
            entries.append(Entry(name, key, sha, cached=sha is not None))
            if sha is None:
                if key not in by_key:   # the same input on both sides of an identity: compile once
                    todo.append((tree, request, entries[-1]))
                by_key.setdefault(key, []).append(entries[-1])
        results[(tree.label, request)] = Result(request, entries, cache)
        for e in entries:
            owners.setdefault(e.key, []).append((tree.label, request))
    n_entries = sum(len(r.entries) for r in results.values() if r.ok)
    print(f"[{label}] {len(results)} requests, {n_entries} entries: {n_entries - len(todo)} cached, "
          f"{len(todo)} to compile with {jobs} jobs (cache {cache.dir})", flush=True)
    failures = {}
    with tempfile.TemporaryDirectory(prefix="b3o-") as out_dir, \
            concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        if todo:
            with Progress(label, len(todo), cached=n_entries - len(todo)) as prog:
                futs = {pool.submit(_compile_entry, tool, tree, request, e.name, Path(out_dir) / f"{i}.spv", cache, e.key):
                        (tree, request, e) for i, (tree, request, e) in enumerate(todo)}
                shared = 0
                for i, f in enumerate(concurrent.futures.as_completed(futs)):
                    tree, request, e = futs[f]
                    try:
                        data, err, from_other = f.result()
                        shared += from_other
                    except Exception as ex:  # noqa: BLE001
                        data, err = None, f"[{e.name}] {ex!r}"
                    if data is None:
                        for t2, r2 in owners[e.key]:
                            failures.setdefault((t2, r2), []).append(err)
                    else:
                        sha = cache.put(e.key, data, request, e.name)
                        for same in by_key[e.key]:
                            same.sha = sha
                    prog.step(failed=data is None)
                    if i % 50 == 49:
                        cache.commit()
                cache.commit()
            if shared:
                print(f"[{label}] {shared} of {len(todo)} entries taken from another run's compile (in-flight lock)", flush=True)
        for k, errs in failures.items():
            results[k].ok = False
            results[k].log = "\n".join(errs)
        if validate:
            shas = {e.sha for r in results.values() if r.ok for e in r.entries}
            need = sorted(s for s in shas if not cache.is_valid(s, tool.val_id))
            bad = {}
            if need:
                print(f"[{label} spirv-val] {len(need)} modules to validate ({len(shas) - len(need)} cached)", flush=True)
                with Progress(label + " spirv-val", len(need), cached=len(shas) - len(need), verb="validated") as prog:
                    futs = {pool.submit(_run_val, tool, cache.blob_path(s)): s for s in need}
                    for f in concurrent.futures.as_completed(futs):
                        ok, msg = f.result()
                        if ok:
                            cache.put_valid(futs[f], tool.val_id)
                        else:
                            bad[futs[f]] = msg
                        prog.step(failed=not ok)
                cache.commit()
            for r in results.values():
                if r.ok:
                    errs = [f"[{e.name}] spirv-val: {bad[e.sha]}" for e in r.entries if e.sha in bad]
                    r.valid = not errs
                    if errs:
                        r.log = "\n".join(errs)
    return results


def compile_requests(tree, requests, **kw):
    """{request: Result} of one tree."""
    res = compile_many([(tree, r) for r in requests], **kw)
    return {r: res[(tree.label, r)] for r in requests}


IDENTICAL_INPUTS = "identical inputs"
SAME = "same"


def identity(base, new, requests, tool=None, cache=None, jobs=None, label="identity", baseline_requests=None):
    """Byte identity of `requests` in `base` vs `new`. baseline_requests: {request in new: request
    in base} when the two spellings differ (default: the same string).
    Returns [(request, state, log)] with state IDENTICAL_INPUTS (equal input keys: identical by
    construction, not compiled), SAME (compiled, equal SPIR-V), 'DIFFERENT', 'compile' (a side
    failed) or 'missing' (the file is absent on a side)."""
    tool = tool or Toolchain()
    cache = cache or Cache()
    baseline_requests = baseline_requests or {}
    out, need = [], []
    _prep(base, tool)
    _prep(new, tool)
    if getattr(base, "rev", None):
        cache.note_baseline(base.rev)
    for r in requests:
        b = baseline_requests.get(r, r)
        kb, kn = request_keys(base, b, tool.id), request_keys(new, r, tool.id)
        if kb is None or kn is None:
            out.append((r, "missing", f"{'baseline' if kb is None else 'current'} has no {split_request(r)[0]}"))
        elif kb == kn and b == r:
            out.append((r, IDENTICAL_INPUTS, ""))
        else:
            need.append(r)
            out.append((r, None, ""))
    print(f"[{label}] {len(requests)} requests: {len(requests) - len(need)} decided by input keys, "
          f"{len(need)} to compare ({base.label} vs {new.label})", flush=True)
    res = compile_many([(base, baseline_requests.get(r, r)) for r in need] + [(new, r) for r in need],
                       tool=tool, cache=cache, jobs=jobs, label=label) if need else {}
    final = []
    for r, state, log in out:
        if state is None:
            a, b = res[(base.label, baseline_requests.get(r, r))], res[(new.label, r)]
            if not a.ok or not b.ok:
                state, log = "compile", (a.log if not a.ok else b.log)
            else:
                state = SAME if a.digest == b.digest else "DIFFERENT"
        final.append((r, state, log))
    return final


def identical(state):
    return state in (IDENTICAL_INPUTS, SAME)


def gate_setup(baseline, dxc=None, sharc="auto", spirv_val=None, no_cache=False):
    """(toolchain, cache, baseline tree, this tree) of an offline shader gate; lowers the priority."""
    lower_priority()
    tool = Toolchain(dxc, spirv_val, sharc)
    cache = MemoryCache() if no_cache else Cache()
    return tool, cache, SourceTree.commit(baseline).with_sharc(tool.sharc), SourceTree.worktree(ROOT).with_sharc(tool.sharc)


def unchanged_since(tree):
    """Raise when shaders/ or common/ changed on disk after `tree` was read (a frozen gate)."""
    now = SourceTree.worktree(ROOT).ids
    mine = {k: v for k, v in tree.ids.items() if not k.startswith(SHARC_LOGICAL + "/")}
    if now != mine:
        raise RuntimeError("Shader/common source changed during the gate; rerun it")


# ---------------------------------------------------------------------------------------------
# The common feature gate of the *_shader_test.py wrappers
# ---------------------------------------------------------------------------------------------
def feature_gate(doc, feature_files, label, contract=None, argv=None, foreign=None, base_overlay=None):
    """Key off: every baseline request byte-identical here; key on: every request of
    `feature_files` (file names, or a predicate on the request) compiles, passes spirv-val and
    `contract(request, paths)` ('' ok, None not checked, text = failure). `foreign(request)`: a new
    request another feature's gate owns (a tree with several features merged), not unexpected here.
    `base_overlay(base, new)`: logical paths whose working-tree copy replaces the baseline's before the
    comparison (another, separately proven change merged into this tree: its files are not this gate's).
    Exit code 1 on any failure, a missing or unexpected request."""
    ap = argparse.ArgumentParser(description=doc)
    ap.add_argument("--baseline", default=default_baseline())
    ap.add_argument("--jobs", type=int, default=default_jobs())
    ap.add_argument("--dxc")
    ap.add_argument("--sharc", default="auto")
    ap.add_argument("--no-cache", action="store_true")
    args = ap.parse_args(argv)
    lower_priority()
    tool = Toolchain(args.dxc, sharc=args.sharc)
    cache = MemoryCache() if args.no_cache else Cache()
    base = SourceTree.commit(args.baseline).with_sharc(tool.sharc)
    new = SourceTree.worktree(ROOT).with_sharc(tool.sharc)
    for path in (base_overlay(base, new) if base_overlay else []):
        base._data[path] = new.read(path)
        base.ids[path] = new.ids[path]
        print(f"baseline overlay: {path} from the working tree", flush=True)
    try:
        old_list, new_list = base.requests(), new.requests()
        missing = [r for r in old_list if r not in new_list]
        in_feature = feature_files if callable(feature_files) else (lambda r: split_request(r)[0] in feature_files)
        feature = [r for r in new_list if in_feature(r)]
        # A new request with a define no baseline request has belongs to another new feature (several
        # merged in one tree, e.g. RF_HIT_GI and RT_HIT_WIND_RGB): its own gate compiles it.
        known = {d.partition("=")[0] for r in old_list for d in split_request(r)[1]}
        def other_feature(r):
            return (foreign and foreign(r)) or any(d.partition("=")[0] not in known for d in split_request(r)[1])
        unexpected = [r for r in new_list if r not in old_list and r not in feature and not other_feature(r)]
        ident = identity(base, new, [r for r in old_list if r in new_list], tool, cache, args.jobs, label + " key-off")
        on = compile_requests(new, feature, tool=tool, cache=cache, jobs=args.jobs, label=label + " key-on", validate=True)
    finally:
        base.cleanup()
    bad = [x for x in ident if not identical(x[1])]
    fbad, checked = [], 0
    for r in feature:
        res = on[r]
        err = "" if res.ok and res.valid else res.log
        if not err and contract:
            c = contract(r, res.paths())
            checked += c is not None
            err = c or ""
        if err:
            fbad.append((r, err))
    for r, state, err in bad:
        print(f"{state}: {r}\n{err}")
    for r, err in fbad:
        print(f"FEATURE FAIL: {r}\n{err}")
    for r in missing:
        print(f"MISSING request (in the baseline, not here): {r}")
    for r in unexpected:
        print(f"UNEXPECTED new request: {r}")
    n_inputs = sum(s == IDENTICAL_INPUTS for _, s, _ in ident)
    total = len(ident) + len(missing)
    print(f"baseline {args.baseline[:12]}: {len(ident) - len(bad)}/{total} key-off requests byte-identical "
          f"({n_inputs} by identical inputs, {len(ident) - len(bad) - n_inputs} compiled and compared); "
          f"{len(feature) - len(fbad)}/{len(feature)} {label} requests compile and validate"
          + (f", {checked} checked against the descriptor contract" if contract else ""), flush=True)
    return 1 if bad or fbad or missing or unexpected or not feature else 0


# ---------------------------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("requests", nargs="*", help="requests (default: every request of the tree)")
    ap.add_argument("--baseline", help="commit to compare with (byte identity)")
    ap.add_argument("--filter", help="regex over the request strings")
    ap.add_argument("--jobs", type=int, default=default_jobs())
    ap.add_argument("--validate", action="store_true", help="also spirv-val every module")
    ap.add_argument("--dxc")
    ap.add_argument("--no-cache", action="store_true")
    ap.add_argument("--stats", action="store_true", help="print the cache size and exit")
    ap.add_argument("--json", help="write {request: digest or state} here")
    ap.add_argument("--prune", action="store_true", help="prune the cache and exit (compile_shaders.py runs it too)")
    ap.add_argument("--prune-days", type=float, default=PRUNE_DAYS, help="keep entries used in the last N days")
    ap.add_argument("--prune-baselines", type=int, default=PRUNE_BASELINES, help="keep the entries of the last K baselines")
    ap.add_argument("--prune-cap-gb", type=float, default=PRUNE_CAP / 2**30, help="size cap of the kept SPIR-V")
    args = ap.parse_args(argv)
    lower_priority()
    cache = MemoryCache() if args.no_cache else Cache()
    if args.stats:
        print(json.dumps(cache.stats(), indent=2))
        return 0
    if args.prune:
        prune(cache, args.prune_days, int(args.prune_cap_gb * 2**30), tool=Toolchain(args.dxc),
              baselines=args.prune_baselines)
        return 0
    tool = Toolchain(args.dxc)
    new = SourceTree.worktree(ROOT).with_sharc(tool.sharc)
    t0 = time.time()
    if args.baseline:
        base = SourceTree.commit(args.baseline).with_sharc(tool.sharc)
        try:
            reqs = args.requests or [r for r in base.requests() if r in set(new.requests())]
            reqs = [r for r in reqs if not args.filter or re.search(args.filter, r)]
            res = identity(base, new, reqs, tool, cache, args.jobs)
        finally:
            base.cleanup()
        bad = [x for x in res if not identical(x[1])]
        for r, s, log in bad:
            print(f"{s}: {r}" + (f"\n{log}" if log else ""))
        n_in = sum(s == IDENTICAL_INPUTS for _, s, _ in res)
        print(f"{len(res) - len(bad)}/{len(res)} requests byte-identical to {base.rev[:12]} "
              f"({n_in} by identical inputs) in {time.time() - t0:.1f} s", flush=True)
        if args.json:
            Path(args.json).write_text(json.dumps({r: s for r, s, _ in res}, indent=1))
        return 1 if bad else 0
    reqs = args.requests or new.requests()
    reqs = [r for r in reqs if not args.filter or re.search(args.filter, r)]
    res = compile_requests(new, reqs, tool=tool, cache=cache, jobs=args.jobs, validate=args.validate)
    bad = [r for r in res.values() if not r.ok or r.valid is False]
    for r in bad:
        print(f"FAIL {r.request}\n{r.log}")
    print(f"{len(res) - len(bad)}/{len(res)} compiled{' and valid' if args.validate else ''} "
          f"in {time.time() - t0:.1f} s", flush=True)
    if args.json:
        Path(args.json).write_text(json.dumps({k: v.digest for k, v in res.items()}, indent=1))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
