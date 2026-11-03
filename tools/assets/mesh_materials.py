"""Additive offline VisualBank submesh -> material catalogue from the installed paks.

Run: python -m tools.assets.mesh_materials --out <NEW assetdb/mesh_materials.json>
No game process, saves, GPU, or SDK is needed. A resource identity is NOT a Vulkan
buffer identity: runtime_join_ready remains false until a verified content join
exists. This file deliberately contains no invented shader rows or UV basis.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import time

from . import lsf
from .extract.materials import parse_template
from .locate import game_data, lslib_tools
from .lspk import PackageSet

FORMAT = "BG3RTX_MESH_MATERIALS_V1"


def source(node):
    return str(node.get("SourceFile") or "").replace("\\", "/")


def add_resource(table, key, value):
    """Reject conflicting IDs rather than silently assigning a wrong material."""
    if not key:
        raise ValueError("resource has no ID")
    if key in table and table[key] != value:
        raise ValueError(f"conflicting resource ID {key}")
    table[key] = value


def collect(resources):
    """Build exact resource joins; ObjectID and LOD are retained, never ordinal guesses."""
    visuals, materials, textures = {}, {}, {}
    conflicts, origins = {}, {}

    def insert(bank, table, key, value):
        try:
            add_resource(table, key, value)
        except ValueError:
            if not key:
                raise
            tag = bank + ":" + key
            candidates = conflicts.setdefault(tag, [table[key]])
            if value not in candidates:
                candidates.append(value)
    for path, resource in resources:
        for region in resource.regions:
            if region.name not in ("VisualBank", "MaterialBank", "TextureBank", "VirtualTextureBank"):
                continue
            for node in region.children:
                key = node.get("ID")
                origins.setdefault(region.name + ":" + str(key), []).append(path)
                if region.name == "VisualBank":
                    objects = []
                    for obj in node.children_named("Objects"):
                        objects.append({"object_id": obj.get("ObjectID") or "",
                                        "lod": int(obj.get("LOD") or 0),
                                        "material_id": obj.get("MaterialID") or "",
                                        "native": obj.to_dict()})
                    value = {"source": source(node), "name": node.get("Name") or "", "objects": objects}
                    insert("VisualBank", visuals, key, value)
                elif region.name == "MaterialBank":
                    insert("MaterialBank", materials, key, {"source": source(node), "native": node.to_dict()})
                else:
                    insert(region.name, textures, key, {"bank": region.name, "native": node.to_dict()})
    rows = []
    for vid, visual in sorted(visuals.items()):
        for obj in visual["objects"]:
            rows.append({"visual_id": vid, "source": visual["source"], **obj,
                         "material_resolved": obj["material_id"] in materials,
                         "ambiguous": "VisualBank:" + vid in conflicts or "MaterialBank:" + obj["material_id"] in conflicts})
    return {"format": FORMAT, "runtime_join_ready": False,
            "identity": "VisualBank resource UUID + exact ObjectID + LOD; no Vulkan/content join",
            "rows": rows, "materials": materials, "textures": textures, "conflicts": conflicts,
            "origins": origins}


def write_new(path, doc):
    """Exclusive creation is mandatory for the shared assetdb junction."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as output:
        json.dump(doc, output, sort_keys=True, separators=(",", ":"), allow_nan=False)
        output.write("\n")


def build(ps, log=print):
    names = sorted(n for n in ps.index if "/Content/" in n and n.endswith(".lsf"))
    start = last = time.monotonic()
    failures, inputs = [], []

    def resources():
        nonlocal last
        for i, name in enumerate(names):
            now = time.monotonic()
            if now - last >= 30:
                rate = i / max(now - start, 1e-9)
                log(f"[mesh materials] {i}/{len(names)} content files, {rate:.1f}/s, "
                    f"ETA {(len(names)-i)/max(rate,1e-9):.0f}s, failures {len(failures)}", flush=True)
                last = now
            raw = ps.read(name)
            try:
                parsed = lsf.parse(raw)
            except (ValueError, IndexError, KeyError) as error:
                failures.append({"path": name, "error": str(error)})
                continue
            inputs.append({"path": name, "package": ps.owner(name), "sha256": hashlib.sha256(raw).hexdigest()})
            yield name, parsed

    doc = collect(resources())
    templates = {}
    paths = sorted({m["source"] for m in doc["materials"].values() if m["source"]})
    for i, path in enumerate(paths):
        if path not in ps:
            templates[path] = {"missing": True}
            continue
        raw = ps.read(path)
        parsed = parse_template(path, raw)
        if parsed:
            template, cbs, params = parsed
            templates[path] = {"template": asdict(template), "constant_buffers": cbs,
                               "parameters": {v: [asdict(p) for p in values] for v, values in params.items()},
                               "sha256": hashlib.sha256(raw).hexdigest()}
        if time.monotonic() - last >= 30:
            now = time.monotonic()
            log(f"[mesh materials] templates {i+1}/{len(paths)}, failures {len(failures)}", flush=True)
            last = now
    doc.update(templates=templates, inputs=inputs, failures=failures,
               counts={"rows": len(doc["rows"]), "materials": len(doc["materials"]),
                       "textures": len(doc["textures"]), "templates": len(templates),
                       "conflicting_ids": len(doc["conflicts"]),
                       "ambiguous_rows": sum(r["ambiguous"] for r in doc["rows"]),
                       "unresolved_rows": sum(not r["material_resolved"] for r in doc["rows"])})
    log(f"[mesh materials] complete {doc['counts']}, failures {len(failures)}, {time.monotonic()-start:.1f}s", flush=True)
    return doc


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game", default=game_data(), help="Installed BG3 Data folder (BG3_DATA/Steam)")
    parser.add_argument("--out", required=True, type=Path, help="NEW file; existing files are never overwritten")
    args = parser.parse_args()
    if not args.game:
        parser.error("game Data folder not found; set BG3_DATA or --game")
    if args.out.exists():
        parser.error("output already exists; choose a new file")
    if args.out.resolve().is_relative_to(Path(args.game).resolve().parent):
        parser.error("output must be outside the game installation")
    doc = build(PackageSet(args.game))
    doc["lslib_available"] = bool(lslib_tools())
    write_new(args.out, doc)


if __name__ == "__main__":
    main()
