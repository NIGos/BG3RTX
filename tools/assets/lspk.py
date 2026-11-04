"""Read-only LSPK v18 package reader (BG3 `.pak` and save `.lsv`).

Layout (LSLib `PackageFormat.cs`, verified on the Patch 8 install):
  header (40 B): "LSPK", u32 version, u64 file list offset, u32 file list size,
                 u8 flags, u8 priority, 16 B MD5, u16 number of parts
  file list: u32 count, u32 compressed size, LZ4 block of `count` 272-byte entries
  entry (272 B): 256 B NUL-padded name, u32 offset low, u16 offset high, u8 part,
                 u8 flags (low nibble = compression), u32 size on disk, u32 size

Multi-part packages keep part N in `<name>_N.pak` next to part 0.

Files are only ever opened for reading; nothing here writes to the game directory.
"""
from __future__ import annotations

import fnmatch
import hashlib
import time
import os
import struct
from dataclasses import dataclass
from typing import Iterable, Iterator

from .compress import decompress, lz4_block_decompress

ENTRY_SIZE_V18 = 272
HEADER_SIZE = 40


class PackageError(ValueError):
    pass


@dataclass(frozen=True)
class Entry:
    name: str
    offset: int
    part: int
    flags: int
    disk_size: int
    size: int

    @property
    def method(self) -> int:
        return self.flags & 0x0F


class Package:
    """One LSPK file. Entries are listed lazily on first access."""

    def __init__(self, path: str):
        self.path = path
        with open(path, "rb") as f:
            hdr = f.read(HEADER_SIZE)
        if len(hdr) < HEADER_SIZE or hdr[:4] != b"LSPK":
            raise PackageError(f"{path}: not an LSPK package")
        (self.version,) = struct.unpack_from("<I", hdr, 4)
        if self.version != 18:
            raise PackageError(f"{path}: LSPK v{self.version} is not supported (v18 only)")
        self._list_offset, self._list_size = struct.unpack_from("<QI", hdr, 8)
        self.flags, self.priority = hdr[20], hdr[21]
        self.md5 = hdr[22:38]
        (self.num_parts,) = struct.unpack_from("<H", hdr, 38)
        self._entries: dict[str, Entry] | None = None

    @property
    def entries(self) -> dict[str, Entry]:
        if self._entries is None:
            self._entries = self._read_list()
        return self._entries

    def _read_list(self) -> dict[str, Entry]:
        with open(self.path, "rb") as f:
            f.seek(self._list_offset)
            n, csize = struct.unpack("<II", f.read(8))
            raw = lz4_block_decompress(f.read(csize), n * ENTRY_SIZE_V18)
        out: dict[str, Entry] = {}
        for i in range(n):
            e = raw[i * ENTRY_SIZE_V18:(i + 1) * ENTRY_SIZE_V18]
            name = e[:256].split(b"\0", 1)[0].decode("utf-8", "replace").replace("\\", "/")
            lo, hi, part, fl, disk, usize = struct.unpack_from("<IHBBII", e, 256)
            out[name] = Entry(name, lo | (hi << 32), part, fl, disk, usize)
        return out

    def part_path(self, part: int) -> str:
        if part == 0:
            return self.path
        base, ext = os.path.splitext(self.path)
        return f"{base}_{part}{ext}"

    def __contains__(self, name: str) -> bool:
        return name in self.entries

    def names(self, pattern: str | None = None) -> list[str]:
        if pattern is None:
            return list(self.entries)
        return [n for n in self.entries if fnmatch.fnmatchcase(n, pattern)]

    def read(self, name_or_entry: str | Entry) -> bytes:
        e = self.entries[name_or_entry] if isinstance(name_or_entry, str) else name_or_entry
        with open(self.part_path(e.part), "rb") as f:
            f.seek(e.offset)
            data = f.read(e.disk_size)
        if len(data) != e.disk_size:
            raise PackageError(f"{self.path}:{e.name}: truncated read")
        if e.method == 0 or e.size == 0:
            return data
        return decompress(e.method, data, e.size)


