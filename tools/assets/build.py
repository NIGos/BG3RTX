"""`python -m tools.assets build`: game install -> data/assetdb/.

Outputs
  bg3rtx_assetdb.bin   the runtime database (dbformat.py), read by layer/assetdb.cpp
  manifest.json        format version, input fingerprint, counts, section sizes
  json/*.json          with --json: the same data as readable JSON (debugging only)

The game directory is only read. Output goes to --out (never inside the game folder).
"""
from __future__ import annotations

import hashlib
import json
import os
import time
from dataclasses import asdict

from . import __version__, dbformat
from .extract import effects as fx
from .extract import lights as lx
from .extract import materials as mx
from .extract import probes as px
from .extract import triggers as tx
from .extract.world import LevelIndex
from .lspk import PackageSet
from .spirv import STAGE_IDS

DEFAULT_GAME = r"D:\SteamLibrary\steamapps\common\Baldurs Gate 3"


def find_data_dir(game: str) -> str:
    d = os.path.join(game, "Data")
    if os.path.isdir(d):
        return d
    if os.path.basename(os.path.normpath(game)).lower() == "data" and os.path.isdir(game):
        return game
    raise SystemExit(f"no Data folder under {game!r}; pass --game <BG3 install folder>")


def _guard_output(out: str, data_dir: str) -> None:
    o, g = os.path.normcase(os.path.abspath(out)), os.path.normcase(os.path.abspath(os.path.dirname(data_dir)))
    if o == g or o.startswith(g + os.sep):
        raise SystemExit("refusing to write inside the game folder")


def _validate_presets(worlds, triggers, probes, preset_index, log=print):
    required = set()
    for world in worlds:
        required.update(r for r in (world.lighting, world.atmosphere) if r)
    for trigger in triggers:
        if trigger.kind in ("lighting", "atmosphere"):
            required.update(r for r in trigger.resources if r)
    for probe in probes:
        required.update(c.lighting for c in probe.captures if c.lighting)
        if probe.trigger_lighting:
            required.add(probe.trigger_lighting)
    missing = sorted(required - preset_index.keys())
    trigger_keys = {(t.level, t.map_key) for t in triggers}
    unresolved_links = [(t.level, key) for t in triggers if t.kind not in ("lighting", "atmosphere") for key in t.links if (t.level, key) not in trigger_keys]
    if unresolved_links:
        log(f"unresolved trigger links ({len(unresolved_links)}): {unresolved_links}")
        raise ValueError(f"unresolved trigger links: {unresolved_links}")
    if missing:
        log(f"unresolved required lighting/atmosphere presets ({len(missing)}): {', '.join(missing)}")
        raise ValueError("missing required presets: " + ", ".join(missing))


