# GPU release performance audit

Audit date: 2026-11-02. Branch: `codex-perf-gpu`. Scope: read-only inspection plus this document. No builds, CMake, CTest, DXC, GPU tools, game launches, queue submissions, commits or Git metadata changes. Shader request delta: **0**. This is a proposal, not a performance or quality qualification.

## Decision

Keep Ultra's current rays, coverage, lighting and reconstruction. Prioritize exact RR temporal simplification, shorter live shader state, and the existing OMM lane. The largest measured cost increase is Ultra's far-field GI: approximately **+3.7 ms camp / +3.2 ms S8**. Turning that off is a lower-preset choice, not an Ultra optimization. OMM has a **5.7 ms camp diagnostic ceiling**, not an achieved saving. No evidence supports promising async overlap or SER savings for this renderer yet.

## Evidence and accounting

Log references below are relative to the **MAIN checkout**, `C:/Users/quali/Documents/Code Projects/BG3RTX/`, not this worktree. For example `captures/queue/logs/gf1-camp-fg2.1.033251.log:3489` is an absolute source when prefixed with that directory. Code and doc references are in this worktree. Line numbers describe files as read during this audit; live logs can subsequently grow.

Hardware: user log `bg3rtx.log:89` identifies **RTX 5090**, Vulkan 1.4.357. Reference internal resolution is 2560 x 1067, output 3840 x 1600 (`docs/TRACK_A_RR.md:237`). “Full resolution” means internal render resolution, not output resolution. Do not transplant these costs to native 4K.

Important timer boundaries:

| Log line | Meaning | Accounting rule |
|---|---|---|
| `gi: GPU cost` | Enclosing injected pre/post work, 300-frame mean (`core/gi.cpp:386`) | Not GI trace alone; not total game GPU time. |
| `rt passes: GPU cost` | Origin, GI trace, temporal, AO and sun region (`core/gi.cpp:2747`, `core/gi.cpp:2758`, `core/gi.cpp:2870`) | GI trace is nested. Never call this sun cost or add it to GI trace. |
| `gi: trace pass ... GPU median` | Trace dispatch, 300 samples (`core/gi.cpp:474`) | Includes nested hit lighting/AO/far work; not just BVH traversal. |
| `ls: local-light shadow pass` | Shadow pass including join (`core/gi.cpp:2555`) | Join subfield is already included. |
| `rf: reflection pass` | Reflection dispatch (`core/gi.cpp:2505`) | Hidden hit shading included when enabled. |
| Water compose / local shadows / refraction | Same compose region including lighting copy (`core/water.cpp:831`, `core/water.cpp:855`) | These are overlapping labels, not three independent costs. |
| `[rt] scene build GPU` | Conversion/extraction, BLAS builds/refits, TLAS | Separate AS work; use its own total rather than summing component medians. |
| `[pace] rendered frame time` | Whole rendered cadence | Includes pacing, CPU waits and diagnostic disturbances; not an additive GPU timer. |

Means and medians, and 120/300/600-frame windows, are different statistics. Do not subtract them to invent an unlogged pass cost. Startup, compiling, RT-disabled and diagnostic windows are not a release benchmark. Stable examples below establish scale, not a synchronized four-scene Ultra benchmark.

### Selected log ledger (ms)

