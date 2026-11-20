## Final consolidation, core and shaders: dead code out, refresh lookups once per frame, NaN-safe giRaw clamp, hidden-hit emission roof key (branch claude/final-core, offline verified, 2026-11-20)

- Dead code removed: `GI_DEBUG_SWIZZLE` and `GI_FAR_M` blocks (gi_trace_rt.hlsl), the unreachable `kCascade` request
  branch (shader_variant.h: every kCascade file is a cascade entry, so the constant carries it) and spec id 1012
  (retired, never reuse). 225 / 225 requests byte-identical to 8d870bf4. `shaders/variants.txt` delta 0.
- Kept: `RT_SCENE_REBASE` / `RT_REBASE_LIGHT_CAMERA` (dead at run time, but `tools/native_hair_gpu_probe` compiles
  its `query_rebase.spv` with them) and the `RT_GI_FAR` define build (`tools/spec_cascade_check.py` reference).
- `refresh()` (core/gi.cpp) reads its 14 cfg keys and 6 persist words once per frame index (`core/frame_memo.h`,
  CTest `frame_memo_test`; build and 238 / 238 CTests pass). About 25 calls per frame, about 0.1 ms CPU estimated before; not measured live.
  A key change applies from the next frame. No ABI change (host `frame_index` already exists). Image unchanged.
- giRaw clamp (gi_trace_rt.hlsl): `select(x <= 65504, x, 65504)` turned a NaN into 65504. Now overflow and +Inf
  store 65504, a NaN stores 0 (fallback0's bounce). Not reachable today. Proof: only the 28 hit-GI requests change;
  their id-agnostic SPIR-V differs in the final select only, equal for every finite input.
- `gi_far_roof_emission` (spec constant 1054, default 0, live, raw key): the far roof rule now also for the emission
  of a hidden hit beyond the base radius (the screen branch already drops such rays). Physically the cut-away roof
  would hide that emitter: the kept emission is a leak. Off: the em files specialize to the old program
  (identity: only the 6 hit_sun_em requests differ; with 1054 frozen to 0 and every other constant symbolic, 6 / 6
  equal the baseline after the shared spirv_equivalence normalizer. `release_shader_equivalence.py --freeze-spec`
  crashed in spirv-opt after 30 min on its exhaustive cross: a tools item).
  A/B to stage (not run): PT reference at a roofed interior with an emitter outside (inn), key 0 / 1 / 0 in one run;
  predicted: GI energy lower or equal with 1, closer to the PT reference; cost within noise (the roof loop runs
  only for emitters beyond 1 m).

Read, no change:
- Hit-GI loop (gi_trace_rt.hlsl 355-737, release path raygen + far + sun + em): no redundant traces; ray count is
  the push constant; stats and SHaRC work are behind uniform branches. Small items left: the raygen prepass
  projects its two hits again in the loop (ALU only), `hit_material_publication_ready()` is one load per hidden ray.
- Handout EXPLAIN (hq-s8-ultra 215258, GI trace -0.36 ms): the core counters show the hit wait on every handout frame
  (hit passes on the committed scene = handout frames +-1 per 120), so the hit GI traces the committed pair as OFF.
  The ON pipeline is another specialization (constant 1051: traces by stamp address), so its register allocation
  differs; hq-s8-modes 205520 read equal. Benign. Small gap: the frame check uses the present counter, which FG
  presents advance; the counters show it does not happen in practice.
- GI images (2560x1067, GI at full resolution): about 202 MB, all bound by the push sets. Hair origin (43.7 MB) is
  already 1x1 unless RTX Hair's `rt_hair_physical_origin` is on at startup; water images are lazy; SHaRC allocates
  nothing without its headers. Skipping rf_hit (5.5 MB), rt_vis (10.9), origin or aux (21.8 each) needs 1x1 dummies
  and a reallocation on live toggles: not done before release.
- Per-frame descriptor writes: push descriptors per dispatch and the sun set ring (9 writes) only. Pipeline builds:
  no new cause found beyond the known startup burst, the `_em` switch and the RTX Hair hotkey.