def build(game: str, out: str, jobs: int = 0, want_json: bool = False, include_honour: bool = False,
          skip_effects: bool = False, names: bool = True, log=print) -> dict:
    t0 = time.time()
    data_dir = find_data_dir(game)
    _guard_output(out, data_dir)
    os.makedirs(out, exist_ok=True)
    ps = PackageSet(data_dir)
    fingerprint = ps.fingerprint(log=log)
    identity = [{k: p[k] for k in ("name", "priority", "sha256")} for p in fingerprint]
    digest = hashlib.md5(json.dumps(identity, sort_keys=True).encode()).digest()
    log(f"reading {len(ps.packages)} packages, {len(ps.index)} files ({time.time() - t0:.1f}s)")

    li = LevelIndex(ps, include_honour=include_honour)
    worlds = li.worlds()
    log(f"levels: {len(worlds)} worlds, {sum(len(w.instances) for w in worlds)} level instances")
    resolver = lx.TemplateResolver(ps)
    lights, lstats = lx.extract(li, worlds, resolver, log)
    log(f"lights: {lstats['total']} ({lstats.get('point', 0)} point, {lstats.get('spot', 0)} spot, "
        f"{lstats.get('box', 0)} box)")
    trigs, tstats = tx.extract(li, worlds, tx.trigger_templates(ps), log)
    log(f"triggers: {tstats}")
    probes, pstats = px.extract_probes(li, worlds, ps, log)
    log(f"probes: {pstats}")
    presets, prstats = px.extract_presets(ps, log)
    log(f"presets: {prstats}")
    mats = mx.extract(ps, data_dir, jobs, log)
    efx, estats = ([], {"skipped": True}) if skip_effects else fx.extract(ps, data_dir, jobs, log)
    log(f"effects: {estats}")

    w = dbformat.Writer()
    S = w.strings.add if names else (lambda s: 0)
    A = w.strings.add  # always kept (identifiers the runtime looks up)
    preset_index = {p.id: i for i, p in enumerate(presets)}

    _validate_presets(worlds, trigs, probes, preset_index, log)

    # ---- levels, instances, aliases ----
    by_level = {}
    for coll, key in ((lights, "l"), (trigs, "t"), (probes, "p")):
        for i, o in enumerate(coll):
            by_level.setdefault((o.level, key), []).append(i)
    inst_rows, lvl_rows, alias_rows = [], [], []
    for wi, wd in enumerate(worlds):
        first_inst = len(inst_rows)
        for ii, inst in enumerate(wd.instances):
            inst_rows.append((A(inst.level), inst.parent, *inst.transform.pos, *inst.transform.rot,
                              inst.transform.scale, 1 if inst.starting_loaded else 0, inst.depth))
            alias_rows.append((A(inst.level), wi, ii))
        ls_ = by_level.get((wi, "l"), [])
        ts_ = by_level.get((wi, "t"), [])
        ps_ = by_level.get((wi, "p"), [])
        pts = [lights[i].pos for i in ls_] + [trigs[i].pos for i in ts_] + [probes[i].pos for i in ps_]
        if pts:
            bmin = tuple(min(p[k] for p in pts) for k in range(3))
            bmax = tuple(max(p[k] for p in pts) for k in range(3))
        else:
            bmin = bmax = (0.0, 0.0, 0.0)
        lvl_rows.append((A(wd.name), preset_index.get(wd.lighting, dbformat.NONE),
                         preset_index.get(wd.atmosphere, dbformat.NONE), 0,
                         first_inst, len(wd.instances), ls_[0] if ls_ else 0, len(ls_),
                         ts_[0] if ts_ else 0, len(ts_), ps_[0] if ps_ else 0, len(ps_),
                         *bmin, *bmax, A(wd.lighting), A(wd.atmosphere)))
    # lights/triggers/probes were produced world by world, so each world's range is contiguous
    for coll in (lights, trigs, probes):
        assert all(coll[i].level <= coll[i + 1].level for i in range(len(coll) - 1))
    w.add_records("LVLS", lvl_rows)
    w.add_records("INST", inst_rows)
    w.add_records("ALIA", sorted(alias_rows))

    # ---- lights ----
    w.add_records("LGHT", [(*l.pos, lx.light_bounds_radius(l), *l.rot, *l.color, l.intensity, l.kelvin, l.gain,
                            l.scattering, l.spot_inner, l.spot_outer, *l.box, l.att_start, l.att_end, l.att_side,
                            l.flicker_speed, l.flicker_amount, l.move_speed, l.move_amount, l.flags,
                            l.type, l.att_function & 0xFF, l.channel, 0, l.instance, S(l.name), A(l.cookie))
                           for l in lights])

    # ---- triggers ----
    tkey = {(t.level, t.map_key): i for i, t in enumerate(trigs)}
    trig_rows, pts_rows, ref_rows = [], [], []
    for t in trigs:
        fp = len(pts_rows)
        pts_rows += [(float(p[0]), float(p[1])) for p in t.points]
        fr = len(ref_rows)
        if t.kind in ("lighting", "atmosphere"):
            ref_rows += [(preset_index[r],) for r in t.resources if r in preset_index]
        else:
            ref_rows += [(tkey[(t.level, k)],) for k in t.links if (t.level, k) in tkey]
        trig_rows.append((*t.pos, t.scale, *t.rot, *t.extents, t.height, t.radius, t.fade_time, t.flags, t.floor,
                          tx.KINDS.index(t.kind), t.shape & 0xFF, t.render_method & 0xFF, 0, t.instance, S(t.name),
                          A(t.building), fp, len(t.points), fr, len(ref_rows) - fr))
    w.add_records("TRIG", trig_rows)
    w.add_records("TPTS", pts_rows)
    w.add_records("TREF", ref_rows)

    # ---- probes ----
    prb_rows, cap_rows = [], []
    for p in probes:
        fc = len(cap_rows)
        for c in p.captures:
            prefix = c.diffuse[:-len("_Diffuse_RGB.dds")] if c.diffuse.endswith("_Diffuse_RGB.dds") else c.diffuse
            cap_rows.append((preset_index.get(c.lighting, dbformat.NONE), S(prefix), A(c.lighting),
                             1 if c.sh else 0, *(c.sh or (0.0,) * 16)))
        prb_rows.append((*p.pos, p.radius, *p.rot, *p.size, p.transition, p.intensity,
                         (1 if p.enabled else 0) | (p.shape & 0xFF) << 8, fc, len(p.captures)))
    w.add_records("PRBE", prb_rows)
    w.add_records("PCAP", cap_rows)
    w.add_records("PRES", [(A(p.id), S(p.name), 0 if p.kind == "lighting" else 1, 1 if p.sun_shadow else 0,
                            *p.sun_color, p.sun_intensity, p.sun_yaw, p.sun_pitch, p.sun_size, p.sun_kelvin,
                            *p.sky_color, p.sky_intensity) for p in presets])

    # ---- materials ----
    w.add_records("MTPL", [(A(t.name), t.flags, mx.CLASS_IDS[t.cls], t.shading_model & 0xFF, t.blend_state & 0xFF,
                            t.material_type & 0xFF, t.material_usage & 0xFF, t.raster_state & 0xFF,
                            t.depth_state & 0xFF, t.uv_count & 0xFF, A(t.diffusion_profile), 0, 0, 0)
                           for t in mats.templates])
    prm_rows, list_index, var_rows = [], {}, []
    for v in mats.variants:
        key = tuple(p.key() for p in v.params)
        if key not in list_index:
            list_index[key] = len(prm_rows)
            for p in v.params:
                prm_rows.append((mx.ROLE_IDS[p.role], mx.KIND_IDS.get(p.kind, 0), p.set & 0xFF, p.binding & 0xFF,
                                 p.offset & 0xFFFF, p.layer & 0xFF, 0, A(p.name), *p.default, 0))
        var_rows.append((v.template, v.variant, v.engine_cb[0] & 0xFF, v.engine_cb[1] & 0xFF, v.material_cb[0] & 0xFF,
                         v.material_cb[1] & 0xFF, 0, len(v.params), v.engine_cb[2] & 0xFFFF,
                         v.material_cb[2] & 0xFFFF, list_index[key], 0))
    w.add_records("MPRM", prm_rows)
    w.add_records("MVAR", var_rows)
    shd_rows, sref_rows = [], []
    for e in mats.shaders:
        shd_rows.append((e.hash, len(sref_rows), min(len(e.refs), 0xFFFF), STAGE_IDS.get(e.stage, 255), e.pass_id,
                         e.refs[0], 0))
        sref_rows += [(r,) for r in e.refs]
    w.add_records("SHDR", shd_rows)
    w.add_records("SREF", sref_rows)
    w.add_records("ESHD", [(h, A(n), STAGE_IDS.get(st, 255)) for h, n, st in mats.engine])

    # ---- effect lights ----
    w.add_records("EFXL", [(A(e.effect), A(e.template), e.start, e.end, e.intensity_max, e.intensity_mean,
                            e.radius_max, *e.color, e.flicker_amount, e.flicker_speed, *e.offset, e.flags)
                           for e in efx])

    db_path = os.path.join(out, "bg3rtx_assetdb.bin")
    sections = w.write(db_path, digest)
    size = os.path.getsize(db_path)
    manifest = {
        "format": "bg3rtx-assetdb", "format_version": dbformat.VERSION, "tool_version": __version__,
        "built": time.strftime("%Y-%m-%dT%H:%M:%S"), "build_seconds": round(time.time() - t0, 1),
        "game_data": data_dir, "source_digest": digest.hex(), "packages": fingerprint,
        "file": "bg3rtx_assetdb.bin", "bytes": size, "names": names,
        "counts": {"worlds": len(worlds), "level_instances": len(inst_rows), "lights": len(lights),
                   "triggers": len(trigs), "probes": len(probes), "probe_captures": len(cap_rows),
                   "presets": len(presets), "material_templates": len(mats.templates),
                   "material_variants": len(mats.variants), "material_shader_hashes": len(mats.shaders),
                   "engine_shader_hashes": len(mats.engine), "effect_lights": len(efx)},
        "stats": {"lights": lstats, "triggers": tstats, "probes": pstats, "presets": prstats,
                  "materials": mats.stats, "effects": estats},
        "sections": sections,
    }
    with open(os.path.join(out, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)
    if want_json:
        jd = os.path.join(out, "json")
        os.makedirs(jd, exist_ok=True)
        dumps = {
            "worlds": [{"name": wd.name, "lighting": wd.lighting, "atmosphere": wd.atmosphere,
                        "instances": [{"level": i.level, "name": i.name, "parent": i.parent, "depth": i.depth,
                                       "starting_loaded": i.starting_loaded, "pos": i.transform.pos,
                                       "rot": i.transform.rot, "scale": i.transform.scale} for i in wd.instances]}
                       for wd in worlds],
            "lights": [dict(asdict(l), world=worlds[l.level].name, type_name=lx.TYPE_NAMES[l.type],
                            flag_names=[n for b, n in enumerate(lx.FLAG_NAMES) if l.flags >> b & 1]) for l in lights],
            "triggers": [dict(asdict(t), world=worlds[t.level].name) for t in trigs],
            "probes": [dict(asdict(p), world=worlds[p.level].name) for p in probes],
            "presets": [asdict(p) for p in presets],
            "materials": {
                "templates": [dict(asdict(t), flag_names=[n for b, n in enumerate(mx.FLAG_NAMES) if t.flags >> b & 1])
                              for t in mats.templates],
                "variants": [{"template": mats.templates[v.template].name, "variant": mx.VARIANTS[v.variant],
                              "engine_cb": v.engine_cb, "material_cb": v.material_cb,
                              "params": [asdict(p) for p in v.params]} for v in mats.variants],
                "shaders": [{"hash": f"{e.hash:016x}", "stage": e.stage,
                             "refs": [f"{mats.templates[mats.variants[r].template].name}:{mx.VARIANTS[mats.variants[r].variant]}"
                                      for r in e.refs]} for e in mats.shaders],
                "engine": [{"hash": f"{h:016x}", "name": n, "stage": st} for h, n, st in mats.engine]},
            "effects": [asdict(e) for e in efx],
        }
        for k, v in dumps.items():
            with open(os.path.join(jd, k + ".json"), "w", encoding="utf-8") as f:
                json.dump(v, f, indent=None, separators=(",", ":"))
    log(f"wrote {db_path}: {size / 1e6:.2f} MB in {time.time() - t0:.0f}s")
    return manifest