| ID / MAIN log | Selected evidence | Configuration / limitation |
|---|---|---|
| C: `captures/queue/logs/gf1-camp-fg2.1.033251.log` | GI OFF far 3.263 (:3187), 3.236 (:3323); ON far 7.000 (:3489), 6.938 (:3563), 6.817 (:3631). LS 1.042 (:3632); reflections 1.186 (:3633). | Same run, bounce on, emission off (:3); far toggles :3398 ON / :3694 OFF. About 3.6-3.8 ms far cost. |
| S: `captures/queue/logs/gf2-s8-fg2.1.031330.log` | GI far OFF 1.099 (:3140); ON 4.382 (:3330), later ON 3.845 (:5178). LS 0.123 (:3331); reflections 0.574 (:3332). | Bounce on, emission off (:3). Far ON :3180, OFF :3597. :3330 p90 23.090 and :5179 LS 6.478 show disturbance: do not treat these as a clean total. Docs report +3.2 ms. |
| U: `captures/queue/logs/su1-ultra-s8-fg2.1.041708.log` | GI 4.079 (:6751; p10 4.062, p90 4.100); LS 0.211 (:6752); reflections 0.595 (:6753); enclosing RT 4.701 (:6750). Water reflection 0.122 (:6812), compose 0.421 (:6822), build 0.280 (:6795). | Far, bounce, hidden emission and full water enabled (:6348). **Box 2 is also enabled**, unlike current Ultra. Emissive check 0.012 ms/run (:6747). Useful near-Ultra sample, not exact shipping preset. |
| I: `captures/queue/logs/m4p2-inn-fg2.1.032112.log` | GI 1.388 (:4411), LS 0.745 (:4412), reflections 0.684 (:4413). Later reduced-config GI 0.457 (:6912), LS 0.748 (:6913), reflections 0.695 (:6914). | :4326 has instanced materials, direct/sun hit lighting and emission ON, far OFF. :4411 has high p90 14.490. :6907 has direct/sun and emission OFF. Do not label 0.457 current Ultra. |
| F: `captures/queue/logs/bx1-box-s6-fg2.1.151641.log` | GI 0.321 (:3149), LS 0.725 (:3150), reflections 0.427 (:3151); enclosing RT 0.629 (:3148). | Legacy box test, box OFF here; excludes present Ultra hidden shading/far/emission. Animated lava and LS tails make this a historical scale only. |
| WC: `captures/queue/logs/m5p3-camp-fg2.1.214813.log` | Water reflection 0.095 (:6433), compose 0.449 (:6426), AS build 0.139 (:6476). | Promotion-run water ON windows; final config later switches water OFF (:6907). |
| WI: log I | Water reflection 0.066 (:6976), compose 0.313 (:6986), AS build 0.277 (:7002). | Local and transmissive records **0** (:6989-6990): proves overhead, not useful inn refraction throughput. |
| WS: `captures/queue/logs/m5p1-s8-fg2.1.213849.log` | Water reflection 0.177 (:8063), compose 0.395 (:8076). | Different view/window from U; do not compare across runs as an A/B. |

### Per-pass scene coverage

`NR` means no separately attributable measurement was found; it never means zero. Values are selected ledger samples or explicitly marked documented deltas. Exact current-Ultra costs at all four saves are **not available** in this evidence set.

| Pass / part | Camp | S8 | Inn (S7) | S6 forge |
|---|---:|---:|---:|---:|
| Sun shadow alone | NR | NR | NR | NR |
| GI trace incl. hit work | 6.817-7.000 far ON (C) | 4.079 near-Ultra (U) | 1.388 bounce/emission ON, far OFF; tails (I) | 0.321 legacy (F); current Ultra NR |
| Base GI without far | 3.236-3.263 (C) | 1.099 (S) | 0.457 reduced config (I) | 0.321 legacy (F) |
| GI material gather / hit direct / hit sun / hit AO individually | NR (inside trace) | NR | NR | NR |
| Temporal / AO apply / origin individually | NR | NR | NR | NR |
| Primary AO ray cost | Shared GI rays | Shared GI rays | Shared GI rays | Shared GI rays |
| RR guides + evaluate | 0.001 + 2.499 (C:4111) | 0.001 + 2.404 (S:5660) | 0.001 + 2.443 (I:7010) | 0.001 + 3.193 (F:5538; variable) |
| Local shadows incl. join | 1.042 (C) | 0.123 (S); 0.211 box ON (U) | 0.745 (I) | 0.725 legacy (F) |
| Box shadows incremental | NR | NR | +0.20 documented | +0.32 documented |
| Reflections, ordinary | 1.186 (C) | 0.574 (S), 0.595 (U) | 0.684 (I) | 0.427 legacy (F) |
| Hidden reflection shading incremental | NR per scene | +1.4-1.8 documented range across tests | NR | +1.4-1.8 documented range across tests |
| Water reflection / primary | 0.095 (WC) | 0.122 (U), 0.177 (WS) | 0.066 (WI; no composed records) | NR |
| Water compose incl. sun, local and refraction | 0.449 (WC) | 0.421 (U) | 0.313 (WI; no composed records) | NR |
| Water local / sun / refraction separately | NR, overlapping compose | NR, overlapping compose | NR, no records | NR |
| Water AS build | 0.139 (WC) | 0.280 (U) | 0.277 (WI) | NR |
| Hidden emissive hit contribution | NR | NR | Included in 1.388 trace | +0.08 trace documented |
| Emissive calibration check | NR | 0.012/run (U), sliced | NR | max 0.026/run documented |
| Emissive AS/capture alone | NR | NR | NR | NR |
| gi_far incremental | about +3.7 | about +3.2 | NR | NR |

