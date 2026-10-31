# Design study: the next three state-of-the-art items (2026-10-27)

This is an offline study. The game was not run. It turns three backlog items into plans
that can be implemented in this codebase:
- **Item 1.** Many-light sampling for local lights (STATUS backlog #3 "emissive light list"
  and the D-DI line, AUDIT_SOTA "Many-light sampling").
- **Item 2.** A radiance cache for GI and reflections (backlog #5, D-GI2).
- **Item 3.** Opacity micromaps for alpha-tested foliage (backlog #6).

**Sources read:** STATUS, AUDIT_SOTA, archive/research.md, QUALITY (R0-R10), TRACK_C_GI, TRACK_D_RT
(sections 7-12), QA.md, ASSET_DB.md, `core/gi.cpp`, `core/ls_lights.cpp`, `shaders/gi/*`,
`shaders/rt/rt_common.hlsli`, `layer/bvhsplit.cpp`, and `layer/rtscene.cpp`. I also read
the D-EM branch `worktree-agent-a2212c228ec492214` (its HANDOFF, `em_common.hlsli`,
`em_lights.hlsl`, TRACK_D section 12, host ABI 8) without changing it. SDK facts come from
the GitHub repositories and their docs (links in section 5).

**Target** (memory, AUDIT_SOTA §0): RTX 5090; 3840x1600 output, 2560x1067 render
(2.73 M px); DLSS-G 2x locked at 120 Hz; a base of at least 65 fps, so a frame of 15.4 ms
or less. QUALITY R7 budgets: AO + GI including the cache 0.6 ms, local-light shadows
0.5 ms, reflections 1.0 ms, all Track D effects 2.5 ms or less, caches 256 MB or less.

**Cost rule of thumb used below** (TRACK_D 8.3): 0.04-0.07 ms per million short rays,
2-3x that for long closest-hit rays. Costs are estimates unless they say "measured".

---

## 0. Summary and recommended order

| # | Plan | Gain | Est. cost (5090, 2560x1067) | Memory | Risk | Needs |
|---|---|---|---|---|---|---|
| **1a** | D-LS **exact-k + RIS remainder**: trace the k strongest unshadowed lights per pixel exactly, and pick one light by RIS from the rest | Grymforge 0.72 ms → about 0.45 ms (under the 0.5 ms budget); the foundry is unchanged | ≤ 0.45 ms worst case (was unbounded) | 0 | low | nothing |
| **2 v1** | **Screen-injected SHaRC**: inject the game's lit diffuse (plus D-EM emission) of visible pixels into a world-space hash grid; GI rays at hidden or off-screen hits and the reflection tier-(c) fallbacks query it; GI radius 1 m → 8 m | Room-scale bounce; the 11.7% hidden + 2.2% off-screen GI hits stop being "occlusion with no bounce"; part of the 27% probe-fallback reflection rays get radiance | +0.25 to +0.4 ms | 160 MiB (2²²), or 80 MiB (2²¹) | medium | **SHaRC download** |
| **3** | **Opacity micromaps**: 4-state OMMs per cached alpha mesh and per foliage draw key, baked on the GPU from the game's own alpha source (VT unpack, then the OMM SDK GPU baker); shadow rays use 4-state, GI/AO/reflection rays forced 2-state | Same image (4-state), or a small bias at partial coverage (2-state); traversal cost in foliage | Campsite: −0.25 to −0.6 ms over all RT passes (from 7.1M alpha candidates/frame) | 16-64 MB | medium-high | **OMM SDK download**; restart setting |
| **1c** | **GPU light BVH** over the unified light list: the game's b25 lights + off-view runtime lights (asset DB/bridge) + D-EM emissive clusters. Used for direct light at secondary vertices (SHaRC update, reflection tier b) and for emissive NEE | Lights at off-screen hits; emissive surfaces as lights | build ~0.05 ms; per sample ~15-25 node visits | ~13 MB | medium | D-EM merged; CPU oracle **done** (this change) |
| **2 v2** | SHaRC **traced update** for voxels never seen on screen (needs 1c + EN2 hit albedo) | Multi-bounce where the screen never looked | +0.15-0.25 ms | same | medium-high | 1c, EN2 |
| **1b** | **ReSTIR DI** temporal (then spatial) reuse for the remainder set of 1a | Less remainder noise in dense torch scenes | +0.1-0.2 ms over 1a | 87 MB (2 × 16 B × 2.73 M) | medium | only if QA shows remainder noise; RTXDI optional |

**Recommended order: 1a → 2 v1 → 3 → 1c → 2 v2 → (1b only on evidence).**
- **1a** is days of work, needs no SDK, and closes the one measured budget miss (Grymforge
  D-LS 0.72 ms against 0.5 ms).
- **2 v1** is the largest quality gain per millisecond. Its inputs (the game's own lighting)
  are physically complete, including every light, probe and shadow the game has.
- **3** is a pure cost item at the same image. It matters outdoors (S3/S4), where foliage
  is the remaining cost risk to the 65 fps base. It goes before 1c because it also speeds
  up every later ray (the SHaRC update, light-tree shadow rays).
- **1c and 2 v2** need the D-EM branch merged and EN2 (hit materials, backlog #4).
- **1b** is deferred. At BG3's 2-6 evaluated lights per pixel, 1a is already close to
  optimal (AUDIT_SOTA "Local-light shadows" row). ReSTIR only pays off where the remainder
  set is large and visibly noisy after RR.

**Implemented in this change:** `common/light_tree.h`, the CPU light BVH (SAOH build,
GPU node layout, reference sampler and PMF), and `tests/light_tree_test.cpp` (ctest, 59
checks). It is not wired into the layer or core: it is the oracle for 1c (section 1.6).
Nothing in the game's frame changes.

**Downloads needed (not done; the coordinator asks the user):** see section 5.

---

## 1. Many-light sampling for local lights

### 1.1 What we do now (measured)

- **D-LS** (`shaders/gi/rt_local_shadows.hlsl`, `core/gi.cpp` `run_post` → `ls_dispatch`):
  - per pixel, it walks the game's froxel list (b23/b24) over the light array (b25);
  - it recomputes each light's exact unshadowed diffuse and specular `c_i` with the game's
    BRDF;
  - it traces one ray per traced light (soft: a uniform solid-angle sample of a sphere of
    the light's source radius);
  - it writes the exact delta `c_i·(v_i − 1)` into b10/b11.
- This is the ratio estimator of Heitz et al. 2018: noise only in visibility, unbiased,
  no light-selection variance.
- **Load:**
  - foundry: 5.61 lights listed and 2.31 evaluated per pixel; the pass is 0.39-0.42 ms
    after the 9.9 optimisation;
  - Grymforge (torches): 0.71-0.73 ms, **over the 0.5 ms budget**;
  - docks: 0.07 listed, 0.068 ms.
- **`ls_samples=1|2`** (RIS, 1-2 rays per pixel) exists only in the `LS_FULL` variant.
  It cost 0.219 ms against 0.345 ms at the time of 9.5.
- **There is no light sampling at secondary vertices.** GI hits and reflection hits take
  last frame's screen radiance, or fall back (GI: occlusion only; reflections: the
  probe). The light list is view-culled into froxels, so it does not exist for points
  outside the frustum.
- **D-EM** (branch `a2212c2`, data only): an emissive TLAS plus a light list of up to
  65,536 clusters of emissive triangles, 80 B each, with area, power and mean radiance.
  - Built per game submission of the effects passes, so consumers see it one frame late
    (`em_shift()` moves positions to the current camera).
  - Foundry: 36 k triangles; build 0.31 ms (first design). Adamantine Forge: 152 k
    triangles.

### 1.2 The options

| Option | What it is | Fits BG3? |
|---|---|---|
| (A) Keep per-light rays for every traced light | Today's D-LS | Optimal up to about 4 traced lights per pixel. Cost grows linearly with the light count (Grymforge) |
| (B) **Exact-k + RIS remainder** | Deterministically split the pixel's traced lights into the k largest by `lum(c_i)`, each traced, and the rest R, from which one light is picked with p ∝ `lum(c_j)` (all of R are candidates: the list is short, so there is no candidate subsampling). Estimator: `Σ_{i∈E} c_i(v_i−1) + c_j(v_j−1)/p_j` | **Yes.** Unbiased (the split depends only on the pixel's deterministic `c_i`). The cost is bounded at k+1 rays per pixel |
| (C) ReSTIR DI (Bitterli et al., SIGGRAPH 2020; RTXDI 3.1) | Reservoirs per pixel, temporal and spatial reuse, 1 final visibility ray | For **R only**. Over the whole list it would add selection noise to lights that (B) shades exactly. The reuse is what correlates samples, which RR dislikes (1.5) |
| (D) ReGIR (Boksansky et al., *Ray Tracing Gems II* ch. 23; RTXDI's ReGIR context) | A world-space grid of per-cell light reservoirs, rebuilt every frame from a power PDF | A candidate source for secondary vertices. Its quality is bounded by the cell size. It needs a power-PDF texture over all lights anyway |
| (E) **Light BVH / light tree** (Conty Estevez & Kulla, HPG 2018; Moreau, Pharr & Clarberg, HPG 2019; pbrt-v4 `BVHLightSampler`) | A tree of light bounds (box, normal cone, power, **range**), sampled by stochastic descent with a conservative importance | **Yes, for secondary vertices and emissives.** It adapts to the receiver's position and normal, and BG3's finite light ranges prune whole subtrees. It handles tens of thousands of emissive clusters. It is rebuilt every frame, so moving lights are exact |

**Choice:**
- **Primary surfaces: (B) now, (C) only on evidence.**
  - The game already gives us the exact unshadowed `c_i` for every light in the froxel
    list. The only unknown is visibility.
  - (B) keeps exactness where it matters (the strongest lights) and bounds the rays.
  - ReSTIR's strength, finding the few important lights among thousands, does not apply:
    the list is already the set of lights that reach the pixel, with exact weights.
- **Secondary vertices and emissives: (E)**, a GPU-built light BVH over a unified light
  list, rather than ReGIR (D):
  - the receivers there (SHaRC update vertices, reflection hits) are few: about 0.1-0.4 M
    per frame, so a 15-25-node descent per sample is affordable;
  - the importance adapts to the receiver's normal and to the light ranges, while ReGIR
    caps quality at the cell size and stores 0.5-8 M reservoir entries;
  - RTXDI's ReGIR stays the fallback if the descent cost turns out to be the problem.

### 1.3 Data flow and integration points

**(B) Exact-k + RIS remainder** changes only the D-LS default variant:
- `shaders/gi/rt_local_shadows.hlsl`, **phase 1** (which entries are traced, TRACK_D 9.9):
  it already evaluates the acceptance tests. Add a running top-k by `lum(c_i)` (k ≤ 4, kept
  in registers as an insertion list of (luminance, list position)) and a streaming RIS
  reservoir over the entries that drop out of the top-k:
  - weight `w = lum(c_i)`, one uniform from `pcg3d(pixel, frame, 0x5EED)`, then keep
    `Σw`, the chosen index and its `c`;
  - a light pushed out of the top-k enters the reservoir at that moment, so a single pass
    over the list suffices.
- **Phase 2** (the rays): k exact rays, plus one ray to the reservoir's light if R is not
  empty. The ray setup is unchanged: `rt_ray_origin_o`, soft sphere sample, `ls_clear`,
  alpha-tested `rt_occluded`.
- **Phase 3** (sums): the exact lights as today. The remainder adds
  `c_j·(v_j−1)·(Σw / lum(c_j))` to diffuse and specular, with the same AO and SO factors.
- **Register budget** (the lesson of 9.9: 96 registers, 21 warps per SM). The top-k list
  and the reservoir add about 12 registers. Check with `tools/pipeline_stats.py`. Fall back
  to k = 2 if the count exceeds 112.
- `core/gi.cpp` `make_push`: **`Push` is at its 256-byte limit** (`static_assert`). The
  new values go into the spare words `pad`, `pad2[0..1]` and `pad3[0..1]` (5 × 32 bit):
  - `ls_exact_k` (bits 0-3) and `ls_remainder` mode (bits 4-7) packed into `pad`;
  - any further growth needs a per-frame UBO ring (as the sun set ring).
- `core/gi.cpp` `run_post`: the variant choice (`full = ...`) is unchanged. `ls_exact_k=0`
  reproduces today's "trace every light" exactly, and `ls_check=1` proves it bit for bit.

**(E) Light BVH**, new code only:
1. **Unified light list.** A new pass `shaders/rt/lt_gather.hlsl`, dispatched from a new
   `core/light_tree.cpp` `lt_build(cmd, s)` called in `run_post` **after D-LS and before
   D-RF**, so this frame's light array is used. Its inputs:
   - **b25** (the snapshot's `s.l_lights`) — every valid slot: point and spot (projector
     lights later). Camera-relative position + `_m6` gives absolute world (R4), and the
     radiance is the game's `m1.xyz` × (`b13[0]` if `m3 < 0`). This is exact, current-frame
     data.
   - **Runtime lights not matched to any b25 slot** (`g_lsl.known`, binding 45, and the
     join result, binding 46). These are the lights outside the view: asset-DB placements
     merged with the bridge.
     - Intensity comes in the game's units through a calibration: `s_type` = the median of
       `lum(m1)/(lum(colour)·intensity)` over the matched slots, per type, logged and
       damped. This is an engine-input parity check (R0): it must agree within 5%
       across scenes.
     - Flicker uses the mean intensity.
     - `Bg3RtLight` has no spot direction, so off-view spots are bounded as point lights
       (conservative).
   - **D-EM clusters** (`emLights`, set 3 via `host->rt_emissive`, ABI 8): position = the
     cluster centroid (shifted by `em_shift`), extent = the cluster's bounding half-size,
     power = `EmLight.power` divided by the pre-exposure.
     - The normal cone comes from a small addition to `em_lights.hlsl` (the cone of the
       cluster's triangle normals; until then cos_o = −1, conservative).
     - **Off by default** (`lt_em=0`), see the double-counting risk in 1.7.
   - **Owners:** character-owned lights follow `ls_owners` (excluded by default). The
     darkvision pair is a readability light with `CastShadow = false` (TRACK_D 9.8); its
     bounce is not authored either.
2. **Build** (`lt_build_*.hlsl`): an LBVH (Karras, HPG 2012):
   - 30-bit Morton codes of the centroids within the 80 m window;
   - a one-sweep radix sort of up to 70 k keys;
   - Karras's hierarchy;
   - a bottom-up fill of the `LightTreeNodeGpu` fields (box, cone, power, range) with
     one atomic flag per node.
   - The node layout and the importance are **exactly** `common/light_tree.h`. LBVH
     topology is worse than SAOH; the oracle measures that (1.6), and a CPU SAOH build
     of the static subset is the fallback.
3. **Sampling** (`shaders/rt/light_tree.hlsli`): `lt_sample(p, n, u, out idx, out pmf)`,
   the same descent as `LightTree::sample`, then a point on the chosen light: a sphere
   sample for b25/DB lights, area-by-power then uniform-on-triangle for emissive clusters.
   - Consumers: the SHaRC update (2 v2), reflection tier (b)
     (`shaders/gi/rt_reflections.hlsl`, the off-screen branch), and later emissive NEE at
     primary hits.
4. **Stable light IDs** across frames (needed by any temporal reuse, and by 1b):
   - the join record index for matched b25 slots, `0x8000_0000 | db_index` for off-view
     lights, and `0x4000_0000 | cluster` for emissives;
   - `lt_gather` writes `slot → id` and the previous frame's `id → slot` map
     (a 4096 + 1024 + 65536 entry table). Whether b25 slots are stable frame to frame is
     unknown: the gather counts slot changes for `lt_stats` (1.8).

**(C) ReSTIR DI for R (1b)**, only if needed:
- **Passes:** `ls_restir_temporal.hlsl` and `ls_restir_spatial.hlsl` between phase 1 and
  phase 2 of D-LS. That means splitting the pass, which 9.9 measured as +0.07 ms for a
  second G-buffer read.
- **Alternative:** keep a single pass and do temporal reuse inline: read the previous
  reservoir at the reprojected pixel inside phase 1. That is "temporal-only ReSTIR". It
  costs one 16-byte load plus the re-evaluation of one light's `c`.
- **Bindings:** 2 storage buffers, ping-pong, as `kExtras` 49/50 in the D-LS layout. It has
  26 of the 32 push descriptors in use, so this fits.

### 1.4 Visibility reuse, bias and boundaries (1b)

- **Target function:** `p̂ = lum(c_i)` (unshadowed). There is no visibility in the target,
  so reuse cannot learn an occlusion that is no longer there. Visibility is traced once
  for the final sample, as in (B).
- **Visibility reuse** in Bitterli's sense (dropping reservoirs whose sample was occluded
  last frame) is **biased** (darkening at moving shadow edges). It is off by default and
  available only as an A/B (`ls_restir_visreuse`). The hard_dark and flicker detectors
  judge it.
- **Temporal reprojection:**
  - uses the game's motion vectors (the same path as the GI and D-RF reprojection);
  - rejects on a linear-depth difference above 10%, a normal dot below 0.9, a different
    material class (RT2.w), or a light ID not present this frame;
  - after a rejection the pixel restarts from fresh RIS. That gives more noise, no bias.
- **Bias correction** when merging: the reused light is re-evaluated at the current pixel
  with **pairwise MIS** (RTXDI "Pairwise" mode). No extra rays.
  - The light lists of neighbours differ by froxel: a reused light that is not in the
    current pixel's list gets `p̂ = 0` there. The game would not light the pixel with it,
    and the exact delta must never touch a light the game did not add.
- **Screen edges and disocclusion:** spatial neighbours outside the screen or failing the
  same tests are skipped, and `M` is capped (`ls_restir_m_max`, 20) to bound the lag.
- **Light changes:** flicker changes `c_i` every frame. The reservoir weight is recomputed
  with the current `c`, so this is unbiased. A switched-off light has `c = 0` and drops.

### 1.5 Character-attached moving lights

- **In b25 they are exact every frame:**
  - (B) uses the current position and colour, so there is no lag;
  - a reservoir (1b) stores the light ID and a **light-local** sample (`uv` on the
    sphere), so the sample moves with the light;
  - the LBVH is rebuilt every frame from the current array.
- **Off-view** they come from the bridge at 4 Hz with a 4 m match tolerance (9.8):
  - their LBVH bounds are inflated by `move_amount`, plus `speed × 0.25 s` when the
    bridge reports a velocity;
  - the importance stays conservative, so a stale position can cost variance, never bias,
    as long as the sample uses the same stale position consistently;
  - the lighting error is the position error, at most about 1 m at a walking pace, off
    screen.
- **Art intent:** character lights are not shadowed (`ls_owners=15`) and are excluded from
  secondary lighting (`lt_owners`, same default). An A/B switch exists for both.

### 1.6 The CPU light tree (implemented) and how it serves 1c

`common/light_tree.h`:
- `bg3::lt::LightTree::build()` is a binned SAOH builder (12 bins, 3 axes, Conty Estevez &
  Kulla's orientation measure and long-axis regularisation).
- Nodes carry a box, a normal cone (θo), the emission spread (θe), the summed power, the
  largest **range** (BG3 lights reach no farther than their radius), and a two-sided flag.
  They are flattened depth first into `LightTreeNodeGpu` (64 bytes, std430).
- `importance(bounds, p, n)` returns 0 only where no light under the node can contribute:
  - p is beyond the range from the box;
  - p is outside the emission cone widened by the box's subtended angle;
  - the box lies entirely below the receiver's hemisphere.
- `sample()` is the stochastic descent. `pmf()` follows the light's stored bit trail.
  `index_of_id()` maps caller IDs.

`tests/light_tree_test.cpp` (ctest `light_tree_test`, 59 checks, all pass):
- the layout;
- single, coincident and zero-power lights;
- the cone union;
- importance sanity;
- sampled frequencies against `pmf()` (worst 0.94 σ over 400 k samples);
- three random scenes of 64, 400 and 1024 lights (0-25% spots, BG3-style ranges):
  - **0 lights that contribute but have pmf 0**: unbiased support;
  - `sample()` and `pmf()` agree;
  - probabilities never sum above 1;
  - the one-sample estimator's mean is within 5.5 standard errors of brute force at 300
    receivers per scene (worst 4.1 σ);
  - mean relative error 3-5% at 4000 samples.

**Finding (to fix in the GPU version).** At some receivers the descent ends in a node
whose two children both have importance 0 (smallest probability sum 0.001 in scene 1):
- this costs efficiency, not bias;
- the cause is the scalar `range` test at an interior node: box + max range is looser
  than its children's;
- **fix:** store an influence box per node (the union of each light's box grown by its
  own range). The node then needs 80 bytes, or the cone can be packed into 16-bit values
  to stay at 64.

**How it is used:**
1. As the **oracle**: `tools/light_tree_dump` (planned) reads a dumped unified light list
   (`lt_dump=1` writes it next to a trace). It builds the CPU SAOH tree and the GPU LBVH
   (read back), and compares the estimator variance at 10 k random receivers. Accept the
   LBVH if its variance is within 1.3× of SAOH; otherwise build the static subset on the
   CPU.
2. As the CPU build of the **off-view runtime lights**: at most 1024 lights, rebuilt when
   `ls_lights` refreshes (every 8 frames). That is a sub-millisecond CPU job, uploaded as
   a second root, so the GPU builds only b25 + D-EM.

### 1.7 Interaction with DLSS RR

- RR wants independent, per-pixel white-noise samples injected where the game's term lives
  (QUALITY R1, R2).
  - (B) keeps that: one more independent ray per pixel, with the remainder's selection
    noise in the same pixel.
  - The deterministic top-k split switches between neighbouring pixels. This is a change
    in noise level, not a pattern, and RR handles it.
- **Guide buffers are unchanged.** The delta goes into b10/b11 before SSS, so RR's diffuse
  and specular albedo, normals and roughness still describe the surface. DI needs no hit
  distance.
- **ReSTIR (1b)** correlates neighbours through spatial reuse and frames through temporal
  reuse. RR can mistake that for signal: blotches, then smearing in motion.
  - Use permutation sampling in temporal reuse (RTXDI 1.2+), randomised spatial
    neighbours per pixel (never a fixed pattern), the boiling filter, and M ≤ 20.
  - RTXDI 3.1's "DLSS-RR compatible" decorrelation presets are the reference.
  - Start temporal-only. Spatial reuse only if QA's flicker detector passes with it.
- **Emissive NEE double counting (the main risk of `lt_em=1`).**
  - BG3 authors point lights at lava, braziers and glyphs to fake their emission. Adding
    the emissive surface as a light would light the scene twice.
  - R0 favours the real emitter, but removing the authored proxy light needs a
    proxy-detection rule: a placed light inside the cluster's bounds plus 1 m, with a
    colour matching its mean radiance.
  - Until that exists, emissives feed GI only through the cache (2 v1, injection), which
    is energy-correct because it is what the game shows.

### 1.8 Settings (bg3rtx.ini; all live unless marked)

| Key | Default | Meaning |
|---|---|---|
| `ls_k` (was `ls_exact_k`, TRACK_D 9.11; removed 2026-10-31, see CONSOLIDATION.md C1) | 0 (measured: k1 −22%, k2 −8% at Grymforge, over budget either way) | Lights traced exactly per pixel; 0 = all (the pass before) |
| `ls_rest_samples` (was `ls_remainder`; removed 2026-10-31 with `ls_k`) | 1 | 0 drop R (biased, debug only), 1-2 RIS rays; ReSTIR (1b) would be a separate key |
| `ls_restir_m_max` | 20 | Temporal M cap |
| `ls_restir_visreuse` | 0 | Bitterli visibility reuse (biased A/B) |
| `lt_build` | 0 | Build the light BVH every frame |
| `lt_sources` | 3 | Bit 0 b25, bit 1 off-view runtime lights, bit 2 D-EM clusters |
| `lt_owners` | 15 | As `ls_owners` |
| `lt_em` | 0 | Emissive NEE (proxy-light rule required first) |
| `lt_debug` | 0 | 1 light count in the tree, 2 sampled light ID hash at the primary hit, 3 PMF heatmap |
| `lt_stats` | 0 | Node visits per sample, failed descents, b25 slot churn, calibration scale |
| `lt_dump` | 0 | Write the unified light list with the next trace |

### 1.9 Cost and memory at 3840x1600 (render 2560x1067)

| Part | Estimate | Basis |
|---|---|---|
| (B), foundry | same as today, 0.39-0.42 ms | 2.31 evaluated, k = 4 covers them |
| (B), Grymforge | 0.42-0.48 ms (from 0.72) | at most 5 rays per pixel instead of every torch; the no-ray part 0.14 ms (9.9) |
| (C) temporal inline | +0.05-0.1 ms | 1 load and 1 light re-evaluation per pixel |
| (C) spatial | +0.08-0.12 ms | 2 neighbours, a second pass |
| Reservoirs | 2 × 16 B × 2.73 M = **87 MB** (DLAA 3840x1600: 197 MB) | light ID 32 b, `uv` 2 × 16 b, `W` fp32, `M` and age 16 b each |
| LBVH build | 0.03-0.06 ms | ≤ 70 k keys; sort, hierarchy, refit |
| Light records + nodes | 70 k × 64 B + 140 k × 64 B ≈ **13 MB** | |
| A light-tree sample | 15-25 node visits (log₂ of 70 k ≈ 16) × 2 importances | about 0.02 ms per 100 k samples |

### 1.10 Risks

- Register growth in the D-LS default variant (9.9): measure, and fall back to k = 2.
- b25 slots that are not stable across frames break temporal IDs. The stable ID mapping
  handles it, at the cost of shorter histories.
- DB intensity calibration drifting by scene: per-type scales, logged, R0 parity gate.
- Off-view spots bounded as points: more variance, no bias.
- Emissive double counting (1.7): off by default.
- LBVH quality: the oracle comparison, with the CPU SAOH fallback.

### 1.11 Test plan

**Offline:**
- `light_tree_test` (done).
- Oracle variance comparison on dumped lists.
- `ls_check=1` bit-exactness with `ls_exact_k=0`.
- `tools/pipeline_stats.py` register count.

**In-game, after build and commit** (the queue format of `scripts/test-queue.ps1`):
```json
{ "name": "ls-exactk-S5", "tree": ".claude/worktrees/<branch>", "save": "Grymforge - 32h 59m",
  "set": "ls_stats=0", "traces": 4, "timeout": 900, "phases": "captures/ls_exactk_phases.json",
  "patterns": ["ls: local-light shadow pass GPU median", "ls stats .*lights per pixel", "ls check"] }
```
- Phases (`live_phases.py`): `ls_exact_k=0` → `ls_check=1` (the k = 0 equivalence) →
  `ls_exact_k=4` → `2` → `ls_remainder=0` (an expected-biased control) → back to the
  defaults.
- Repeat on S1 (foundry), S12 (Moonrise, torches) and S10 (magic lights).

**QA** (`python -m scripts.qa run --scenes S1,S5,S12 --reference`):
- `hard_dark` must not change;
- `flicker` excess area ≤ the current value + 1‰;
- `--reference` (FLIP against `ls_samples=2`-style references) at k = 4 must be ≤ +0.01
  above the floor;
- cost: the `ls:` median ≤ 0.5 ms on S5.

**Light tree:**
- `lt_debug=3` on S1/S5 (images);
- `lt_stats`: failed descents < 5%, b25 churn, a calibration scale within 5% across S1,
  S5, S7 and S12;
- `-Validation`, `-Move`.

---

## 2. A radiance cache for GI and reflections

### 2.1 What we do now (measured)

- **GI** (`gi_trace_rt.hlsl`, `core/gi.cpp` `run_pre`): 1 cosine ray per pixel at full
  resolution under RR, radius `gi_rt_radius` = 1 m.
  - A hit on screen with matching depth takes `prev_radiance_at()`: last frame's lit
    diffuse + specular. That gives multi-bounce by feedback.
  - Hidden (11.7% of hits) and off-screen (2.2%) hits are occlusion with no bounce
    (`gi_rt_fallback=0`): a darkening bias.
  - 0.175 ms for the trace.
  - It is "AO with near-field colour bleeding", not room-scale GI (AUDIT_SOTA).
- **Reflections** (`rt_reflections.hlsl`): tier (a) on screen 42%; tier (c) the game's
  probe for misses (28%), hidden (21%) and off-screen (6%) hits.

### 2.2 The options

| | SHaRC (spatial hash radiance cache) | NRC (neural radiance cache) | Today + longer rays only |
|---|---|---|---|
| What | A hash grid over world space (64-bit keys: quantised position with a camera-distance LOD, optional normal bits). Update traces sparse paths and accumulates radiance per voxel; Resolve merges frames and evicts stale entries; Query reads a voxel. Shader headers only (`SharcCommon.h`, `HashGridCommon.h`, `SharcTypes.h`) | A small MLP trained online on path vertices (Müller et al., SIGGRAPH 2021); RTXGI 2.x ships it as experimental | |
| Shipped | Cyberpunk 2077 RT Overdrive, Indiana Jones (SharcUpdate 1.99 → 0.89 ms with OMM) | research / experimental | |
| Inputs at path vertices | radiance at each hit: the integrator decides how | position, direction, normal, roughness, **diffuse/specular reflectance** at every vertex | |
| Fit for BG3 | **Yes**, in a **screen-injected** form: the game's lit diffuse of visible pixels is the exact radiance at those surfaces | **No**: no materials at secondary vertices (EN2 not done), a training loop, and cooperative vectors / a Slang toolchain (AUDIT_SOTA §1) | longer rays without a cache only turn more of the frame into "occlusion without bounce" |

**Choice: SHaRC, screen-injected (v1), then traced update (v2). NRC rejected.**
- This is the D-GI2 design of TRACK_D 8.6, made concrete. The key difference from
  standard SHaRC is the update source.
- A path tracer computes radiance at every vertex from materials and lights. We do not
  have that off screen. **On screen we have something better**: the game's own lit
  diffuse, with all its lights, probes, shadow maps, SSS profiles, and now our D-LS and
  sun.
- So v1 **injects visible surfaces** into the cache every frame. The cache then holds what
  they looked like for `stale` frames after they leave the screen (retention, the same
  idea as Lumen's surface cache and AMD's GI-1.0 two-level cache, Boissé et al. 2023).
- v2 adds traced update paths only for voxels that have never been seen.

### 2.3 Data flow and integration points

**Frame order** (all in `core/gi.cpp`; the cache is a new module `core/sharc.cpp` with
`sharc_begin/inject/resolve/bind`):
1. **`run_pre`**, before `gi_trace_rt`:
   - `sharc_resolve()`: one thread per entry over last frame's injections and updates
     (`SharcResolveEntry`, accumulation cap `gi_cache_frames`, eviction after
     `gi_cache_stale` frames).
   - Then `gi_trace_rt` queries.
   - The resolve sits here so the post pass's injection of frame N is resolved before
     frame N+1's queries.
2. **`gi_trace_rt.hlsl`**: at a hit that is not shaded from the screen (`!visible`, `t ≥ 0`):
   - `SharcGetCachedRadiance` at the hit position (absolute world = camera-relative +
     `_m6`) with its geometric normal (`rt_hit_normal`);
   - a hit radiance from the cache: `bounce += L·w`, and occlusion as now;
   - no cache entry: the current `gi_rt_fallback` rule;
   - with SHaRC's "segment shorter than the voxel size: don't use the cache" rule, near
     hits keep the screen path.
   - Then raise `gi_rt_radius` from 1 m to 8 m (A/B 4/8/16). The `1 − (t/R)²` fade stays.
3. **`run_post`**, after D-LS and D-RF, before `g_composite`:
   - `sharc_inject.hlsl`, 1 pixel per `2×2` (`gi_cache_inject_stride`), position randomised
     per frame (white noise, R1);
   - per pixel: the lit **diffuse b10** (view-independent, R3; specular excluded, Q8), plus
     the **D-EM effects-target emission** of that pixel when present
     (`Bg3EmissiveInfo.fx_view`; sky pixels have depth 0 and are skipped);
   - written as a one-vertex SHaRC update (`SharcInit` → `SharcUpdateHit` with
     `directLighting = L` and the throughput 1).
   - The b10 used here contains this frame's GI composite of the *previous* cache.
     That is the same multi-bounce feedback `prevRadiance` already has. It converges
     because each bounce is multiplied by the albedo.
4. **`rt_reflections.hlsl`**: for hidden and off-screen hits with roughness above
   `rf_cache_rough` (0.2), SHaRC's cone test (spread from ray length × roughness², larger
   than the voxel), then the cache, else the probe as now.
   - Mirrors stay on tier (a)/(c): a voxel-sized blur in a mirror is worse than the probe.
5. **Misses are never sky indoors** (R3). They keep the game's probe, as today. The cache
   only replaces "hidden/off-screen hit" outcomes, never misses.

**Binding:** the D-RF push layout uses 31 of the 32 push descriptors. So the cache's
buffers go into a **persistent descriptor set**, set 4 (set 0 push, set 1 the host's
alpha set, set 2 the sun ring or the game's set 1, set 3 D-EM). It is written once at
allocation. It is shared by `gi_trace_rt`, `rt_reflections`, `sharc_inject` and
`sharc_resolve`.

**Device and toolchain (layer, restart):**
- `layer/rtscene.cpp` `rt_device_create_info`: add `shaderBufferInt64Atomics` (Vulkan 1.2
  core feature, for `HASH_GRID_ENABLE_64_BIT_ATOMICS 1`), plus `shaderFloat16` and
  `storageBuffer16BitAccess` (for `-enable-16bit-types`).
  - It uses the same probe → append → retry-without fallback chain. Without the 64-bit
    atomics, use SHaRC's lock-buffer mode.
- `layer/shaders.cpp` `compile`: add `-enable-16bit-types` for files whose first line is
  `// dxc: -enable-16bit-types` (SHaRC's integration guide requires it for every
  permutation that includes its headers).
  - This is a host change. It does not affect existing shaders.

**How emissive capture (D-EM) feeds it:**
- **v1:** through the injection. The effects-target radiance at visible emissive pixels is
  added to the injected radiance, so lava seen on screen and then panned away still
  bounces for `stale` frames. This is exact: it is the game's own emission.
- **v2:** traced update vertices that hit an emissive surface (`em_lookup` in the D-EM
  TLAS, a hit at `t ± eps`) add its record's radiance. NEE at update vertices samples the
  light BVH (1c) with D-EM clusters, `lt_em=1` for the cache only. That is safe from
  proxy double counting only if the proxy rule (1.7) is in place, so v2 starts with the
  emissive *hit* term and no emissive NEE.

**v2: traced update.**
- `sharc_update.hlsl`: 1 random pixel per 5×5 block (SHaRC's recommendation, 4%), paths of
  2-3 bounces from the primary surface.
- At each vertex:
  - **on screen with matching depth:** the radiance is the screen value; terminate (as now);
  - **otherwise:** emission (D-EM) + albedo (EN2: the VT base colour, else the voxel's
    injected radiance divided by its injected irradiance, else 0.5) × (sun: 1 ray in the
    cone + light BVH NEE: 1 sample, 1 ray + the game's probe irradiance b34 for the sky
    term outdoors only);
  - then `SharcUpdateHit` and continue (Russian roulette after 2 bounces).
- It runs only where the voxel has no injected samples (a `SharcGetCachedRadiance` miss at
  the vertex). That avoids paying for, or double counting, what the screen already gives.

### 2.4 New buffers (2²² entries, SHaRC defaults, no SH encoding)

| Buffer | Element | Size |
|---|---|---|
| hash entries | uint64 | 32 MiB |
| accumulation (this frame) | 16 B | 64 MiB |
| resolved (cross-frame) | 16 B | 64 MiB |
| **total** | | **160 MiB** (2²¹: 80 MiB) |
| lock buffer | only without 64-bit atomics | 16 MiB |

- Occupancy check: SHaRC expects 10-20% in use for a static camera with path-traced
  updates. Injection-only is sparser (visible surfaces at the LOD of their distance), so
  2²¹ is likely enough. Measure with `HashGridDebugOccupancy` and choose (`gi_cache_log2`).
- The 160 MiB fits the 256 MB cache budget (R7), but not alongside 1b's 87 MB of
  reservoirs. If both are on, one of them must shrink: 2²¹ entries, or 12-byte
  reservoirs.

### 2.5 Update rate and settings

| Key | Default | Meaning |
|---|---|---|
| `gi_cache` | 0 | 1 inject + query (v1), 2 + traced update (v2) |
| `gi_cache_log2` | 22 | Entries (restart of the module, not the game: reallocated on change) |
| `gi_cache_inject_stride` | 2 | 1 pixel per stride² injected per frame (randomised) |
| `gi_cache_frames` | 16 | Accumulation cap: the lag in frames. Torch flicker (~4-8 Hz) is averaged over 0.25 s at 65 fps; injection refreshes visible voxels every frame anyway |
| `gi_cache_stale` | 128 | Eviction: about 2 s of retention off screen |
| `gi_cache_scene_scale` | 50 | `HashGridParameters::sceneScale` (voxel size); `SHARC_GRID_LOGARITHM_BASE` and `LEVEL_BIAS` at SDK defaults first |
| `gi_cache_query` | 3 | Bit 0 GI hidden hits, bit 1 GI off-screen, bit 2 reflections (r > `rf_cache_rough`) |
| `gi_rt_radius` | 1 → 8 after A/B | GI ray length |
| `gi_cache_debug` | 0 | 1 `HashGridDebugColoredHash`, 2 occupancy, 3 cached radiance at the primary hit, 4 GI hit tier (screen / cache / fallback) |
| `gi_cache_stats` | 0 | Occupancy, query hit rate per tier, injected count, evictions |

### 2.6 Expected noise and cost

**Noise:**
- A cache hit is an average of many injected pixels and frames, so it has low variance. It
  *removes* noise compared with today's binary "bounce or not" at hidden hits.
- The new artefact is **voxel quantisation**: blocky bounce in a debug view, but blurred
  by the cosine-hemisphere integral and RR at the receiver.
- Temporal lag of `gi_cache_frames` for off-screen changes: a light switched off behind
  the camera keeps bouncing for about 0.25 s.
- RR sees a less noisy GI signal. No guide changes are needed. The GI is still injected
  into b10 through the existing composite.

**Cost:**

| Pass | Estimate |
|---|---|
| inject (0.68 M px at stride 2, 1 hash insert each, 64-bit atomics) | 0.03-0.06 ms |
| resolve (4.2 M entries, 2²²; 2.1 M at 2²¹) | 0.04-0.08 ms |
| queries (≈ 14% of 2.73 M GI rays plus ≈ 0.3 M reflection rays) | 0.01-0.02 ms |
| `gi_rt_radius` 1 → 8 m (longer rays, more hits) | +0.1-0.2 ms (the trace is 0.175 ms at 1 m; long rays cost 2-3×) |
| **v1 total** | **+0.2 to +0.35 ms** (GI + cache budget 0.6 ms: today 0.26-0.28 → about 0.5-0.6) |
| v2 traced update (0.11 M paths × 2-3 bounces × (1 closest + 2 shadow rays)) | +0.15-0.25 ms (with OMM; Indiana Jones measured −55% on SharcUpdate from OMM) |

### 2.7 Risks

- **Leaks into closed or roofed rooms.** Injected radiance of a lit exterior wall lands in
  voxels shared with the dark interior side when the voxel is larger than the wall
  thickness.
  - Mitigations: normal bits in the hash key (SHaRC supports normal-dependent keys); and
    a query that rejects entries whose stored normal faces away from the ray (dot < 0).
  - QA gate: brightening inside asset-DB roofed rooms (2.8).
- **The cut-away roof:** surfaces the camera never draws are never injected. Interiors
  stay "occlusion only" there, as today: no leak, just no gain.
- **Feedback runaway:** injected b10 contains the cache's own bounce. Energy stays bounded
  because the albedo is below 1 and `gi_strength` is 1, but an inject bug (for example
  including specular) could gain energy. QA `brightening` and a mean-luminance
  convergence log over 600 frames on a static camera.
- **Hash collisions at 2²¹:** the occupancy debug view, then 2²².
- **Toolchain:** `-enable-16bit-types` and int64 atomics on the game's device (restart);
  validation coverage is good (core features).
- **The D-EM effects target lags one frame:** injection uses the frame it belongs to (the
  info struct's `built_frame`), or skips it.

### 2.8 Test plan

**Offline:**
- DXC compile of the four shaders with `-enable-16bit-types`;
- `pipeline_stats` for `gi_trace_rt` (the query adds registers);
- a hash round-trip debug (R4): `gi_cache_debug=1`.

**In-game jobs** (one run per scene, live phases):
```json
{ "name": "sharc-v1-S1", "tree": ".claude/worktrees/<branch>", "save": "QuickSave_3784",
  "set": "gi_cache=0,gi_cache_stats=1", "traces": 6, "timeout": 900,
  "phases": "captures/sharc_phases.json",
  "patterns": ["gi: trace pass GPU", "sharc: ", "rt stats .*hybrid GI"] }
```
- Phases: `gi_cache=0` → `gi_cache=1` (radius 1) → `gi_rt_radius=8` → `gi_cache_debug=2`
  (occupancy) → `gi_cache_debug=4` (tiers) → `gi_cache=0`.
- Scenes: S1 foundry; S6 Adamantine Forge (lava bounce via D-EM injection); S7 Blushing
  Mermaid (roof cut-away interior: the leak gate); S8 Water Queen's House (reflections
  tier); S3 Campsite (foliage: the cache under alpha cards).
- One `-Move` run on S1 and one `-Validation` run on S1.

**QA** (`python -m scripts.qa run --scenes S1,S6,S7,S8 --reference`):
- `hard_dark` must improve or stay (the darkening at hidden hits goes away);
- `brightening` is for review (R0, expected: it is the missing bounce);
- `flicker` within +1‰ (cache lag can flicker when a voxel flips between injected and
  stale);
- `nonfinite` 0.
- **New detector `roofed_brightening`** (gate): GI brightening (`on/gi_off` > 1.3) on
  pixels inside an asset-DB roofed room (`room_at`) whose exterior is sunlit. That is
  light through geometry, physically implausible (R0).

**Cost:** the `gi:` trace median plus the new `sharc:` timestamps ≤ 0.6 ms for AO + GI on
S1 and S6.

### 2.9 v1 as implemented (2026-10-27; built and compiled, not run in game yet)

Off by default (`sharc=0`). Differences from the plan above:
- **Keys** are `sharc` and `sharc_*` (bg3rtx.ini), not `gi_cache_*`: `sharc_log2` (22),
  `sharc_inject_stride` (2), `sharc_frames` (16), `sharc_stale` (128), `sharc_scene_scale`
  (50), `sharc_log_base` (2), `sharc_level_bias` (0), `sharc_query` (7: reflections on too),
  `sharc_gi_radius` (4 since 2.11, was 8: replaces `gi_rt_radius` while the cache runs), `sharc_rf_rough`
  (0.2), `sharc_rf_cone` (1), `sharc_near` (1), `sharc_em` (1), `sharc_reset_jump` (50 m),
  `sharc_debug` (0-5; 5 is the QA roof mask), `sharc_stats`, `sharc_device` (restart).
- **Injection** runs after the GI composite (so b10 includes this frame's GI: the
  multi-bounce feedback of 2.3 step 3), not before it. It pauses while `gi_debug`, `rf_debug`,
  `ls_debug` or `rf_validate` rewrite the lit diffuse. Stochastic rounding of the fixed-point
  accumulation keeps dim interiors unbiased; injected radiance is clamped at 256.
- **Normals**: the injection uses `rt_origin.hlsl`'s geometric normal (else the shading
  normal), facing the camera; queries the hit triangle's geometric normal facing the ray.
- **Binding**: set 4 with empty sets 2-3 (D-RF: the game's set 2). The trace and D-RF have
  `_sharc` variants (`gi_trace_rt_sharc.hlsl`, `rt_reflections_sharc.hlsl`), so `sharc=0`
  runs the unchanged pipelines. The D-EM effects target goes in push binding 49.
- **D-EM guard**: `BG3RTX_SHARC_EM` (CMake `AUTO`/`OFF`) follows `BG3RTX_ABI_VERSION >= 8`;
  this branch is ABI 7, so it builds and runs without the emission term.
- **Device** (`layer/rtscene.cpp`): shaderInt64, shaderBufferInt64Atomics, shaderFloat16 and
  storageBuffer16BitAccess where supported, in the game's own structs where it chains them
  (it chains Features2, Float16Int8 and 16BitStorage), logged `[rt] SHaRC device features`;
  one retry without them before dropping RT. The core reads them, and the memory types for
  its device-local buffers, from `persist(BG3_DEVICE_INFO_KEY)` (no ABI change). Without
  64-bit atomics the injection inserts through the lock buffer (`sharc_inject_lock.hlsl`).
- **Clears**: on allocation, when the cache was not updated for 8 frames (re-enabled, a load
  screen) and on a camera jump over `sharc_reset_jump` (in the resolve).
- **Registers** (`tools/pipeline_stats.py`, RTX 5090): GI trace 96 → 113 in the `_sharc`
  variant, D-RF 96 → 96, injection 24, resolve 31.
- **QA**: `python -m scripts.qa run --sharc` (docs/QA.md); `roofed_brightening` scores the
  roofed area the cache adds over GI without it.

### 2.10 First in-game run (2026-10-27, queue jobs `30-sharc-*`, QA `captures/qa/20261027-123953`)

Measured on the 5090 at 2560x1067, RR on, static camera, timing runs without statistics:

| | S8 Water Queen's House | S3 Campsite | S6 Adamantine Forge |
|---|---|---|---|
| GI trace off → on (8 m) | 0.156 → 0.354 ms | 0.536 → 1.002 ms | 0.163 → 0.249 ms |
| D-RF off → on | 0.369 → 0.399 ms | 0.261 → (n/a) | 0.224 → 0.259 ms |
| resolve / inject | 0.030 / 0.061 ms | 0.033 / 0.071 ms | 0.028 / 0.050 ms |
| **total added** | **≈ +0.32 ms** | **≈ +0.6 ms** | **≈ +0.20 ms** |
| occupancy (2²²) | 1.3% | 2.7% | 0.9% |
| GI hits not on screen found in the cache | 17% | 30% | 1.6% |
| reflection fallbacks found | 13% | 16% | 1.6% |

- Compiles, device features (`0xf` on the device, one field set in the game's structs), no
  insert failures, no faults or non-finite values from the cache (S6's non-finite values are the
  game's D32F depth `76433aef`, in every trace including all-RT-off).
- Debug view 3 finds every visible surface (keys of injection and query agree); the low S6
  rate is the scene: 88% of its non-screen hits are hidden surfaces the static camera never saw.
- **Fixed after the run:**
  - Hits without radiance occluded over the whole 8 m: in S6 that is 98% of the hidden hits,
    and `hard_dark` flagged 47‰ (sharc) / 39‰ (gi_sharc). They now occlude only within
    `gi_rt_radius` (the fade of the base radius), so the longer rays add bounce and never
    unexplained darkening.
  - The roof mask tested `cam.v0.y` for "sun up" and marked S8 and S3, both in daylight,
    unknown, so `roofed_brightening` was never evaluated. The mask is now volumes only; the QA
    takes "sunlit exterior" from the game's sun mask (`sun_dbg`).
  - `sharc_stats` counters hit one address per counter: the GI trace took 3.6 ms with them.
    They are now spread over 64 bins.
- **Default: stays off** (High and Ultra unchanged). The leak gate has not been evaluated
  yet, the darkening fix is unmeasured, and the Campsite costs +0.6 ms (8 m alpha-tested rays
  through foliage), over the 0.6 ms AO + GI budget on its own. Next: the re-run QA
  (`32-sharc-*` jobs), and `sharc_gi_radius` 4 against 8 on S3.

### 2.11 Second run (fixes of 2.10; QA `captures/qa/20261027-132732`, jobs `sharc2-*`)

- **Radius** (S3, no statistics): GI trace 0.539 ms off, 0.998 at 8 m, 0.865 at 4 m; D-RF
  0.230 → 0.243; resolve 0.033, inject 0.069. 4 m adds ≈ +0.44 ms on the Campsite (8 m
  ≈ +0.57) and finds 37.5% of the non-screen hits (8 m: 30%). **`sharc_gi_radius` default 4.**
- **Statistics**: binned counters cost 0.015 ms on the trace (0.865 → 0.880), not 3 ms.
- **Leak gate S8**: now evaluated (73% of the frame roofed, the game's sun lights 4%): **fail,
  0.74‰ > 0.5**. Every flagged region (24-64 px, gain 1.5-2.5x) is sunlit exterior rock and a
  column capital inside the volumes' padding, RR speckle, not light through a wall. The mask
  now excludes pixels the game's sun lights; to be re-run.
- **S6 hard_dark** with the cache, reflections off: 24.4‰ final (39‰ in run 1), 1.2‰ in the
  lighting buffer; the all-on `sharc` effect 0.05‰. The regions are the ground under large
  overhangs: rays now reach the overhang and replace the probe's ambient with its (dark)
  bounce. GI without the cache already removes 95-97% there (unqualified regions of 1.5k px);
  sharp edges come from the overhang. Physically plausible large-scale occlusion, but it is a
  gate failure until a reference says otherwise.
- S3: no roofed pixels (gate not applicable); hard_dark 0.06‰ (`sharc`), 1.0‰ (`gi_sharc`)
  on the same foliage regions plain GI flags.
- **Default: still off** for every preset: the leak gate has not passed yet, S6 fails
  hard_dark, and the Campsite cost (+0.44 ms) leaves no room in the 0.6 ms AO + GI budget
  (plain GI there is already 0.54 ms).

### 2.12 Third run (main 2653f76, 4 m, new leak mask; QA `captures/qa/20261027-153639`)

- **Leak gate, S8: fail, 0.506‰ > 0.5** (roofed and not sunlit by the game: 67% of the frame;
  the cache brightens 2.77‰ of it beyond 1.3x, plain GI 1.43‰). The flagged regions (27-84 px,
  1.3-2.5x) are ivy on a wall, a column and a roof slope in shade, not lit by the game's sun
  but open to the sky; the gain is RR-level speckle of the added bounce. No region shows light
  through a wall, but the asset-DB volumes (padded 1 m / 3 m) cover most of this view, so S8 is
  not a discriminating interior. The gate stays as specified; the deciding scene is a closed
  room with a cut-away roof (S7 The Blushing Mermaid), not run yet.
- **S6 hard_dark at 4 m: pass relative to plain GI.** GI with the cache: 6.4‰ final / 0.9‰
  lighting, against plain GI's 13.1‰ / 1.8‰ in the same run; all-on `sharc` 0. The cache halves
  what the GI's AO fold flags. Cache hits stay at 1.5% of the non-screen hits (hidden machinery).
- **Cost at 4 m** (from the QA's own log, `sharc_stats=1`): S8 GI trace 0.154 → 0.275 ms, resolve
  + inject 0.157 ms (0.09 without statistics), ≈ +0.21-0.28 ms; S6 0.163 → 0.230 ms + 0.096 ms,
  ≈ +0.17 ms; Campsite (2.11) ≈ +0.44 ms.
- Cache found: S8 22%, S6 1.5%; reflections 13-14% / 1.6%. Occupancy ≤ 1.3% at 2²².
- **Default: off for every preset, Ultra included.** The leak gate is a gate (R0, QA.md) and
  has not passed: S8 is marginal for reasons unrelated to leaks, and no closed roofed interior
  has been measured. Ultra would also pay +0.2-0.45 ms. Flip after S7 passes `roofed_brightening`
  (job `35-sharc4-qa-S7`), with S8 kept as a review.

### 2.13 Physical checks instead of thresholds (user decision 2026-10-27)

`roofed_brightening` (ratio 1.3, 0.5‰) is retired as a gate and kept as a review map. In its
place (docs/QA.md):
1. **`sharc_reference`**, the gate: the cache's lighting against a reference without the cache
   (rays as long as the cache's, 8 per pixel), on the roofed interior. The tolerance is the
   measured per-pixel noise of the difference of the two means, σ_d = √(var_c/N_c + var_r/N_r);
   the decision compares the excess tail with the deficit tail (the null's own control) on
   pixels thinned to the measured correlation length, against the binomial noise √(N_e + N_d).
   Its limit is written down: the reference has no radiance at hidden hits (`gi_rt_fallback=1`
   keeps the game's ambient there), so legitimate hidden-surface bounce can show as excess. A
   fail is then attributed with check 2.
2. **`sharc_leak_cells`** and the fix of the mechanism: per cache hit, the cell centre is
   projected into the hit's plane and a ray tests whether it is visible from the hit point
   (`sharc_cell_test`, `sharc_common.hlsli`); a second ray into the surface reports slabs
   thinner than the cell (their far side has the opposite normal octant, so they cannot mix
   and are only reported). `sharc_reject_occluded=1` (default) drops entries from occluded
   cells: rejecting entries from behind an occluder, one shadow ray per cache hit, no tolerance
   (both ray ends are lifted by `rt_offset_ray`'s ULP offset). `sharc_leak_check=1` measures the
   share of the contribution such cells would supply ("sharc stats ... leak test", view 6).
3. **Cost tolerance from the frame budget:** 15.4 ms at 65 fps; SETTINGS_DESIGN (Ultra, ranked
   spend list) puts Ultra at ≈ 13.4 ms in pans (2.0 ms headroom) and reserves 0.4-0.6 ms of it
   for this item (row 6, D-GI2). **Superseded by the user's directive of 2026-10-27:** cost does not
   gate the default on Ultra (quality is not traded for frame rate); 0.6 ms is the optimisation
   target for the cache's added cost, nothing more. Measured so far: measured +0.17 (S6) to
   +0.44 ms (S3) at 4 m before the leak rays (≤ one shadow ray per cache hit: 0.02-0.23 M
   rays/frame, ≈ 0.01 ms by TRACK_D 8.3's 0.04-0.07 ms per M rays). Register count of the GI
   trace variant rose 113 → 128 (two more inlined ray queries): to be measured, not assumed.
4. **Default:** decided by quality only: on for Ultra once the leak test and the reference
   comparison pass on S7 and S8 (the latter possibly only with the v2 traced update, which gives
   the reference radiance at hidden hits). Cost is optimised separately.

### 2.14 First run of the physical checks (QA `captures/qa/20261027-155820`, S7 + S8)

- **S7 The Blushing Mermaid: void.** The camera settled after `on0`: every later trace is
  shifted against it, so the same-view check voided all comparisons, the cache's included. The
  QA now anchors the view on the traces that agree with each other (on0 is replaced by the
  first agreeing `onN`, same settings). Its counters still ran: occupancy 1.6%, 10.7% of the
  non-screen GI hits found, 19% of the cache's GI contribution from occluded cells.
- **`sharc_leak_cells`** (GPU counters, rejection off): S7 19.0%, S8 18.5% of the cache's GI
  contribution came from cells the test marked occluded; 0.000% from cells thicker than the
  surface. With the rejection on, the sharc_on traces logged 27-34% of S8's cache hits as
  occluded. That rate is too high for walls alone: the test's grazing ray (lifted by the ULP
  offset only) also hit neighbouring triangles of curved and tessellated surfaces. **Fixed:**
  the path now runs half a cell edge above the plane (the cell's own extent along the normal),
  so only geometry crossing the cell's upper half counts. To be re-measured.
  The debug view's channel split is not usable as a measure (the preview's tone mapping mixes
  channels: 63% "thin" in the view against 0.000% counted); the QA now scores from the counters.
- **`sharc_reference`, S8 interior: fail** (excess 209,475 against deficit 42,371 independent
  samples, noise 502; the measured correlation length is 1 px). The cache is brighter than the
  reference on 46% of the interior pixels, by +1.7% in the mean (4.0% of the reference's energy
  above σ_d). This is the documented limit, now measured: the reference keeps the probe's
  ambient for hidden hits, the cache replaces it with the hidden surfaces' cached radiance, and
  occluded cells were already rejected in these traces. The comparison cannot tell legitimate
  hidden-surface bounce from a leak while the reference has no radiance of its own at hidden
  hits; that needs the traced update (v2: materials and lights at hidden hits).
- **Cost** (S8, from the run's phases, statistics on): GI trace 0.152 → 0.322 ms (with the
  rejection's shadow rays; 0.275 without them in 2.12), D-RF 0.361 → 0.455 ms, resolve 0.031 +
  inject 0.071 ms: **+0.37 ms** (under the 0.6 ms optimisation target; not a gate). Registers of both cache
  variants: 128 (plain GI trace 96).
- **Default: off, Ultra included, on quality grounds only** (cost is no reason either way). The leak mechanism is real (≈ 19% of the contribution before
  the rejection); the corrected test and its rejection must be re-measured, and S7 has no
  valid comparison yet. Job `37-sharc6-qa-S7-S8` (`--sharc --reference --keep-raw`). If the reference
  still cannot separate hidden-surface bounce from a leak, the next step is the v2 traced update
  (2.3): it gives the reference, and the cache, radiance at hidden hits.

### 2.15 Corrected leak test (QA `captures/qa/20261027-175117`, S7 + S8, raw kept) and v2

- **`sharc_leak_cells`** (counters, rejection off), with the test path half a cell above the
  plane: occluded cells supply **4.7%** (S7) and **4.0%** (S8) of the cache's GI contribution
  (6.8% / 8.9% of its hits), down from 19% / 18.5% with the grazing path. Thin: 0.000%. The
  mechanism is active at a few per cent; `sharc_reject_occluded=1` (default) removes it.
- **`sharc_reference`**: **S7 interior passes** (excess 5,919 against deficit 6,487 independent
  samples, noise 111; mean −1.1%: in the closed tavern the cache is not brighter than the
  reference). **S8 interior fails** as before (excess 211,985 against 43,453, noise 505; mean
  +1.5%, 4.2% of the energy above σ_d), uniformly over shaded stone (crops), where the cache
  replaces the probe ambient at hidden hits with cached radiance. The S8 "interior" is mostly
  exterior shade inside the padded volumes. The reference stays indecisive there.
- hard_dark: S7 `sharc` 0.43‰ final only, a character's arm that moved between traces; S8 0.
  Non-finite: 0.
- **v2 (traced update), started on branch `sharc-v2` from 67b1e44.** Its purpose here is a
  decisive reference: radiance of its own at hidden hits. That radiance is albedo x incident
  light, and the albedo at a hidden hit needs hit materials (STATUS #4, EN2: not done; D-EM not
  merged either). The design's fallbacks for it ("injected radiance / injected irradiance", only
  for voxels already seen; "else 0.5", an assumed albedo) do not give a physical value for a
  surface never seen, and an assumed albedo is excluded by R0. So v2's traced update waits for
  EN2. **What it starts with instead is an assumption-free reference for the leak question:**
  `sharc_fine_check=1` injects every sample also into a grid with cells `sharc_fine_scale` (4)
  times smaller (same table, keys 16 levels apart), and per cache hit logs coarse − fine
  luminance with its standard error, for clean and for occluded cells. The finer grid is the
  same estimator closer to its leak-free limit (cells 4-8 cm at 10-30 m, under a wall's
  thickness); repeating at 8 shows whether 4 has converged. QA check `sharc_fine_reference`
  (gate): the clean cells' mean excess must not exceed z x its measured standard error, z = 1.96 =
  `thresholds.z_one_sided(2)` (ALPHA_FW 5% over the scene's two SHaRC gates; corrected from k = 1,
  which would false-alarm 16% of the time). Caveat,
  written down: where the GI rays' hit density correlates with brightness inside a cell, coarse
  and fine differ without a leak; the occluded cells against the clean ones separate the two.
  Job `38-sharc7-qa-S7-S8`. Hit materials (EN2) are the blocker for the traced update itself.
### 2.16 sharc7 (QA `captures/qa/20261027-183526`) and the plan with EN2

- **`sharc_fine_reference`: not measured.** The QA phases last 240 presents (about 6 s); the
  300-frame "sharc stats" window never closed inside the `sharc_leak` phase before `sref0` turned
  the cache off, so no fine-cell line was logged (and the leak-test line scored for that phase
  covered mostly the phase before it). Fixed: `sharc_stats_frames`, 60 in the QA, so several
  windows close inside each phase and the last line of a phase covers it alone.
- `sharc_leak_cells` (counters): 6.0% (S7) and 4.1% (S8) of the cache's GI contribution from
  occluded cells (same windowing caveat). Rejected by default.
- `sharc_reference` with z = 1.96: **S7 interior passes** (excess 24,638 against deficit 25,650;
  limit 440), **S8 interior fails** (217,427 against 42,592; limit 999; mean +1.9%), as in
  2.14-2.15: indecisive there for the reason given.
- hard_dark: S7 `sharc` 0.42‰ final only (the moving character again); non-finite 0.
- **Default: off.** The fine-cell gate has not produced a measurement; job `39-sharc8-qa-S7-S8`.
- **EN2 (branch `worktree-agent-a94065fdd50bc8494`) and SHaRC in D-RF.** EN2 adds
  `rt_reflections_hm.hlsl` (HITMAT_PASS) with the lighting's sun set as set 3; SHaRC's variant
  binds the cache at set 4 with set 3 empty, so the two layouts nest. When both land: one shader
  `rt_reflections_hm.hlsl` with both HITMAT_PASS and SHARC_QUERY_PASS, layout {D-RF push, host,
  game set 1, sun set, cache set}, replacing `rt_reflections_sharc.hlsl`; `rf_dispatch` keeps
  plain / cache-only / material(+cache) pipelines. Per hidden or off-screen hit: EN2's tier (b)
  (material x direct light at the hit) where it applies, the cache where tier (b) has no
  material, the probe otherwise; the cache's diffuse-only radiance never adds to tier (b)'s
  direct term (double counting). EN2 also unblocks v2's traced update: its hit albedo is
  exactly the missing input (2.15).
---

## 3. Opacity micromaps for alpha-tested foliage

### 3.1 What we do now (measured)

- Alpha-tested instances and geometries are non-opaque. Every candidate runs
  `alpha_pass_hit()` (`rt_common.hlsli`): `InstInfo`, three vertices, a UV interpolation, a
  ray-cone footprint, then either `vt_alpha()` (an indirection `texelFetch` plus a tile
  cache sample: a dependent chain) or `tex_alpha()`, then the cut-off with the hashed
  threshold at coarse levels.
- **Campsite** after the grass fix (7.1b):
  - 412 alpha-tested draws, 1.85 M alpha triangles;
  - **visibility candidates 2.0 M → 7.1 M per frame**;
  - RT passes 1.35-1.40 → 1.40-1.53 ms; GI 2.95-3.0 → 3.0-3.4 ms (shared-GPU runs, noise
    about 0.2 ms).
- **Foundry:** GI trace 0.124 (alpha off) → 0.175 ms (on); sun + AO 0.201 → 0.22 ms; D-LS
  +0.015 ms.
- **Mesh classes:** static instanced meshes have cached object-space BLAS (pool with
  UVs, stride 24). **Wind foliage (class D) is per draw** (`DynSlot`, soup, refit/rebuild):
  that is most of the grass.

### 3.2 Algorithm choice

**`VK_EXT_opacity_micromap`, 4-state, baked per mesh on the GPU.** It is exposed here
(rev 2) and hardware-accelerated on Ada/Blackwell.
- Micro-triangles classified opaque or transparent are resolved in traversal with no
  shader at all.
- "Unknown-opaque/unknown-transparent" micro-triangles still return as candidates, and
  `alpha_pass_hit()` resolves them exactly. So a 4-state OMM gives **the same image**.
- Shadow rays (`rt_occluded`) keep 4-state. GI, AO and reflection visibility rays use the
  `ForceOpacityMicromap2StateEXT` ray flag (SPIR-V value 0x400). Unknown states then
  resolve to their opaque or transparent side: no candidates at all, and a bias confined
  to sub-micro-triangle coverage. This is NVIDIA's Indiana Jones split (TraceMain 7.90 →
  3.58 ms, −55%; SHaRC update −55%; sun −14%; AUDIT_SOTA "BVH: opacity micromaps").

**Baking source: run time on the GPU, not offline.**

| | Offline (asset DB) | Run time (GPU) |
|---|---|---|
| Alpha texture | Granite tile sets (`.gts/.gtp`) in the paks: a tile-set decoder (LSLib can extract them) plus BC decompression | **the exact texels the game uses**: `vt_alpha()` already reads them, for every record |
| Geometry | GR2 meshes, which must be matched to runtime mesh keys (buffer, offset and count based) by content hashes of object-space positions: the "offline static world" work (backlog #9) | **the mesh pool already holds object-space positions + UVs** (stride 24); per-draw foliage has its UVs in the soup |
| Cut-off | the material DB has `alpha_cutoff` | the record has the effective cut (`rt_alpha_cut`) |
| Verdict | later, together with backlog #9 | **chosen** |

**The SDK.** NVIDIA's OMM SDK (1.10.0) has a CPU baker and a GPU baker for Vulkan.
- The GPU baker is designed to be executed by the client RHI: it returns a list of
  dispatches and resources that we record ourselves. That suits a layer.
- It bakes from **a texture plus UVs**, while BG3's alpha mostly sits behind virtual
  texturing (252 of 372 alpha shader families). So the bake is two steps:
  1. **VT unpack** (`shaders/rt/omm_vt_unpack.hlsl`, ours): for one mesh and one alpha
     record, write an R8 texture covering the mesh's UV bounding rectangle at mip `m`.
     `m` is chosen so that a micro-triangle covers about 2-4 texels. Each texel is
     `vt_alpha()` at that mip.
     - **Pages not resident at `m`** are written as a 0/1 checkerboard. Any
       micro-triangle that touches them is then mixed, so the baker classifies it
       **unknown**, and the shader decides as today. Residency can never make an OMM
       wrong.
     - `tex` families skip the unpack: the game's texture view goes straight to the
       baker.
  2. **OMM SDK GPU bake** over that texture with the mesh's UVs (rebased into the rect):
     4-state, subdivision level `rt_omm_level`, alpha cut-off = the record's effective
     cut. It outputs the OMM array data, descriptors and index buffer, with uniform
     triangles given the special indices (fully opaque or transparent, no storage), then
     `vkCmdBuildMicromapsEXT`.
- **Stochastic coverage** (`rt_alpha_stochastic`, levels > 1) does not fit a fixed bake
  at mip `m`. Keep it for 4-state rays: coarse-level texels are mixed, so unknown, so
  the shader handles them. Under 2-state rays distant coverage becomes binary at mip `m`.
  That is acceptable for GI (it is what the game's own prepass does at that mip).

### 3.3 Integration points

1. **Device (restart).** `layer/rtscene.cpp` `rt_device_create_info`: probe and append
   `VK_EXT_opacity_micromap` and `VkPhysicalDeviceOpacityMicromapFeaturesEXT::micromap`
   (the same chain and fallback as the AS and ray-query extensions). Load
   `vkCreateMicromapEXT`, `vkCmdBuildMicromapsEXT`, `vkGetMicromapBuildSizesEXT` and
   `vkDestroyMicromapEXT` into `vk_funcs.h`. Setting `rt_omm` (restart).
2. **Cache.** New `layer/omm.cpp`:
   - the key is (mesh content key, alpha record identity: tile set + UV transform + cut +
     channel). Draws sharing a mesh and a material share one OMM array;
   - LRU over `rt_omm_budget_mb`;
   - eviction follows the mesh's (`bvh_forget_buffer`, `bvh_buffer_written`).
3. **Static meshes.** `layer/bvhsplit.cpp` `bvh_build`, mesh BLAS loop (`mesh_builds`,
   around line 1042):
   - for a mesh whose OMM is ready, chain `VkAccelerationStructureTrianglesOpacityMicromapEXT`
     (index type `UINT16/32`, the index buffer address, `micromap`) into
     `geo.geometry.triangles.pNext`;
   - `blas_sizes()` must see the same chain;
   - the OMM must exist before the BLAS build, so an alpha mesh gets its BLAS **rebuilt
     once** when its OMM is ready (a one-time build per mesh, budgeted with
     `rt_mesh_new_tris`);
   - compaction keeps the OMM reference, and the micromap must outlive every BLAS that
     references it (the deferred free list `PendingFree` covers it).
4. **Wind foliage** (`DynSlot`, the per-draw soup BLAS, around line 1056):
   - OMMs live in UV space and are indexed by triangle, so they **survive vertex
     animation**. The XFB soup's triangle order equals the draw's index order. A
     per-draw BLAS can therefore attach the OMM of its (index range, material) key on
     every rebuild **and** refit.
   - `VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_OPACITY_MICROMAP_UPDATE_EXT` is not needed as
     long as the attachment does not change between build and update.
   - A draw whose key changes gets a rebuild (already the policy when flags change).
   - **This is where the Campsite gain is.**
5. **Bake scheduling** in `bvh_build`, before the BLAS loop: at most
   `rt_omm_bakes_per_frame` keys per frame, in the scene build's command buffer, after
   the extraction jobs.
   - The bake needs the alpha set's views of this frame: they are bound only for draws
     seen this frame, the "one frame late" note in STATUS. So bake from the draw's own
     record in the frame it is seen.
6. **Shaders** (`shaders/rt/rt_common.hlsli`):
   - `rt_occluded`: unchanged flags (4-state).
   - `rt_closest`, `rt_closest_hit`: `| (pc_omm2 ? 0x400 : 0)` (ForceOpacityMicromap2StateEXT).
     DXC needs the capability declared: `[[vk::ext_capability(RayTracingOpacityMicromapEXT)]]`
     and `[[vk::ext_extension("SPV_EXT_opacity_micromap")]]` on the entry points that use
     the flag. **To verify:** the capability number in `spirv.core.grammar.json` of the
     Vulkan SDK, and that the compute pipeline needs no create flag for ray-query OMM
     traversal. Check empirically with `rt_omm_debug=2`.
   - `RT_ALPHA_CANDIDATE` is unchanged: it now runs only for unknown micro-triangles.
7. **Instances:** `VK_GEOMETRY_INSTANCE_FORCE_OPACITY_MICROMAP_2_STATE_EXT` is per
   instance, so it is not used. The 2-state decision is per ray class.

### 3.4 New buffers and sizes

- **Per OMM array:** subdivision level L gives 4^L micro-triangles per triangle, 2 bits
  each (4-state). L = 4: 64 B per triangle; L = 5: 256 B.
  - Special-index triangles (uniform) cost 0.
  - Identical UV triangles are deduplicated by the baker (grass cards repeat their UV
    layout).
- **Estimate for Campsite:**
  - unique alpha triangles: probably 50-150 k (1.85 M are instances of far fewer meshes);
  - after dedup and special indices, 30-50% need storage, so **3-8 MB at L = 4,
    12-30 MB at L = 5**;
  - plus index buffers (4 B per triangle) and the baker's scratch (transient, about
    64 MB at peak, released).
  - Budget `rt_omm_budget_mb` = 64.
- **VT unpack texture:** a transient R8 rectangle ≤ 2048², 4 MB, reused per bake.

### 3.5 Settings

| Key | Default | Meaning |
|---|---|---|
| `rt_omm` | 0 | Restart. Enables the extension and baking |
| `rt_omm_level` | 4 | Subdivision level (try 5 for close grass) |
| `rt_omm_2state` | 7 | Ray classes forced 2-state: bit 0 GI, 1 AO, 2 reflections (shadows never) |
| `rt_omm_bakes_per_frame` | 16 | Keys baked per frame |
| `rt_omm_budget_mb` | 64 | Cache size |
| `rt_omm_use` | 1 | Live: 0 builds without the OMM chain (an A/B in one run; rebuilds the affected BLAS) |
| `rt_omm_debug` | 0 | 1 micro-triangle state at the primary hit; 2 count of alpha candidates (with `rt_stats`) |
| `rt_omm_check` | 0 | At every committed hit on an alpha instance, re-run `alpha_eval` and count "OMM said opaque, the shader says clear" (and the reverse via `rt_closest_debug`). Must be 0 for 4-state |

### 3.6 Expected speed-up

- **Candidate shader work removed:** grass-card textures are mostly fully opaque or
  fully clear at L = 4 (about 2-4 texels per micro-triangle at the bake mip). Expect
  70-90% of candidates resolved in traversal for 4-state rays, and 100% for 2-state rays.
- **Campsite** (7.1 M visibility candidates per frame, plus the shadow rays' candidates):
  - the alpha-test overhead measured in the foundry was +41% on the GI trace and +10% on
    sun + AO;
  - at Campsite's 3.5× candidate density it is larger;
  - **estimate: −0.25 to −0.6 ms over all RT passes**, of which GI is −0.1 to −0.25,
    sun + AO −0.05 to −0.15, D-LS and reflections −0.05 to −0.15, and the later SHaRC
    update about −50% (Indiana Jones).
- **Foundry:** −0.05 to −0.1 ms.
- One-time costs: baking about 0.05-0.2 ms per frame while a level streams in (budgeted),
  and one BLAS rebuild per alpha mesh.
- **Unmeasured.** The baseline candidate counts per ray class come from `rt_stats`; a
  baseline job is queued (section 6).

### 3.7 Risks

- The bake source is residency-dependent: handled by mixing non-resident pages (always
  unknown, so exact).
- **Wrong UV rect or mip:** wrong states are invisible under 4-state only if they are
  "unknown". A *known* wrong state is a hard error, which `rt_omm_check=1` must show as 0.
- **Interaction with `rt_alpha_cut` and the 20 shaders with a UBO cut-off** (7.1b):
  - bake with exactly the cut the shader uses, keyed in the OMM key;
  - when `rt_alpha_cut` changes live, the cache is invalidated.
- **Screen-door dithered geometry** (0.95% opaque in front): its prepass `discard` is
  `gl_FragCoord` based, not texture based. It is not in the alpha table, so it gets no OMM.
- **SDK integration:** CMake 4.1+ and submodules (xxHash, lz4, glm, stb, ShaderMake) for
  the SDK build. We use only its library and pre-compiled SPIR-V. The license is the
  NVIDIA RTX SDKs license.
- **Toolchain:** the SPIR-V capability for the ray flag via `vk::ext_*` (3.3 item 6).
  Fallback: no 2-state, 4-state only. That still removes most candidates.
- **Validation layer coverage of `VK_EXT_opacity_micromap`:** a ratified EXT, so it is
  expected to be covered. Verify with `-Validation`.
- **Memory spikes** while baking a whole level at load: per-frame budget.

### 3.8 Test plan

**Offline:**
- DXC compile with the extension attributes;
- a unit test of the UV-rect rebasing and the mip choice (`tests/omm_rect_test.cpp`,
  pure math);
- the SDK's `omm-cpu-benchmark` sanity on one dumped grass texture (`dump` of the unpack
  output).

**In-game:**
```json
{ "name": "omm-S3", "tree": ".claude/worktrees/<branch>", "save": "Campsite - 27h",
  "set": "rt_omm=1,rt_stats=0", "traces": 6, "timeout": 900, "phases": "captures/omm_phases.json",
  "patterns": ["\\[omm\\] ", "\\[rt\\] scene build GPU", "gi: trace pass GPU", "rt stats .*alpha candidates"] }
```
- Phases: `rt_omm_use=0` → `1` (timings, the in-run A/B) → `rt_omm_check=1` →
  `rt_stats=1` (candidates per ray class with OMM) → `rt_omm_2state=0` (4-state only) →
  `rt_omm_debug=1`.
- Scenes: S3 Campsite, S4 Goblin Camp (dense vegetation), S2 Rivington (sun through
  trees), S1 (regression).
- **Primary-ray gate:** `rt_debug=3 rt_stats=1` match within ±0.01% of the OMM-off value
  (4-state).
- **QA** (`--scenes S3,S4 --reference`): `hard_dark` 0 and unchanged; `sun_extra_casters`
  unchanged; flicker within +1‰; `--reference` FLIP within the noise floor for 4-state,
  ≤ +0.01 with 2-state.
- `-Validation` (S3); `-Move` (S4) for the per-draw refit path with OMM.

---

## 4. Shared notes

- **R9:** every item lands off by default. The ini and the track docs are updated in the
  change that implements it, and a default is flipped only after R5, R6 and R7.
- **The serial test queue:** all in-game work above goes through
  `captures/queue/pending/*.json` after a build and commit, never by launching the game
  directly.
- **The push-descriptor ceiling** (32 on this GPU) is close: D-LS 26, D-RF 31. New
  resources for D-RF consumers go into persistent sets (cache set 4).
- **`Push` is full** (256 B): new per-pass constants go into the 5 spare words, then a
  UBO ring.

## 5. Downloads needed (not done)

| SDK | URL | Version | License | Size | Needed by | Notes |
|---|---|---|---|---|---|---|
| **SHaRC** | https://github.com/NVIDIA-RTX/SHARC | CHANGELOG 1.8.3 (latest tag `v1.6.5.0`, 2026-02-28; main pushed 2026-08-02) | NVIDIA RTX SDKs License (`License.md`) | repository about 14.4 MB (GitHub `size` 14,730 KB); the headers alone are a few hundred KB | item 2 | AUDIT_SOTA assumed `external/SHARC` 1.8.3 existed; **it is not in `external/` now** (only DLSS and Streamline). Take main at 1.8.3 for the overflow fix |
| **OMM SDK** | https://github.com/NVIDIA-RTX/OMM (release asset `Opacity.Micro-Map.SDK.Windows.zip`) | 1.10.0 (2026-10-14) | NVIDIA RTX SDKs License (`LICENSE.txt`) | Windows release zip 0.36 MB (360,881 B); repository about 21 MB plus submodules if built from source (CMake 4.1+) | item 3 | Check whether the zip includes the GPU baker's SPIR-V; otherwise build with the SPIR-V option and the Vulkan SDK |
| **RTXDI** (optional) | https://github.com/NVIDIA-RTX/RTXDI-Library (runtime only); full SDK https://github.com/NVIDIA-RTX/RTXDI | 3.1.0 (2026-09-03) | NVIDIA RTX SDKs License (`LICENSE.txt`) | runtime library 0.3 MB (294 KB); full SDK about 21 MB plus submodules (Donut, NRD, DLSS, LFS media) | 1b only | Not needed for 1a, 1c, 2 or 3. Take the runtime library only, for the ReSTIR DI resampling functions and the DLSS-RR decorrelation |

No NRC download: it was rejected (2.2).

## 6. Queued now (baseline measurements for the estimates above)

These run the main checkout's build, with no `tree`. This branch adds no runtime change,
and its worktree has no `data/assetdb`, which would turn the per-light data off. They give
the baselines the plans need:
- `20-baseline-alpha-S3.json`: Campsite, `rt_stats=1`. It gives the alpha candidates per
  ray class (visibility and shadow) and the hybrid GI hit tiers. This is the OMM baseline.
- `20-baseline-ls-S5.json`: Grymforge, default settings. It gives the D-LS cost, the GI
  trace and the scene build without the statistics' overhead. This is the 1a cost
  baseline.
- `20-baseline-ls-stats-S5.json`: Grymforge, `ls_stats=1`. It gives the listed, evaluated
  and traced lights per pixel. This is the 1a load baseline.

## References

- Bitterli, Wyman, Pharr, Shirley, Lefohn, Jarosz. *Spatiotemporal reservoir resampling
  for real-time ray tracing with dynamic direct lighting.* SIGGRAPH 2020.
- Heitz, Hill, McGuire. *Combining analytic direct illumination and stochastic shadows.*
  I3D 2018.
- Conty Estevez, Kulla. *Importance sampling of many lights with adaptive tree splitting.*
  HPG 2018.
- Moreau, Pharr, Clarberg. *Dynamic many-light sampling for real-time ray tracing.* HPG 2019.
- Pharr, Jakob, Humphreys. *Physically Based Rendering*, 4th ed., "BVH light sampler".
- Karras. *Maximizing parallelism in the construction of BVHs, octrees, and k-d trees.*
  HPG 2012.
- Boksansky, Jukarainen, Wyman. *Rendering many lights with grid-based reservoirs.* Ray
  Tracing Gems II, ch. 23 (ReGIR).
- Ouyang et al. *ReSTIR GI.* HPG 2021.
- Müller, Rousselle, Novák, Keller. *Real-time neural radiance caching for path tracing.*
  SIGGRAPH 2021.
- Boissé et al. *GI-1.0: a fast and scalable two-level radiance caching scheme.* AMD, 2023.
- NVIDIA RTXDI 3.1 (`Doc/Integration.md`, ChangeLog: DLSS-RR support and decorrelation in
  3.1; sample permutations since 1.2; boiling filter since 2.1).
- NVIDIA SHaRC `docs/Integration.md`: buffers (8 B hash, 16 B accumulation, 16 B resolved;
  2²² = 160 MiB), Update/Resolve/Query, the 5×5 update pattern, `-enable-16bit-types`,
  `HASH_GRID_ENABLE_64_BIT_ATOMICS`, debug views.
- NVIDIA OMM SDK README 1.10.0; the `VK_EXT_opacity_micromap` specification; NVIDIA blog
  *Path tracing optimizations in Indiana Jones: opacity micromaps and compaction of dynamic
  BLASs*.
