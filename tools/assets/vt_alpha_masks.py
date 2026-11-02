"""Offline VT alpha masks for the RT alpha test (docs/P4_OMM.md 11.2): no game, no GPU.

    py -3 -m tools.assets.vt_alpha_masks --pack <replay pack> --fs 738494714d50b4c7 --work <scratch dir>
        [--out <dir under data/assetdb>] [--lslib <LSLib Tools dir>]

1. Reads the pack's alpha records (s1b0.bin, layer/host.h AlphaRec) of the given fragment shaders:
   VT records (kind 1) name their tile set (SSBO entry m0 row 3 column 3) and their texture rectangle in it
   (p0: tile-set uv = frac(uv) * p0.zw + p0.xy).
2. Extracts those tile sets' GTS from VirtualTextures*.pak (tools/assets/lspk.py), resolves each rectangle to
   its unique GTS texture and the GTP page files it needs (tools/assets/vt_alpha.ps1, LSLib), extracts those GTP.
3. Decodes every level of each texture (layer 0, the albedo the RT test reads .w from) to DDS via LSLib and keeps
   the alpha half of every BC3 block: a BC3 alpha block IS a BC4 block, so the mask is bit-exact (no re-encode).
4. Writes one file per texture to --out (only NEW files; an existing file is never overwritten: data/assetdb is a
   shared junction) and a manifest json there, and prints sizes. Work files stay in --work.

Mask file "<gts>_<texture>.vtalpha": header "BG3VTA1\\0", uint32 width, height, levels, then per level the BC4
blocks (ceil(w/4) x ceil(h/4) x 8 bytes) of that level, row-major. vt_alpha_index.txt: the layer's lookup.
"""
import argparse
import json
import os
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.assets.lspk import PackageSet  # noqa: E402

GAME = r"D:\SteamLibrary\steamapps\common\Baldurs Gate 3\Data"
VT_DIR = "Generated/Public/VirtualTextures/"
LSLIB = r"C:\Users\quali\.codex\worktrees\m2-foundry-mesh\BG3RTX\captures\m2-foundry\deps\lslib-v1.20.4\Packed\Tools"
REC_BYTES = 208
MAGIC = b"BG3VTA1\0"


def vt_records(pack, shaders):
    """{(tileset, p0 tuple): triangles} of the pack's VT alpha records of `shaders` (fs hashes)."""
    b = (Path(pack) / "s1b0.bin").read_bytes()
    n = struct.unpack_from("<I", b, 0)[0]
    out = {}
    for i in range(n):
        o = 16 + REC_BYTES * i
        first, tris, kind = struct.unpack_from("<3I", b, o)
        lo, hi = struct.unpack_from("<2I", b, o + 40)
        if kind != 1 or ((hi << 32) | lo) not in shaders:
            continue
        p0 = struct.unpack_from("<4f", b, o + 48)
        t = struct.unpack_from("<32f", b, o + 80)
        key = (int(t[15]), p0)
        out[key] = out.get(key, 0) + tris
    return out


def bridge(lslib, work, request):
    req = Path(work) / "request.json"
    res = Path(work) / "result.json"
    req.write_text(json.dumps(request))
    subprocess.run(["pwsh", "-NoProfile", "-File", str(ROOT / "tools/assets/vt_alpha.ps1"), "-Tools", lslib,
                    "-Request", str(req), "-Output", str(res)], check=True)
    return json.loads(res.read_text(encoding="utf-8-sig"))


