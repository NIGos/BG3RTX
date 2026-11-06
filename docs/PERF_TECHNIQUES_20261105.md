# Exact GPU techniques — 2026-11-05

Read-only source analysis, branch techscan. Only this report and its progress fragment were written.
No build, compiler, GPU replay, game, queue, Git mutation or qualification was performed. Shader request delta: 0.
Read the handoff, progress, architecture, GPU audit, optimization outcomes and scene-build design.
All source references below are checkout-relative. Log references prefixed MAIN mean `C:/Users/quali/Documents/Code Projects/BG3RTX/`.

## Decision and ranking

Start with a cheap, bijective lane/tile permutation of the existing GI kernel. Keep each pixel's two rays serial.
Software hidden-hit shading has the largest plausible upside, but needs ordered replay of additions, not atomic summation.
A camera-centred 6 m TLAS is incorrect: GI rays start on visible surfaces, potentially much farther from the camera.
There is no demonstrated exact multi-ms saving remaining in these sources. Every estimate below is unmeasured.

RTX 5090 Ultra input budgets (today's user medians): GI trace inn 12.8 / S8 8.0 ms; origin 1.3–1.5;
sun 0.5–1.4; reflections 1.1–2.2; local shadows 0.6–2.0; scene 1.2–2.4; RR about 2.5 ms.
Ranges are **net potential saved GPU ms/frame**, include zero, and are not additive. Regression remains possible.
Confidence C is subjective chance of a useful exact win; effort E is focused implementation plus proof increments, not days.
Score = midpoint of range × C / E. This is a prioritization heuristic, not a measured forecast.

| Rank | Candidate | Net ms | C | E | Score |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 | GI lane/tile permutation, no work queue | 0–0.40 | .60 | 2 | .060 |
| 2 | Auxiliary AO R16F | 0–0.06 | .90 | 1 | .027 |
| 3 | Hidden-hit wavefront shading, ordered final reduction | 0–1.50 | .35 | 10 | .026 |
| 4 | Conservative GI-only reduced TLAS | 0–0.70 | .30 | 8 | .013 |
| 5 | Same-frame async AS build | 0–0.40 | .20 | 14 | .003 |
| 6 | Batch tiny mesh extraction jobs, only during churn | 0–0.10 | .30 | 6 | .0025 |
| 7 | Fuse origin with sun, retain origin publication | 0–0.10 | .20 | 10 | .001 |

Completed/rejected work is excluded: compact hit state/holds, 243→239 register trims, reflection probe state,
SHaRC, roof reject, lazy origin normal, empty water tiles, alpha metadata cache, OMM, half-resolution tracing.
Also do not reopen temporal current-sample, RF/LS early-outs or cascade reuse: outcomes in
`docs/OPTIMIZATION_20261105.md:5–18` and `docs/TRACK_D_SCENE_BUILD.md:1194–1199` supersede historical proposals.

## 1. GI-only near-field TLAS

- Mechanism: reuse existing BLAS addresses, create an additional TLAS containing all instances intersecting the union
  of GI-origin balls of radius R. R is `max(pc.radius, pc.gi_rt_radius)`, not necessarily hardcoded 6
  (`shaders/gi/gi_trace_rt.hlsl:197–224`). Origins use reconstructed depth plus geometric offset (`:189–196`, `:241–245`).
- A conservative enclosing box of **all current visible world-space origins**, expanded by R and a derived offset/bounds
  rounding bound, suffices; retain every instance whose world AABB intersects it. A camera sphere needs radius
  `max |origin-camera| + R + margin`, not R. Frustum-depth bounds must include peripheral rays, not just view-z.
  Test AABB intersection, never require whole-instance containment; retain unknown/nonfinite bounds and legacy soup.
- Build today: cached object-space meshes plus one camera-relative BLAS per dynamic draw, plus mask-0x02 cascades
  (`layer/bvhsplit.cpp:2844–2888`, `:2906–2929`, `:2981–2982`). TLAS BUILD/UPDATE already exists with list signature
  and periodic rebuild (`:3458–3497`); legacy fallback has one soup instance (`layer/rtscene.cpp:1283–1338`).
- GI closest rays use mask 0x01 with cascades (`shaders/rt/rt_common.hlsli:162–197`, `:1497–1502`).
  Those far objects are already spatially rejected by the full TLAS and ray TMax: fewer instances is not proportional saving.
  Keep the **full** TLAS for origin, sun and nested hit lighting/AO. Sun visibility can see 0x02 roof casters;
  replacing shared gScene globally would silently shorten secondary visibility domains.
- Missing infrastructure: Mesh has no ordinary conservative AABB (`layer/bvhsplit.cpp:397–415`); matrices are fixed up
  at submit (`:2831–2845`; `docs/TRACK_D_SCENE_BUILD.md:591–596`). Dynamic bounds need current GPU vertices;
  earlier-frame hair diagnostic bounds (`layer/bvhsplit.cpp:1081–1107`) cannot authorize culling current geometry.
  Avoid CPU depth readback: GPU origin bounds arrive after the present scene-build recording point.
- Identity trap: alpha and shading use **InstanceIndex**, not custom index (`shaders/rt/rt_common.hlsli:918`, `:1565`).
  Preserve original slots with masked exclusions initially, or remap every candidate/committed lookup and associated
  material/OMM identity. Copying instanceCustomIndex alone does not preserve the contract. Keep OMM and reference twins coherent.
- Exactness is conditional beyond coverage: changed BVH order can change capped-query abort/fallback and equal-distance
  winners (`shaders/rt/rt_common.hlsli:1504–1541`; GI fallback `shaders/gi/gi_trace_rt.hlsl:252–257`, `:570–588`).
  A retained-hit coverage proof does not prove identical truncated traversal. Do not remove the cap to manufacture equivalence.
- Population evidence: MAIN `captures/queue/logs/opt-profile-inn.1.005111.log:7135` reports 900 static instances plus
  605.4 captured draws/build; S8 `opt-profile-s8.1.065142.log:3944` reports 620 plus 224. These are rounded window means,
  not exact TLAS counts or distance histograms; optional cascade adds another instance. No retained fraction is established.
- Estimate 0–0.70 ms net: hypothesize roughly 1–8% of 8–12.8 ms trace gross, subtract about 0.1–0.4 ms for bounds/filter/build.
  Existing inn TLAS alone is about 0.325 ms (`docs/TRACK_D_SCENE_BUILD.md:1202–1203`); a second TLAS is not free.
  Effort 8; high risk of missing geometry, mapping errors, abort changes and little root-level traversal benefit.
- Cheapest offline proof: first scan existing replay instance/vertex/depth data for conservative bounds and retained fractions,
  including large straddling meshes and offset origins. Stop if little is removed. Then paired full/reduced queries on one device:
  compare t, instance/primitive, transform, abort and alpha decisions, followed by exact stored GI outputs. Include cap/tie fixtures.
  `tools/frame_replay.md:26–43` supports ordered instance reconstruction, but explicitly masks unsupported legacy/hair cases;
  such packs cannot certify those cases or production OMM without extending the existing replay contract.

## 2. Software wavefront for hidden hits

- Mechanism here: split after closest hit and screen qualification (`shaders/gi/gi_trace_rt.hlsl:241–320`, `:415–429`).
  Emit eligible hidden-hit work; stable bucket by material record/recipe and instance, shade in a second dispatch, scatter by
  `(pixel, ray)`, then finish each pixel in original ray order. Sorting cannot improve the primary traversal already completed.
  Material/VT cache fetches (`shaders/rt/native_hit_material.hlsli:72–101`, `:262–361`) and local-light lists
  (`shaders/rt/native_hit_direct.hlsli:168–200`) are the plausible coherence win; sun/AO queries also become grouped.
- Exactness: retain fp32 ray/hit operands and original seeds, cone, screen classification, statuses and fallback state.
  Sun uses `(fp, frame*8+r)` and AO the same key (`shaders/gi/gi_trace_rt.hlsl:458–468`); use source pixel, never queue index.
  Material sampling uses explicit gradients, so lane remapping must retain those gradients rather than derive them from neighbours.
- **Original ray order alone is insufficient.** Emission is added separately before direct/far bounce (`:402–408`, `:528–536`).
  Replay the original sequence `(((0 + emission0) + shade0) + emission1) + shade1`, including conditional no-add paths;
  summing each ray first changes rounding. Keep seen/occ/wfall updates and final division/clamp order (`:598–613`).
  No float atomics or unordered wave reductions. Recompilation/FMA contraction across the split must also be checked.
- Memory scale: 2560×1067×2 = 5,463,040 ray slots. A proposed 64–128 B full-precision record costs 350–699 MB worst case;
  three float3x4 transform rows alone are 48 B. Two 32-bit sort/index arrays add 43.7 MB; 32 B result slots add 174.8 MB.
  Compact allocation scales records with hidden fraction but needs overflow-safe worst-case capacity/fallback. Additional
  write/read/scatter traffic exceeds 1 GB/frame in a dense case, before sorting; use bounded tiles/chunks if needed.
- Estimate 0–1.50 ms net: gross 5–20% of 8–12.8 ms = 0.4–2.6 ms, less queue/sort/shade/reduce costs provisionally 0.3–1.5 ms.
  These fractions are hypotheses, not hidden-hit attribution. Two dispatches alone are insufficient if a final ordered reducer
  and global sorting are required. Effort 10; high memory/dispatch/register risk, especially when most rays reuse screen radiance.
- Cheapest offline proof: record/replay the current per-pixel addition event sequence in CPU fixtures, with fp32 rounding,
  zero/NaN/Inf/fallback/emission cases; byte-compare half outputs. Then extend existing frame replay with the split dispatches,
  retaining the same AS, textures and seeds. Include all queue/sort/reduction time in comparison; do not time shading alone.

## 3. Async compute AS build

- Queues exist for Streamline, not as an unused scene scheduler: device patch chain in `layer/layer.cpp:218–235`,
  extra graphics/compute queues appended in `layer/streamline.cpp:1648–1693`, indices passed to SDK at `:1787–1788`.
  Do not borrow an SDK queue without an explicit synchronization/ownership contract.
- Current graph: G-buffer raster/XFB → convert/extract → BLAS → TLAS → origin/GI → AO → native lighting.
  The G-buffer and lighting command buffers go in one submit, G-buffer first (`layer/rtscene.cpp:151–156`).
  Split-path barriers and shared scratch serialize required steps (`layer/bvhsplit.cpp:3025–3037`, `:3416–3418`, `:3494–3497`).
  At the present end-of-G-buffer injection point there may be little independent raster left before GI consumes the TLAS.
- Candidate overlap is only independent raster **after capture readiness and before first AS consumption**; early cached BLAS
  work is possible only when current inputs are ready. Current dynamic XFB cannot overlap its producer. GI cannot overlap
  native lighting that consumes its AO. Submit-time instance writes must precede the asynchronous TLAS read.
- Requires command-buffer/submission splitting, explicit producer→compute→consumer semaphores, queue-family transfers for
  exclusive resources if families differ, same-frame descriptors and completion-based scratch/AS/texture retirement.
  CPU scene waiting is recording readiness, not GPU idle time (`docs/TRACK_D_SCENE_BUILD.md:811–817`).
- Exactness: same current geometry and resource snapshot, no previous-scene substitution, unchanged descriptor identity;
  preserve all existing host-write, transfer, XFB, OMM and material publication dependencies.
- Estimate 0–0.40 ms **hidden critical-path time**, not 1.2–2.4 ms eliminated build work. Bound by min(independent raster gap,
  eligible build time) and shared execution/bandwidth contention. No measured gap establishes a positive lower bound.
  Effort 14; very high synchronization/lifetime risk and low confidence on an already GPU-bound Ultra frame.
- Cheapest offline proof: inspect existing frame/submission traces to find a real producer/consumer gap; stop if absent.
  Then a small same-frame two-queue dependency fixture on the existing scene/replay data, synchronization validation and
  output comparison. Offline fixtures prove ordering only; actual hidden ms needs a later authorized production queue timeline.

## 4. Ray direction binning / coherent thread order

- Existing dispatch is 8×8, one pixel/thread, two sequential cosine rays; fp-based RNG (`shaders/gi/gi_trace_rt.hlsl:152–180`,
  `:227–245`; `shaders/gi/gi_common.hlsli:196–207`). Start with a bijective lane permutation inside each tile, or Morton tile
  ordering with unchanged tile contents. Keep source fp, bounds checks, seeds and per-pixel loop exactly as written.
- Exactness: independent pixels still execute identical math and rays against the same AS; outputs return to original pixel.
  Keep full groups at barriers, including inactive edge lanes. Stats binning changes if based on physical gid/lane: retain
  logical group IDs for counters (`shaders/gi/gi_trace_rt.hlsl:157–169`). Compare images separately from diagnostic regrouping.
- Estimate 0–0.40 ms for this cheap permutation: at most a few percent of the 8–12.8 ms trace, no queue traffic.
  Rays are deliberately decorrelated and adjacent pixels already share origin locality, so zero is credible. Effort 2; low risk.
- Actual direction bins require generating and storing the **same** directions, sorting an index list, then tracing/scattering.
  Do not sort random numbers, rotate samples or change seed progression. Splitting ray0/ray1 adds fp32 result slots and ordered
  assembly as in §2. A global direction-only bin can worsen origin/texture locality; local direction+normal bins are safer.
  Net 0–0.50 ms, effort 8, low confidence; consider this expensive follow-up only if the cheap permutation wins.
- Cheapest offline proof: CPU bijection/edge/pixel-seed fixtures; then same-AS replay byte equality and complete-dispatch timing.
  For bins compare generated ray tuples bitwise first. Existing replay (`tools/frame_replay.md:56–78`) accepts cached SPIR-V
  and specialization values; shader compilation, if later authorized, must use `tools/shader_identity.py`.

## 5. Bandwidth and precision audit

| Resource | Actual storage / evidence | Exact narrowing verdict |
| --- | --- | --- |
| GI raw + 2 histories | RGBA16F, `core/gi.cpp:733–735` | RGB radiance plus AO alpha; not dead alpha. No RGBA32F saving. |
| Raw + 2 history depths | R32F, `core/gi.cpp:736–738` | Used in depth rejection; half rounding can change branch/coverage. Retain. |
| Previous radiance | RGBA16F, `core/gi.cpp:739` | RGB diffuse+spec sum and luminance share alpha; both consumed (`shaders/gi/gi_common.hlsli:175–188`). |
| Origin | RGBA16F, `core/gi.cpp:741` | XYZ geometric normal + relative surface offset, not RGB color; retain all precision/channels. |
| Auxiliary AO | RGBA16F, `core/gi.cpp:742` | Ordinary writer stores `(ao.x,0,0,0)` (`shaders/gi/gi_ao_apply.hlsl:33`); R16F retains x exactly. |
| RT visibility / RF distance | RGBA8 / R16F, `core/gi.cpp:740`, `:744` | Already narrow; visibility includes native mask/unknown codes. |
| RR guides | RGBA8 + 2×RGBA16F, `layer/ngx.cpp:569`; `shaders/rr_guides.hlsl:24–31` | Normal XYZ and roughness all used; no supported lower-precision consumer contract proved. |
| Hair origin / no-data | RGBA32F opt-in, otherwise 1×1; RGBA32UI 1×1 (`core/gi.cpp:743–747`) | No meaningful full-res 32F waste in stock Ultra. Preserve integer/header types. |

- Narrow only auxiliary x for normal operation; keep a separate diagnostic RGBA16F target/view contract for primary/pan/LS
  outputs (`shaders/gi/pan_primary.hlsl:70–103`; `shaders/gi/rt_local_shadows.hlsl:1058–1059`, `:1427`). Ordinary consumers
  read x (`:363`, `:995`); update storage image declarations/descriptors, not just allocation. Savings: 6 B/pixel/write =
  16.39 MB/frame at 2560×1067, plus eligible reads; net 0–0.06 ms, bandwidth/transaction dependent, not an RR-evaluate saving.
- Do not infer that an 11F lighting destination permits 11F intermediates: `prevRadiance` sums lighting then computes a share
  (`shaders/gi/gi_composite.hlsl:45–52`), GI feeds visibility and later nonlinear decisions. 11F RGB also has no AO/share alpha.
- Derived numerical rule: for normal positive x with exponent e, half-ULP bounds are 2^(e−11) for 16F,
  2^(e−7) for R/G11F and 2^(e−6) for B10F; use actual adjacent representable values at zero/subnormals/boundaries.
  This bounds one rounding only. A value arbitrarily near a destination midpoint can change bits for arbitrarily small error.
  Require unchanged branch/ray decisions and same stored output, or propagate an explicit error bound through each consumer;
  final color tolerance alone cannot justify changed origin normals, ray directions or alpha decisions.
- Cheapest offline proof: exhaustive 10-bit native AO → original half → R16F comparison plus diagnostic binding fixtures.
  For any other narrowing, quantize existing float replay data through all consumers and compare stored outputs/branch masks;
  mean energy must remain within OFF-repeat noise, never a chosen percentage. Do not reopen the rejected temporal shortcut.

## 6. Origin fusion or depth reuse

- Origin already uses game depth to restrict primary traversal to a short interval, not a full camera ray
  (`shaders/gi/rt_origin.hlsl:49–70`). It obtains triangle geometric normal and `t_hit/|P|−1`, matching by derived tolerance,
  with depth/shading fallbacks (`:87–93`, `:145–148`). Game depth/normal alone cannot reproduce that tuple or alpha-hit identity.
- Direct fusion into GI is invalid without another publication stage: GI reads origin at arbitrary projected qp (`gi_trace_rt.hlsl:287`),
  reflections likewise (`rt_reflections.hlsl:304`). A group barrier cannot publish a full-image origin dependency.
  Recomputing origins at every qp adds rays. RR guides run at G-buffer end and have no current AS/origin contract.
- A feasible narrow fusion is origin+sun: calculate/store exactly the same half-quantized origin, then execute sun for that pixel;
  finish full dispatch before GI. Today order is origin → GI/temporal/AO → sun (`core/gi.cpp:3079–3089`, `:3108–3156`).
  Moving sun earlier needs an audit of modified AO, trust inputs and descriptor snapshots; these shaders also share headers.
  Even local register reuse must explicitly round to the original 16F store/load value before calculating a ray.
- Estimate 0–0.10 ms from one launch/barrier and some receiver loads, **not** the 1.3–1.5 ms primary traversal, which remains.
  Effort 10; medium/high risk. Reusing only game depth has 0 exact saving justified; do not re-propose lazy normal.
- Cheapest offline proof: dependency table for every sun/origin binding first, then paired fixtures for the full origin image,
  sun mask/unknown outputs, subsequent GI and projected-hit qualification. Stop if ordering changes an input snapshot.

## 7. Other evidenced waste and proof limits

- Negative finding: box-stat barriers (`shaders/gi/rt_local_shadows.hlsl:1414–1420`, `:1438–1442`) look unconditional,
  but require LS_FULL. Quiet Ultra selects the non-full kernel (`core/gi.cpp:3347–3350`, `:3373–3380`);
  `shaders/gi/rt_local_shadows_box_k.hlsl:3–5` does not enable LS_FULL. No quiet-Ultra saving is claimed here.
- Tiny extraction dispatches: per-job push/dispatch loop (`layer/bvhsplit.cpp:3225–3243`) can batch small disjoint jobs
  while preserving triangle/status addresses and arithmetic. Net 0–0.10 ms during churn, often zero static; effort 6,
  medium risk of status/publication races. Cheapest proof: existing extraction fixtures with byte-equal vertices/status and
  unchanged cache decisions; first inspect job-size distribution. No new general scheduler; no claimed per-mesh barrier removal.
- No evidence here licenses skipping dynamic/cascade conversion, refits, alpha/OMM validation or RR. Existing rejected
  immutability and temporal proofs remain rejected. No speculative ray/sample/precision reduction is an exact optimization.
- All offline proofs above are proposed, not executed. Replay is a useful paired test, not universal proof: current documented
  unsupported AS cases and missing production resources must be covered by fixtures before any exactness claim.