Documented deltas: `docs/PROGRESS_CODEX.md:53-54` (emission/far), `:85` (water costs), `:99` (hidden reflections), `:109-111` (box), `:112` (emissive check). Promotion water summary gives reflection 0.113, compose 0.098, refraction 0.24 and LS 0.18; these are historical feature comparisons, **not four disjoint timestamps**. Current near-Ultra compose includes their work in one region. There is no honest way to fill missing sun/GI subpart cells with precise ms from these logs.

### Scene build, waiting and user-log cross-check

| Scene / source | Conversion | BLAS | TLAS | Timed total |
|---|---:|---:|---:|---:|
| Camp C:4107, static | 1.381 | 0.537 (p99 1.469) | 0.030 | 1.949, p99 3.007 |
| S8 S:5649, static | 0.616 | 0.383 (p99 3.619) | 0.033 | 1.035, p99 4.407 |
| Inn I:6980, static | 3.444 | 0.685 (p99 2.256) | 0.164 | 4.301, p99 5.890 |
| S6 F:5553, after restore | 0.164 | 0.086 (p99 0.531) | 0.027 | 0.278, p99 0.834 |

These are workload-dependent windows, not four identical-preset measurements. Inn's conversion can dominate BLAS; “refit everything” does not remove capture/material extraction. Legacy camp WC:6950 has BLAS 2.580 / total 3.516; C has 0.537 / 1.949. That cross-run difference is **not** an achieved refit saving.

CPU scene waiting is separate: C:3708 reports mean 5.648 ms, max 23.795, zero timeouts; C:3709 permits traced frames up to 48 ms. Never add that wait to GPU timestamps as if serial GPU work. The user's ongoing log has GI trace 8.184, reflections 3.059 and enclosing GI 13.967 ms (`bg3rtx.log:9488-9495`), but 27/300 scenes are stale (:9487) and view/settings are uncontrolled. It confirms variability, not a fifth benchmark.

`[watch]` counts indicate CPU submission pressure, not GPU ms: C:3873 records 294 rendered presents, 588 displayed, 1,176 submits, 1,307 descriptor writes, 1,633,481 draws. Normalize by rendered presents (about 4 submits, 4.45 writes, 5,556 draws/frame); FG display count is not a throughput denominator. C:4071 rendered median is 17.09 ms with max 1,776 ms; I:6862 is 16.62 ms; F:5432 is 10.17 ms with max 2,410 ms. None establishes a clean uncapped release fps comparison. Progress reports smoke median 16.65 vs 13.75 before far/emission (`docs/PROGRESS_CODEX.md:56`), also not a pass timer.

## Shader cost map

