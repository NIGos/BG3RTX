"""Builders for small, known test files: LZ4 blocks, LSPK packages, LSF resources, BSHD shaders.

They write the formats from their specification (the same layouts the readers document)
rather than reusing the readers, so a reader bug is not mirrored by its test.
"""
from __future__ import annotations

import struct
import uuid as _uuid


def lz4_literal_block(data: bytes) -> bytes:
    """An LZ4 block that stores `data` as one literal run (valid LZ4)."""
    n = len(data)
    if n < 15:
        return bytes([n << 4]) + data
    out = bytearray([0xF0])
    rest = n - 15
    while rest >= 255:
        out.append(255)
        rest -= 255
    out.append(rest)
    return bytes(out) + data


def lz4_frame(block: bytes) -> bytes:
    """An LZ4 frame (independent blocks, no checksums) around one compressed block."""
    flg, bd = 0x60, 0x40
    return struct.pack("<IBBB", 0x184D2204, flg, bd, 0) + struct.pack("<I", len(block)) + block + b"\0\0\0\0"


def lspk(files: dict[str, bytes], priority: int = 0) -> bytes:
    """An LSPK v18 package with uncompressed entries."""
    body = bytearray(b"\0" * 40)
    entries = bytearray()
    for name, data in files.items():
        off = len(body)
        body += data
        e = name.encode().ljust(256, b"\0")
        e += struct.pack("<IHBBII", off & 0xFFFFFFFF, off >> 32, 0, 0, len(data), len(data))
        entries += e
    list_off = len(body)
    comp = lz4_literal_block(bytes(entries))
    body += struct.pack("<II", len(files), len(comp)) + comp
    struct.pack_into("<4sIQIBB16sH", body, 0, b"LSPK", 18, list_off, 8 + len(comp), 0, priority, b"\0" * 16, 1)
    return bytes(body)


# ---- LSF v6, format 1 (keys and adjacency), sections stored uncompressed ----
T_INT, T_FLOAT, T_FVEC3, T_FVEC4, T_BOOL, T_FIXED, T_GUID, T_UINT8 = 4, 6, 12, 13, 19, 22, 31, 1


def guid_bytes(s: str) -> bytes:
    b = _uuid.UUID(s).bytes_le
    tail = b[8:]
    return b[:8] + bytes((tail[1], tail[0], tail[3], tail[2], tail[5], tail[4], tail[7], tail[6]))


def _encode(t, v) -> bytes:
    if t == T_INT:
        return struct.pack("<i", v)
    if t == T_UINT8:
        return struct.pack("<B", v)
    if t == T_FLOAT:
        return struct.pack("<f", v)
    if t == T_FVEC3:
        return struct.pack("<3f", *v)
    if t == T_FVEC4:
        return struct.pack("<4f", *v)
    if t == T_BOOL:
        return struct.pack("<?", v)
    if t == T_FIXED:
        return v.encode() + b"\0"
    if t == T_GUID:
        return guid_bytes(v)
    raise ValueError(t)


def lsf(tree) -> bytes:
    """`tree` = (name, [(attr, type, value)...], [children...]); the root is a region."""
    names: list[str] = []

    def nh(s):
        if s not in names:
            names.append(s)
        return names.index(s)  # one bucket: handle = 0 << 16 | index

    nodes, attrs, vals = [], [], bytearray()

    def add(node, parent):
        name, at, ch = node
        idx = len(nodes)
        nodes.append([nh(name), parent, -1, -1])
        prev = -1
        for an, t, v in at:
            d = _encode(t, v)
            ai = len(attrs)
            attrs.append([nh(an), t | (len(d) << 6), -1, len(vals)])
            vals.extend(d)
            if prev < 0:
                nodes[idx][3] = ai
            else:
                attrs[prev][2] = ai
            prev = ai
        for c in ch:
            add(c, idx)

    add(tree, -1)
    sraw = bytearray(struct.pack("<I", 1) + struct.pack("<H", len(names)))
    for s in names:
        e = s.encode()
        sraw += struct.pack("<H", len(e)) + e
    nraw = b"".join(struct.pack("<Iiii", *n) for n in nodes)
    araw = b"".join(struct.pack("<IIiI", *a) for a in attrs)
    head = b"LSOF" + struct.pack("<I", 6) + struct.pack("<Q", 0)
    md = struct.pack("<10I", len(sraw), 0, 0, 0, len(nraw), 0, len(araw), 0, len(vals), 0)
    md += struct.pack("<BBHI", 0, 0, 0, 1)
    return head + md + bytes(sraw) + nraw + araw + bytes(vals)


# ---- SPIR-V / BSHD ----
def spirv_module(model: int, name: str = "main") -> bytes:
    """A minimal SPIR-V header + OpEntryPoint (enough for stage detection and hashing)."""
    nm = name.encode() + b"\0"
    nm += b"\0" * (-len(nm) % 4)
    words = 3 + len(nm) // 4
    ep = struct.pack("<III", (words << 16) | 15, model, 1) + nm
    return struct.pack("<5I", 0x07230203, 0x00010300, 0, 10, 0) + ep


def bshd(*mods: bytes) -> bytes:
    out = bytearray(b"BSHD" + struct.pack("<III", 2, 32, 4) + b"test".ljust(32, b"\0"))
    for i, m in enumerate(mods):
        out += b"SHD8" + struct.pack("<HHHI I", 0, 0, i * 4, len(m), 0) + b"\x11" * 16 + b"\0" * 16 + m
    return bytes(out)
