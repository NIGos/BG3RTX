# Plan M4: offscreen shaded reflections and GI hit lighting

2026-10-31, docs only. Main 776441e, ABI 24. Milestone row: RENDERING_EXECUTION_PLAN.md M4.
Runs after the consolidation live batch passes. Live jobs for increment 1:
[tools/live_batches/m4_m5_first/](../tools/live_batches/m4_m5_first/README.md).

## Promotion to Ultra (2026-11-01; preset change on branch, NOT merged; decided by `m4_promotion`)

Bundle: `rt_hit_material` (restart), `rt_hit_material_instanced` (startup patch, then live), `gi_hit_direct`,
`gi_hit_sun`, `rf_hit_shade`, `rt_hit_emission=1` (hidden hits; needs `rt_emissive=1` at startup). The branch
turns them on in Ultra only (`common/settings.h`: knobs out of Experimental, preset `{0,0,0,1}`; `kPresetExtra`
Ultra adds `rt_emissive=1`, `rt_xfb_mb=1024`, `rt_cascade_mb=427`). The hit-material record widens the capture
vertex from 24 to 64 B, so equal vertex capacity is 384 x 64 / 24 = 1024 MB and ceil(160 x 64 / 24) = 427 MB
(peaks seen at 1408/587: camera 59 + 198 MB, cascade 208 MB, S8). VRAM +640 MB camera, +3 x 267 MB cascade rings.
`rt_emissive` 0 -> 1 is now a restart in `key_change_needs_restart` (D-EM allocates at device creation; off is live).

Gate results already measured (queue logs in `captures/queue/logs`, read with `python -m tools.m4_promotion_gate <log>`):

| Key | Gate | Measured | Run |
|---|---|---|---|
| rt_hit_material | qualified share of hit lookups | S8 48.1%, S6 27.9% (S8 was 0.4%, then 17%); classifier built share 54.4% | m4r1 030749 / 031149 (26f7735) |
| rt_hit_material_instanced | qualified share ON vs OFF | S14 22.3-23.6% -> 40.4% | m4i 074646 (e86fa5e) |
| | clip rejects (gate < 0.04%) | 1.9% of lookups before the wind tolerance fix; fix unmeasured live | m4i 074646 |
| | cost ON - OFF | gi trace +0.14 ms, rt passes +0.11, frame +1.17 ms (OFF spread 0.28) | m4i 074646 |
| | energy (scene colour 5c3fdabc) | +0.27%, OFF spread 0.04%: brighter, explained by more qualified GI hits (GI hit direct added +8.6%) | m4i 074646 |
| gi_hit_direct (+ gi_hit_sun) | never darker, nonzero | R3 static: 0 negative px, 21517 positive; S6 nonzero 8810 of 32563 qualified | R3; m4r1 055118 |
| | cost ON - OFF (with sun) | S8 gi trace +0.52 ms, whole GI +0.67, frame +1.40 (spread 0.13) | m4r1 030749 |
| gi_hit_sun | participation, roof rule | S8 lit 278764 of 12.45M evaluated, S6 2410; roof-unsupported 0 (casters on). S3 gate not run | m4r1 030749 / 055118 |
| rf_hit_shade | tier (a) L_b / L_a | S8 0.926 (mean L_b - L_a -0.0095 = -136 SE; AO 0.771 vs 0.775); S6 1.091 (+0.0064 = +317 SE) after the box AO fix (was 1.48) | m4r1 030749 / 031149, 055118 |
| | strict M4.2 z gate | FAILS at both (any bias is significant with 10^7 hits); bias within 9% | same |
| | tier (b) shaded share | S8 24.0%, S6 3.0% | same |
| | cost ON - OFF | S8 rf pass +1.84 ms, frame +2.32; S6 rf +1.36, frame +2.80 (budget M4.2: +0.2-0.4) | m4r1 030749 / 031149 |
| rt_hit_emission | participation (S6) | rf tier b emissive 6.9% of 9.48M lookups; GI hidden 23856 of 221200; uncalibrated skipped 58520 | m4r1 055118 |
| D-EM families | AF gate | 5 of 6 pass; 304be1c7 excluded (`rt_em_fx_pulse=0`); 159a6211 legitimately 0 | ef1 (TRACK_D_EMISSIVE 13.22) |

