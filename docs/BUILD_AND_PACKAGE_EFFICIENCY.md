# Build, shader check and package efficiency

Goal: shorter shader build and check times, and a smaller, simpler release package, without
redistributing Larian data. Measured on 2026-11-02 (branch worktree-agent-a0dba2e726bdabdad, main 16b78a7,
RTX 5090, 8 machine-wide DXC slots shared with other agents). Offline only; no game launch.

## Today

- 2962 shader requests: 85 base files + 2877 variants in `shaders/variants.txt` (3007 entries).
  The variants are the cross product of startup capabilities and keys (common/shader_variant.h).
  Requests per define: RT_HIT_INSTANCED 1824, RT_NATIVE_SCALP/HAIR 1756, RT_OMM_SCENE 1450,
  RT_NATIVE_TILESET 1450, RT_CASCADE_MASKS 1376, RT_HIT_WIND_RGB 912, RT_HIT_IP_GRASS 912,
  RT_HAIR_PHYSICAL_ORIGIN 612, RF_HIT_GI 480, RT_GI_FAR 320, the water and current-sun defines 2-27 each.
  The four rf_hit_shade files alone are 956 requests (1.5 GB of SPIR-V).
- One DXC compile: 3.1-3.4 s uncontended (gi_trace_rt_hit_direct, 437 KB SPIR-V); 0.45 compiles/s with
  8 jobs while other agents compile. The cache key is the git blob closure of the source, so an edit to
  `shaders/rt/rt_common.hlsli` changes the key of 2860 of 2962 requests, even a comment.
  `compile_shaders.py`: 2860 compiles, 40-50 min. `shader_identity.py --baseline`: both sides, ~2 h.
- The SPIR-V of all entries: 3.0 GB raw (2403 unique modules, 2.4 GB), 0.78 GB zipped, ~0.4 GB lzma.
  The release preset (every experimental key off) uses only the 100 plain entries: 15 MB raw, 4.7 MB zip.
