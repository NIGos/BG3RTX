"""Changed-key readiness contracts for generators and offline lint.

Build/status patterns in existing bundle13 plans are conditional witnesses in
camera-plan.ps1: acknowledge settings, then reconcile starts and completions.
Shader changes require fresh driver quiescence, never guessed request names.
Layer-only and push-constant changes need no pipeline wait.
"""
import re

from scripts.queue_gates import phase_states

QUIET = r"\[watch\].*\| pipelines 0 \(0 ms in driver\)"
# Keep the legacy pattern registry so checked-in plans remain compatible.
# These records are conditional on an actual build, not mandatory announcements.
TABLE = {
    "rt_water_surface_fresnel": [r"\[water\] surface Fresnel \(\d+ frames\): enabled {value},"],
    "rr_post_temporal": [r"\[rrpost\] rr_post\.hlsl:temporal pipeline built",
                         r"\[rrpost\] rr_post\.hlsl:temporal_final pipeline built"],
    "rt_cascade_casters": [r"gi: gi/rt_shadow\.hlsl[^ ]* generation .*spec cascade={value}",
                           r"gi: gi/rt_local_shadows\.hlsl[^ ]* generation .*spec cascade={value}",
                           r"gi: gi/rt_reflections\.hlsl[^ ]* generation .*spec cascade={value}"],
}
# VALID su1-ultra-s8-fg2.1.004527.log records these core pipeline families;
# core/gi.cpp's specialization selection maps the keys to their consumers.
# Completion records can be cached. Newly selected variants must additionally
# clear the runner's outstanding build records after settings acknowledgement.
GI_TRACE = r"gi: gi/(?:gi_trace_rt[^ /]*|gi_trace)\.hlsl[^ ]* generation "
# Specialization 1029 in core/gi.cpp; cached selections still require fresh QUIET.
TABLE["rt_compact_hit_state"] = [GI_TRACE]
# GI roof rejection, specialization 1038 (forced OFF when rt_stats != 0).
TABLE["gi_far_roof_early"] = [GI_TRACE]
# Far roof rule for hidden-hit emission, specialization 1054 (core/gi.cpp, gi_far files only; claude/final-core).
TABLE["gi_far_roof_emission"] = [GI_TRACE]
# RF specialization 1031; newly selected and cached variants both need fresh QUIET.
TABLE["rf_compact_probe_state"] = [r"gi: gi/rt_reflections[^ /]*\.hlsl[^ ]* generation "]
GI_QUERY = [GI_TRACE, r"gi: gi/rt_local_shadows\.hlsl[^ ]* generation ",
            r"gi: gi/rt_reflections\.hlsl[^ ]* generation "]
# These variants may already be cached: TABLE consumers must not turn the
# legacy family patterns into mandatory generation announcements.
# rt_hair (5m live switch, layer/util.cpp hair_gated): every RTX Hair key reads 0 while it is 0, which selects the
# groom-off GI variants, the RR guide pipeline and specialization 1026 as those keys do.
# The RTX Hair bundle's pipeline keys (5m): rt_hair_groom_primary (strand passes in core/gi.cpp refresh), AO
# transmittance (spec 1044), fibre GI/specular/mapped variants (spec 1030) and the RR guide pipeline (layer/ngx.cpp).
for key in ("rt_hair", "rt_hair_groom_primary", "rt_hair_groom_ao_t", "rt_hair_fibre_gi", "rt_hair_fibre_spec",
            "rt_hair_fibre_mapped", "rr_hair_guides",
            "rt_hair_groom", "rt_hair_physical_origin", "rt_hit_material",
            "rt_hit_material_instanced", "rt_hit_material_wind_rgb",
            "rt_alpha_fine_lod", "rt_vt_alpha_masks", "rt_omm", "rt_omm_dyn"):
    TABLE[key] = [QUIET]
for key in ("gi_far", "gi_hit_direct", "gi_hit_sun", "gi_rr_rays",
            "rt_hit_emission", "rt_emissive"):
    TABLE[key] = [QUIET]