Notes: at S6 the gi trace and rt passes timers read lower with `rf_hit_shade` on (work moves between timed
ranges); read the frame time and the whole-GI line. S6 rf +1.76 ms with emission on vs +1.36 without is a
cross-run hint only.
m4p 11xxxx (2026-11-01): S6 `gi: GPU cost` (an average, not a median) +9.12 ms but trace +0.41, rf +1.58, rt passes
+0.45. The jump comes with `rt_emissive=1` (shot-step-inst -> cost-em-off-a, 11:29:48), not with a hit key, and
stays with `rt_hit_emission=0` (em-off windows 10.4 ms). Inside the gi timer's [2, 3] range `run_post` calls
`em_debug` mode 0 (the D-EM projected check, one non-opaque primary ray per pixel against the emissive TLAS)
every `rt_em_check_frames=16` frames; at S6 that TLAS holds ~335k triangles / 55.8k lights (camp, S8 few), so a
~100 ms check frame every 16 frames would read as +7 ms average while every median is flat (inferred from the
timer ranges and the timing; the check's own GPU time is not logged: time it, or set rt_em_check_frames=0, to confirm). Owner: D-EM
(core/emissive.cpp em_check_due, shaders/rt/em_debug.hlsl). rf_hit_shade cost cuts and bias terms:
TRACK_D_REFLECTIONS 14 "Cost and bias breakdown".

Still unmeasured (the batch measures all but the last two):
- `rt_hit_emission` OFF/ON/OFF at S6: energy and cost.
- Energy of each step toggled live (scene colour float mean): +gi_hit_direct, +gi_hit_sun, +rf_hit_shade,
  +instanced, +emission, at camp, inn, S6, S8.
- Total cost of the bundle (live keys + D-EM capture) on camp, inn, S6, S8; rf_hit_shade bias at camp and inn.
- gi_hit_sun at S3 camp (lit > 0); capture capacity at 1024/427 on all four scenes.
- Not in this batch: the startup hit-material capture cost (restart key, stays on in both states) and motion (M4.1).

Batch `tools/live_batches/m4_promotion` (4 launches, ~18 min, ~1.2-1.8 GB). Pass, to merge the preset branch:
all 4 runs valid; bundle ON scene colour within the OFF-a/OFF-b spread or brighter with the step that carries it
named (no step darker beyond the spread); emission ON within or brighter; capacity 0 overflow; costs reported
(user call: quality before frame rate on Ultra). The rf_hit_shade bias decision stays the user's (strict gate fails).

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
  pool instances qualified/without by reason.
- Pool instances, steps 1-4 (branch pool-hit-path, READY_FOR_LIVE, no ABI change, behind
  `rt_hit_material`):
  1. Audit (dumped SPIR-V, `native_hit_pool_test <2d2ac3c3.spv>`, spirv-val): 2d2ac3c3 writes UV =
     input 4 at location 0 and N/B/T = normalize(A b) at 1/2/3 (b from the vertex quaternion, A the
     instance 3x3); `vt_frame` now patches it too (16/24/36/48, stride >= 64). FS 14678345 = the 8995
     recipe with engine p0 0, p1 16, tile-set index 32, channel 36 (`Recipe::channel_offset`).
     The class A depth VS 646a930e has no UV output: the pool UV slot is empty for these meshes, so
     an exact pool-UV check is impossible. Replaced by the extraction position check below.
  2. A qualified record's mesh gets a basis range in the mesh pool (16 B header + 44 B/vertex,
     132 B/triangle, all ranges <= 256 MB = ~2M triangles, freed with the mesh). At its next STI
     G-buffer draw one XFB of instance 0 only (the bound range holds tris*3 vertices) is taken;
     `rt/mesh_extract_basis.hlsl` (after the pass) compares W with M_0 P under the mesh cache
     tolerance, writes UV + A_0^-1 N/B/T, and a header only if every vertex passed. Result read back
     (`[hit material pool basis]`); fail -> retry with backoff; a pool growth discards the result.
  3. register_use appends each ready record as a row in a pool region after the publication word
     (slot = stable index <= 4096), gathered by `hit_material_gather.hlsl` from the record's bytes.
  4. InstInfo of a non-alpha pool instance: meta bit 15, rec = 1<<31 | slot. `rt_hit_material_pool`
     checks row/InstInfo base+tris, basis header magic/generation, then samples as the per-draw path
     with per-vertex normalize(A_i A_0^-1 w_0). Same status codes. Non-hit-material SPIR-V identical.
  Remaining: live gate on S6/S8 (`[hit material pool basis]` ok > 0, mismatch 0; `hit material`
  qualified up, base/without record down; GI/RF A/B energy rule). Alpha pool instances: none (STI
  is opaque); other class A pairs need their own recipe audit.
