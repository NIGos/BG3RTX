## Round 2 core and shaders: skipped unread work (sun roof test, RF probe at w = 0, GI temporal in RR), two early-return fixes, water passes without readers (branch claude/r2-core, offline verified, 2026-11-20)

Fixes (image identical unless noted; no ABI change, no new key, `shaders/variants.txt` delta 0):
- `rt_shadow.hlsl`: the roof-volume test (up to 512 volumes per traced pixel) runs only where `rt_sun_resolve`
  reads it: vis != fold, t_off <= 0 (roof-query sizing), roof mode != 0, a debug view or the trust counters.
  Proof: with vis == fold the resolve writes lerp(mask, lerp(fold, vis, trust), s) = lerp(mask, vis, s) for any
  trust (fold + trust * 0); the sizing reads `roof` only where t_off <= 0. Cost: not measured; the
  `kSunTrace` gpu line shows it in the next Ultra live (most pixels have vis == fold).
- `rt_reflections.hlsl` (compact probe state, Ultra): the probe term is skipped where w = 0 (rough, miss,
  hidden, off screen, aborted) unless validation, rf_debug or rf_stats read it. Output bit-identical for finite
  probe terms. Inn pack 093603-61 (spec 1031=1): one dispatch each, baseline and candidate bit-equal to each
  other and to live (specular, diffuse, hit distance, counters). Paired 40-pair replay: -0.034 ms (95% CI
  -0.037 to -0.027); the known sustained-replay 1-ULP instability shows on both sides (A 4/40, B 2/40).
- `gi_temporal.hlsl`: history 1 (RR mode, Ultra) writes the fresh sample directly: no MV, history or depth-slope
  loads at full resolution. FMix(hist, cur, 1) may evaluate as hist + (cur - hist); cur is the intended value
  (rounding-level change only, energy equal).
- `core/water.cpp`: water_primary and water_reflect (two full-screen passes) run only when a reader exists
  (rt_debug 21-24, stats, WATER_RR_HIT). Experimental water only.
- `core/gi.cpp`: a refused rt_debug dispatch no longer skips run_post's tail (history parity, closing barrier,
  restore_bindings); a refused pan_primary restores the game's bindings before returning. Debug paths only.
  Identity vs e2770961: 201 / 225 byte-identical, the 24 changed are the three files' requests. 238 / 238 CTests.

Found, not fixed (owner or A/B needed):
- Water game-shadow record (`common/water_fs_patch.h:699`, layer): Dg = D (1 - Vg) / Vg is NaN at Vg = 0, so fully
  game-shadowed water pixels are never restored (`water_compose.hlsl:500`). Store D_u (1 - Vg) instead. Stream A.
- `rr_post_temporal.hlsl:51`: the history depth test (3x3 range + 2 ulps) rejects most taps under camera motion;
  energy-safe, little smoothing. `rr_post.hlsl:250` (non-temporal `final`, Low-High) lacks the jitter offset that
  `temporal_final` applies. Both image-changing.
- `core/gi.cpp:3650-3672`: rt_shadow waits for the whole GI chain; it reads none of its outputs. Recording it
  right after rt_origin may overlap 0.05-0.2 ms (GPU A/B needed).
- GI ray cone `pa * 2.0` (`gi_trace_rt.hlsl:141, 313`) assumes half resolution; in RR mode (full res) the
  alpha-test footprint is one mip too coarse. Image-changing, small.
- CPU: `stats_enabled()` (5 lookups, ~25 calls per frame) and ~100 raw cfg lookups in run_pre/run_post: about
  40 us per frame together; a per-frame memo like `core/frame_memo.h`.
Read and clean: GI screen part of gi_trace_rt (1-355, 737-end), gi_composite, gi_ao_apply, gi_contact, gi_trace,
rt_origin, rt_sun_resolve, rt_local_shadows (all variants), rt_reflections, water_compose / view, rr_post,
rr_guides, rr_strand_aa, hair fibre passes, hair_primary_*; every dispatch grid in core/*.cpp matches numthreads.
Tools (stream C), `--freeze-spec`: no crash in 3 min on hit_sun_em (baseline 967b68cc). `spirv_equivalence.opt()`
prints stdout+stderr only, so a native spirv-opt crash has no text (add exit code, input); after a first DIFFERENT
it still checks every Cartesian assignment (12292 here, about 3.5 h) instead of stopping.