# gi_rt_pipeline (core/gi.cpp hit_gi_dispatch): selects the cached raygen or compute hit-GI pipeline; the raygen
# one builds in the background on first use. gi_rt_pipeline_reorder: specialization 1050 of the raygen request.
for key in ("gi_rt_pipeline", "gi_rt_pipeline_reorder"):
    TABLE[key] = [QUIET]
# rt_scene_handout (docs/SCENE_HANDOUT_DESIGN.md): specialization 1051 of every gi/ and water pipeline (core/gi.cpp
# refresh, core/water.cpp) at 1 and 2; both values stay cached, so a toggle needs a quiet driver, not a build line.
TABLE["rt_scene_handout"] = [QUIET]
# r3-core: rr_post_jitter_align 1/2 runs the rr_post_temporal.hlsl temporal_final entry (layer/rrpost.cpp rrpost_final;
# built in the background on first use, the plain final meanwhile).
TABLE["rr_post_jitter_align"] = [r"\[rrpost\] rr_post\.hlsl:temporal_final pipeline built"]
WATER_COMPOSE = r"\[water\] rt/water_compose\.hlsl[^ ]* water_compose: built"
for key in ("rt_water_compose_shift",
            "rt_water_shadows", "rt_water_ls_shadows",
            "rt_water_refraction", "rt_water_game_shadow"):
    TABLE[key] = [WATER_COMPOSE]
# rt_hair_groom_shadow_t: specialization 1026 of every rt_visibility consumer (core/gi.cpp refresh, core/water.cpp):
# the shadow, local-shadow, reflection and trace pipelines are recreated (driver compile, no DXC).
TABLE["rt_hair_groom_shadow_t"] = list(GI_QUERY) + [r"gi: gi/rt_shadow\.hlsl[^ ]* generation "]
TABLE["ls_k"] = [r"gi: gi/rt_local_shadows[^ ]*_k\.hlsl[^ ]* generation "]
TABLE["ls_box"] = [r"gi: gi/rt_local_shadows_box\.hlsl[^ ]* generation "]
TABLE["rr_post"] = [r"\[rrpost\] rr_post\.hlsl:resolve pipeline built"]
# rr_hair_strand_aa (layer/rrpost.cpp strand_resolve): selects the strand_resolve dispatch, built once (prewarmed when on).
TABLE["rr_hair_strand_aa"] = [r"\[rrpost\] rr_post\.hlsl:strand_resolve pipeline built"]

# VALID su1-ultra-s8-fg2.1.004527.log: core/gi.cpp refresh() records;
# these scalar settings are push constants / pass enables, not new variants.
TABLE.update({
    "gi_ao": [r"gi: gi/gi_ao_apply\.hlsl[^ ]* generation "],
    "gi_strength": [r"gi: gi/gi_composite\.hlsl[^ ]* generation "],
    "rt_shadows": [r"gi: gi/rt_shadow\.hlsl[^ ]* generation "],
    "rf_max_rough": [r"gi: gi/rt_reflections\.hlsl[^ ]* generation "],
    "ls_samples": [r"gi: gi/rt_local_shadows\.hlsl[^ ]* generation "],
    "ls_shadowed": [r"gi: gi/rt_local_shadows\.hlsl[^ ]* generation "],
    "rr_cbt": [r"\[rrpost\] rr_post\.hlsl:resolve pipeline built"],
    "rt_alpha": list(GI_QUERY),
    "rt_water_compose": [WATER_COMPOSE],
    "rt_water_reflections": [r"\[water\] rt/water_reflect\.hlsl[^ ]* water_reflect: built"],
})
# No authoritative pipeline completion witness in the VALID corpus for these
# paths. Explicitly opt them into the conservative generic shader completion
# fallback requested for preparation. Quiet and outstanding-build reconciliation
# are still required; this does not qualify their timing or rendering behaviour.
# hair_fibre selects gi/hair_fibre.hlsl (core/gi.cpp); primary_order and crowd
# change CPU publication/LOD; paks changes host data, geometry capture is host
# side, and rr/fg use vendor pipelines. preset and ab_off affect many passes.
GENERIC = r"\[shaders\].*(?: compiled(?: in| \()|read from the cache|cache: prefetched )"
CONSERVATIVE = set("rt_hair_fibre rt_hair_groom_primary_order rt_hair_primary_passes rt_hair_groom_tips rt_hair_groom_crowd_ksegments rt_water_geometry rr fg preset ab_off".split())
for key in CONSERVATIVE:
    TABLE[key] = [GENERIC]
