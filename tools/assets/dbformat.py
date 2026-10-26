"""The asset database container (`bg3rtx_assetdb.bin`), version 1.

Little endian, fixed-size records, every section 16-byte aligned, so the layer reads it
with plain pointer casts after a size check (`layer/assetdb.cpp` mirrors these layouts;
`tests/test_format.py` pins the record sizes).

  header (64 B)  "BG3RTXDB", u32 version, u32 header size, u32 section count, u32 flags,
                 u64 TOC offset, u64 file size, u64 build time (unix), 16 B source digest
  TOC (32 B per section)  char tag[4], u32 section version, u64 offset, u64 size,
                          u32 record count, u32 record size
String references are u32 byte offsets into STRS (0 is the empty string). Indices that
point nowhere are 0xFFFFFFFF.
"""
from __future__ import annotations

import struct
import time

MAGIC = b"BG3RTXDB"
VERSION = 1
NONE = 0xFFFFFFFF

HEADER = struct.Struct("<8sIIIIQQQ16s")
TOC = struct.Struct("<4sIQQII")

# tag -> (record struct, section version)
RECORDS = {
    # name, lighting preset, atmosphere preset, flags, first/count instance, light, trigger, probe,
    # bounds min/max, lighting id, atmosphere id
    "LVLS": struct.Struct("<IIII8I6fII"),
    # name, parent, pos[3], rot[4], scale, flags, depth
    "INST": struct.Struct("<Ii3f4ffII"),
    # level name, world, instance
    "ALIA": struct.Struct("<III"),
    # pos[3], radius, rot[4], color[3], intensity, kelvin, gain, scattering, spot inner, spot outer,
    # box[3], att start/end/side, flicker speed/amount, move speed/amount, flags,
    # type, att function, channel, pad, instance, name, cookie
    "LGHT": struct.Struct("<3ff4f3ff5f3f3f2f2fI4BIII"),
    # pos[3], scale, rot[4], extents[3], height, radius, fade time, flags, floor,
    # kind, shape, render method, pad, instance, name, building, first/count point, first/count ref
    "TRIG": struct.Struct("<3ff4f3ffffIi4BIIIIIII"),
    "TPTS": struct.Struct("<2f"),
    "TREF": struct.Struct("<I"),
    # pos[3], radius, rot[4], size[3], transition, intensity, flags, first/count capture
    "PRBE": struct.Struct("<3ff4f3fffIII"),
    # preset, path prefix, map key, flags, sh[16]
    "PCAP": struct.Struct("<IIII16f"),
    # id, name, kind, flags, sun color[3], sun intensity, yaw, pitch, size, kelvin, sky color[3], sky intensity
    "PRES": struct.Struct("<IIII3ff4f3ff"),
    # name, flags, class, shading, blend, material type, usage, raster, depth, uv count, diffusion profile, pad[3]
    "MTPL": struct.Struct("<II8BI3I"),
    # template, variant, ecb set, ecb binding, mcb set, mcb binding, pad, param count, ecb size, mcb size,
    # first param, pad
    "MVAR": struct.Struct("<I4B2BHHHII"),
    # role, kind, set, binding, offset, layer, flags, name, default[4], pad
    "MPRM": struct.Struct("<4BH2BI4fI"),
    # hash, first ref, ref count, stage, pass, canonical variant, pad
    "SHDR": struct.Struct("<QIHBBII"),
    "SREF": struct.Struct("<I"),
    # hash, name, stage, pad[3]
    "ESHD": struct.Struct("<QIB3x"),
    # effect, template, start, end, intensity max, intensity mean, radius max, color[3], flicker amount,
    # flicker speed, offset[3], flags
    "EFXL": struct.Struct("<II5f3f2f3fI"),
}
EXPECTED_SIZES = {"LVLS": 80, "INST": 48, "ALIA": 12, "LGHT": 128, "TRIG": 96, "TPTS": 8, "TREF": 4, "PRBE": 64,
                  "PCAP": 80, "PRES": 64, "MTPL": 32, "MVAR": 24, "MPRM": 32, "SHDR": 24, "SREF": 4, "ESHD": 16,
                  "EFXL": 64}


class StringPool:
    def __init__(self):
        self.data = bytearray(b"\0")
        self.index = {"": 0}

    def add(self, s: str) -> int:
        s = s or ""
        off = self.index.get(s)
        if off is None:
            off = len(self.data)
            self.data += s.encode("utf-8") + b"\0"
            self.index[s] = off
        return off


class Writer:
    def __init__(self):
        self.sections: list[tuple[str, int, bytes, int, int]] = []
        self.strings = StringPool()

    def add_records(self, tag: str, rows: list[tuple]) -> None:
        rec = RECORDS[tag]
        buf = bytearray(rec.size * len(rows))
        for i, r in enumerate(rows):
            rec.pack_into(buf, i * rec.size, *r)
        self.sections.append((tag, VERSION, bytes(buf), len(rows), rec.size))

    def write(self, path: str, digest: bytes = b"\0" * 16, build_time: int | None = None) -> dict:
        sections = list(self.sections) + [("STRS", VERSION, bytes(self.strings.data), len(self.strings.index), 1)]
        out = bytearray(HEADER.size)
        placed = []
        for tag, ver, data, count, rsize in sections:
            out += b"\0" * (-len(out) % 16)
            placed.append((tag, ver, len(out), len(data), count, rsize))
            out += data
        out += b"\0" * (-len(out) % 16)
        toc_off = len(out)
        for tag, ver, off, size, count, rsize in placed:
            out += TOC.pack(tag.encode("ascii"), ver, off, size, count, rsize)
        HEADER.pack_into(out, 0, MAGIC, VERSION, HEADER.size, len(placed), 0, toc_off, len(out),
                         int(build_time if build_time is not None else time.time()), digest[:16].ljust(16, b"\0"))
        with open(path, "wb") as f:
            f.write(out)
        return {tag: {"bytes": size, "count": count} for tag, _v, _o, size, count, _r in placed}


class Reader:
    """Reads the container back (tests, `info`, and the offline join tools)."""

    def __init__(self, data: bytes):
        magic, ver, hsize, nsec, _flags, toc, fsize, self.build_time, self.digest = HEADER.unpack_from(data, 0)
        if magic != MAGIC or ver != VERSION or fsize != len(data):
            raise ValueError("not a version 1 asset database (or truncated)")
        self.data = data
        self.toc = {}
        for i in range(nsec):
            tag, sv, off, size, count, rsize = TOC.unpack_from(data, toc + i * TOC.size)
            self.toc[tag.decode()] = (off, size, count, rsize)

    def rows(self, tag: str) -> list[tuple]:
        if tag not in self.toc:
            return []
        off, size, count, rsize = self.toc[tag]
        rec = RECORDS[tag]
        return [rec.unpack_from(self.data, off + i * rsize) for i in range(count)]

    def string(self, ref: int) -> str:
        off = self.toc["STRS"][0] + ref
        end = self.data.index(b"\0", off)
        return self.data[off:end].decode("utf-8")
