"""Bounded, read-only archive probe. Run from the repository; never reads saves.

This checks resource references and headers, not decoded mesh/material correctness.
Only the JSON evidence destination is written. No game process or GPU is used.
"""
from __future__ import annotations

import collections
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.assets import lsf
from tools.assets.build import DEFAULT_GAME, find_data_dir
from tools.assets.extract.lights import TemplateResolver
from tools.assets.lspk import PackageSet


def main():
    head_start = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    ps = PackageSet(find_data_dir(DEFAULT_GAME))
    names = list(ps.index)
    folded = {n.casefold(): n for n in names}
    errors = []

    def read_resource(name):
        try:
            return lsf.parse(ps.read(name))
        except Exception as exc:
            errors.append({"path": name, "error": str(exc)})
            return None

    level = "BGH_SteelWatchFoundry_B"
    base = f"Mods/GustavDev/Levels/{level}/"
    categories = {}
    records = {}
    for cat in ("Scenery", "TileConstructions", "Items", "Terrains"):
        name = base + cat + "/_merged.lsf"
        resource = read_resource(name)
        objs = list(resource.find_all("GameObjects")) if resource else []
        records[cat] = objs
        categories[cat] = {
            "path": name, "objects": len(objs),
            "with_direct_visual": sum(bool(o.get("VisualTemplate")) for o in objs),
            "with_template": sum(bool(o.get("TemplateName")) for o in objs),
            "child_nodes": dict(collections.Counter(c.name for o in objs for c in o.children)),
        }
    print("placement categories read", flush=True)
    resolver = TemplateResolver(ps, types=("scenery", "item", "TileConstruction", "terrain"))
    visual_ids = set()
    placement_visuals = []
    unresolved = []
    for obj in records["Scenery"]:
        chain = [obj] + list(resolver.chain(obj.get("TemplateName", "")))
        visual = next((n.get("VisualTemplate") for n in chain if n.get("VisualTemplate")), None)
        if visual:
            visual_ids.add(visual)
            placement_visuals.append(visual)
        else:
            unresolved.append(obj.get("MapKey"))

    visuals = {}
    materials = {}
    other_resource_matches = {}
    bank_count = 0
    hlod_visuals = []
    bank_names = sorted(n for n in names if "/Content/" in n and n.endswith("_merged.lsf"))
    for name in bank_names:
        resource = read_resource(name)
        if resource is None:
            continue
        bank_count += 1
        for bank in resource.regions:
            for node in bank.children:
                if node.get("ID") in visual_ids and bank.name != "VisualBank":
                    other_resource_matches[node.get("ID")] = {
                        "bank_kind": bank.name, "path": name, "name": node.get("Name"),
                        "source": node.get("SourceFile"),
                    }
            if bank.name == "VisualBank":
                for node in bank.children:
                    ident = node.get("ID")
                    if ident in visual_ids:
                        visuals[ident] = (name, node)
                    if "/HLOD/[PAK]_CTY_LowerCity_A/" in name:
                        hlod_visuals.append((name, node))
            elif bank.name == "MaterialBank":
                for node in bank.children:
                    materials[node.get("ID")] = (name, node)
    print(f"{bank_count} content banks read", flush=True)

    missing_mesh = []
    material_ids = set()
    mesh_paths = set()
    samples = []
    for ident, (bank, node) in sorted(visuals.items()):
        src = str(node.get("SourceFile", "")).replace("\\", "/")
        actual = folded.get(src.casefold())
        objects = list(node.find_all("Objects"))
        mids = [o.get("MaterialID") for o in objects if o.get("MaterialID")]
        material_ids.update(mids)
        if actual:
            mesh_paths.add(actual)
        else:
            missing_mesh.append({"visual": ident, "source": src})
        if len(samples) < 3:
            samples.append({"visual": ident, "bank": bank, "source": src,
                            "source_exists": bool(actual), "material_ids": mids,
                            "object_lods": [o.get("LOD") for o in objects]})

    header_samples = []
    for name in sorted(mesh_paths)[:3] + [folded.get(str(n.get("SourceFile", "")).casefold())
                                         for _, n in hlod_visuals[:1]]:
        if not name:
            continue
        data = ps.read(name)
        section_offset, section_count = struct.unpack_from("<II", data, 44)
        section_start = 32 + section_offset
        sections = []
        if 0 <= section_count <= 64 and section_start + section_count * 44 <= len(data):
            for i in range(section_count):
                codec, offset, compressed, uncompressed = struct.unpack_from("<IIII", data, section_start + i * 44)
                sections.append({"codec": codec, "compressed_bytes": compressed,
                                 "uncompressed_bytes": uncompressed,
                                 "stored_range_in_file": offset + compressed <= len(data)})
        header_samples.append({"path": name, "bytes": len(data),
                               "sha256": hashlib.sha256(data).hexdigest(),
                               "first_64_bytes": data[:64].hex(),
                               "gr2_version_word_at_32": struct.unpack_from("<I", data, 32)[0],
                               "sections": sections})

    vt_names = [n for n in names if n.lower().endswith((".gts", ".gtp"))]
    vt_stats = {}
    for suffix in (".gts", ".gtp"):
        selected = [n for n in vt_names if n.lower().endswith(suffix)]
        vt_stats[suffix] = {"files": len(selected),
                             "logical_bytes": sum(ps.index[n][1].size or ps.index[n][1].disk_size for n in selected)}
    gts = folded.get("generated/public/virtualtextures/albedo_normal_physical_0.gts")
    gts_sample = None
    if gts:
        package, entry = ps.index[gts]
        # The install stores this resource uncompressed; avoid a full GTS read.
        if entry.method == 0:
            with open(package.part_path(entry.part), "rb") as handle:
                handle.seek(entry.offset)
                header = handle.read(80)
            gts_sample = {"path": gts, "magic": header[:4].decode("ascii"),
                          "version": struct.unpack_from("<I", header, 4)[0],
                          "layers": struct.unpack_from("<I", header, 28)[0],
                          "mip_levels": struct.unpack_from("<I", header, 40)[0]}

    tile_sample = records["TileConstructions"][0] if records["TileConstructions"] else None
    result = {
        "scope": "Package index, foundry placement references, content-bank joins and bounded resource headers only.",
        "limitations": ["No GR2 vertex/index decode or export", "No VT tile decode or shading comparison",
                        "No runtime state, save, game launch, GPU execution or performance measurement",
                        "Scenery results do not cover terrain, constructions, instances, characters or mods",
                        "Content-bank ID collisions use lexical last match in this probe; not an engine precedence proof"],
        "git_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "git_head_start": head_start,
        "package_fingerprint": ps.fingerprint(),
        "packages": len(ps.packages), "indexed_paths": len(names),
        "gr2_files": sum(n.lower().endswith(".gr2") for n in names),
        "hlod_gr2_files": sum(n.lower().endswith(".gr2") and "/hlod/" in n.lower() for n in names),
        "terrain_patch_files": sum(n.lower().endswith(".patch") for n in names),
        "level": level, "categories": categories,
        "extra_instance_lsf_files": sum(n.startswith(base + "Instances/") and n.endswith(".lsf") for n in names),
        "content_banks_parsed": bank_count,
        "scenery": {"placements_with_visual": len(placement_visuals), "unique_visual_ids": len(visual_ids),
                    "unresolved_template_visuals": unresolved, "visual_ids_found": len(visuals),
                    "missing_visual_ids": sorted(visual_ids - visuals.keys()),
                    "non_visual_bank_matches": other_resource_matches,
                    "missing_visual_placement_counts": dict(collections.Counter(v for v in placement_visuals if v not in visuals)),
                    "unique_mesh_paths_found": len(mesh_paths), "missing_mesh_paths": missing_mesh,
                    "unique_material_ids": len(material_ids),
                    "material_ids_in_banks": sum(m in materials for m in material_ids),
                    "missing_material_ids": sorted(material_ids - materials.keys()), "samples": samples},
        "lowercity_hlod_visuals": len(hlod_visuals), "mesh_header_samples": header_samples,
        "virtual_textures": vt_stats, "gts_sample": gts_sample,
        "tile_construction_sample": tile_sample.to_dict(2) if tile_sample else None,
        "read_errors": errors,
        "source_sha256": {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in (
            "tools/assets/geom.py", "tools/assets/build.py", "tools/assets/extract/world.py",
            "shaders/rt/rt_common.hlsli", "shaders/rt/bg3_materials.hlsli",
            "shaders/gi/sharc_inject.hlsl", "shaders/gi/rt_reflections.hlsl",
            "mods/BG3RTX_Bridge/ScriptExtender/Lua/BootstrapClient.lua", "layer/bvhsplit.cpp")},
    }
    out = Path(__file__).with_suffix(".json")
    out.write_text(json.dumps(result, indent=2, default=str) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(out), "gr2": result["gr2_files"], "hlod": result["hlod_gr2_files"],
                      "scenery": {k: v for k, v in result["scenery"].items() if k != "samples"},
                      "read_errors": len(errors)}, indent=2))


if __name__ == "__main__":
    main()