# 5u: the count shader's HP_COUNT_UNION=1 request (layer/hair_primary.cpp count_pipeline), compiled at its first use.
TABLE["rt_hair_coverage_union"] = [GENERIC]
# 5u: specialization 1047 of the strand G-buffer pipelines (created with the key at startup; live it is a header flag).
TABLE["rt_hair_groom_nearest"] = []
# 5y: specialization 1049 of the strand velocity pipelines (created with the key at startup; live it is a header flag).
TABLE["rt_hair_mv_card"] = []
# 6f: specialization 1052 of the strand G-buffer pipelines (created with the key at startup).
TABLE["rt_hair_dense_sample"] = []

# Settings which schedule observations or change CPU/log policy, without
# selecting pipelines. Unknown rendering keys require explicit completion evidence.
# Alpha spill only selects the CPU capture allocator (layer/geocapture.cpp:2634);
# its current-frame scene publication is checked by the job's scene-wait gates.
CONTROL = set("rt_stats_period_frames rt_stats_reset qa_exposure_hold fg_snap pan_capture pan_prepare pan_post pan_mvec pan_edge pan_inputs pan_water rt_stats rt_primary_check rt_primary_identity rt_primary_epoch ls_stats ls_check rf_stats rt_debug ls_debug gi_debug rf_debug rt_occ_stats rr_show fg_snap_any replay_pack replay_pack_pass rt_mesh_audit_frames rt_mesh_audits rt_scene_wait_ms rt_scene_wait_log_frames rt_rebuild_frames fg_state_every fg_log_presents crash_dump dump_images dump_preview_f32 dump_png_width fg_snap_delay_ms pan_delay_ms rt_mesh_content_id rt_cascade_mb rt_hair_pool_trim".split())

