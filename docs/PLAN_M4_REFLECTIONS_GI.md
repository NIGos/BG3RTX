# Plan M4: offscreen shaded reflections and GI hit lighting

2026-10-31, docs only. Main 776441e, ABI 24. Milestone row: RENDERING_EXECUTION_PLAN.md M4.
Runs after the consolidation live batch passes. Live jobs for increment 1:
[tools/live_batches/m4_m5_first/](../tools/live_batches/m4_m5_first/README.md).

## Current state (exact keys)

| Part | Key, default | State |
|---|---|---|
| D-RF reflections, tiers (a) on-screen hit and (c) probe | `rt_reflections=1` (on), `rf_max_rough` 0.6, `rf_*` | Qualified 10-26 (TRACK_D_REFLECTIONS 10). 0.33 ms foundry |
| D-RF tier (b), shaded hidden/offscreen hits | `rf_hit_shade=0`, live, GUI `exp_rf_hit_shade` | Offline done, READY_FOR_LIVE (TRACK_D_REFLECTIONS 14). Foundry: 21% hidden, 6% offscreen, 28% miss keep the probe |
| D-RF + SHaRC for glossy hidden hits | `sharc=1`, `sharc_query` bit 2 (`gi/rt_reflections_sharc.hlsl`) | Off, cache not qualified (fine-cell gate open) |
| Hit-material provider (M3a) | `rt_hit_material=0`, restart, GUI `exp_hit_material` | Static native VT opaque, verified draw join. R8 median 2246464 qualified/window |
| GI hidden-hit point/spot bounce | `gi_hit_direct=0`, live, GUI `exp_gi_hit_direct`; needs `rt_hit_material=1`, `gi_rt=1`, `gi_rt_fallback=0` | GI R3 VALID (static OFF/ON/OFF): 0 negative px, 21517 positive; motion, visible gain and isolated cost not qualified |
| Cache-free reference | `rt_hit_reference` 1 direct / 2 two surfaces; `rf_debug=7` | Inspection only, point/spot, no sun/ambient/emission |
| Emissive data | `rt_emissive`, policy in core (ABI 24, TRACK_D_EMISSIVE 13.21) | Separate emissive TLAS + light list; no consumer adds emission at hits |

Code: `shaders/gi/rt_reflections.hlsl`, `rt_reflections_sharc.hlsl`, `gi_trace_rt_hit_direct.hlsl`
(define `RT_HIT_GI_DIRECT`), `shaders/rt/native_hit_material.hlsli` (provider, in the alpha set 1),
`native_hit_direct.hlsli` (froxel point/spot, RT-shadowed), `core/gi.cpp` (`rf_dispatch`,
`hit_gi_dispatch`, `gather_hit_gi_writes`), `common/native_hit_gi_bindings.h` (typed 19/20-slot
contract). Variants are defines (`common/shader_variant.h`, `shaders/variants.txt`).

Limits that decide the order:
- The froxel light provider only indexes lights for points inside the view frustum.
  Offscreen lights, projector lights, sun and emission at hits are unsupported today.
- Hit-material coverage (2026-10-31, branch hitmat-reasons): the provider covers only the audited
  static VT opaque pair (and tileset with `rt_native_tileset=1`), non-instanced, fresh XFB only.
  Live S8/S6: 0.83% of GI candidates qualified, S6 98.7% unknown. Every other instance is reason
  `base` (counter 127 separates them). Caps raised on branch m3-coverage (next item).
- Caps (m3-coverage, READY_FOR_LIVE): opaque rows 128 -> 4096, deferred rows 128 -> 4096,
  samplers 16 -> 64, cache views 64 -> 256 (`common/native_hit_records.h`); streams sized by need
  (source <= 128 MB per slot, deferred 32 MB chunks <= 4, query basis <= 256 MB). COVERAGE.md
  sees at most 1026 standard G-buffer draws per frame, so the row caps are 4x that.
  `[hit material capacity]` logs used/capacity/overflow of each cap. The caps were likely not the
  main limit: most static geometry is BVH-cached (`kInfoPool`, no per-frame XFB), see below.
- `native_hit_direct` traced to the light centre, into the light's own fixture: 0 nonzero of 22871
  supported. It now stops `ls_clear` short, as D-LS. Reasons and the zero split (134/135) are logged
  by the real consumers (`hit material`, `GI hit direct ... supported zero`).
- D-RF already uses 32 push descriptors (the RTX 5090 limit). A material variant must take
  its extra inputs from descriptor sets, not new push slots.
- GI R2 failed from a shader dispatched through the wrong (contracted D-LS) layout. Every new
  dispatch gets its own typed binding contract, checked against the compiled SPIR-V.

## Hit-material coverage: next families (m3-coverage, 2026-10-31)