- Runtime: the layer compiles HLSL with dxcompiler.dll on first use and keeps the SPIR-V in
  `%LOCALAPPDATA%\BG3RTX\shader_cache` (key includes the dll's hash). No VkPipelineCache.

## Ranked avenues

| # | Avenue | Saving | Cost | Risk |
|---|---|---|---|---|
| 1 | Preprocessed-text cache key (section 3) | rt_common guarded/comment edit: 2860 -> 0 compiles (40 min -> ~20 s); real commits 773 -> 296, 628 -> 48 | 0.5 day, tools only | low: 52/52 sampled requests agree |
| 2 | Specialization constants for on/off keys (sections 1-2) | 2962 -> 1622 modules (cascade masks), -> 662, -> 176, -> ~100; a header change then costs <= 176 compiles (~4 min); toggling a key in game needs no DXC | 1 day for the cascade masks, 2-3 days for the rest, family by family | medium: layouts must hold every binding; new identity gate |
| 3 | Release ships the plain set's SPIR-V, or pre-warms it (section 4) | first launch: no 100 x 3 s DXC wait; package +4.7 MB zipped | 0.5-1 day when packaging resumes | low |
| 4 | Generate data on the user's machine (section 5) | no Larian-derived data in the package; VT masks 1.3-1.7 GB never shipped | assetdb: frozen Python 1 day or C++ port 1-2 weeks; masks: .NET 8 tool 2-3 days | user decision (below) |
| 5 | Cache prune cap (done with 1), merge-green, CTests (section 6) | small: the cache already de-duplicates | hours | low |
| 6 | VkPipelineCache, sccache for C++ (sections 4, 6) | mostly redundant (NVIDIA driver cache; C++ incremental build 17 s) | 0.5 day each | low; skip for now |

Recommended order: 1 (now, offline tools only), 2 step S1 then S2-S3 (one family per branch, each
gated by `tools/spec_constant_proto.py`-style checks and a replay), 3 when packaging resumes, 4 after
the user decides about the asset DB, 5-6 only if they become a bottleneck.

## 1. Specialization constants instead of defines

A `[[vk::constant_id(N)]] const uint k = 0;` in HLSL becomes an OpSpecConstant; the core sets it in
`VkComputePipelineCreateInfo.stage.pSpecializationInfo`; the driver folds it before its backend compile.
DXC 1.9.0.5399 (Vulkan SDK 1.4.357) supports it for compute (already used: shaders/rt/omm_content_epoch.hlsl,
layer/omm_content.cpp). Every pass is a compute shader with inline ray queries, so the ray tracing pipeline
restrictions do not apply. Rules that decide whether a define can become a constant:
- The code it guards may only change values and control flow. Declarations stay unconditional.
- Every descriptor the module references must be in the pipeline layout, even when the constant is 0:
  Vulkan counts a binding as statically used if any code references it, also dead code. Today several
  layouts gain bindings only with a capability (set 1 b19 tileset samplers, layer/alphatest.cpp:614;
  set 0 b50 hair origin, core/gi.cpp:603; set 6 for RF_HIT_GI). Those need the binding always, with a
  dummy descriptor when off.
- `RayQuery<FLAGS>` template flags must be literals: move them to the `TraceRayInline` flags argument
  (the same SPIR-V RayFlags operand).
- groupshared arrays and private struct fields stay declared; check with `pipeline_stats` that the driver
  drops them (registers, shared memory, binary size).

Classification (`#if` blocks over shaders/, with the use sites):

| Define | Kind | Notes |
|---|---|---|
| RT_CASCADE_MASKS | pure switch | ray masks 0xFF / 0x01 (rt_common.hlsli:161), the hit sun's roof rule; prototype below |
| RT_CASCADE_SUN_HIT, RT_CASCADE_SUN, RT_CASCADE_TMIN(_REL) | pure switch | mask expression and the cascade candidate test; TMIN as float constants |
| RT_HIT_WIND_RGB, RT_HIT_IP_GRASS | pure switch | family field decode and counters |
| RT_HIT_INSTANCED | pure switch | RtHit.dir (private struct field), two-sided flip |
| RT_GI_FAR | switch + groupshared | gs_far_stat; check shared memory |
| RT_NATIVE_TILESET | switch + binding | set 1 b19 must always be in the layer's alpha layout |
| RT_HAIR_PHYSICAL_ORIGIN | switch + binding | set 0 b50 always in g_layout |
| RT_NATIVE_SCALP / RT_NATIVE_HAIR | switch + refactor | NativeHairData struct, procedural candidates, RT_SKIP_PROCEDURAL in RayQuery template flags |
| RT_OMM_SCENE | binding qualifier | gSceneStamp is `globallycoherent RWByteAddressBuffer` vs `ByteAddressBuffer` at the same binding: two aliased declarations or keep the define |
| RF_HIT_GI | layout | set 6 (rfGiStats), its own Pipes; always-present set 6 or keep |
| RT_SUN_CURRENT | layout | sun sets b12-16, own Pipe; keep as a define (12 requests) |
| WATER_SHADOWS / LS / REFR / GSH | struct layout | 40/96/104-byte records; keep (27 requests) |
| WATER_RF_EDGE | pure switch | one term |
| WATER_RR_HIT | binding | b30 output; keep (2 requests) |

Module count if each step's defines become constants (one module per file and remaining defines):

| Step | Defines turned into constants | Modules |
|---|---|---|
| today | - | 2962 |
| S1 | RT_CASCADE_MASKS (+ the sun-hit mask logic) | 1622 |
| S2 | + RT_HIT_WIND_RGB, RT_HIT_IP_GRASS, RT_HIT_INSTANCED, RT_GI_FAR | 662 |
| S3 | + RT_NATIVE_TILESET, RT_HAIR_PHYSICAL_ORIGIN, RT_NATIVE_SCALP/HAIR | 176 |
| S4 | + RT_OMM_SCENE (aliased stamp) | 117 |
| S5 | + RF_HIT_GI (set 6 always), WATER_RF_EDGE | ~100 |

Effects: a header edit that changes code then costs at most 176 compiles at S3 (~4 min at 0.45/s, vs 40-50
min); the SPIR-V for every key fits in ~180 MB raw (~55 MB zipped); toggling a key in game is a pipeline
creation only (no DXC). The module per file holds every feature's code, so DXC time per module may grow:
the prototype measured no difference (3.4 s vs 3.1-3.4 s), a full union may be ~1.2-1.5x (estimate).

Identity changes meaning: a key's code now lives in the base module, so "key off = byte-identical SPIR-V"
becomes "key off = the same program": the module specialized with the constants at 0, frozen and folded
(`spirv-opt --freeze-spec-const --fold-spec-const-op-composite -O`), equals the previous commit's module
through the same passes, compared up to id numbering (canonical() in tools/spec_constant_proto.py, ~3 s
per module). A replay pack run (bit-identical outputs) is the end-to-end check per family.

### Migration plan (user decision 2026-11-02: reach the final variant set first, then compile once)

Owner: the spec-cascade agent. Every step lives on one integration branch `variant-migration` (from main after
S1 merges), each step its own commit. Constant ids come from the one registry `bg3::shader_variant::SpecId`
(common/shader_variant.h); flag bits 12-15 stay free for the pending branches (kSpecCascade is bit 16).

| Step | Defines -> constants (id) | Host work | Requests after (base files + variants.txt; S2-S5 estimated) |
|---|---|---|---|
| S1 | RT_CASCADE_MASKS (+ RT_CASCADE_SUN in rt_shadow) (1000) | none: done, core only | 2986 + 1348 spec (branch spec-cascade-s1) |
| S1-default | the spec build is the only cascade path | none (branch spec-cascade-s1-default) | 1674 |
| G | GI_TEMPORAL_MV_SAMPLE (1001), GI_PREV_RADIANCE_GUARD (1002): pure switches | none; gi-reproj-guards must land as constants, never as defines (+2192 requests as defines) | 1674 (no growth) |
| S2 | RT_HIT_INSTANCED (1003), RT_HIT_WIND_RGB (1004), RT_HIT_IP_GRASS (1005), RT_HIT_POOL_2S (1006, hm-recipe-rows), RT_GI_FAR (1007) | none: private struct fields, counters, gs_far_stat stays declared | ~660 |
| S3 | RT_NATIVE_TILESET (1008), RT_HAIR_PHYSICAL_ORIGIN (1009), RT_NATIVE_SCALP + RT_NATIVE_HAIR (1010, incl. the water passes the native hair branch adds), RT_OCC_STATS (1011) | layer: set 1 b19 tileset samplers always in the alpha layout (alphatest.cpp:614/1518, dummy samplers); core: set 0 b50 rtHairOrigin always in g_layout (gi.cpp:603, dummy image); the occ statistics set always bound (dummy buffer); RayQuery<RT_SKIP_PROCEDURAL> template flag -> TraceRayInline flags | ~180 |
| S4 | RT_OMM_SCENE (1012) | gSceneStamp declared twice (read-only and globallycoherent RW) on set 1 b9, the RW one used under the constant; the OMM bake reads both: check its SPIR-V and the bake identity test | ~120 |
| S5 | RF_HIT_GI (1013), WATER_RF_EDGE (1014) | set 6 (rfGiStats) always in the rf hit-shade layouts | ~100 |

Defines that stay (they change layouts, record strides or descriptor sets, and are few): RT_SUN_CURRENT with
RT_CASCADE_TMIN(_REL) (its own pipe and sets b12-b16; 12 requests), WATER_SHADOWS / WATER_LS / WATER_REFR /
WATER_GSH (40/96/104-byte records and the refraction binding b68), WATER_RR_HIT (b30 output), and the replay-only
HM_BLOCKERS. The OMM bake (worktree-agent-a53f0b584fa39317b) and wf5 add no variant: rebase only. OMM: only
RT_OMM_SCENE is a request define; it becomes a constant at S4 (or stays, ~60 requests, if the aliased stamp
binding costs anything). Occ diag: RT_OCC_STATS at S3 (its stats set always bound); until then it must not enter
the cross product (diagnostic: one request per pass, not x every capability). Hair: RT_NATIVE_SCALP/HAIR and
RT_HAIR_PHYSICAL_ORIGIN at S3, the water hair variants of worktree-agent-a78f48005e3b350bd included.

Offline verification per step, cheap (no full compile): `tools/spec_cascade_check.py` generalized to every key of
the registry (`--key <name>`): sample = each file once + 24 random requests; for each old request, the new module
specialized with the constants its defines imply, frozen, folded and -O, must be the same program (canonical
compare with reference-order blocks and parallel phi declarations grouped by type) as the old request's module; a control pair must differ; the old modules are
mostly cached already (~60-100 DXC compiles per step: the new modules of the sample). Plus one replay per touched
family (gi_hit_direct S8 pack; the rf / ls packs once they exist) bit-identical per value, and `pp-diff` against the
step's base: every request that keeps its spelling preprocesses to the same text (default path byte-identical).
Host steps (S3 layouts, S4 aliasing) also get the descriptor-interface checkers of compile_shaders.py.

One merge-green at the end: yes. Each step's checks run on its own commit with sampled compiles; the integration
branch then merges once with one merge-green, which compiles the final set (~100-180 modules, ~5-10 min idle)
instead of each intermediate set (S1 alone would add 1348). Then the live `spec_cascade` job (scc1, extended with
one window per step's key: toggling it with the constant must compile nothing and keep energy and GPU ms), then
the pending branches rebase: gi-reproj-guards (as constants 1001/1002), the occ diag (1011), hm-recipe-rows
(1006), native hair (water hair variants as 1010), OMM bake and wf5 (no variants). Risk: a step whose sampled
check passes but an unsampled combination differs; mitigation: `--all` on the final set before the merge-green
(it compiles each new module once, which the merge-green then takes from the cache).

## 2. Prototype: RT_SPEC_CASCADE on gi_trace_rt_hit_direct

- `shaders/rt/rt_common.hlsli`: with `RT_SPEC_CASCADE` (no request sets it) the masks come from constant
  1000 (`kSpecCascadeMasks`); an `#error` refuses the combinations it does not cover yet (cascade sun ray,
  current sun, the hit sun's roof rule in native_hit_sun.hlsli). Without it the preprocessed text of all
  2962 requests is unchanged (section 3), so the ordinary SPIR-V is byte-identical. Not wired into the core.
- `py -3 tools/spec_constant_proto.py [--driver] [--replay <pack>]` (compiles through shader_identity,
  replay outputs to a temp dir). Results:
  - Modules for this file's plain and cascade variants: 2 -> 1 (437084 / 437084 bytes -> 437312 bytes).
  - DXC uncontended: 3.1 s + 3.3 s for the two old modules, 3.4 s for the new one.
  - Static: specialized to 0 / 1, frozen, folded and -O: the SAME program as the old plain / cascade module
    (873 globals, 24774 body lines, 0 differ). Control: old plain vs old cascade differ in 4 lines (the masks).
  - Driver (RTX 5090, pipeline_stats): 168 registers, 325760-byte binary, 7936 B shared memory for all four
    modules: the driver folds the constant. Wall time with device creation 2.3-2.7 s each (one 12 s outlier
    with 15 DXC processes running): not a clean pipeline-creation number. Estimate: the driver compile is
    the same work for a specialized module as for the old one; the saving is the DXC compile (3-10 s CPU).
  - Replay (S8 pack replay-20261101-142819-1, gi_hit_direct, 2.7 M pixels): giRaw, giRawDepth and rtStats
    bit-identical between new spec 0 and old plain, and between new spec 1 and old cascade; GPU 9.6-11.0 ms
    each (one run each, noise). The pack has no cascade instance, so plain and cascade agree here too: the
    static check is what separates them.
- Next for S1: the sun-hit mask (`g_rt_cascade_ray`) and the roof rule as constant expressions, then core
  `refresh()` passes a VkSpecializationInfo instead of adding RT_CASCADE_MASKS to the request (core only,
  hot reload, no ABI change), variants.txt and shader_variant_test updated, a replay with a casters pack.

## 3. Preprocessed-text cache key

`py -3 tools/shader_identity.py --pp-diff <rev> [--pp-new <rev>]` (new, measurement only): for every request
whose blob closure changed, DXC -P with the compile's exact arguments on both sides; the digest drops
`#line` lines (absolute tree paths and comment line numbers; without -Zi they do not reach the SPIR-V),
CRLF and blank lines. ~155 requests/s with 8 threads, no DXC slot (-P is light).

| Change | Requests | Compiles with today's key | With a preprocessed key | Time of the check |
|---|---|---|---|---|
| this branch's rt_common/native_hit_sun edit (guarded by an unset define) | 2962 | 2860 | 0 | 37.7 s (both sides) |
| 341f3b2 gi_hit_sun (rt_common + new files) | 709 | 628 | 48 | 17.3 s |
| 211e69e / 468f58c hit material instanced | 872 | 773 | 296 | 10.4 s |
| 22dbf4c OMM qualification (code change in rt_common) | 2033 | 1933 | 1933 | 23.7 s |

Soundness samples, compiled for real: 211e69e -> 468f58c: 24/24 preprocessed-equal requests gave identical
SPIR-V and 4/4 preprocessed-different ones different SPIR-V; main -> this tree: 24/24 identical SPIR-V.
Implemented (branch pp-cache-key, 2026-11-02): a second key level in `compile_many`, so compile_shaders.py,
merge-green and every `*_shader_test.py` gate use it. An entry whose closure key misses is preprocessed and
looked up under sha256(schema, toolchain id, entry, profile, first-line flags, preprocessed digest); a hit
reuses the SPIR-V and links the closure key; a compile stores both. compile_shaders.py backfills the
preprocessed keys of entries cached before (once: 34 s for all 2962 requests). `--pp-verify N`
(shader_identity.py and compile_shaders.py; merge-green runs N = 4) compiles N random requests from scratch
and fails on any difference. BG3RTX_SHADER_PP=0 turns the level off. The runtime twin (layer/shader_cache.cpp)
stays on the closure key. A real code change in a common header still recompiles everything that reaches
it: that is what section 1 removes.
Measured (merge-green's shader step, `compile_shaders.py`, a comment-only edit of rt_common.hlsli): 0 of 3007
entries compiled (2892 found by preprocessed text) in 35 s; with --pp-verify 4: 4/4 identical, 4.6 min (the
four fresh compiles waited for DXC slots). Before (BG3RTX_SHADER_PP=0): the same edit misses 2892 entries; 120 of them
compiled cold in 810 s today (0.15/s with other agents on the 8 DXC slots), so ~5 h at that load, 40-50 min on
an idle machine.
The prune cap is now at least 1.25x the protected SPIR-V (2.47 GB protected -> 3.0 GB cap; it was 2 GB,
so every prune ran over it), and a row that shares a protected blob (its preprocessed link) is kept.

## 4. Runtime: compile on the user's machine vs ship SPIR-V

- DXC licence: DirectXShaderCompiler is LLVM-based, University of Illinois/NCSA licence: redistributable
  with its notice. package.ps1 already ships dxcompiler.dll (21.7 MB) and warns that
  `external/licences/DirectXShaderCompiler-LICENSE.txt` is missing. dxil.dll is not needed (SPIR-V only).
- Options for the release preset (100 entries):

| Option | Package | First launch |
|---|---|---|
| A today: HLSL + headers + dxcompiler.dll | ~23 MB | each pass compiles on first use (~3 s each, async on worktree-agent-a78f48005e3b350bd); ~100 x 3 s CPU |
| B A + pre-warm in BG3RTX.exe before the game starts | ~23 MB | ~40-60 s with 8 threads, once per DXC/shader version |
| C A + the plain set's SPIR-V as a read-only seed of the runtime cache (keyed with the shipped dll's tool id) | +15 MB raw, +4.7 MB zip | none for the preset; DXC only for experimental keys |
| D every entry's SPIR-V | +3.0 GB raw, 0.78 GB zip | none; rejected |
| E C after section 1 S3 (every key) | +~180 MB raw, ~55 MB zip | none for any key |

  Recommended: C (or B if the seed tool is not worth it). C needs a small host tool that runs the layer's
  key and compile code offline (shader_cache_test already links layer/shader_cache.cpp), and a second,
  read-only lookup directory in layer/shaders.cpp.
- VkPipelineCache: none today. The NVIDIA driver keeps its own disk cache of compiled pipelines (GLCache
  354 MB here), keyed by module and specialization, so a persisted VkPipelineCache mostly duplicates it on
  the only vendor the mod supports (DLSS). A pipeline cache cannot be shipped (driver and GPU specific).
  ~40 lines (create with the saved blob, save at shutdown); low value, skip unless a measurement shows
  repeated driver compiles.

## 5. Package audit (scripts/package.ps1)

| Content | Size | Ship? |
|---|---|---|
| bin/VkLayer_bg3rtx.dll (lean build; the diagnostics build is 6.5 MB) | < 6.5 MB | yes |
| bin/bg3rtx_core.dll, bin/BG3RTX.exe | 2.2 + 1.4 MB | yes |
| bin/dxcompiler.dll | 21.7 MB | yes (NCSA notice) unless only shipped SPIR-V is used |
| bin/sl: nvngx_dlss 59.0, nvngx_dlssd 48.3, nvngx_dlssg 7.5, sl.* + NvLowLatencyVk 2.9 MB | 117.7 MB | yes (NVIDIA redistributables; the bulk of the package) |
| shaders/ + the common headers they include | 1.2 + < 0.7 MB | yes (hot reload, experimental keys) |
| data/assetdb/bg3rtx_assetdb.bin | 7.6 MB (2.0 MB gzipped) | derived from the game's paks: generate on the user's machine (decision below) |
| VT alpha masks (data/assetdb/vt_alpha, 76 textures 81 MB today; all 1174: 1.3-1.7 GB) | 0 shipped | never ship: generate (P4_OMM.md 11.2h) |
| SPIR-V | 0 today | option C above: +4.7 MB zipped |

Generation on the user's machine (never inside the game folder; read-only paks):
- Asset DB: `python -m tools.assets build` takes 40 s on 16 cores with CPython 3.14 alone (own LSPK, LSF,
  LZ4 readers, no LSLib). For players: an embedded CPython (~12 MB zip) or a frozen executable
  (PyInstaller, ~10-15 MB) run by BG3RTX.exe at install and when the paks' fingerprint changes; or a C++
  port of the readers into BG3RTX.exe (LSPK/LSF/LZ4 plus the light/material/probe resolution, ~1-2 weeks).
  Frozen Python first: 1 day, no second implementation to keep in sync.
- VT masks: LSLib 1.20.4 (MIT, needs .NET 8). A self-contained trimmed single-file .NET 8 tool is ~30-70 MB
  (estimate; framework-dependent ~5 MB plus a 30 MB runtime install); a native GTS/GTP reader avoids .NET
  (estimate 1-2 weeks). Masks of the ranked miss list first, the rest at low priority, resumable;
  30-60 min for all (P4_OMM.md 11.2h).

## 6. Other

- CTests do not compile shaders twice: the `*_shader_test.py` gates are not CTests; the CTest run is
  dominated by frame_replay_test 8.8 s and hit_material_generated_test 7.6 s; incremental C++ build 17 s.
- merge-green: build-ctest, then compile_shaders once, through the shared cache with the in-flight lock
  (a warm run: 2962/2962 from the cache). Agents' feature gates compile the same entries into the same cache.
  Nothing is recompiled twice; the cost is the invalidation, which sections 1 and 3 address.
- Cache prune: the cap is 2 GB but the protected keys (working tree + 3 baselines, 5937 keys) hold
  2.67 GB, so every prune runs over cap. Fixed: the cap is now derived (section 3).
- sccache for MSVC needs /Z7 instead of /Zi; with a 17 s incremental build the gain is small. Skip.

## Decisions for the user

- Asset DB: is shipping `bg3rtx_assetdb.bin` (light positions, colours, names, UUIDs derived from the
  game's paks) acceptable for the beta, or must it be generated on the player's machine from day one?
  Conservative default here: generate it (frozen Python tool), like the VT masks.