| Pass | Main drivers and existing optimizations | Concrete direction |
|---|---|---|
| Sun | One disc-sampled visibility ray per eligible pixel, transmission/backlighting (`shaders/gi/rt_shadow.hlsl:92-130`). Alpha candidates cause UV/texture work and divergence; visibility already terminates on first accepted hit (`shaders/rt/rt_common.hlsli:955`). | Exact alpha/OMM, avoid redundant receiver loads; retain transmission and roof coverage. No Ultra ray reduction. |
| GI trace | `pc.rays` cosine loop (`shaders/gi/gi_trace_rt.hlsl:210`); Ultra 2, Medium/High 1. Full internal resolution under RR. Closest-hit, reprojection/depth qualification, material joins, local/sun visibility, AO and emission branch by hit class. | Reduce live state and redundant fetches; lower-preset spatial budget only. `gi_steps` controls screen-space marching, not RT BVH steps. |
| gi_far | Radius grows from 1 to 6 m (`shaders/gi/gi_trace_rt.hlsl:180-193`); more traversal, offscreen material hits, sun tests and roof-volume loop (`:118-124`, `:409-423`). | Preserve Ultra radius and qualified fallback. Exact roof candidate culling can reduce tests only if bounds prove the same answer. |
| Temporal / denoise inputs | RR sets history weight 1 (`core/gi.cpp:1883-1885`), yet temporal reads history, depth and MV and validates (`shaders/gi/gi_temporal.hlsl:32-65`; dispatch `core/gi.cpp:2804`). RR guides read full-resolution material/depth/normal/albedo (`shaders/rr_guides.hlsl:3-9`, `:52-88`). | Exact RR current-sample copy/alias path; preserve finite fallback, depth outputs, bindings and resource lifetime. RR evaluate itself is about 2.4-3.2 ms here; this proposal does not remove it. |
| AO | Primary AO is already `1-occ/n` from GI rays (`shaders/gi/gi_trace_rt.hlsl:471-473`); full-res apply (`core/gi.cpp:2807-2811`). Hidden-hit AO adds 2 default cosine rays, cap 8 (`shaders/rt/native_hit_direct.hlsli:317-342`, `common/settings.h:681`). | Do not propose a second saving for “sharing GI and AO.” Lower `hit_ao_rays` only below Ultra and only if hit shading is enabled. |
| Local / box shadows | Full walks eligible light lists; Fast samples one (`common/settings.h:164-174`). Native matching, shadow-map ownership, BRDF tests, projector loops and per-light visibility. Already split/reloaded to reduce registers: 96 versus former 252 (`shaders/gi/rt_local_shadows.hlsl:41-42`, `:376-384`); box loop `:879`. | Exact zero-contribution/domain rejection before traversal and cache invariant metadata. Do not repeat implemented register split or promote rejected exact-k. |
| Reflections / hit shading | One GGX VNDF ray per eligible pixel (`shaders/gi/rt_reflections.hlsl:218-274`), roughness/model early eligibility already exists. Hit shading (`:315-376`) adds material/light/AO/sun work and scattered memory. | Keep roughness 0.8 in Ultra. Compact live hit state, specialize unused hit features; lower resolution/roughness only below Ultra. Hidden reflection shading is currently off in Ultra. |
| Water | Opaque water TLAS classification (`shaders/rt/water_reflect.hlsl:123-125`), neighbors for normals (`:169-220`), scene reflection ray. Compose records/images, sun ray (`shaders/rt/water_compose.hlsl:471-489`), local list/rays (`:571-592`), refraction (`:611-635`). | Exact ownership tile rejection/dispatch bounds; avoid dispatching empty water work while preserving copy/forward semantics. Substantial traffic even in WI's zero-record window. |
| Emissive | Hidden material emission joins inside GI/reflections; separate capture/AS and calibration. Per-group light selection lacks prefix CDF (`docs/TRACK_D_EMISSIVE.md:999-1003`). Device-local reductions and sliced check already fixed the old host-atomic spike (`:1011-1030`). | Reuse immutable selection data only with identical distribution; do not sell the already-fixed ~140 ms check frame as a new saving. |
| Shared alpha / AS | `Proceed` loops (`shaders/rt/rt_common.hlsli:979`, `:1269`) perform geometry/UV reconstruction, plain or VT indirection/cache texture reads (`:599-689`). Closest-hit state includes a float3x4 transform (`:1215`). | Conservative known-state alpha, smaller live state, correct static/dynamic AS update policy. Candidate limits, stochastic thresholds and UNKNOWN semantics are quality constraints. |

## Ranked optimization proposals

Scores use **expected saved ms x confidence / effort units**, with 1 unit about one focused implementation/verification increment. Confidence is subjective, not a statistical probability. Expected savings are planning hypotheses derived from logged budgets; every unmeasured candidate has a **0 ms guaranteed saving**. Ranges overlap and must not be summed. Ranking includes both scopes but clearly labels quality scaling. New keys named below are **proposed**, not implemented. Feature keys use specialization constants where possible; no new define-product expansion (`docs/BUILD_AND_PACKAGE_EFFICIENCY.md:54-94`).