# Explicit non-pipeline contracts, audited against core/gi.cpp refresh()/Push
# and common/shader_variant.h. A cfg acknowledgement is sufficient: these
# values change host policy, dispatch counts or push constants, not requests
# or specialization constants. Keep them out of build reconciliation entirely.
# NR bypass changes only the layer's dispatch decision/history reset, not a shader pipeline.
# core/gi.cpp make_push reads hit_ao_rays into Push::rt_ao_rays every frame.
NO_PIPELINE = set("fg_destroy_wait_exact fg_present_metering hit_ao_rays rt_scene_compact_holds cpu_opt1 cpu_opt2 cpu_opt3 dlss5_nr_bypass rt_build fps_cap cpu_timing detailed_logging dev "
    "gi_rr_rays gi_rays gi_ao gi_strength rf_max_rough ls_shadowed rr_cbt rr fg "
    "rt_shadows rt_alpha rt_hair_groom_primary_order rt_hair_primary_passes rt_scene_wait "
    "rt_hair_groom_crowd_ksegments rt_hair_groom_tips rt_water_geometry preset ab_off "
    # rr_hair_strand_aa snapshot site (layer/rrpost.cpp): which hook dispatches the already-built snapshot pipeline.
    "rr_hair_strand_snap_site "
    # RTX Hair host/header keys (layer/hair_primary.cpp, bvhsplit.cpp, hair_capture.cpp): header flags, submit-time
    # header choice, LOD and coverage policy, the existing cascade clone, the capture writer. No pipeline is selected.
    "rt_hair_groom_primary_current rt_hair_primary_frame_tag rt_hair_groom_lod_keep rt_hair_primary_hold "
    "rt_hair_tangent_world rt_hair_groom_cascade_drop rt_hair_capture rt_hair_warmup "
    # 5i item 11: the header camera copy with the layer's existing camera UBO copy
    # (geocapture geo_ubo_copy, built once at its first use: a job exercises the key in a warm phase). No core pipeline.
    "rt_hair_header_cam_gpu "
    # 5i shared draw key: the groom rows' key filter (bvhsplit). CPU only.
    "rt_hair_shared_key "
    # Removed 2026-11-14 (docs/RTX_HAIR_PRIMARY.md 6k): no code reads these keys. They stay known here only so that
    # historical jobs that set them still lint.
    "rt_hair_watchdog_draw rt_hair_watchdog_one_key rt_hair_swap_latch rt_hair_coverage_min_pixels "
    "rt_hair_row_stable rt_hair_stale_publish "
    # 5u: two bad coverage measurements in a row (hair_primary CoverageGate::confirm). CPU only.
    "rt_hair_coverage_confirm "
    # 5u audit: the live-on warm-up window (hair_primary). CPU only.
    "rt_hair_live_warmup "
    # 5u: the late-arrival test aid (hair_primary LateArrival, bvhsplit groom match). CPU only.
    "rt_hair_debug_late_arrival "
    # 5v: the camera strand LOD pick and its camera BLAS/TLAS (bvhsplit, host side).
    "rt_hair_pixel_lod rt_hair_pixel_lod_k rt_hair_pixel_lod_small_keep "
    # 6g: the card fill's header flag (hair_primary publish) and gate policy; its specialization 1053 is read when the
    # strand pipelines are created at startup, a live change builds no pipeline.
    "rt_hair_card_fill "
    # 6h: which pass applies the strand header (hair_primary_before_pass) and the velocity draws' label rule. CPU only.
    "rt_hair_header_scene_pass "
    # 6i: the coverage gate's age rule (CoverageGate::keep, read once per publish). CPU only.
    "rt_hair_coverage_keep "
    # Elastic capture reserve policy (common/rt_capture_budget.h CaptureHeadroom, read per frame in layer/geocapture.cpp).
    "rt_capture_rise_reserve rt_alpha_floor_yield "
    # Mesh cache budget accounting (common/rt_capture_budget.h mesh_cache_charged_as, read per build/present in
    # layer/bvhsplit.cpp): which bytes rt_mesh_budget_mb charges. Host policy only, no pipeline.
    "rt_mesh_budget_own "
    # BLAS chunk cap by occupancy (common/as_chunk_alloc.h chunk_within_cap, layer/bvhsplit.cpp as_create): host only.
    "rt_as_cap_occupied "
    # VRAM right-sizing (claude/vram-rightsize): capture sizing (common/rt_capture_budget.h, layer/geocapture.cpp), the
    # mesh pool trim (common/vram_compact.h) and the BLAS chunk fit (common/as_chunk_alloc.h). Host policy, no pipeline.
    "rt_capture_peak_reserve rt_capture_demand_size rt_capture_split_mb rt_mesh_pool_trim rt_as_chunk_fit "
    # Startup-only capture layout (common/capture_stride.h): set at launch, never toggled in a run; no core pipeline.
    "rt_capture_wide_split "
    # Hold snapshot pack for the 64-byte layout (claude/final-layer, common/dyn_hold.h): the layer's hold_pack compute
    # pipeline, already made for the 88-byte hair draws; the strand header at device creation (startup only, not toggled).
    "rt_hold_pack_base rt_hair_header_early "
    # BLAS chunk size classes and the kept cascade BLAS on a refusal (claude/r2-layer, common/as_chunk_alloc.h same_class,
    # layer/bvhsplit.cpp as_create and the cascade build): host placement only, no pipeline.
    "rt_as_size_class rt_cascade_keep_refused "
    # Groom LOD decode on a worker thread (layer/hair_groom.cpp read_lod_async, read per groom upload in
    # layer/bvhsplit.cpp groom_lod): host scheduling only, no pipeline (claude/r4-hitch).
    "rt_hair_groom_async "
    # Scene handout: the hit-material passes take the committed pair (core/gi.cpp handout_hit_settle) and the layer
    # skips the commit-time query (layer/rtscene.cpp). The same pipelines (constant 1051 follows rt_scene_handout).
    "rt_scene_handout_hit_wait "
    # r3-core: rt_sun_overlap only reorders the existing rt_shadow / rt_sun_resolve dispatches (core/gi.cpp run_pre).
    "rt_sun_overlap".split())
