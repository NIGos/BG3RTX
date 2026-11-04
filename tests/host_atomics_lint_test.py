"""Lint: no shader atomic may target a buffer the core or the layer allocates host-visible.

    py -3 tests/host_atomics_lint_test.py          (CTest host_atomics_lint)

GPU atomics on host-visible memory cross the PCIe bus: they cost 15 ms (water reflections,
m5w2), 7.2-8.2 ms (water compose, m5w3) and ~80 ms/frame (the rt_stats pattern) in 2026-11.
Statistics counters belong in device-local memory, reduced per workgroup, copied to the host
after the timed region (core/water.cpp g_rts / g_dev / g_cdev is the pattern).

How the lint works (all offline, text only):
1. Shaders: every Interlocked* on a buffer resource (method form `buf.InterlockedAdd(...)` and
   free form `InterlockedAdd(buf[...], ...)`; groupshared variables and images excluded) is
   resolved to its [[vk::binding]] declaration. Each such target must be classified in
   ATOMIC_TARGETS: per binding site, the C++ file, an exact one-line substring that proves the
   backing allocation (checked to still exist) and its memory, "host" or "device".
2. C++: every host-visible allocation call with VK_BUFFER_USAGE_STORAGE_BUFFER_BIT in core/ and
   layer/ (alloc_host_buffer, and the per-file helpers called with a host flag that is not the
   literal false) must be classified in HOST_STORAGE: either "atomic" (naming its
   ATOMIC_TARGETS keys) or "plain" (naming the shader resources it backs, which must then
   have no atomics). Files that request VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT are listed in
   HOST_MEMORY_FILES, so a new host-visible allocator is reviewed.
3. A "host" binding of an atomic target fails unless it is in KNOWN_HOST_ATOMICS (the
   2026-11-01 baseline, to be fixed); a baseline entry that is no longer host fails as stale,
   so the list only shrinks. Unclassified targets or allocations, or a stale evidence
   substring, fail: classify them (follow the descriptor write back to the allocation).
Limit: usage flags passed through variables (e.g. rt_xfb_usage()) are not seen by step 2.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FAILS = []


def check(name, ok, info=""):
    print(f"{'PASS' if ok else 'FAIL'} {name}{(': ' + info) if info and not ok else ''}")
    if not ok:
        FAILS.append(name)


H, D = "host", "device"
# key "declaring shader:resource" -> [(C++ file, exact evidence substring, memory, note)]
ATOMIC_TARGETS = {
    "shaders/rt/hit_material_instanced_remap.hlsl:remap": [
        ("layer/hit_material.cpp", "if(!remaps.empty() && remap_ready() && create(q.remap,perm,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true)){",
         H, "q.remap counters + permutations (rt_hit_material_instanced only; one atomic per instance, not per pixel)")],
    "shaders/rt/rt_common.hlsli:rtStats": [
        ("core/gi.cpp", "if (!host->alloc_host_buffer(kStatsSlot * kStatsRing, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,",
         H, "g_stats_buf bound at set 0 binding 20 for every gi.cpp pipeline (rt_stats=1)"),
        ("core/water.cpp", "!make_device_buffer(kRtStatsBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, g_rts, g_rts_mem) ||",
         D, "water pipelines bind g_rts at binding 20")],
    "shaders/gi/rt_local_shadows.hlsl:lsBoxStats": [
        ("core/gi.cpp", "bci.size = kLsBoxXBytes;", D,
         "g_bx_buf (ensure_bx: DEVICE_LOCAL memory type; set 4 b0 of the box layout, group-reduced, copied to rtStats 385..416)")],
    "shaders/rt/occ_stats.hlsli:occStats": [
        ("core/gi.cpp", "bci.size = kOccBytes;", D,
         "g_occ_buf (ensure_bx: DEVICE_LOCAL memory type; set 4 b1 of the box and occ layouts, group-reduced, copied to the g_occ_host ring)")],
    "shaders/gi/rt_reflections.hlsl:rfGiStats": [
        ("core/gi.cpp", "bci.size = kRfGiBytes;", D,
         "g_rg_buf (ensure_rg: DEVICE_LOCAL memory type; set 6 b0 of the rf_hit_gi layouts, group-reduced, copied to rtStats 419..426)")],
    "shaders/gi/sharc_common.hlsli:shState": [
        ("core/sharc.cpp", "make_buf(g_state, kStateBytes, sb | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);", D, "g_state")],
    "shaders/rt/em_debug.hlsl:emDev": [
        ("core/emissive.cpp", "if (!make_device_buffer(kStatBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, g_dev,",
         D, "g_dev (the D-EM check's counters; its flush entry stores them into the host's emStats, no atomics)")],
    "shaders/rt/em_sanitize.hlsl:Header": [
        ("layer/emissive.cpp", "ok = ok && make_buf(g_host, kRegion * kRing, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true) && g_host.map;",
         H, "g_host region kOffHeader")],
    "shaders/rt/hair_bounds.hlsl:Status": [
        ("layer/alphatest.cpp", "VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)",
         H, "g_stamp_buf slot (alpha_hair_build_status)")],
    "shaders/rt/hair_groom_update.hlsl:Pool": [
        ("layer/bvhsplit.cpp", "VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |",
         D, "g_pool (the mesh pool; rt_hair_groom bounds, group-reduced, copied to the groom readback)")],
    "shaders/rt/hair_primary_count.hlsl:Counts": [
        ("layer/hair_primary.cpp", "VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,",
         D, "g_counts (make: DEVICE_LOCAL memory type; rt_hair_groom_primary strand pixels per groom, group-reduced, copied to the host-visible readback ring)")],
    "shaders/rt/mesh_extract.hlsl:Status": [
        ("layer/bvhsplit.cpp", 'VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "split status");',
         H, "g_status (DEVICE_LOCAL|HOST_VISIBLE tried first)")],
    "shaders/rt/mesh_extract_basis.hlsl:Host": [
        ("layer/hit_material.cpp", "!create(j.host,64,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true)",
         H, "j.host (DEVICE_LOCAL|HOST_VISIBLE tried first)")],
    "shaders/rt/omm_bake_plain.hlsl:opacityData": [
        ("layer/omm_bake.cpp", "VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|buildInput,false) ||",
         D, "inputs->data")],
    "shaders/rt/omm_bake_plain.hlsl:bakeStats": [
        ("layer/omm_bake.cpp", "if(!allocate(dd,*device,kStatsBytes,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,false) ||",
         D, "stats_device (wave-reduced bake state counters, copied to the transfer-only stats_host readback)")],
    "shaders/rt/omm_bake_mask.hlsl:bakeStats": [
        ("layer/bvhsplit.cpp", "if (!make_buf(g_omm_dyn_stats, kOmmDynStatsBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |",
         D, "g_omm_dyn_stats (rt_omm_dyn: wave-reduced bake/check counters, copied to the transfer-only g_omm_dyn_readback)")],
    "shaders/rt/omm_bake_plain.hlsl:contentEpochs": [
        ("layer/omm_content.cpp", "(mem.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;break;}",
         H, "omm content counters")],
    "shaders/rt/omm_content_epoch.hlsl:globalEpoch": [
        ("layer/omm_content.cpp", "(mem.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;break;}",
         H, "omm content counters")],
    "shaders/rt/omm_content_epoch.hlsl:imageEpoch": [
        ("layer/omm_content.cpp", "(mem.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;break;}",
         H, "omm content counters")],
    "shaders/rt/omm_scene_validate.hlsl:epochs": [
        ("layer/omm_content.cpp", "(mem.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;break;}",
         H, "omm content counters (bvhsplit source.epochs)")],
    "shaders/rt/omm_scene_guard.hlsli:gOmmContentEpochs": [
        ("layer/omm_content.cpp", "(mem.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;break;}",
         H, "alpha set binding 11: omm content counters when present (BAR VRAM preferred, system memory fallback; guard atomics once per wave and invocation)"),
        ("layer/alphatest.cpp", "ok = ok && make_buffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, g_dummy_buf,",
         D, "g_dummy_buf fallback")],
    "shaders/rt/rt_common.hlsli:gSceneStamp": [
        ("layer/alphatest.cpp", "VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)",
         H, "g_stamp_buf (alpha set binding 9; atomics only in RT_OMM_SCENE variants)")],
    "shaders/rt/water_compose.hlsl:cStats": [("core/water.cpp", "if (!make_device_buffer(kCDevBytes,", D, "g_cdev")],
    "shaders/rt/water_reflect.hlsl:wStats": [("core/water.cpp", "!make_device_buffer(kStatBytes,", D, "g_dev")],
}

# Host-visible atomic targets on 2026-11-01 (META_AUDIT_20261101 section 5). Fix: device-local
# counters, group-reduced, copied to the host outside the timed region; then delete the entry.
KNOWN_HOST_ATOMICS = {
    ("shaders/rt/hit_material_instanced_remap.hlsl:remap", "layer/hit_material.cpp"): "instanced remap counters (added 2026-11-01 after review: per instance, a few hundred atomics per frame, key off by default; move to device-local with the next hit_material change)",
    ("shaders/rt/rt_common.hlsli:rtStats", "core/gi.cpp"): "rt_stats counters (~80 ms/frame with rt_primary_check)",
    ("shaders/rt/em_sanitize.hlsl:Header", "layer/emissive.cpp"): "D-EM build header (collapsed triangles)",
    ("shaders/rt/hair_bounds.hlsl:Status", "layer/alphatest.cpp"): "hair build status",
    ("shaders/rt/mesh_extract.hlsl:Status", "layer/bvhsplit.cpp"): "split extract status",
    ("shaders/rt/mesh_extract_basis.hlsl:Host", "layer/hit_material.cpp"): "hit-material basis status",
    ("shaders/rt/omm_bake_plain.hlsl:contentEpochs", "layer/omm_content.cpp"): "OMM content epochs",
    ("shaders/rt/omm_content_epoch.hlsl:globalEpoch", "layer/omm_content.cpp"): "OMM content epochs",
    ("shaders/rt/omm_content_epoch.hlsl:imageEpoch", "layer/omm_content.cpp"): "OMM content epochs",
    ("shaders/rt/omm_scene_validate.hlsl:epochs", "layer/omm_content.cpp"): "OMM content epochs",
    ("shaders/rt/omm_scene_guard.hlsli:gOmmContentEpochs", "layer/omm_content.cpp"): "OMM content epochs",
    ("shaders/rt/rt_common.hlsli:gSceneStamp", "layer/alphatest.cpp"): "scene stamp (RT_OMM_SCENE)",
}

# Host-visible STORAGE allocations: (file, substring of the call) -> ("atomic", [targets]) or
# ("plain", [shader resources "file:name" it backs, none of them an atomic target]).
HOST_STORAGE = {
    ("layer/hit_material.cpp", "create(q.remap,perm,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true)"): ("atomic", ["shaders/rt/hit_material_instanced_remap.hlsl:remap"]),
    ("core/gi.cpp", "&g_stats_buf"): ("atomic", ["shaders/rt/rt_common.hlsli:rtStats"]),
    ("core/ls_lights.cpp", "&g_buf"): ("plain", ["shaders/gi/rt_local_shadows_match.hlsl:lsKnown"]),
    ("core/water.cpp", "&g_host"): ("plain", ["shaders/rt/water_reflect.hlsl:wp"]),
    ("layer/bvhsplit.cpp", '"instance/InstInfo ring"'): ("plain", ["shaders/rt/rt_common.hlsli:gInstInfo", "shaders/rt/mesh_extract.hlsl:Mats", "shaders/rt/hold_pack.hlsl:Jobs"]),
    ("layer/bvhsplit.cpp", '"OMM scene replacements"'): ("plain", ["shaders/rt/omm_scene_clone.hlsl:replacements"]),
    ("layer/bvhsplit.cpp", '"OMM scene source stamps"'): ("plain", ["shaders/rt/omm_scene_validate.hlsl:rows"]),
    ("layer/bvhsplit.cpp", '"split status"'): ("atomic", ["shaders/rt/mesh_extract.hlsl:Status"]),
    ("layer/emissive.cpp", "g_host"): ("atomic", ["shaders/rt/em_sanitize.hlsl:Header"]),
    ("layer/emissive.cpp", "g_inst"): ("plain", ["shaders/rt/em_common.hlsli:emInfo", "shaders/rt/em_lights.hlsl:instances"]),
    ("layer/emissive.cpp", "g_globals"): ("plain", ["shaders/rt/em_common.hlsli:emGlobal", "shaders/rt/em_copy.hlsl:Dst"]),
    ("layer/geocapture.cpp", "c->ubo"): ("plain", ["shaders/geo_ubo_copy.hlsl:Out"]),
    ("layer/hit_material.cpp", "j.host"): ("atomic", ["shaders/rt/mesh_extract_basis.hlsl:Host"]),
    ("layer/hit_material.cpp", "q.rows"): ("plain", ["shaders/rt/native_hit_material.hlsli:gHitRecords", "shaders/rt/hit_material_gather.hlsl:rows"]),
    ("layer/rrpost.cpp", "&g_dummy_buf"): ("plain", ["shaders/rr_post.hlsl:BufA", "shaders/rr_post.hlsl:Probe"]),
    ("layer/rrpost.cpp", "&g_probe_buf"): ("plain", ["shaders/rr_post.hlsl:Probe"]),
    ("layer/rtscene.cpp", "&scene.pose"): ("plain", ["shaders/geo_ubo_copy.hlsl:Out"]),
}
# Files that request host-visible memory types themselves (reviewed for step 2's blind spots).
HOST_MEMORY_FILES = {
    "layer/alphatest.cpp", "layer/bvhsplit.cpp", "layer/dump.cpp", "layer/emissive.cpp", "layer/geocapture.cpp",
    "layer/hit_material.cpp", "layer/hotreload.cpp", "layer/omm_bake.cpp", "layer/omm_content.cpp",
    "layer/replay.cpp", "layer/streamline.cpp", "layer/water.cpp",
    "layer/hair_primary.cpp",   # make_readback: the strand feedback ring, TRANSFER_DST only (no storage)
}
# Allocation helpers: (file, name) -> (usage argument index, host argument index or None = always host).
HELPERS = {
    ("*", "alloc_host_buffer"): (1, None),
    ("layer/bvhsplit.cpp", "make_buf"): (2, 3), ("layer/bvhsplit.cpp", "ensure_omm_buffer"): (2, 3),
    ("layer/emissive.cpp", "make_buf"): (2, 3), ("layer/geocapture.cpp", "make_buf"): (2, 3),
    ("layer/water.cpp", "make_buf"): (2, 3), ("layer/hit_material.cpp", "create"): (2, 3),
    ("layer/omm_bake.cpp", "allocate"): (3, 4),
}


def strip_comments(t):
    t = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), t, flags=re.S)
    return re.sub(r"//[^\n]*", "", t)


DECL = re.compile(r"\[\[vk::binding\(\s*([^,\)]+?)\s*(?:,\s*([^\)]+?)\s*)?\)\]\](?:\s*\[\[[^\]]*\]\])*\s*"
                  r"(?:globallycoherent\s+)?(\w+)(?:<[^>]*>)?\s+(\w+)")
METHOD = re.compile(r"\b(\w+)\s*(?:\[[^\]]*\])?\s*\.\s*Interlocked\w+\s*\(")
FREE = re.compile(r"(?<![\.\w])Interlocked\w+\s*\(\s*(\w+)")
GROUPSHARED = re.compile(r"\bgroupshared\b[^;]*?\b(\w+)\s*(?:\[[^\]]*\])*\s*;")


def shader_scan():
    """(declarations {name: [(file, type)]}, atomic uses [(file, name)], groupshared {file: names})."""
    decls, uses = {}, []
    for p in sorted((ROOT / "shaders").rglob("*.hlsl*")):
        t = strip_comments(p.read_text(encoding="utf-8", errors="replace"))
        rel = p.relative_to(ROOT).as_posix()
        for m in DECL.finditer(t):
            decls.setdefault(m.group(4), []).append((rel, m.group(3)))
        gs = set(GROUPSHARED.findall(t))
        for n in sorted(set(METHOD.findall(t)) | set(FREE.findall(t))):
            if n not in gs:
                uses.append((rel, n))
    return decls, uses


def resolve(decls, rel, name):
    """The declaring 'file:name' of an atomic target, 'image' for a texture, or None (unresolved)."""
    cands = decls.get(name, [])
    same = [c for c in cands if c[0] == rel]
    pick = same or cands
    files = sorted({f for f, _ in pick})
    if len(files) != 1:
        return None
    types = {ty for f, ty in pick}
    if all("Texture" in ty for ty in types):
        return "image"
    return f"{files[0]}:{name}"


def split_args(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "([{<" and not (ch == "<" and depth == 0 and cur.rstrip().endswith((" ", "<"))):
            depth += ch in "([{"
        if ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    out.append(cur.strip())
    return out


def host_storage_calls():
    """[(file, call text)] of host-visible allocation calls whose usage has STORAGE_BUFFER_BIT."""
    out = []
    files = sorted(list((ROOT / "core").glob("*.cpp")) + list((ROOT / "layer").glob("*.cpp")))
    for p in files:
        rel = p.relative_to(ROOT).as_posix()
        t = strip_comments(p.read_text(encoding="utf-8", errors="replace"))
        for (hf, name), (ui, hi) in HELPERS.items():
            if hf not in ("*", rel):
                continue
            for m in re.finditer(r"(?<![\w>.])(?:host->)?" + name + r"\s*\(", t) if name != "alloc_host_buffer" else \
                    re.finditer(r"\balloc_host_buffer\s*\(", t):
                i, depth = m.end(), 1
                while i < len(t) and depth:
                    depth += {"(": 1, ")": -1}.get(t[i], 0)
                    i += 1
                body = t[m.end():i - 1]
                pre = t[max(0, m.start() - 12):m.start()]
                if re.search(r"\bbool\s*$|\b\w+\s+$", pre) and re.match(r"\s*(VkDeviceSize|Buf|DeviceData|Buffer)\b", body):
                    continue  # the helper's own definition
                args = split_args(body)
                if len(args) <= ui or "VK_BUFFER_USAGE_STORAGE_BUFFER_BIT" not in args[ui]:
                    continue
                if hi is not None and (len(args) <= hi or args[hi] == "false"):
                    continue
                out.append((rel, " ".join(t[m.start():i].split())))
    return out


def main():
    decls, uses = shader_scan()
    targets, unresolved = {}, []
    for rel, name in uses:
        key = resolve(decls, rel, name)
        if key is None:
            unresolved.append(f"{rel}:{name}")
        elif key != "image":
            targets.setdefault(key, set()).add(rel)
    check(f"every atomic target resolves to one declaration ({len(uses)} uses)", not unresolved, ", ".join(unresolved))
    missing = sorted(set(targets) - set(ATOMIC_TARGETS))
    check(f"every atomic target ({len(targets)}) is classified in ATOMIC_TARGETS", not missing,
          "classify (descriptor write -> buffer -> allocation): " + ", ".join(missing))
    stale = sorted(set(ATOMIC_TARGETS) - set(targets))
    check("ATOMIC_TARGETS has no entry without an atomic use", not stale, ", ".join(stale))
    texts = {}
    bad_ev, host_now = [], set()
    for key, sites in ATOMIC_TARGETS.items():
        for f, ev, mem, _note in sites:
            text = texts.setdefault(f, (ROOT / f).read_text(encoding="utf-8", errors="replace") if (ROOT / f).exists() else "")
            if ev not in text:
                bad_ev.append(f"{key} @ {f}: '{ev[:60]}...'")
            if mem == H:
                host_now.add((key, f))
    check("every ATOMIC_TARGETS evidence substring is still in its file", not bad_ev, "; ".join(bad_ev))
    new_host = sorted(host_now - set(KNOWN_HOST_ATOMICS))
    check("no NEW atomic on host-visible memory (use device-local counters, group-reduced, copied after the timer)",
          not new_host, ", ".join(f"{k} ({f})" for k, f in new_host))
    gone = sorted(set(KNOWN_HOST_ATOMICS) - host_now)
    check("KNOWN_HOST_ATOMICS has no stale entry (fixed ones must be removed)", not gone, ", ".join(f"{k} ({f})" for k, f in gone))
    print(f"note: {len(KNOWN_HOST_ATOMICS)} known host-visible atomic targets remain (baseline 2026-11-01)")

    calls = host_storage_calls()
    used, unclassified = set(), []
    for f, call in calls:
        hits = [k for k in HOST_STORAGE if k[0] == f and k[1] in call]
        if not hits:
            unclassified.append(f"{f}: {call[:140]}")
        used.update(hits)
    check(f"every host-visible STORAGE allocation ({len(calls)}) is classified in HOST_STORAGE", not unclassified,
          " | ".join(unclassified))
    check("HOST_STORAGE has no stale entry", not (set(HOST_STORAGE) - used), str(sorted(set(HOST_STORAGE) - used)))
    wrong = []
    for (f, sub), (kind, res) in HOST_STORAGE.items():
        for r in res:
            sf, name = r.rsplit(":", 1)
            if not any(df == sf for df, _ in decls.get(name, [])):
                wrong.append(f"{r}: no such declaration")
            elif kind == "plain" and r in targets:
                wrong.append(f"{r}: atomic target in a host-visible buffer ({f} {sub})")
            elif kind == "atomic" and (r not in ATOMIC_TARGETS or (r, f) not in host_now):
                wrong.append(f"{r}: 'atomic' allocation without a host ATOMIC_TARGETS site for {f}")
    check("HOST_STORAGE resources exist; 'plain' ones have no atomics; 'atomic' ones are listed host", not wrong, "; ".join(wrong))
    hv = set()
    for p in list((ROOT / "core").glob("*.cpp")) + list((ROOT / "layer").glob("*.cpp")):
        if "VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT" in strip_comments(p.read_text(encoding="utf-8", errors="replace")):
            hv.add(p.relative_to(ROOT).as_posix())
    check("files requesting host-visible memory are the reviewed set", hv == HOST_MEMORY_FILES,
          f"new: {sorted(hv - HOST_MEMORY_FILES)}, gone: {sorted(HOST_MEMORY_FILES - hv)}")
    if FAILS:
        print(f"{len(FAILS)} check(s) failed")
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
