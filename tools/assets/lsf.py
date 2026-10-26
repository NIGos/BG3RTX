"""LSF (binary Larian resource) reader, versions 2-7.

Layout (LSLib `Resources/LSF/LSFReader.cs`):
  "LSOF", u32 version, engine version (u32 before v5, u64 from v5)
  metadata: (uncompressed, on-disk) size pairs for strings, [keys (v6+)], nodes,
            attributes, values; u8 compression flags, u8, u16, u32 format
  sections in order: strings, nodes, attributes, values, [keys]
  An on-disk size of 0 means the section is stored uncompressed. From v2 the
  node/attribute/value sections are LZ4 *frames*; the string table is a block.

  strings: u32 buckets, per bucket u16 count, per string u16 length + UTF-8
  name handle = bucket << 16 | index
  format 1 (keys and adjacency): node 16 B (name, parent, next sibling, first
            attribute), attribute 16 B (name, type | length << 6, next, value offset)
  format 0: node 12 B (name, first attribute, parent), attribute 12 B
            (name, type | length << 6, owning node); values are consecutive.
"""
from __future__ import annotations

import struct
from typing import Any

from .compress import decompress
from .resource import Node, Resource, guid_from_lsf

# LSLib AttributeType ids -> struct format of fixed-size values.
_FIXED = {
    1: "B", 2: "h", 3: "H", 4: "i", 5: "I", 6: "f", 7: "d",
    8: "2i", 9: "3i", 10: "4i", 11: "2f", 12: "3f", 13: "4f",
    14: "4f", 15: "9f", 16: "12f", 17: "12f", 18: "16f",
    19: "?", 24: "Q", 26: "q", 27: "b", 32: "q",
}
_FIXED_STRUCT = {t: struct.Struct("<" + f) for t, f in _FIXED.items()}
_STRING_TYPES = (20, 21, 22, 23, 29, 30)
T_UUID = 31
T_SCRATCH = 25
T_TRANSLATED = (28, 33)
TYPE_NAMES = {
    0: "None", 1: "uint8", 2: "int16", 3: "uint16", 4: "int32", 5: "uint32", 6: "float", 7: "double",
    8: "ivec2", 9: "ivec3", 10: "ivec4", 11: "fvec2", 12: "fvec3", 13: "fvec4", 14: "mat2x2",
    15: "mat3x3", 16: "mat3x4", 17: "mat4x3", 18: "mat4x4", 19: "bool", 20: "string", 21: "path",
    22: "FixedString", 23: "LSString", 24: "uint64", 25: "ScratchBuffer", 26: "old_int64", 27: "int8",
    28: "TranslatedString", 29: "WString", 30: "LSWString", 31: "guid", 32: "int64",
    33: "TranslatedFSString",
}


class LsfError(ValueError):
    pass


def decode_value(t: int, d: bytes) -> Any:
    s = _FIXED_STRUCT.get(t)
    if s is not None:
        v = s.unpack_from(d, 0)
        return v[0] if len(v) == 1 else list(v)
    if t in _STRING_TYPES:
        return d.split(b"\0", 1)[0].decode("utf-8", "replace")
    if t == T_UUID:
        return guid_from_lsf(d)
    if t in T_TRANSLATED:
        # u16 version, u32 length, handle (TranslatedFSString adds arguments; ignored)
        if len(d) >= 6:
            ln = struct.unpack_from("<I", d, 2)[0]
            return {"handle": d[6:6 + ln].split(b"\0", 1)[0].decode("utf-8", "replace"),
                    "version": struct.unpack_from("<H", d, 0)[0]}
        return {"handle": "", "version": 0}
    return bytes(d)  # ScratchBuffer and unknown types stay raw


def _read_names(raw: bytes) -> list[list[str]]:
    p = 0
    (nb,) = struct.unpack_from("<I", raw, p)
    p += 4
    names = []
    for _ in range(nb):
        (cnt,) = struct.unpack_from("<H", raw, p)
        p += 2
        bucket = []
        for _ in range(cnt):
            (ln,) = struct.unpack_from("<H", raw, p)
            p += 2
            bucket.append(raw[p:p + ln].decode("utf-8", "replace"))
            p += ln
        names.append(bucket)
    return names


def parse(data: bytes) -> Resource:
    if data[:4] != b"LSOF":
        raise LsfError(f"not an LSF file (magic {data[:4]!r})")
    (ver,) = struct.unpack_from("<I", data, 4)
    if not 2 <= ver <= 7:
        raise LsfError(f"LSF v{ver} is not supported")
    p = 8 + (8 if ver >= 5 else 4)
    has_keys = ver >= 6
    fields = ["su", "sd"] + (["ku", "kd"] if has_keys else []) + ["nu", "nd", "au", "ad", "vu", "vd"]
    md = dict(zip(fields, struct.unpack_from("<" + "I" * len(fields), data, p)))
    p += 4 * len(fields)
    cflags, _, _, fmt = struct.unpack_from("<BBHI", data, p)
    p += 8
    method = cflags & 0x0F

    def take(u: str, d: str, frame: bool) -> bytes:
        nonlocal p
        usize, dsize = md[u], md[d]
        size = dsize if dsize else usize
        raw = data[p:p + size]
        p += size
        if dsize == 0 or usize == 0:
            return raw
        return decompress(method, raw, usize, frame=frame)

    names = _read_names(take("su", "sd", False))
    nodes_raw = take("nu", "nd", True)
    attrs_raw = take("au", "ad", True)
    vals = take("vu", "vd", True)

    def nm(h: int) -> str:
        return names[h >> 16][h & 0xFFFF]

    nodes: list[Node] = []
    parents: list[int] = []
    if fmt == 1:
        firsts: list[int] = []
        rec = struct.Struct("<Iiii")
        for i in range(len(nodes_raw) // 16):
            h, par, _sib, first = rec.unpack_from(nodes_raw, i * 16)
            nodes.append(Node(nm(h)))
            parents.append(par)
            firsts.append(first)
        arec = struct.Struct("<IIiI")
        attrs = [arec.unpack_from(attrs_raw, i * 16) for i in range(len(attrs_raw) // 16)]
        for n, a in zip(nodes, firsts):
            while a >= 0:
                h, tl, nxt, off = attrs[a]
                t, ln = tl & 0x3F, tl >> 6
                n.attrs[nm(h)] = decode_value(t, vals[off:off + ln])
                a = nxt
    else:
        rec = struct.Struct("<Iii")
        for i in range(len(nodes_raw) // 12):
            h, _first, par = rec.unpack_from(nodes_raw, i * 12)
            nodes.append(Node(nm(h)))
            parents.append(par)
        arec = struct.Struct("<IIi")
        off = 0
        for i in range(len(attrs_raw) // 12):
            h, tl, ni = arec.unpack_from(attrs_raw, i * 12)
            t, ln = tl & 0x3F, tl >> 6
            nodes[ni].attrs[nm(h)] = decode_value(t, vals[off:off + ln])
            off += ln

    roots = []
    for n, par in zip(nodes, parents):
        if par >= 0:
            n.parent = nodes[par]
            nodes[par].children.append(n)
        else:
            roots.append(n)
    return Resource(roots, ver)