for key in NO_PIPELINE:
    TABLE[key] = []


def resolved(state):
    """Match common/settings.h resolve_derived before comparing target states."""
    state = dict(state)
    if str(state.get('rt_build', '')).lower() == 'auto':
        state['rt_build'] = str(int(any(float(state.get(k, 0)) != 0 for k in
            ('rt_shadows', 'gi_rt', 'rt_local_shadows', 'rt_reflections', 'rt_debug'))))
    return state


def requirements(job, plan):
    states = [resolved(s) for s in phase_states(job, plan)]
    out = []
    for i, state in enumerate(states):
        previous = states[i-1] if i else state
        changed = {k: v for k, v in state.items() if previous.get(k) != v and k not in CONTROL}
        wanted = {}
        for key, value in changed.items():
            if key in NO_PIPELINE:
                continue
            if key in TABLE:
                # Pipeline requests depend on the complete target state and may
                # already be resident. Only fresh driver quiescence is required.
                wanted[key] = [QUIET]
            else:
                # Unknown keys need an explicit contract, never an invented
                # shader build/quiet interval (they may be layer-only too).
                wanted[key] = []
        out.append(wanted)
    return out


def apply(job, plan):
    for phase, wanted in zip(plan, requirements(job, plan)):
        if not any(wanted.values()):
            continue
        patterns = phase.get("wait_log", [])
        patterns = [patterns] if isinstance(patterns, str) else list(patterns)
        fresh = list(phase.get("wait_log_fresh", []))
        for key, required in wanted.items():
            if not required:
                continue
            patterns.extend(required)
            fresh.append(QUIET)
        phase["wait_log"] = list(dict.fromkeys(patterns))
        phase["wait_log_fresh"] = list(dict.fromkeys(fresh))
        phase["wait_log_timeout"] = max(float(phase.get("wait_log_timeout", 0)), 180)


def problems(job, plan):
    errors = []
    for phase, wanted in zip(plan, requirements(job, plan)):
        have = phase.get("wait_log", [])
        have = [have] if isinstance(have, str) else have
        fresh = phase.get("wait_log_fresh", [])
        for key, patterns in wanted.items():
            missing = [p for p in patterns if p not in have]
            if key not in TABLE:
                # Existing explicit key-specific waits remain supported. Never
                # invent an authoritative completion from a quiet driver alone.
                specific = [p for p in have if p != QUIET and
                            key in p]
                if not specific:
                    missing.append("unknown pipeline mapping; declare key-specific wait_log")
            if patterns and QUIET not in fresh:
                missing.append("fresh quiescence after settings write")
            if missing:
                errors.append(f"readiness: {phase.get('label')}: changed {key} requires pipeline wait_log ({'; '.join(missing)})")
    return errors


def conditional(pattern):
    """Keep aligned with Test-CameraPlanBuildPattern (legacy plan encoding)."""
    return bool(re.search(
        r'generation |pipeline built|: built|surface Fresnel|read from the cache|cache: prefetched |pipelines 0', pattern))


def reconciled(pattern):
    """Only tracked GI/water completions and replaced quiet/cache records."""
    return conditional(pattern) and bool(re.search(
        r'^gi: |water.*: built|shaders|watch|read from the cache|cache: prefetched |pipelines 0', pattern))