- Reflectance variants (branch next-families, READY_FOR_LIVE, no ABI change, behind `rt_hit_material`).
  Ranking (main 0ad3fac; S8 m4r1-gi-rf-s8 / S6 m4r1-rf-hit-shade-s6): qualified 15% / 5.8%, base 58% / 85%,
  row 27% / 9%. Row is the per-draw path (8.0M of 27M in S8 already before the pool path): opaque depth
  records (940da312/19994bf3) whose G-buffer draw is not an audited pair get no row (S8 opaque rows 32,
  query mapped 13; S6 8 and 6). Declined pairs by vertices: S8 41a168e2/42a95e17 hair 839k, 54df894a/613ead9c
  685k, 2d2ac3c3/81657854 512k, 1ed985dd/2257579f 473k, 54df894a/7a07134e 433k, terrain 393k, f111e292/288876d1
  342k, 3812bc98/3da860be 276k; S6 c32ce535/9064a6bd 1.53M, STI 1.24M (pool), bd5dbda2/0b8dfd73 728k,
  54df894a/613ead9c 497k, 2d2ac3c3/3df678c0 215k. Pool "other pair" 111/391 (S8), 93/351 (S6) draws/instances.
  Audit (dumped GLSL, renamed and diffed): 3da860be = 8995 and 3df678c0 = 14678345 with RT2.y = 0.5 and no
  1:1 UBO; 81657854 = 14678345 with RT2.y = mix(0.5, normal-layer x, 1:1 x). Built: recipe reflectance
  modes (`native_vt_surface.h`, `nv_reflectance`), 3812bc98/3da860be as a per-draw pair (gather push word 3),
  both 2d2ac3c3 FSs as pool pairs (row word 220 = mode). Check: `[hit material pool pairs]` records/ready
  per FS > 0; `hit material` row and base down, qualified up; `[hit material capacity]` opaque used vs mapped.
  Offline ranking of all families (world placements, test scenes, traces) with COPY/VARIANT/NEW
  classes and the next step per pair: docs/MATERIAL_FAMILIES.md (`python -m tools.material_families`).
  Next, larger audits: 54df894a/613ead9c (two-layer VT blend + 2D mask, VS 54df894a, 7 VS outputs),
  c32ce535/9064a6bd (plain 2D textures, set 0:7 sampler), class B 1ed985dd/f111e292 (FS 2257579f/288876d1).
