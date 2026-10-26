"""BSHD shader containers and the layer's shader hash.

A Vulkan `.bshd` (Materials.pak, EngineShaders.pak) is a "BSHD" header followed by one
chunk per stage. Each chunk is a 50-byte "SHD8" header and the plain SPIR-V module:

  +0  "SHD8"
  +4  u16, u16       (unknown)
  +8  u16            engine stage id
  +10 u32            SPIR-V size in bytes
  +14 u32            0
  +18 16 bytes       digest of the module
  +34 16 bytes       0
  +50 SPIR-V         (`size` bytes)

The hash the layer gives a shader module (`layer/util.cpp` hash64, `tracker.cpp`) is
FNV-1a 64 over exactly the bytes passed to vkCreateShaderModule, i.e. the module above,
with one quirk: its offset basis is 1469598103934665603, the standard basis
(14695981039346656037) with the last digit missing. Every shader name in the captures
and docs uses it, so this module reproduces it rather than the textbook value.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

SPIRV_MAGIC = 0x07230203
SHD8 = b"SHD8"
CHUNK_HEADER = 50

FNV_OFFSET = 1469598103934665603   # the layer's (non-standard) basis, see above
FNV_PRIME = 1099511628211
MASK64 = 0xFFFFFFFFFFFFFFFF

# SPIR-V ExecutionModel -> short stage name
EXEC_MODELS = {0: "vs", 1: "tcs", 2: "tes", 3: "gs", 4: "fs", 5: "cs"}
STAGE_IDS = {"vs": 0, "tcs": 1, "tes": 2, "gs": 3, "fs": 4, "cs": 5}


def fnv1a64(data: bytes) -> int:
    h = FNV_OFFSET
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & MASK64
    return h


@dataclass(frozen=True)
class Module:
    stage: str          # from the module's OpEntryPoint
    hash: int           # FNV-1a 64, the layer's shader hash
    size: int
    entry: str


def entry_point(spv: bytes) -> tuple[str, str]:
    """(stage, name) of the first OpEntryPoint."""
    if len(spv) < 20 or struct.unpack_from("<I", spv, 0)[0] != SPIRV_MAGIC:
        raise ValueError("not a SPIR-V module")
    i, n = 20, len(spv)
    while i + 4 <= n:
        w = struct.unpack_from("<I", spv, i)[0]
        wc, op = w >> 16, w & 0xFFFF
        if wc == 0:
            break
        if op == 15:  # OpEntryPoint: model, id, name
            model = struct.unpack_from("<I", spv, i + 4)[0]
            raw = spv[i + 12:i + wc * 4]
            return EXEC_MODELS.get(model, f"model{model}"), raw.split(b"\0", 1)[0].decode("ascii", "replace")
        i += wc * 4
    raise ValueError("no OpEntryPoint")


def modules(bshd: bytes) -> list[Module]:
    """The SPIR-V modules of a BSHD file, located by their SHD8 chunk headers."""
    out = []
    i = 0
    while True:
        i = bshd.find(SHD8, i)
        if i < 0:
            break
        if i + CHUNK_HEADER > len(bshd):
            break
        size = struct.unpack_from("<I", bshd, i + 10)[0]
        start = i + CHUNK_HEADER
        spv = bshd[start:start + size]
        if size >= 20 and len(spv) == size and struct.unpack_from("<I", spv, 0)[0] == SPIRV_MAGIC:
            stage, name = entry_point(spv)
            out.append(Module(stage, fnv1a64(spv), size, name))
            i = start + size
        else:
            i += 4
    return out