`[hit material families]` (rt_stats) lists the declined G-buffer pairs by vertices per packet.
- Instanced statics (Base_VT_STI_DEF, FS `14678345c10aefbc`, class A VSs 2d2ac3c3 and others):
  not extended. The audited VS 3812bc98 is a per-draw UBO-matrix VS, so it never draws instanced;
  STI is a different pair. Blockers: its depth/deferred VS XFB patch and binding map are not
  audited, and its meshes are BVH-cached (pool), so no per-frame prepass/deferred XFB exists for
  the exact per-hit geometry check. Needs a per-mesh material row on pool instances (record id in
  InstInfo for kInfoPool, UV/basis from the pool) plus the STI audit.
- Pool instances, step 1 (branch pool-materials, READY_FOR_LIVE, no shader/ABI change): the
  pool holds only class A meshes; the audited pair is class B and never pooled, so pool records
  come from the STI pair (2d2ac3c3/14678345, fields recipe in `native_vt_surface.h`). Every class A
  G-buffer draw is joined to its pool mesh by index range + position stream
  (`common/native_hit_pool.h`); STI bindings (same set 1 map) + engine/reflectance bytes go into a
  persistent record (refresh every 30 frames, dropped on mesh eviction, "stale" when a view/sampler
  dies, "ambiguous" for two materials on one geometry). `[hit material pool]` (rt_stats) gives
  pool instances qualified/without by reason. Hits still use no pool record. Remaining:
  1. Audit the 2d2ac3c3 G-buffer XFB patch (`vt_frame` today only for 3812bc98): UV + T/N/B outputs.
  2. At record creation, one XFB of that draw; a `mesh_extract` mode maps instance 0's UV + basis to
     object space (inverse matrix, as positions) into a pool range (44 B/vertex, ~130 B/triangle:
     ~200 MB at 1.5M triangles; only qualified meshes, under `rt_mesh_pool_max_mb`).
  3. Per frame, register_use appends the current qualified records as rows (same 256 B layout, a
     gather dispatch for t/p0/p1 from the record's engine bytes), views into the cloned set.
  4. InstInfo: the material row in a side buffer indexed by instance (rec stays the alpha record);
     the shader path for kInfoPool reads UV/basis from the pool and replaces the prepass/deferred
     clip check by an exact UV check (pool UV vs the G-buffer XFB UV at extraction).
  5. Ordinary shaders byte-identical with rt_hit_material=0 (variant define); one A/B on S6/S8.
- Tileblend/terrain (tileset, 64e25689 terrain VS): tileset works with `rt_native_tileset=1`;
  terrain draws patches from `_m0[gl_InstanceIndex]` + a heightmap, needs its own recipe.
- Skinned characters (class B always-skinned VSs, CHAR_* STI FSs): positions from the skinning
  SSBO (0.13) per frame; fresh XFB already, needs each CHAR FS recipe audited (fields, SSS).
- Decals (COVERAGE: up to 9.3% S1): they edit the G-buffer, have no own geometry at a hit; needs
  the decal projection volumes in the scene, after the above.

## What is missing for a visible gain

1. GI: proof that the existing hidden-hit bounce is a visible, stable gain (R3 had no motion
   and no crop review). It adds light only where the old fallback added none.
2. Reflections: tier (b). Today a reflection ray that hits a hidden or offscreen surface keeps
   the probe, which does not see that surface. Replacing the probe needs the hit's full outgoing
   radiance (local lights + sun + ambient), or reflections get darker (energy rule).
3. Sun and emission at hits, for both GI and reflections. Outdoors, hidden hits are mostly sunlit.

## Increments, in order (each off by default, one A/B per key)

### M4.1 Qualify `gi_hit_direct` in motion (no code)
- Key: existing `gi_hit_direct` (live), startup `rt_hit_material=1`.
- Files: none. Live job `mm1` (S1 QuickSave_3784, as R3): OFF/ON/OFF static windows plus a
  Q/E turn in each, a K16 burst each, `rt_stats` only in the warm-up.
- Offline: dry run of the job; pan_artifacts and the R3 analyzer on the bursts afterwards.
- Gate: `GI hit direct` candidates/qualified/supported/nonzero all > 0; per pixel no GI
  radiance below OFF beyond kσ (QA derived k, OFF-repeat σ) on static and turn bursts;
  final-image crops show added bounce in hidden-hit regions (review); transient events in the
  ON turn burst not above the bound of the two OFF turn bursts (pan_artifacts).
- Cost: `gi: trace pass (ray-traced ...) GPU median` ON minus OFF (only the trace shader
  changes). Expected +0.1 to +0.2 ms (R3 whole GI +0.19 ms). Budget AO + GI 0.6 ms (QUALITY R7).
- If it passes: propose ON in the Ultra preset (user decision, top of PROGRESS_CODEX).

### M4.2 Reflection tier (b) with the hit material: `rf_hit_shade=0`
- Domain: hidden or offscreen hits inside the frustum, qualified material, no projector light in
  the froxel, finite inputs. Anything else keeps the probe (tier c), as now.
- Radiance: L_b = `native_hit_direct` (point/spot, already RT-shadowed) + sun at the hit (the
  replicated `bg3_sun_colour()` x one RT visibility ray, no native mask there) + diffuse ambient
  at the hit (sky irradiance b34 x diffuse colour x DFG.z, AO 1). Built (2026-10-31): the hit's
  specular ambient (`bg3_spec_ambient` at the hit, local probes included) is in L_b too, since it
  is the same set-2 code; local DIFFUSE probes are not (counted).
- Inputs: material set via `rt_hit_material_use` as set 1, the game's lighting set 1 as set 2
  (already bound by D-RF), the D-LS sun set as set 3 (the EN2 layout note in DESIGN_SOTA_NEXT 2.16).
  No new push descriptors. No ABI change.
- Files: `shaders/gi/rt_reflections.hlsl` (define `RF_HIT_SHADE`), `common/shader_variant.h`,
  `shaders/variants.txt` (regenerate), `core/gi.cpp` (`rf_dispatch`, layout), a typed contract
  `common/native_hit_rf_bindings.h` + test, `common/settings.h`, `bg3rtx.ini`, GUI entry
  `exp_rf_hit_shade` (experimental, off in presets, needs `exp_hit_material`).
- In-frame check (reuses the `ls_check` idea): with `rf_stats=1`, tier-(a) pixels also compute
  L_b for the same hit. Their screen radiance L_a is the game's full lit result. Log mean L_b,
  mean L_a, mean |L_b - L_a| and the count per class (direct only, + sun, + ambient).
  `rf_debug=8`: L_b / L_a per pixel.
- Offline: shader compile; key off gives byte-identical SPIR-V for `rt_reflections.hlsl` and its
  SHaRC variant; descriptor contract test; settings test; GUI layout check.
- Live gate: same run OFF/ON/OFF at S8 Water Queens House (polished stone) and S1. On tier-(a)
  hits, mean(L_b - L_a) within z x its standard error (QA thresholds), else tier (b) is biased
  and stays off. Then on tier-(b) pixels: hard_dark against the `amb_zero` floor passes; the
  frame mean changes only by the accounted delta (logged probe term after D-RF); crops review.
- Cost: reflection pass `rf: reflection pass GPU median` ON minus OFF. Expected +0.2 to 0.4 ms
  (TRACK_D_ALPHA_GI 10 plan). Budget reflections 1.0 ms total.

### M4.3 Sun at GI hidden hits: `gi_hit_sun=0`
- Adds the M4.2 sun term (same helper, shared header) to the `gi_hit_direct` zero-fallback
  replacement. Still an addition only where the fallback was zero. Needs the sun set in the
  hit-GI layout (extend `native_hit_gi_bindings.h`, one more set, not push slots).
- Files: `gi_trace_rt.hlsl` (define), `core/gi.cpp`, contract + test, settings, GUI.
- Live gate: as M4.1, at S3 Campsite (outdoor). `GI hit direct` line gains a sun count.
- Cost: one more ray per qualified hit. Expected +0.1 ms.

### M4.4 Emission at hits (GI and reflections): `rt_hit_emission=0`
- A hit on an emissive surface adds its D-EM record radiance (emissive TLAS, core policy ABI 24),
  for both consumers. The probe and the native bounce never count it twice: tier (a) and screen
  hits already contain the emission, so only tier (b) and hidden GI hits take it.
- Gate: S5/S6 lava and S3 braziers; same-run A/B; the `rt_debug` 16-18 D-EM checks stay clean.

### M4.5 Lights outside the frustum (needs M3b)
- Replace the froxel walk at hits by the world-light provider (asset DB + SE bridge lights,
  DESIGN_SOTA_NEXT 1c light tree). Only after M3b qualifies the light list.

### M4.6 Traced SHaRC update (v2) using M4.2 shading at secondary vertices
- Then compare cache against the cache-free reference (`rt_hit_reference`), DESIGN_SOTA_NEXT 2.15-2.16.

## Rules for every increment

Energy: tier (b) replaces the probe only with a complete supported term; unknown keeps the
probe. GI additions replace only a zero fallback. Unknown traversal (candidate cap) keeps the
old term (TRACK_D_REFLECTIONS 13). Report the pass GPU ms from the existing `gpu` lines.
After a pass: key, gate numbers, cost and the user check go to PROGRESS_CODEX and the TRACK doc.

## Open questions (answered 2026-10-31 by reading the code, for M4.2)

- Sets: yes. The material set is the host alpha set's layout (`rt_hit_material_use` returns a
  query-owned AlphaDSL), so it replaces set 1; the game's set 1 stays set 2; the D-LS sun set
  goes to the free set 3 (`BG3_SUN_SET`); SHaRC keeps set 4. 5 sets, as the existing SHaRC D-RF
  variant; `maxBoundDescriptorSets` is 32 on the RTX 5090. Push set unchanged (32).
- b34: yes. The dumped f469db78 declares set 1 b34 `textureCube _14` (b36 `_16`), read at N,
  mip 0, x `_m27`/`_m28`, blended by `_m29`, x diffuse colour x DFG.z (lines 265-267, 969-994);
  D-RF binds that exact set as set 2. Implemented: TRACK_D_REFLECTIONS 14.