- Families 2 (branch families-2, behind `rt_hit_material`, no ABI change). Shares: docs/MATERIAL_FAMILIES.md.
  - 50fbfceb/8e49cde8 ATM_Wind_Vertex_RG_VT (3.9%): FS = 14678345 with RT2.y = 1:1 byte 16 (row
    `reflectance_offset`). VS audit: UV/N/B/T as 2d2ac3c3 (inputs moved), but W = M P + wind(time,
    vertex color RG, 30-45 m fade) before the clip transform: not class A (`analyze_vs_instancing`
    on the dump), so never pooled; instanced, so no per-draw row. Row `CaptureNone`: counted in
    `[hit material families] ... audited recipe, no capture path`, never qualified. Path: per-draw
    rows for instanced draws (prepass vs G-buffer clip check per instance), not the pool.
  - 1e9145e4/923131c7 Base_AlphaTest_2s_VT (2.4%): FS = 14678345 with engine 1:0 tile-set index @44,
    channel @48 (flag @52) and mat3(T, N * (gl_FrontFacing ? 1 : -1), B). At a hit the facing comes
    from the geometry (`nv_two_sided_sign`: viewer and authored N on the same side of the plane). VS
    audit: UV/N/B/T as 2d2ac3c3, but every matrix row's translation adds the per-draw engine offset
    1:0 @32 before W = M P: not class A, never pooled. Row `CaptureNone` (counted as above). Path:
    class A with a per-draw translation offset (bvhsplit instance transform + UBO offset), or the
    instanced per-draw rows above.
  - 54df894a/613ead9c ENV_DetailMap_VT (12.4%, built, READY_FOR_LIVE): pool pair (VS class A). Decode
    from the dumped GLSL: VT layers and engine 1:0 as 14678345 but at 1:4..7; 2D detail normal 1:3
    and mask 1:8 with sampler 0:7 at detail UV = (UV * |A P| / |P|) * 1:1 _m1 (FS: |W - T| / |P| of
    VS outputs 0/1/2); frame = normalize(fma(n_detail * _m0, (1,0,1), n_base)); roughness =
    clamp(fma(1 - mask.y, _m4, phys.y)); aux = clamp(pow(max(|mask.x|, 1e-4), _m3)) * phys.z;
    reflectance _m2 (byte 8). `nv_detail_fields` (shared C++/HLSL, unit-tested vs the FS statements).
    VS 54df894a: UV at 3, N/B/T at 4/5/6 -> `patch_vs` frame location 4 (same XFB 16/24/36/48, so
    mesh_extract_basis is unchanged). Pool row words 224..255: layout, detail table indices, detail
    sampler slot, the four 1:1 floats. Detail derivative: declared isotropic ray cone as the VT layers.
    Check: `[hit material pool pairs]` 54df894a/613ead9c records/ready > 0; `[hit material pool]`
    failures bits 11/12 = 0; `[hit material pool basis]` mismatch 0; `hit material` qualified up.
  - c32ce535/9064a6bd ENV_DetailMap_Iridescent (3.8%, the S6 forge family; branch forge-family, built,
    READY_FOR_LIVE): pool pair. VS c32ce535 = 2d2ac3c3 plus W at location 4 (class A; its depth VS
    e6fc6bf6 is class A too, so the meshes are pooled); same XFB frame patch (location 1). FS from the
    dumped GLSL: no VT, no tile set; engine 1:0 {channel @0, flag @4, profile @8}; five 2D views 1:2..1:6
    with sampler 0:7: normal A at UV * 2, physical/normal B/albedo at UV * tiling (_m1), iridescent colour
    at UV * 20; n = normalize(fma((1, 0, -1) * _m0, n_A * p.w, n_B)); albedo = clamp(mix(albedo, colour,
    max(|1 - clamp(dot(n, V))|, 1e-4) * p.w * _m2)) with V = normalize(-W) in mat3(T, N, B) (at a hit:
    the interpolated camera-relative position, i.e. the camera direction the G-buffer would use);
    RT2 = (p.y, mix(_m3, _m4, p.w), p.x). `nv_iridescent_fields` (shared, unit-tested vs the FS, 108
    cases). Row: word 224 = 2, 228..244 the 2D table indices, floats in p0/p1, gather mode 2 (no tile
    set). The FS writes material class 4 (thin translucency, RT3 = profile, thickness): the hit gets the
    exact class 0 front lobe; the transmission lobe is missing (darker only for light from behind).
    Check (S6): `[hit material pool pairs]` c32ce535/9064a6bd records/ready > 0, `[hit material pool]`
    failure bits 3..6/11 = 0, basis mismatch 0, `hit material` qualified up, GI/RF energy A/B.
  - Capture paths for 50fbfceb and 1e9145e4 (forge-family audit, asset DB depth pairs + dumps): not
    built. Wind: the depth VS 1719e9d0 applies the same displacement as 50fbfceb (time, vertex colour,
    camera-distance fade), so its positions never equal a static pool mesh: the pool option is
    impossible; "no capture path" is correct. The per-draw option is viable (pre and G-buffer XFB of
    the same frame compute the same W; 1719e9d0 exports UV at 0) but needs: the depth pair
    1719e9d0/4bed5db2 as an opaque depth family, instanced prepass/deferred rows (triangles x
    instances), draw keys for the STI input layouts, and a gather mode for the STI engine layout
    (p0 @0, not 8995's @64). Alpha-test 2S: its depth VS 106a0550 adds the same per-draw offset (class
    C), so it is not pooled either; the pool option needs class C pooling in bvhsplit (TLAS transform
    = rows + UBO offset, mesh-cache check with it) AND alpha pool material rows (today pool rows are
    opaque only, `pool_row = !alpha`); the per-draw option needs instanced alpha rows. Neither is a
    small step; the wind per-draw path is the cheaper one (no alpha, no host BVH change).
    Built 2026-11-01 as the per-draw option for both (`rt_hit_material_instanced`, TRACK_D_REFLECTIONS 14):
    4bed5db2 is an alpha-table FS, so both are instanced ALPHA rows (no opaque depth family needed); the
    key is draw words + index range; the STI engine bytes are staged in the gather's layout (no new gather
    mode); 2S flips the N column at the hit (`nv_two_sided_sign`). No host BVH change.
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

- Built 2026-11-01 (branch gi-hit-sun, offline, READY_FOR_LIVE): TRACK_C_GI "Sun at qualified
  hidden hits". The roof rule is shared with rf_hit_shade (TRACK_D_REFLECTIONS 14).

### M4.4 Emission at hits (GI and reflections): `rt_hit_emission=0`
- A hit on an emissive surface adds its D-EM record radiance (emissive TLAS, core policy ABI 24),
  for both consumers. The probe and the native bounce never count it twice: tier (a) and screen
  hits already contain the emission, so only tier (b) and hidden GI hits take it.
- Gate: S5/S6 lava and S3 braziers; same-run A/B; the `rt_debug` 16-18 D-EM checks stay clean.
- Correction (2026-11-01, read in the code): the screen value has no emission either. `prevRadiance` is the
  lit diffuse + specular kept by `gi_composite.hlsl` before compose adds the effects target, so tier (a) and
  GI screen hits lack it too. Hence `rt_hit_emission=1` hidden hits only (the planned domain), `=2` also
  screen hits. SHaRC hits already carry it (the injection adds the effects target).
- Built 2026-11-01 (branch hit-emission, offline, READY_FOR_LIVE): TRACK_D_EMISSIVE 13.23. Emitters as
  light sources at hidden hits (light-list sampling) not built; why and the next step there.

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
