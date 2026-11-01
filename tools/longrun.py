"""Shared helpers for offline tools that can run longer than a minute (AGENTS.md "Measuring correctly").

    from tools.longrun import Progress, FileCache, file_sig

Progress: prints `<label>: done/total (pct), rate/s, elapsed, ETA, failures` at most every `every`
seconds (default 30) and once at the end when the work took longer than that, always flushed, to
stderr by default so a tool's stdout (tables, reports) is unchanged.

FileCache: a JSON cache of per-file results keyed by (path, mtime_ns, size) of every input file, so
a rerun parses only what changed. Stored under the main checkout's captures/.cache/ (gitignored,
shared by the worktrees). A missing, corrupt or foreign cache is ignored and rebuilt; it never
changes a result. BG3RTX_NO_CACHE=1 disables reading it (still written).
"""
from __future__ import annotations

import json
import os
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]


def main_checkout() -> Path:
    parts = REPO.parts
    for i in range(len(parts) - 1):
        if parts[i] == ".claude" and parts[i + 1] == "worktrees":
            return Path(*parts[:i])
    return REPO


CACHE_DIR = main_checkout() / "captures" / ".cache"


def _fmt(s: float) -> str:
    s = max(0, int(round(s)))
    return f"{s // 3600}h{s // 60 % 60:02d}m" if s >= 3600 else (f"{s // 60}m{s % 60:02d}s" if s >= 60 else f"{s}s")


class Progress:
    def __init__(self, label: str, total: int | None = None, every: float = 30.0, stream=None, unit: str = ""):
        self.label, self.total, self.every, self.unit = label, total, every, unit
        self.stream = stream if stream is not None else sys.stderr
        self.t0 = self.last = time.monotonic()
        self.done = 0
        self.failed = 0
        self.printed = False

    def line(self, note: str = "") -> str:
        el = time.monotonic() - self.t0
        rate = self.done / el if el > 0 else 0.0
        parts = [f"{self.label}: {self.done}" + (f"/{self.total} ({100 * self.done / self.total:.0f}%)" if self.total else "")
                 + (f" {self.unit}" if self.unit else "")]
        parts.append(f"{rate:.2f}/s, elapsed {_fmt(el)}")
        if self.total and rate > 0 and self.done < self.total:
            parts.append(f"ETA {_fmt((self.total - self.done) / rate)}")
        if self.failed:
            parts.append(f"{self.failed} failed")
        if note:
            parts.append(note)
        return ", ".join(parts)

    def tick(self, n: int = 1, failed: bool = False, note: str = "") -> None:
        self.done += n
        self.failed += int(failed)
        now = time.monotonic()
        if now - self.last >= self.every:
            self.last = now
            self.emit(note)

    def emit(self, note: str = "") -> None:
        print(self.line(note), file=self.stream, flush=True)
        self.printed = True

    def finish(self, note: str = "") -> None:
        """Final line only when an interim line was printed (short runs stay silent)."""
        if self.printed or time.monotonic() - self.t0 >= self.every:
            self.emit(("done" + (f"; {note}" if note else "")))


def file_sig(p) -> list | None:
    """[mtime_ns, size] of a file, None when it does not exist."""
    try:
        st = os.stat(p)
    except OSError:
        return None
    return [st.st_mtime_ns, st.st_size]


class FileCache:
    """{key: {"sig": [...], "value": ...}} in captures/.cache/<name>.json, `version` invalidates all."""

    def __init__(self, name: str, version: int = 1, path: Path | None = None):
        self.path = path or CACHE_DIR / f"{name}.json"
        self.version = version
        self.data: dict = {}
        self.hits = self.misses = 0
        self.dirty = False
        if os.environ.get("BG3RTX_NO_CACHE") != "1":
            try:
                d = json.loads(self.path.read_text(encoding="utf-8"))
                if d.get("version") == version:
                    self.data = d.get("entries", {})
            except (OSError, ValueError, AttributeError):
                self.data = {}

    def get(self, key: str, sig):
        e = self.data.get(key)
        if e is not None and sig is not None and e.get("sig") == sig:
            self.hits += 1
            return e["value"]
        self.misses += 1
        return None

    def put(self, key: str, sig, value) -> None:
        if sig is None:
            return
        self.data[key] = {"sig": sig, "value": value}
        self.dirty = True

    def save(self) -> None:
        if not self.dirty:
            return
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            tmp = self.path.with_suffix(f".{os.getpid()}.tmp")
            tmp.write_text(json.dumps({"version": self.version, "entries": self.data}), encoding="utf-8")
            os.replace(tmp, self.path)
            self.dirty = False
        except OSError:
            pass
