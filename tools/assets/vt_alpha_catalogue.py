"""Game-wide catalogue of the alpha-tested VT materials and the BC4 mask size they would need (docs/P4_OMM.md 11.2e).

    py -3 -m tools.assets.vt_alpha_catalogue --work <scratch dir> [--out <data/assetdb/vt_alpha>] [--lslib <dir>]

From the installed paks (tools/assets/lspk.py PackageSet, every package): every MaterialBank resource of every
Content .lsf whose template (SourceFile, tools/assets/extract/materials.py parse_template) is a virtual-texture
template with an alpha test (F_VIRTUAL_TEXTURE and F_ALPHA_TEST); its enabled VirtualTextureParameters IDs through
the VirtualTextureBank (GTexFileName); each texture's tile set and level-0 size from the 16 GTS metadata tables
(tools/assets/vt_alpha.ps1 list). Mask size per texture: BC4 0.5 B/texel, levels 0..2 (the RT test's mip clamp
P1.x is 2 for most records; option B reads only level 0). Nothing is extracted. Prints totals and the templates
ranked by texture count, and marks the textures that already have a mask (data/assetdb/vt_alpha manifests).
Writes vt_alpha_catalogue.json (a NEW file; never overwrites).
"""
import argparse
import json
import sys
import time
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.assets import lsf  # noqa: E402
from tools.assets.extract.materials import F_ALPHA_TEST, F_VIRTUAL_TEXTURE, parse_template  # noqa: E402
from tools.assets.lspk import PackageSet  # noqa: E402
from tools.assets.vt_alpha_masks import GAME, LSLIB, VT_DIR, bridge  # noqa: E402


def mask_bytes(w, h, levels=3):
    n = 0
    for l in range(levels):
        lw, lh = max(w >> l, 1), max(h >> l, 1)
        n += ((lw + 3) // 4) * ((lh + 3) // 4) * 8
    return n


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--work", required=True)
    ap.add_argument("--out", default=None)
    ap.add_argument("--lslib", default=LSLIB)
    a = ap.parse_args()
    work = Path(a.work)
    work.mkdir(parents=True, exist_ok=True)
    ps = PackageSet(GAME)
    vt_tex, materials, n_files = {}, [], 0
    content = [n for n in ps.index if "/Content/" in n and n.endswith(".lsf")]
    t0 = last = time.time()
    for k, name in enumerate(content):
        if time.time() - last >= 30:
            last = time.time()
            rate = k / (last - t0)
            print(f"[catalogue] {k}/{len(content)} content files, {rate:.0f}/s, ETA {(len(content) - k) / max(rate, 1e-9):.0f} s, "
                  f"{len(materials)} materials so far", flush=True)
        try:
            r = lsf.parse(ps.read(name))
        except Exception:
            continue
        n_files += 1
        for reg in r.regions:
            if reg.name == "VirtualTextureBank":
                for res in reg.children:
                    vt_tex[res.get("ID")] = res.get("GTexFileName") or ""
            elif reg.name == "MaterialBank":
                for res in reg.children:
                    ids = [v.get("ID") for v in res.children_named("VirtualTextureParameters")
                           if v.get("ID") and str(v.get("Enabled", "True")).lower() not in ("false", "0")]
                    materials.append((res.get("ID"), (res.get("SourceFile") or "").replace("\\", "/"), ids))
    print(f"[catalogue] {n_files} content files, {len(materials)} materials, {len(vt_tex)} VT resources", flush=True)
    tflags = {}
    for _, src, _ in materials:
        if src in tflags:
            continue
        tflags[src] = None
        if src in ps:
            try:
                parsed = parse_template(src, ps.read(src))
                tflags[src] = parsed[0].flags if parsed else None
            except Exception:
                pass
    want = F_VIRTUAL_TEXTURE | F_ALPHA_TEST
    picked = [(mid, src, ids) for mid, src, ids in materials if tflags.get(src) is not None and tflags[src] & want == want]
    print(f"[catalogue] {len(picked)} alpha-tested VT materials of {sum(1 for f in tflags.values() if f is not None and f & want == want)} templates", flush=True)
    # GTS metadata: texture name -> (tile set, w, h)
    where = {}
    for ts in range(16):
        gts_name = f"Albedo_Normal_Physical_{ts:x}.gts"
        gts = work / gts_name
        if not gts.exists():
            gts.write_bytes(ps.read(VT_DIR + gts_name))
        for t in bridge(a.lslib, work, {"mode": "list", "gts": str(gts)}):
            where.setdefault(t["name"], (ts, t["w"], t["h"]))
    print(f"[catalogue] {len(where)} textures in the 16 tile sets", flush=True)
    have = set()
    if a.out:
        for m in Path(a.out).glob("vt_alpha_manifest*.json"):
            for x in json.loads(m.read_text()).get("masks", []):
                have.add(x["texture"])
    tex_by_tpl, tex_info, missing = defaultdict(set), {}, set()
    for mid, src, ids in picked:
        for i in ids:
            name = vt_tex.get(i, "")
            if name in where:
                tex_by_tpl[Path(src).stem].add(name)
                tex_info[name] = where[name]
            elif name:
                missing.add(name)
    total = sum(mask_bytes(w, h) for _, w, h in tex_info.values())
    total_l0 = sum(mask_bytes(w, h, 1) for _, w, h in tex_info.values())
    masked = [n for n in tex_info if n in have]
    print(f"[catalogue] {len(tex_info)} unique alpha-tested VT textures ({len(missing)} names not found in a GTS); "
          f"BC4 masks levels 0..2 {total / 1048576:.0f} MB (level 0 only {total_l0 / 1048576:.0f} MB); "
          f"already masked {len(masked)} ({sum(mask_bytes(*tex_info[n][1:]) for n in masked) / 1048576:.0f} MB)", flush=True)
    sizes = Counter(f"{w}x{h}" for _, w, h in tex_info.values())
    print("[catalogue] sizes: " + ", ".join(f"{k} {v}" for k, v in sizes.most_common(8)), flush=True)
    ranked = sorted(tex_by_tpl.items(), key=lambda kv: -len(kv[1]))
    for tpl, names in ranked[:15]:
        mb = sum(mask_bytes(*tex_info[n][1:]) for n in names) / 1048576
        print(f"[catalogue] {tpl}: {len(names)} textures, {mb:.1f} MB, masked {sum(1 for n in names if n in have)}", flush=True)
    cat = {"materials": len(picked), "templates": {tpl: sorted(n) for tpl, n in ranked},
           "textures": {n: {"tileset": ts, "w": w, "h": h, "mask_bytes": mask_bytes(w, h), "masked": n in have}
                        for n, (ts, w, h) in tex_info.items()},
           "not_in_gts": sorted(missing), "bytes_levels_0_2": total, "bytes_level_0": total_l0}
    (work / "vt_alpha_catalogue.json").write_text(json.dumps(cat, indent=1))
    if a.out:
        dst = Path(a.out) / "vt_alpha_catalogue.json"
        if dst.exists():
            print(f"[catalogue] {dst} exists: kept, not overwritten")
        else:
            dst.write_bytes((work / "vt_alpha_catalogue.json").read_bytes())
            print(f"[catalogue] added {dst}")


if __name__ == "__main__":
    main()