| Rank / scope | Candidate, evidence and estimate on 5090 | Score inputs | Measurement / A/B job |
|---|---|---|---|
| 1 / lower presets | Keep far/bounce/emission off below Ultra; preserve existing settings levels. Far alone avoids about 3.7 camp / 3.2 S8 from C/S. Already current policy; **0 additional release saving** unless regressed. | 3.2 x .95 / 1 = 3.04, policy protection | Extend `gf1-camp` / `gf2-s8` same-run OFF/ON/OFF; `gi: trace pass`, float energy. Not an Ultra proposal. |
| 2 / lower presets | Half width+height GI and reflections, 1 spp. Proposed `rt_trace_scale=0.5`; relative to current High's full internal grid expect 0.4-1.3 ms in camp/S8, not 75% of the whole frame. Full roughness-eligible lighting and reconstruction overhead remain. | .8 x .65 / 3 = .17 | Proposed `perf_scale_high` at camp/S8/inn/S6; trace + reflection medians and RR input validity/energy; compare existing High against scaled High. |
| 3 / Ultra exact | RR temporal fast path (history weight 1). Plan 0.03-0.15 ms; less than the unseparated enclosing non-trace work, **not directly measured**. Proposed `gi_rr_current_fast`. | .08 x .75 / 1 = .06 | Proposed `perf_rr_current`, derived from `su1` + camp/inn: `gi: GPU cost` improves while `gi: trace pass` stays flat; exact replay output/NaN semantics first. |
| 4 / Ultra exact | Shorten live hit/material state, defer transform fetches and reuse immutable light metadata. Plan 5-10% of camp trace 6.9 plus reflection 1.2 = 0.4-0.8 ms; not a measured bottleneck attribution. Proposed `rt_compact_hit_state`. | .6 x .55 / 6 = .055 | Proposed `perf_hit_state` using `m4_promotion` scenes; trace and reflection median/p90, exact identity/precision gate. Driver registers/spills later, no GPU profiling run in this audit. |
| 5 / Ultra only if exact | Existing OMM lane: plan 0.5-2.0 ms camp **if** useful known-state coverage is proven. Ceiling from forced opaque: 4.2 RT + .62 LS + .77 RF = 5.59 ms (~5.7 rounded in design). Keys `rt_omm`, `rt_vt_alpha_masks`; no new parallel OMM framework. | 1.25 x .35 / 8 = .055 | Existing `omm_qual` / om4 after owner completes offline gates; RT enclosing, LS, RF, build GPU, bake/residency and candidate classification. Validate every ON window, matching native alpha. |
| 6 / Ultra exact | Cache proven immutable alpha metadata / constant ranges, retain ray-dependent LOD and threshold. Plan 2-5% of 5.7 ms alpha diagnostic ceiling = 0.1-0.3 ms camp; ceiling is not a separable cost model. Proposed `rt_alpha_metadata_cache`. | .2 x .6 / 3 = .04 | Proposed `perf_alpha_exact`, camp + S8 foliage; same rays/native alpha comparison, trace/LS/RF and total. OMM and this row target overlapping work. |
| 7 / Ultra exact | Skip provably empty water tiles; preserve untouched native color and forwarding. WI has 0 useful records but 0.313 ms compose; saving 25-50% of that = 0.08-0.16 ms in empty views, about 0-0.1 in populated S8. Proposed `rt_water_empty_tiles`. | .1 x .65 / 3 = .022 | Proposed `perf_water_empty` from `m5_promotion`, include inn zero-record window; water compose/reflection lines, stability and energy, copy/ownership proof. |
| 8 / Ultra exact | Improve dynamic/cascade refit eligibility only for compatible geometry. Already enabled `rt_refit`, `rt_tlas_refit`, `rt_cascade_refit`; plan 10-30% of observed .38-.69 ms BLAS = .04-.2 ms typical, mainly tail wins. **Do not claim legacy 2 ms saving again.** | .12 x .5 / 4 = .015 | Proposed `perf_scene_refit`, based on camp/inn rotation jobs; `[rt] scene build GPU`, rebuild/refit counts, scene wait; also trace costs because refits can degrade traversal. |
| 9 / Ultra experiment | SER hit objects: illustrative 5-15% of 6.9 ms camp trace = .35-1.0 ms, before added dispatch/state overhead; no BG3 evidence. Proposed `rt_ser`. Major pipeline change. | .65 x .25 / 16 = .010 | Proposed `perf_ser_camp`, exact same rays/material outputs and scene, GI/RF lines, total GPU critical path, supported-device fallback. |
| 10 / Ultra experiment | Async overlap: **0 committed ms**, exploratory ceiling below 0.5 ms if independent work and idle capacity exist. Scene build totals of .28-4.3 ms are not all overlap opportunities. Proposed `rt_async`. | 0 committed; defer | Proposed `perf_async_scene` from rotation/smoke jobs; scene-build and end-to-end GPU critical path/queue timeline, scene age, no stale-scene substitution. |
| 11 / Ultra experiment | Share identical visibility requests only; primary GI/AO already shared. Sun/local/GI/reflection directions and origins differ. **0 evidence-based saved ms**; dedup traffic may lose. Proposed `rt_visibility_reuse`. | 0; defer | Proposed `perf_visibility_reuse`: same-ray identity/footprint/domain proof, trace/LS/RF medians and sum of non-overlapping regions; reject if duplicates do not repay lookup. |

For sun/local lights, exact zero BRDF or zero transmission rejection must happen before expensive traversal only when the existing result is provably zero; arbitrary luminance cutoffs lose energy. No independent sun timing exists here, so no stand-alone sun saving is promised. For emissive sampling, a prefix CDF can preserve the distribution but changes work order/random mapping; estimate 0-0.08 ms from the documented +0.08 ms hit contribution, with capture/build costs separate. Low priority pending proof of lookup cost; measure `m4p5` / `em_forge` OFF/ON/OFF trace, RF, `[em] check GPU cost` and `[em] BLAS` counts. Calibration is not disabled to manufacture a win.

