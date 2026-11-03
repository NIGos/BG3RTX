"""Read installed S8 water materials and every foam/flow mip; never launches BG3.

Run at BelowNormal: python -m tools.assets.water_foam_coverage --out build/water-foam.json
The zero-coverage bound is independent of UV, phase, depth and mip selection.
"""
import argparse
import hashlib
import json
import struct
import time
from pathlib import Path

import numpy as np

from . import lsf
from .locate import game_data
from .lspk import PackageSet

TEMPLATE = "Public/Shared/Assets/Materials/Fluids/FLUID_Water_Plane_flowmap_NoCaustics_Waves.lsf"
CONTENT = "Public/SharedDev/Content/Assets/Nature/[PAK]_Water/_merged.lsf"
IDS = ["45e698fc-b82e-aac1-d651-f41066319f95", "8a7a0b51-9f4d-8f0b-00dc-5ca1ebc9e4f0"]


def bc4(blocks):
    a, b = blocks[:, 0].astype(float), blocks[:, 1].astype(float)
    lut = np.zeros((len(blocks), 8))
    lut[:, :2] = np.stack([a, b], axis=1)
    for i in range(2, 8):
        lut[:, i] = np.where(a > b, ((8-i)*a+(i-1)*b)/7,
                            ((6-i)*a+(i-1)*b)/5 if i < 6 else (0 if i == 6 else 255))
    bits = sum(blocks[:, i].astype(np.uint64) << (8*(i-2)) for i in range(2, 8))
    indices = np.stack([(bits >> (3*i)) & 7 for i in range(16)], axis=1)
    return np.take_along_axis(lut, indices.astype(int), axis=1)/255


def mips(raw):
    if raw[:4] != b"DDS ":
        raise ValueError("not DDS")
    h, w = struct.unpack_from("<II", raw, 12)
    count = struct.unpack_from("<I", raw, 28)[0] or 1
    fmt = raw[84:88]
    if fmt not in (b"DXT5", b"BC5U"):
        raise ValueError(f"unsupported DDS format {fmt}")
    off, result = 128, []
    for level in range(count):
        bw, bh = (w+3)//4, (h+3)//4
        blocks = np.frombuffer(raw[off:off+bw*bh*16], dtype=np.uint8).reshape(-1, 16)
        if fmt == b"DXT5":
            # Foam samples .x: only the BC1 colour block's red endpoints matter.
            endpoints = blocks[:, 8:12].copy().view('<u2').reshape(-1, 2)
            red5 = endpoints >> 11
            red = ((red5 << 3) | (red5 >> 2)).astype(float)/255
            lut = np.stack([red[:, 0], red[:, 1], (2*red[:, 0]+red[:, 1])/3,
                            (red[:, 0]+2*red[:, 1])/3], axis=1)
            bits = blocks[:, 12:16].copy().view('<u4').reshape(-1)
            idx = np.stack([(bits >> (2*i)) & 3 for i in range(16)], axis=1)
            channels = [np.take_along_axis(lut, idx.astype(int), axis=1)]
        else:
            channels = [bc4(blocks[:, :8]), bc4(blocks[:, 8:])]
        row = {"level": level, "width": w, "height": h, "channels": []}
        for values in channels:
            image = values.reshape(bh, bw, 4, 4).transpose(0, 2, 1, 3).reshape(bh*4, bw*4)[:h, :w]
            row['channels'].append({"min": float(image.min()), "max": float(image.max()),
                                    "mean": float(image.mean()), "zero_fraction": float(np.mean(image == 0))})
        result.append(row)
        off += bw*bh*16
        w, h = max(1, w//2), max(1, h//2)
    if off != len(raw):
        raise ValueError(f"DDS trailing/missing bytes: {off}/{len(raw)}")
    return {"format": fmt.decode(), "mips": result}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--game', default=game_data())
    ap.add_argument('--out', required=True)
    args = ap.parse_args()
    ps = PackageSet(args.game)
    content = lsf.parse(ps.read(CONTENT))
    template = lsf.parse(ps.read(TEMPLATE))
    defaults = {n.get('ParameterName'): n for n in template.walk() if n.get('ParameterName')}
    textures = {n.get('ID'): n for n in content.walk() if n.get('SourceFile', '').lower().endswith('.dds')}
    # Default foam lives in Shared's texture bank; resolve UUID through all content
    # only when it is absent from the water resource.
    needed = {defaults['Foam_Texture_NMA'].get('ID')}
    paths = sorted(n for n in ps.index if '/Content/' in n and n.endswith('.lsf') and 'Shared/' in n)
    start = last = time.monotonic()
    for i, path in enumerate(paths):
        if needed <= textures.keys():
            break
        now = time.monotonic()
        if now-last >= 30:
            rate = (i+1)/max(now-start, 1e-9)
            print(f"Texture UUID lookup {i+1}/{len(paths)}, {rate:.1f}/s, ETA {(len(paths)-i-1)/rate:.0f}s, failures 0", flush=True)
            last = now
        res = lsf.parse(ps.read(path))
        for n in res.walk():
            if n.get('ID') in needed and n.get('SourceFile', '').lower().endswith('.dds'):
                textures[n.get('ID')] = n
    report = {"template": TEMPLATE, "template_package": ps.owner(TEMPLATE),
              "template_sha256": hashlib.sha256(ps.read(TEMPLATE)).hexdigest(),
              "content": CONTENT, "content_package": ps.owner(CONTENT), "materials": [], "textures": {}}
    for material_id in IDS:
        node = next(n for n in content.walk() if n.get('ID') == material_id)
        params = dict(defaults)
        params.update({n.get('ParameterName'): n for n in node.children if n.get('Enabled')})
        get = lambda k: params[k].get('Value', params[k].get('ID'))
        e, m = get('Foam_exponent'), get('Foam_Multiplier')
        report['materials'].append({"id": material_id, "name": node.get('Name'),
            "foam_parameters": {k: get(k) for k in params if 'foam' in k.lower() or k == 'Flowmap'},
            "coverage_lower_bound": m*(float(np.float32(1e-4))**e), "exact_zero_fraction": 0 if m > 0 and e > 0 else None})
        for param in ['Foam_Texture_NMA', 'Flowmap']:
            texture_id = get(param)
            if texture_id in report['textures']:
                continue
            tex = textures[texture_id]
            source = tex.get('SourceFile').replace('\\', '/')
            raw = ps.read(source)
            report['textures'][texture_id] = {"source": source, "package": ps.owner(source),
                "sha256": hashlib.sha256(raw).hexdigest(), **mips(raw)}
    Path(args.out).write_text(json.dumps(report, indent=2)+'\n')
    print(f"Read {len(report['materials'])} materials, {len(report['textures'])} textures; zero coverage 0% for both authored materials; wrote {args.out}")


if __name__ == '__main__':
    main()