class PackageSet:
    """The game's packages as one virtual file system.

    When several packages hold the same path, the one loaded last by the engine
    wins: higher header priority first, then the patch/hotfix packages, which sort
    after the base ones by name (`Patch8_HotFix10.pak` overrides `Gustav.pak`).
    Part files (`Textures_1.pak`, ...) belong to their part-0 package and are not
    opened on their own.
    """

    def __init__(self, data_dir: str, only: Iterable[str] | None = None):
        self.data_dir = data_dir
        self.packages: list[Package] = []
        names = sorted(os.listdir(data_dir))
        wanted = None if only is None else {n.lower() for n in only}
        for fn in names:
            if not fn.lower().endswith(".pak"):
                continue
            if wanted is not None and fn.lower() not in wanted:
                continue
            stem = fn[:-4]
            head, _, tail = stem.rpartition("_")
            if tail.isdigit() and head and os.path.exists(os.path.join(data_dir, head + ".pak")):
                continue  # a numbered part of another package
            try:
                self.packages.append(Package(os.path.join(data_dir, fn)))
            except PackageError as exc:
                raise PackageError(f"rejected package {fn}: {exc}") from exc
        if wanted is not None:
            missing = wanted - {os.path.basename(p.path).lower() for p in self.packages}
            if missing:
                raise PackageError(f"required packages missing: {', '.join(sorted(missing))}")
        self.packages.sort(key=lambda p: (p.priority, _patch_rank(os.path.basename(p.path)), os.path.basename(p.path).lower()))
        self._index: dict[str, tuple[Package, Entry]] | None = None

    @property
    def index(self) -> dict[str, tuple[Package, Entry]]:
        if self._index is None:
            idx: dict[str, tuple[Package, Entry]] = {}
            for p in self.packages:  # later packages override earlier ones
                for name, e in p.entries.items():
                    idx[name] = (p, e)
            self._index = idx
        return self._index

    def __contains__(self, name: str) -> bool:
        return name in self.index

    def names(self, pattern: str | None = None) -> list[str]:
        if pattern is None:
            return list(self.index)
        return [n for n in self.index if fnmatch.fnmatchcase(n, pattern)]

    def iter_suffix(self, suffix: str) -> Iterator[str]:
        for n in self.index:
            if n.endswith(suffix):
                yield n

    def read(self, name: str) -> bytes:
        p, e = self.index[name]
        return p.read(e)

    def owner(self, name: str) -> str:
        return os.path.basename(self.index[name][0].path)

    def fingerprint(self, log=print) -> list[dict]:
        """Manifest metadata plus a content hash of every package part."""
        out = []
        total = sum(os.path.getsize(p.part_path(i)) for p in self.packages for i in range(max(1, p.num_parts)))
        done, started = 0, time.monotonic()
        last = started
        for p in self.packages:
            sha = hashlib.sha256()
            for part in range(max(1, p.num_parts)):
                with open(p.part_path(part), "rb") as source:
                    while chunk := source.read(1024 * 1024):
                        sha.update(chunk)
                        done += len(chunk)
                        now = time.monotonic()
                        if now - last >= 30:
                            rate = done / max(now - started, 1e-9)
                            log(f"package hashing: {done}/{total} bytes, {rate:.0f} B/s, ETA {(total-done)/max(rate,1):.0f}s, failures 0")
                            __import__("sys").stdout.flush()
                            last = now
            st = os.stat(p.path)
            out.append({"name": os.path.basename(p.path), "size": st.st_size, "mtime": int(st.st_mtime),
                        "priority": p.priority, "md5": p.md5.hex(), "sha256": sha.hexdigest()})
        return out


def _patch_rank(fn: str) -> tuple:
    """Patch and hotfix packages load after the base game; HotFix10 after HotFix9."""
    low = fn.lower()
    if not low.startswith("patch"):
        return (0, ())
    digits = [int(t) for t in "".join(c if c.isdigit() else " " for c in low).split()]
    return (1, tuple(digits))