### SER availability and implementation boundary

SER is available on RTX 40/50-class hardware, but device support must be queried; a GPU name alone is not a feature check. Vulkan has `VK_NV_ray_tracing_invocation_reorder`, promoted to `VK_EXT_ray_tracing_invocation_reorder`. Query feature support and the reordering property; NONE can mean hit-object functionality without actual reorder. The NV extension depends on `VK_KHR_ray_tracing_pipeline`; its HLSL mapping uses SPIR-V intrinsics. See [Khronos NV extension reference](https://docs.vulkan.org/refpages/latest/refpages/source/VK_NV_ray_tracing_invocation_reorder.html) and [EXT proposal](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_ray_tracing_invocation_reorder.html).

BG3RTX currently uses compute `RayQuery`/`TraceRayInline` (`shaders/rt/rt_common.hlsli:955-979`), not a raygen/closest-hit SBT pipeline. SER therefore needs a deliberate raygen/hit-object backend, device setup and shader-toolchain verification. EXT hit objects can retain query-style manual material dispatch, but that does not make reorder an ordinary compute-shader operation. [Khronos's sample](https://docs.vulkan.org/samples/latest/samples/extensions/ray_tracing_invocation_reorder/README.html) uses Slang and warns about compiler support. No toolchain replacement is proposed as part of release work.

[NVIDIA's live-state study](https://developer.nvidia.com/blog/path-tracing-optimization-in-indiana-jones-shader-execution-reordering-and-live-state-reductions/) reports a 24% TraceMain gain on RTX 5080 in another engine after reducing live state. That is feasibility evidence, not a BG3 forecast. Begin with state lifetime in existing compute shaders. DXC already uses `-O3` (`layer/shaders.cpp:292-305`); adding optimization flags is not a missing optimization. The spec-constant prototype already reports 168 registers and 7,936 B shared memory (`docs/BUILD_AND_PACKAGE_EFFICIENCY.md:110`), which suggests inspection but does not prove spills. Preserve float geometry/alpha precision; blanket fp16 conversion is not Ultra-safe.

### OMM quality boundary

`docs/P4_OMM.md:399-407` gives the forced-opaque ceiling and the VT coverage bottleneck. Masks alone were cost-neutral (`:525-530`). Candidate coverage of 97-98% (`:520-522`) does **not** equal known micromap-state coverage. Hardware UNKNOWN must run the original predicate, with fallback scene/content-epoch guarantees.

The current runtime predicate depends on ray cone, transform, mip, cutoff and per-frame stochastic threshold (`docs/P4_OMM.md:411-423`). Option B `rt_alpha_fine_lod` fixes mip 0 and hard cutoff (`:496-500`), **changing the predicate**. It cannot be advertised as quality-neutral Ultra acceleration without independent equivalence/visible-quality qualification. The exact footprint-gated option A (`:417-432`) may preserve quality but its admitted rays can be tiny. Do not enable masks, force opaque, clamp LOD, lower candidate caps or shorten rays in Ultra solely to meet a performance target. Continue the existing owner lane and report net bake/build/traversal/residency cost.

### Async and BLAS boundary

Pre-light GI depends on current origin/depth/material inputs, then feeds temporal/AO and native lighting (`core/gi.cpp:2750-2766`, `:2791-2811`). Post-light LS/reflections read and modify native lighting resources. Injection is into game command buffers (`docs/ARCHITECTURE.md:58-59`); a separate queue needs resource transitions, ownership where required, semaphores and same-frame lifetimes. Additional queues do not create idle SM/RT capacity. First candidate is independent AS/capture work with a proven fence and current-frame dependency; never overlap by accepting stale geometry.

Static BLAS caching and refits already exist; update modes are explicit (`layer/bvhsplit.cpp:1874`, `:1888`, `:1986`; flags `:1261-1264`). Historical refit .39 vs rebuild 2.3-2.5 ms (`docs/TRACK_D_SCENE_BUILD.md:464-481`) is already realized design history. Existing split build costs and camera-motion results (`:706-718`) should guide work. Changed primitive counts/topology and periodic rebuilds still require rebuild. The historical `PREFER_FAST_TRACE` experiment increased build/refit cost without a demonstrated trace gain (`:474-477`). Optimize **build + subsequent trace**, not build time alone. Emissive refit is also already implemented (`docs/TRACK_D_EMISSIVE.md:740-746`, `:789-796`).

## Current and proposed preset budgets

Current preset truth is `common/settings.h`, not UI tooltip cost estimates. Order is Low / Medium / High / Ultra (`:124-196`, `:249-300`).

| Current knob | Low | Medium | High | Ultra |
|---|---|---|---|---|
| RR | Off | On | On | On |
| Geometry | Off | Opaque | Alpha-tested | Alpha-tested |
| Sun / roof casters | Contact / off | RT / off | RT / off | RT / on |
| GI | Screen-space 2 rays, 12 steps | RT 1 spp | RT 1 spp | RT 2 spp |
| Local / box | Off / off | Fast, 1 sampled light / off | Full / off | Full / off |
| Reflections | Off | roughness <= .4 | <= .6 | <= .8 |
| Water, bounce, hidden emission, far | Off | Off | Off | Full refraction; bounce; hidden emission; 6 m far |

Proposed release **scaling experiments**, with Ultra unchanged. These do not change settings in this audit:

| Budget / controlling knob | Low | Medium | High | Ultra |
|---|---|---|---|---|
| Trace grid relative to internal width/height | SS half width/height | RT .5 each axis | RT 1.0 each axis | RT 1.0 each axis |
| Grid control | Existing non-RR half-res | Proposed `rt_trace_scale=.5` | Proposed `rt_trace_scale=1` | `rt_trace_scale=1` locked by preset |
| GI spp / step control | `gi_rays=2`, `gi_steps=12` | `gi_rr_rays=1` | `gi_rr_rays=1` | `gi_rr_rays=2` |
| GI rays per internal pixel | .5 SS rays, up to 12 depth steps each | .25 RT ray | 1 RT ray | 2 RT rays, plus nested qualified-hit rays |
| Reflection primary budget | 0 | <= .25 ray/internal pixel, .4 roughness | <= 1 ray/internal pixel, .6 | <= 1 ray/internal pixel, .8 |
| Sun rays / local policy | Contact, no scene rays | Full-grid 1 eligible sun ray; Fast LS | Full-grid sun; Full LS | Full-grid sun+roofs; Full LS |
| AO | Shared SS GI; own temporal | Shared GI; RR with dense guides | Shared GI; RR | Shared GI + 2 hit AO rays where eligible |
| Features | Current Low | Current Medium with spatial scaling | Current High | Preserve full current Ultra above |
| Optional further floor | None | `rt_trace_scale=.25` experiment only: 1/16 pixels, **not shipped recommendation** | `.5` experiment as rank 2 | None |

“Half width/height” is quarter pixel count; quarter width/height is one-sixteenth. Checkerboard half pixel count is a different budget and is not modeled here. Lower-preset grain, thin geometry and disocclusion can worsen. Never silently apply spatial scaling to Ultra.

RR already denoises/upscales dense internal render inputs; it does not automatically repair an independently undersized GI/reflection attachment. Reconstruct low-grid radiance/visibility into full internal lighting, keep depth/normal/MV/hit-distance registration, preserve sample expectation and native fallbacks before NGX. Guide bindings are in `shaders/rr_guides.hlsl:27-40`; current GI allocation chooses full resolution with RR (`core/gi.cpp:327-334`, `:548-552`). A global lower DLSS input size would also reduce rasterization and guides, but is a different experiment from this per-pass budget. Low has RR off; do not assume RR for Low's upscale.

### Expected incremental GPU cost, not rendered frame time

The following are **planning ranges**, not measured preset runs. Scope is BG3RTX injected lighting/scene work **plus RR evaluate**, excluding native game raster, FG, pacing, streaming and unmeasured post work. No fps promise follows. Same reference resolution on both GPUs; a 4070 at 1080p is a different workload.

Model: `G + L + R + W + B + O + RR`, using non-overlapping trace/local/reflection/water/build regions only. Camp Ultra anchor: G ~6.9, L ~1.1, R ~1.2, W .6-.8, B ~2, RR ~2.5, O assumed .3-1.2 => roughly 14-16 ms. S8: G ~4.1, L .1-.2, R .6, W .8, B ~1, RR ~2.5, O .3-1.2 => roughly 9.5-11.5 ms. These are synthesized budgets from different windows, **not measured sums**. O covers unseparated origin/temporal/AO/post work; its uncertainty is explicit. Inn/S6 current-Ultra far/hit costs are missing, so their ranges are intentionally broad.

For High/Medium, assume removing hidden/far/water reduces GI to roughly .3-1.2 ms at 1 spp, local .1-1.2, reflections .2-1.2, AS .3-2 (inn can exceed this), RR 2.4-3.2 and O .3-1.2. Medium additionally scales GI/reflection pixel count by .25 but retains fixed setup/reconstruction costs. Low uses no RT scene/RR; historical SS trace is .18 ms at 2 rays/12 steps (`docs/TRACK_C_GI.md:60`), plus uncertain accumulation/composite overhead.

| Proposed preset | RTX 5090 camp / S8 / inn / S6 (ms) | 4070-class guess, same scenes (ms) | Main uncertainty |
|---|---|---|---|
| Low | .3-1 / .3-1 / .3-1 / .3-1 | .8-4 / .8-4 / .8-4 / .8-4 | Own temporal/post budget not separately timed. |
| Medium (.5 grid) | 4-7 / 3.5-6 / 4-9 / 3.5-7 | 10-28 / 9-24 / 10-36 / 9-28 | Reconstruction overhead; opaque-only capture; LS sampling. |
| High (full grid) | 5-9 / 4-7 / 5-11 / 4-8 | 13-36 / 10-28 / 13-44 / 10-32 | Alpha and inn extraction; no far or hidden-hit bundle. |
| Ultra (unchanged) | 14-16 / 9.5-11.5 / 8-16 / 7-15 | 35-64 / 24-46 / 20-64 / 18-60 | Inn/S6 are extrapolations; far/material scene coverage, memory pressure. |

4070-class numbers use a deliberately rough **2.5-4x** multiplier on these mixed workloads, not a measured architecture ratio or a spec-derived guarantee. There are no 4070 logs in this audit. RT throughput, memory bandwidth/capacity, occupancy and RR scaling differ; residency cliffs can exceed the range. Do not infer 5090-to-4070 performance from CUDA core count or assume FG reduces any listed pass time. Exact Ultra optimizations are not credited in this table until qualified.

## Proposed verification contract (not executed or queued)

Reuse existing serial queue and saves: camp S3 foliage/pond, S8 Water Queens House, inn S7 rotation/interior, S6 Adamantine Forge lava. Use named jobs above as extensions of existing batches, not a new harness. No jobs were placed in watched pending folders. A future live batch requires explicit user steering after candidate-dependent offline checks.

For each candidate, one named hypothesis and predicted delta, then same-run OFF/ON/OFF static and moving windows. Shader cache warm, no builds/compiles during cost windows, diagnostics off there. **300 samples** are needed for trace/LS/RF median lines; use enough frames to keep complete 300-frame timer windows within each state, plus settle, rather than allowing accumulators to span toggles. Existing 120-frame stats windows alone are insufficient for these cost timers. Add short separate diagnostic windows with hold >= 90 where needed. Keep each scene about 2-5 minutes if available frame rate permits; otherwise reduce the candidate count per session.

Every job needs `patterns`, `expect`, and `expect_every` per ON window; require fresh-scene and capacity gates, no variant skips, calibration validity, water compose stability. Preserve Ultra mean pre-post scene energy using float `dumps.json`, compare OFF repeats for a noise-derived band, and test thin foliage/hair, disocclusion and water pan stability. Exact candidates should pass offline output/alpha equivalence first. Energy alone does not prove visibility or temporal quality. Use existing PNG pairs/debug views for detail, not PNG mean as energy.

After isolated validation, `smoke_ultra` with every qualified key together is mandatory. Measure actual current preset, including whether box remains off. Lower preset tests compare like-for-like knob/resolution states and document quality loss explicitly; Ultra never takes that trade. For asynchronous proposals, an end-to-end critical-path timing is necessary: shorter individual dispatches or CPU waits alone do not establish saved frame time. All proposals remain **UNMEASURED**, with OMM's existing lane awaiting its own qualification; no release performance pass is claimed here.

## Changes and unresolved evidence

- Added only `docs/PERF_AUDIT_GPU.md`; no commit, no settings/source changes, no shader variants.
- Missing: synchronized current-Ultra camp/inn/S6 pass receipts, separate sun/temporal/origin/AO and GI-subpart timers, exact water incremental component timings, actual 4070 measurements, SER feature/toolchain verification and async queue critical path.
- Existing release config needs coordinator review: `docs/PROGRESS_CODEX.md:127` notes `release.ini` forcing emissive off despite Ultra's setting. This audit does not change it or count its removal as a speedup.
