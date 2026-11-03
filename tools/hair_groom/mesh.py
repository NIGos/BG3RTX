"""Load one glTF mesh (LSLib export of a BG3 hair GR2) into numpy arrays.

Only the attributes the groom converter needs: positions, UV0, joints/weights, indices.
"""
from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np

_COMPONENTS = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
_WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


@dataclass
class Mesh:
    positions: np.ndarray  # (V, 3) float32, metres
    uv: np.ndarray         # (V, 2) float32, TEXCOORD_0
    joints: np.ndarray     # (V, 4) int, skin joint slots
    weights: np.ndarray    # (V, 4) float32
    tris: np.ndarray       # (T, 3) int64
    joint_names: list
    joint_positions: np.ndarray  # (J, 3) bind-pose joint origins, from the inverse bind matrices


def _accessor(gltf, binary, index):
    acc = gltf["accessors"][index]
    view = gltf["bufferViews"][acc["bufferView"]]
    dtype = np.dtype(_COMPONENTS[acc["componentType"]])
    width = _WIDTH[acc["type"]]
    count = acc["count"]
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    stride = view.get("byteStride") or dtype.itemsize * width
    raw = np.frombuffer(binary, np.uint8, count=stride * (count - 1) + dtype.itemsize * width, offset=start)
    rows = np.lib.stride_tricks.as_strided(raw, (count, dtype.itemsize * width), (stride, 1))
    out = np.ascontiguousarray(rows).view(dtype).reshape(count, width)
    if acc.get("normalized"):
        out = out.astype(np.float32) / np.iinfo(dtype).max
    return out


def load(path, mesh_name):
    path = Path(path)
    gltf = json.loads(path.read_text(encoding="utf-8"))
    binary = (path.parent / gltf["buffers"][0]["uri"]).read_bytes()
    index = next(i for i, m in enumerate(gltf["meshes"]) if m["name"] == mesh_name)
    prim = gltf["meshes"][index]["primitives"][0]
    attr = prim["attributes"]
    skin = next(n["skin"] for n in gltf["nodes"] if n.get("mesh") == index)
    names = [gltf["nodes"][j].get("name", "") for j in gltf["skins"][skin]["joints"]]
    inverse_bind = _accessor(gltf, binary, gltf["skins"][skin]["inverseBindMatrices"]).reshape(-1, 4, 4)
    # glTF matrices are column-major: the stored 4x4 rows are the matrix columns.
    origins = np.linalg.inv(inverse_bind.transpose(0, 2, 1).astype(np.float64))[:, :3, 3]
    return Mesh(positions=_accessor(gltf, binary, attr["POSITION"]).astype(np.float32),
                uv=_accessor(gltf, binary, attr["TEXCOORD_0"]).astype(np.float32),
                joints=_accessor(gltf, binary, attr["JOINTS_0"]).astype(np.int64),
                weights=_accessor(gltf, binary, attr["WEIGHTS_0"]).astype(np.float32),
                tris=_accessor(gltf, binary, prim["indices"]).reshape(-1, 3).astype(np.int64),
                joint_names=names, joint_positions=origins.astype(np.float32))


def _str(raw, at):
    n = int(np.frombuffer(raw, "<u4", 1, at)[0])
    return raw[at + 4:at + 4 + n].decode("utf-8"), at + 4 + n


def load_gr2_dump(path, mesh_name):
    """One mesh of a tools/hair_groom/gr2_dump.ps1 file (LSLib's GR2 reader, native vertex/index order)."""
    raw = Path(path).read_bytes()
    if raw[:4] != b"GR2D" or int(np.frombuffer(raw, "<u4", 1, 4)[0]) != 1:
        raise ValueError(f"{path}: not a GR2 dump v1")
    meshes = int(np.frombuffer(raw, "<u4", 1, 8)[0])
    at, found = 12, None
    for _ in range(meshes):
        name, at = _str(raw, at)
        nv, ni, nb = (int(x) for x in np.frombuffer(raw, "<u4", 3, at))
        at += 12
        bind = []
        for _ in range(nb):
            b, at = _str(raw, at)
            bind.append(b)
        vtx = np.frombuffer(raw, np.dtype([("p", "<f4", 3), ("uv", "<f4", 2), ("j", "u1", 4), ("w", "u1", 4)]), nv, at)
        at += nv * 28
        idx = np.frombuffer(raw, "<u4", ni, at)
        at += ni * 4
        if name == mesh_name:
            found = (vtx, idx, bind)
    if found is None:
        raise KeyError(f"{path}: no mesh {mesh_name}")
    bones = {}
    nbones = int(np.frombuffer(raw, "<u4", 1, at)[0])
    at += 4
    for _ in range(nbones):
        name, at = _str(raw, at)
        bones[name] = np.frombuffer(raw, "<f4", 16, at).reshape(4, 4).astype(np.float64)
        at += 64
    vtx, idx, bind = found
    # LSLib's InverseWorldTransform is row-vector form (translation in the last row): world origin = inverse's last row.
    origins = np.array([np.linalg.inv(bones[b])[3, :3] if b in bones else np.zeros(3) for b in bind], np.float32).reshape(-1, 3)
    return Mesh(positions=np.ascontiguousarray(vtx["p"], np.float32), uv=np.ascontiguousarray(vtx["uv"], np.float32),
                joints=vtx["j"].astype(np.int64), weights=vtx["w"].astype(np.float32) / 255.0,
                tris=idx.reshape(-1, 3).astype(np.int64), joint_names=bind, joint_positions=origins)