def dds_bc3_alpha(path):
    """(w, h, BC4 blocks) of a BC3 DDS: the first 8 bytes of every 16-byte block."""
    d = Path(path).read_bytes()
    if d[:4] != b"DDS ":
        raise ValueError(f"{path}: not a DDS")
    h, w = struct.unpack_from("<2I", d, 12)
    fourcc = d[84:88]
    off = 128
    if fourcc == b"DX10":
        dxgi = struct.unpack_from("<I", d, 128)[0]
        if dxgi not in (77, 78):  # BC3_UNORM / BC3_UNORM_SRGB
            raise ValueError(f"{path}: DXGI format {dxgi}, not BC3")
        off = 148
    elif fourcc != b"DXT5":
        raise ValueError(f"{path}: fourcc {fourcc!r}, not BC3")
    nb = ((w + 3) // 4) * ((h + 3) // 4)
    blocks = d[off:off + 16 * nb]
    if len(blocks) != 16 * nb:
        raise ValueError(f"{path}: truncated")
    return w, h, b"".join(blocks[16 * i:16 * i + 8] for i in range(nb))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--pack", required=True)
    ap.add_argument("--fs", nargs="+", required=True, help="fragment shader hashes (hex)")
    ap.add_argument("--work", required=True)
    ap.add_argument("--out", default=None)
    ap.add_argument("--lslib", default=LSLIB)
    a = ap.parse_args()
    work = Path(a.work)
    work.mkdir(parents=True, exist_ok=True)
    recs = vt_records(a.pack, {int(h, 16) for h in a.fs})
    print(f"[vt alpha] {len(recs)} distinct (tile set, texture) records, {sum(recs.values())} triangles", flush=True)
    ps = PackageSet(GAME, only=["VirtualTextures.pak"] + [f"VirtualTextures_{i}.pak" for i in range(1, 64)])
    masks, total = [], 0
    for ts in sorted({k[0] for k in recs}):
        gts_name = f"Albedo_Normal_Physical_{ts:x}.gts"
        gts = work / gts_name
        if not gts.exists():
            gts.write_bytes(ps.read(VT_DIR + gts_name))
        mine = [(p0, n) for (t, p0), n in recs.items() if t == ts]
        res = bridge(a.lslib, work, {"mode": "resolve", "gts": str(gts), "rects": [{"p0": list(p0)} for p0, _ in mine]})
        tex = []
        for (p0, tris), r in zip(mine, res):
            if r.get("matches") != 1:
                print(f"[vt alpha] tile set {ts:x}: p0 {p0} -> {r.get('matches')} textures (rect {r['rect']}); skipped", flush=True)
                continue
            for page in r["pages"]:
                dst = work / page
                if not dst.exists():
                    dst.write_bytes(ps.read(VT_DIR + page))
            tex.append({"name": r["name"], "out": str(work / f"{ts:x}_{r['name']}"), "levels": r["levels"],
                        "tris": tris, "p0": list(p0), "pages": len(r["pages"])})
        if not tex:
            continue
        ext = bridge(a.lslib, work, {"mode": "extract", "gts": str(gts), "textures": tex})
        for t, e in zip(tex, ext):
            levels, w0, h0 = [], 0, 0
            for i, f in enumerate(e["files"]):
                w, h, bc4 = dds_bc3_alpha(f)
                if not w or not h:
                    break   # LSLib extracts no level smaller than a tile; the RT test reads mip <= record p1.x
                if i == 0:
                    w0, h0 = w, h
                levels.append(bc4)
            body = MAGIC + struct.pack("<3I", w0, h0, len(levels)) + b"".join(levels)
            name = f"{ts:x}_{t['name']}.vtalpha"
            masks.append({"tileset": ts, "texture": t["name"], "p0": t["p0"], "triangles": t["tris"], "width": w0,
                          "height": h0, "levels": len(levels), "bytes": len(body), "file": name, "gtp_files": t["pages"]})
            total += len(body)
            (work / name).write_bytes(body)
            print(f"[vt alpha] {name}: {w0}x{h0}, {len(levels)} levels, {len(body)} bytes, {t['tris']} triangles", flush=True)
    print(f"[vt alpha] {len(masks)} masks, {total} bytes ({total / 1048576:.2f} MB)", flush=True)
    manifest = {"source_pack": str(a.pack), "fs": a.fs, "format": "BG3VTA1: BC4 blocks per level (BC3 alpha halves)",
                "masks": masks, "bytes": total}
    (work / "vt_alpha_manifest.json").write_text(json.dumps(manifest, indent=1))
    # The layer's index (layer/alphatest.cpp vt_masks_load): "<tileset> <p0 as 4 float32 bit patterns> <file>"; the
    # p0 bits are the records' own, so a match is exact.
    lines = ["# tools/assets/vt_alpha_masks.py: tile set, record uv transform p0 (float32 bits, hex), mask file"]
    for m in masks:
        bits = " ".join(f"{struct.unpack('<I', struct.pack('<f', x))[0]:08x}" for x in m["p0"])
        lines.append(f"{m['tileset']} {bits} {m['file']}")
    (work / "vt_alpha_index.txt").write_text("\n".join(lines) + "\n")
    if a.out:
        out = Path(a.out)
        out.mkdir(parents=True, exist_ok=True)
        for m in masks + [{"file": "vt_alpha_manifest.json"}, {"file": "vt_alpha_index.txt"}]:
            dst = out / m["file"]
            if dst.exists():
                print(f"[vt alpha] {dst} exists: kept, not overwritten", flush=True)
                continue
            dst.write_bytes((work / m["file"]).read_bytes())
        print(f"[vt alpha] added to {out}", flush=True)


if __name__ == "__main__":
    main()
