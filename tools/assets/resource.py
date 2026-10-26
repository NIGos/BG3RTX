"""The tree both LSF (binary) and LSX (XML) resources decode to."""
from __future__ import annotations

import uuid as _uuid
from typing import Any, Iterator


class Node:
    """One resource node: a name, typed attributes and ordered children."""

    __slots__ = ("name", "attrs", "children", "parent")

    def __init__(self, name: str, parent: "Node | None" = None):
        self.name = name
        self.attrs: dict[str, Any] = {}
        self.children: list[Node] = []
        self.parent = parent

    def __repr__(self) -> str:
        return f"Node({self.name!r}, {len(self.attrs)} attrs, {len(self.children)} children)"

    def get(self, key: str, default: Any = None) -> Any:
        return self.attrs.get(key, default)

    def child(self, name: str) -> "Node | None":
        for c in self.children:
            if c.name == name:
                return c
        return None

    def children_named(self, name: str) -> list["Node"]:
        return [c for c in self.children if c.name == name]

    def walk(self) -> Iterator["Node"]:
        stack = [self]
        while stack:
            n = stack.pop()
            yield n
            stack.extend(reversed(n.children))

    def find_all(self, name: str) -> Iterator["Node"]:
        for n in self.walk():
            if n.name == name:
                yield n

    def to_dict(self, depth: int = 64) -> dict:
        d: dict[str, Any] = {"_": self.name}
        for k, v in self.attrs.items():
            d[k] = v.hex() if isinstance(v, (bytes, bytearray)) else v
        if self.children and depth > 0:
            d["children"] = [c.to_dict(depth - 1) for c in self.children]
        return d


class Resource:
    """A decoded file: its top-level regions (root nodes)."""

    __slots__ = ("regions", "version")

    def __init__(self, regions: list[Node], version: int):
        self.regions = regions
        self.version = version

    def region(self, name: str) -> Node | None:
        for r in self.regions:
            if r.name == name:
                return r
        return None

    def walk(self) -> Iterator[Node]:
        for r in self.regions:
            yield from r.walk()

    def find_all(self, name: str) -> Iterator[Node]:
        for r in self.regions:
            yield from r.find_all(name)


def guid_from_lsf(b: bytes) -> str:
    """Formats a 16-byte LSF UUID as text the way LSLib does for BG3.

    BG3 stores a .NET Guid (Data1-3 little endian) with the last eight bytes
    swapped in pairs.
    """
    if len(b) != 16:
        return b.hex()
    tail = bytes((b[9], b[8], b[11], b[10], b[13], b[12], b[15], b[14]))
    return str(_uuid.UUID(bytes_le=bytes(b[:8]) + tail))
