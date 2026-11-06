# Offline frame replay (GI, Ultra local shadows and default reflections)

Re-run one production pass on one captured frame without the game (docs/OFFLINE_OPPORTUNITIES.md A).
Pass: `gi_hit_direct` (`shaders/gi/gi_trace_rt_hit_direct.hlsl`, base variant). It was chosen over
`rf_hit_shade` because it reads only two sets (the GI push set and the material set 1): no box
lights, no probes, no sky cubes, no sun set. `rf_hit_shade` needs those in the pack first.
Since 2026-11-01 the pack also carries the deferred lighting's inputs (sets 2-3, "Lighting inputs"):
the probes, the sky cubes, the DFG LUT and the sun set, which `--native-ambient` and the PT read.

## Harness: `bin\frame_replay.exe` (tools/frame_replay.cpp)

```
frame_replay --pack <dir> [--out <dir>] [--hlsl <file> | --spv <file>] [--define N=V]... [--repeat N] [--pipeline-stats] [--spec ID=UINT]... [--reference-out <dir>] [--no-validation]
frame_replay --compile-only [--compile-pass gi|origin|reflections|local-shadows] (--hlsl <file> | --spv <file>) [--define N=V]... [--spec ID=UINT]... [--pipeline-stats] [--out <dir>]
frame_replay --synthesize <dir> [--dark] [--box] [--lighting] [--casters]
frame_replay --self-test <dir>          (CTest frame_replay_test)
frame_replay --load-test                (manual: the 2 GB S8 hit cache shape through the staged upload)
frame_replay --pack <dir> --native-ambient [--pt-iters N] [--out <dir>]   (default out <pack>/native-ambient)
```
- Windowless, own Vulkan device, implicit layers disabled (never the game layer), below-normal priority.
- Runs of many dispatches (`--repeat`, `--pt-reference`, `--native-ambient`) print `[replay] progress: done/total
  dispatches, iterations, rate, elapsed, ETA` every 30 s (flushed; silent below 30 s).
- On every run it compiles the pass shader from this checkout's `shaders/` with the layer's DXC flags
  (layer/shaders.cpp: vulkan1.3, HLSL 2021, cs_6_6, `BG3RTX=1`, SHaRC headers, `-O3`, the file's
  `// dxc:` line, the pack's `defines`). Edit the shader, rerun: about 3 s, of which DXC is most.
- It rebuilds one BLAS per distinct instance key from each instance's InstInfo (base, stride, tris;
  opaque soup or mesh pool, opaque unless kInfoAlpha), as layer/bvhsplit.cpp does, then the TLAS from
  the pack's instance array in its order (InstanceIndex = InstInfo index). Legacy soup, hair AABB and
  empty instances are kept with mask 0 and counted in a note.
- rt_cascade_casters (`casters FILE INSTANCE TRIS STRIDE` in the manifest, 2026-11-01): the cascade instance's
  triangles (the cut-away roofs and off-screen walls; its live InstInfo is `{0, stride, 0, 0}`) are appended to
  the soup b6 at the next 16-byte boundary past its captured range and its InstInfo entry points there
  (`rp::caster_patch`); its BLAS is built not opaque and the instance keeps the live mask 0x02, custom index
  0xFFFFFF and transform. Note: `casters: instance <i>, <n> triangles of stride <s> in the TLAS ...`.
  `--no-casters` leaves it out (mask 0, the older behaviour) for an A/B. GI rays of the pack's variant
  (`RT_CASCADE_MASKS=1`, recorded in `defines` since this change) trace 0x01, as live, so the replay of the pass
  is unchanged by them; packs before it have `defines -` and no casters and replay as before (S8 142819-1:
  bit-equal 99.5738%, the same counters, 2026-11-01).
- Every descriptor of the pass contract (common/replay_pack.h `kGiHitDirect`) is bound: pack data,
  else a zero dummy (listed as "not in the pack"). An image the writer `omitted` is bound NaN, so a
  material lookup that needs it fails as "sampling" (reason 6) instead of shading with garbage.
  One VkImage per pack file (the writer names a shared image from every element that binds it);
  a note counts the images the writer stored decoded (`image ... SOURCE`).
- Output in `--out` (default `<pack>/replay`): `giRaw.raw` (RGBA16F), `giRaw.pfm` (RGB float),
  `giRawDepth.raw`, `stats.txt`, `pass.spv`. The console prints the dispatch GPU time and the live
  log's lines for this pass with "1 frames": `rt stats ... hybrid GI`, `GI hit direct`,
  `GI hit direct lights`, `GI hit direct policy`, `hit material` (wording of core/gi.cpp), plus a guard
  line. With a live `output ... FILE` reference it prints the bit-equal share and mean/max |diff|.
- `rtstats.bin`: the raw counters (512 x 64 bins of uint32). With `--define HM_BLOCKERS=1` (replay only, never a
  game variant) hit_material_count also counts every lookup per TLAS instance (< 1024) and outcome (qualified,
  base without a record, row, other) in counters 448..511; `py -3 tools/hit_material_blockers.py --pack <dir>
  --stats <out>/rtstats.bin` ranks the blockers by capture class and instance (extent, area, distance, alpha FS).
  The outputs are bit-identical to the run without the define (S8 pack: same giRaw fnv1a).

## Driver executable statistics and exact offline A/B

`--pipeline-stats` enables `VK_KHR_pipeline_executable_properties` and its
`pipelineExecutableInfo` feature when supported, and creates the measured compute pipeline
with `VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR`. It prints every driver executable's
name, description, stages and subgroup size, then every statistic with its name, typed value
(boolean, signed/unsigned 64-bit integer or double) and description. NVIDIA can report register
count and shared/local memory. Unsupported drivers print an explicit `unavailable` line and
continue without capture. Statistics concern the specialized driver executable; SPIR-V instruction
counts are a different measure. Capture is opt-in because it may affect pipeline creation cost.

`--spec ID=UINT` (repeatable; decimal or `0x` integer) passes uint32 Vulkan specialization values
to the measured pipeline. Unspecified IDs keep their shader defaults. It prints the chosen IDs.
Use the same values as `core/gi.cpp` for a production comparison.

`--compile-only` builds a production pipeline without a pack, resources or dispatch.
`--compile-pass gi` (default) supports non-SHaRC GI sets 0/1, the emission scene at set 2
and the sun at set 3, including sun trust and the native tileset sampler.
`--compile-pass reflections` supports non-SHaRC hit-shade reflections: push/material/game/sun
at sets 0/1/2/3, empty set 4, emission set 5 and hit-GI counters set 6.
`--compile-pass local-shadows` supports box local shadows: push/material/sun/game/counters
at sets 0/1/2/3/4. The layouts reuse the production binding tables; the game's runtime
image array is represented with the replay's 4096-element capacity. Set 0 is an ordinary
descriptor layout here (no push commands are submitted). This is not generic shader reflection: arbitrary passes
with other descriptor contracts need their own layout. Give exactly one of `--hlsl` and `--spv`,
no `--pack`. Default output: `.scratch/pipeline-stats`. Prefer `--spv` from
`tools/shader_identity.py` for cached compilation and machine-wide DXC slot coordination.
`--hlsl` uses the harness's existing compiler and the explicitly supplied `--define`s; compile-only
has no pack from which to infer defines. Production Ultra (non-OMM) request example:

```powershell
frame_replay --compile-only --hlsl shaders/gi/gi_trace_rt_hit_sun_em.hlsl --pipeline-stats `
  --define RT_SPEC_NATIVE=1 --define RT_SPEC_HIT_INST=1 --define RT_SPEC_CASCADE=1 --define RT_SPEC_GI_FAR=1 `
  --spec 1000=1 --spec 1003=1 --spec 1004=1 --spec 1007=1 --spec 1008=0 --spec 1010=0
```

For `rt_compact_hit_state`, compare the same module with `--spec 1029=0` and
`--spec 1029=1`. Keep all other production constants fixed. To replay a captured pass,
compile its exact manifest `defines` (S8 has none; camp/inn have `RT_CASCADE_MASKS=1`);
`--spv` supplies the complete module and cannot apply the manifest's defines afterward.
The compile-only layout does not support SHaRC's set 4. Driver statistics are printed
verbatim, including unexpected values; do not interpret them as measured occupancy or spills.

Production RF/LS (Ultra, non-OMM) requests in `shaders/variants.txt`:

```text
gi/rt_reflections_hit_shade_em.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SPEC_HIT_INST=1;RT_SPEC_CASCADE=1;RF_SPEC_HIT_GI=1
gi/rt_local_shadows_box_k.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SPEC_CASCADE=1
```

Compile those exact requests through `tools/shader_identity.py` and pass the cached modules
with `--spv`. Match `core/gi.cpp::refresh` specialization values; stock visible hair has
1008/1009/1010/1020/1026 = 0, cascade 1000 = 1, instancing 1003 = 1 and wind RGB
1004 = 1. Hit GI 1013 = 1 for the requested hit-GI reflection tier (use 0 for the current release Far choice). Example (after checking no `bg3.exe`):

```powershell
bin/frame_replay --compile-only --compile-pass reflections --spv .scratch/rf-ls-regs/rf.spv --pipeline-stats `
  --spec 1000=1 --spec 1003=1 --spec 1004=1 --spec 1008=0 --spec 1009=0 --spec 1010=0 `
  --spec 1013=1 --spec 1020=0 --spec 1026=0
bin/frame_replay --compile-only --compile-pass local-shadows --spv .scratch/rf-ls-regs/ls.spv --pipeline-stats `
  --spec 1000=1 --spec 1008=0 --spec 1009=0 --spec 1010=0 --spec 1020=0 --spec 1026=0
```

`rf_compact_probe_state=0` is a developer raw key, specialization 1031. Compare
`--spec 1031=0` / `--spec 1031=1` on the same module. Core permits it on every
`rt_reflections` request, including default Ultra with hit shading and hit GI OFF.
No shader requests are added. Default Ultra's request is
`gi/rt_reflections.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SPEC_CASCADE=1`;
use the compile-only reflections command above with its cached module and omit
the unused hit-instancing, wind RGB and hit-GI constants. On RTX 5090, 1031 OFF/ON
uses 128/96 registers and 0/1536 shared bytes (2026-11-06). These are driver
pipeline statistics, not a live timing or quality qualification.

These compile-only selectors only create pipelines and print driver statistics. Ordinary
`--pack` replay also supports production Ultra local shadows and default reflections as
described below. Compile-only statistics cannot establish image exactness.

With `--repeat N`, the summary prints the median, min, max and max-minus-min spread of **all**
dispatch GPU timestamps, including the first dispatch. Retain the individual run lines to diagnose
warm-up or outliers; compare at least two OFF repeats before attributing a small timing change.

`--reference-out <OFF-output-dir>` compares the final replay's raw outputs component by component
and its raw `rtstats.bin` counters against a previous replay. It prints `vs replay ...: bit-equal`
lines and returns nonzero for any changed output bit or counter. This is separate from the pack's
`vs live` comparison (reconstructed replay geometry can differ from the live frame). The reference
and output directories must differ; use `--out` outside source packs to keep captures unchanged.
For example, run OFF to `.scratch/gi-off`, then ON to `.scratch/gi-on` with
`--reference-out .scratch/gi-off`, matching every other shader, specialization and push option.

## Paired A/B timing

Use `--ab-spec ID=A,B` (repeatable) with common `--spec ID=UINT` settings to
create both pipelines once over the same uploaded pack and acceleration structures.
`--ab-spv <B.spv>` optionally supplies a different module for B; `--spv <A.spv>`
supplies A. This supports sun versus direct attribution without a new production flag.
Both modules must use the same descriptor/push contract. Prefer cached SPIR-V from
`tools/shader_identity.py`, with the exact pack defines.

`--repeat N` means N paired samples (default 40 in A/B mode). Four warmup dispatches
(two per pipeline) are excluded. Each sample runs ABBA, then the next BAAB, and uses
`mean(B) - mean(A)` timestamps: the symmetric sample cancels linear drift in dispatch
order. Timings include the compute dispatch, excluding uploads, output readback
and clears. A negative delta means B is faster. `ab.csv` retains every pair; the
console and `ab_summary.txt` report the median delta and a distribution-free 95%
interval for its population median, using binomial order statistics (conservative
coverage for ties). Small samples can produce infinite bounds.

`--ab-warmup N` (0..256, default 0) runs N additional untimed dispatches immediately
before each measured dispatch in the same GPU submission. All mutable outputs and
counters are restored before every warmup and again before timing. This avoids
timing a cold GPU after CPU readback gaps; the selected count is printed in the
receipt. It changes no production shader or global GPU setting. Check a same-module
40-pair control before interpreting a candidate's interval.

Every measured dispatch is compared byte for byte with the first dispatch in its
quartet: all stored output components and every raw counter/bin. The summary requires
all comparisons to match to print `100% bit-equal`. The first failed quartet reports
the differing-byte count and first output component/pixel/channel, plus the first
raw counter index/bin and its values when counters differ. That quartet also saves
`ab_A_<output>.raw` and `ab_B_<output>.raw` for full numeric diagnosis (and
`ab_A_rtstats.bin` / `ab_B_rtstats.bin` when counters differ). An unexpected
same-pipeline mismatch uses `_repeat` for its second snapshot. Attribution can intentionally
change outputs; add `--ab-require-equal` to return failure for any changed bit.
Ordinary output files contain the final dispatch; use `ab.csv` for paired timings.

`--ab-self-test` checks drift cancellation and confidence bounds without a GPU.

```powershell
bin/frame_replay --pack <pack> --spv sun.spv --sun-trust trust.bin --push radius=6 `
  --ab-spec 1007=1,0 --repeat 40 --out .scratch/far-split
bin/frame_replay --pack <pack> --spv sun.spv --ab-spv direct.spv --spec 1007=0 `
  --sun-trust trust.bin --repeat 40 --out .scratch/sun-split
```

## BLAS policy A/B (replay only, 2026-11-05)

`--blas-policy legacy|layer|trace` selects reconstruction flags. Default `legacy` preserves
historical replay (all FAST_TRACE, no update flag). `layer` uses FAST_TRACE | ALLOW_COMPACTION
for static mesh-pool BLAS and FAST_BUILD | ALLOW_UPDATE for dynamic/cascade BLAS.
`trace` changes only dynamic/cascade to FAST_TRACE | ALLOW_UPDATE. The cascade policy models
`rt_cascade_refit=1` (Ultra default). Static BLAS are built but not compacted in replay.

`--ab-blas --repeat 40` keeps both layer A and trace B BLAS/TLAS resident and selects the
TLAS outside each timed dispatch. Static BLAS are shared. Existing ABBA/BAAB timing,
per-sample warmup, output/counter checks and first-mismatch snapshots apply unchanged.
`--blas-control` with `--ab-blas` builds both sides using layer flags (zero-change control).
After trace measurement, `blas.csv` and `blas_summary.txt` report 40 paired GPU timings
of build and refit, separately for dynamic class 1 and cascade class 2. Each timestamp
covers one batched build call with disjoint aligned scratch for all BLAS in that class;
it excludes allocation, upload and TLAS work. Four warmups per measurement are excluded.

Classes are derived from InstInfo: soup = dynamic, pool = static unless kInfoHeld bit 31;
static alpha pool row indices (1..4096 in bits 16..31) cannot overlap bit 31. The manifest's
casters directive identifies cascade geometry before its patched InstInfo is classified.
Unique BLAS/tris and held counts are printed; conflicting classes for a shared key fail.
Legacy/hair/unavailable geometry remains masked out as in ordinary replay.

Run only without bg3.exe. Use a supported sun/far module, `--sun-trust`, Ultra spec values,
`--push radius=6 --push rays=2 --push rt_stats=0 --ab-warmup 8`. Old GI packs lack the
emissive scene set: `_em` shaders cannot run on their lighting set 2. Results without
emission are an approximation to full Ultra, and frozen-geometry refits cannot establish
cost or traversal quality after animated deformation. A stable-scene estimate uses
(build + R * refit) / (R + 1), R = 600 dynamic and 60 cascade; topology changes add builds.
`--ab-self-test` also checks class derivation, including every static alpha pool row.

## Origin replay (2026-11-05)

`--origin` adapts an existing `gi_hit_direct` pack in memory. It reads `Push::full`,
requires full-resolution depth/normal images and captured camera buffers, dispatches
8 x 8 groups over the full image, and replaces GI outputs with `rtOrigin.raw`
(RGBA16F) and `rtOrigin.pfm`. The captured origin input becomes the live reference;
input and output never alias. Set 2 b3 supplies material at set 0 b4. Source packs
are unchanged. `--origin-self-test` checks the adapter without a GPU (also a CTest).

Use cached SPIR-V for the exact production request through `tools/shader_identity.py`.
`--compile-only --compile-pass origin` includes the material binding missing from the
GI compile-only layout. Stock Ultra request:
`gi/rt_origin.hlsl?RT_SPEC_NATIVE=1;RT_SPEC_HAIR_ORIGIN=1;RT_SPEC_CASCADE=1`.
Use `Result.path()` / `Result.blob()` from the helper: its CLI JSON digest identifies
all request entries together, rather than naming an individual SPIR-V blob.

```powershell
bin/frame_replay --compile-only --compile-pass origin --spv origin.spv --pipeline-stats --spec 1000=1 --out .scratch/origin-stats
bin/frame_replay --origin --pack <pack> --spv origin.spv --ab-spv candidate.spv --repeat 40 --ab-require-equal --spec 1000=1 --out .scratch/origin-ab
```

Paired A/B compares **every byte**, including raw counter bins. The rounded `vs live`
percentage is only context: the S8 pack differed by at most 0.00006103516 in one
component despite printing 100.0000%. Rebuilt AS order and unsupported/masked instances
prevent a universal live-equivalence claim. This mode supports stock alpha-card hair;
native/scalp/groom/physical-hair/tileset specialization values must remain 0. It shares
the ordinary replay's missing-input/AS limitations. OMM is not reconstructed. Check no
`bg3.exe` before each GPU command and serialize GPU lanes for useful timing intervals.

## PT reference: `--pt-reference` (tools/pt_reference.hlsl, M6 offline)

```
frame_replay --pack <dir> --pt-reference [--pt-iters N] [--out <dir>] [--define PT_ALBEDO=0.3]...   (default out <pack>/pt-reference)
frame_replay --pack <dir> --pt-cost                                                                  (only the cost kernels)
frame_replay --pt-self-test <dir>                                                                    (CTest pt_reference_test)
```
- An offline, cache-free, unfiltered opaque path tracer over the pack, NOT a game shader: the harness
  compiles `tools/pt_reference.hlsl` with the gi_hit_direct contract (it binds a subset) instead of the pass.
- Per GI pixel it estimates giRaw.rgb's quantity (cosine-weighted mean incident radiance) from the raster
  G-buffer point, without radius, screen radiance or fallback: N-vertex paths (default 8, Russian roulette
  from vertex 3), NEE at every vertex for the froxel's point/spot lights (`native_hit_direct`, the screen's
  light policy, RT shadows) and the sun (cam `_m0`, colour x b13[0], disc-sampled RT ray), the declared
  FresnelBlend BSDF (diffuse + GGX, `common/transport_bsdf.h`), continuation by `hb_continuation`.
- Materials: `rt_hit_material` where it qualifies; else the declared fallback `PT_ALBEDO` (0.5), `PT_ROUGH`
  (0.5), reflectance 0.5. So texture sampling is optional: a pack without the VT cache shades every hit
  with the fallback, a pack with it uses the native layers where the rows qualify.
- Not in the pack, so not in the PT (each reported): sky/environment (a miss is 0; its share is the miss
  fraction), emission, projector lights, cloud shadows, the atmosphere LUT (`PT_SUN_T`, estimated from the
  sun elevation with Kasten-Young air mass + Rayleigh/aerosol depth unless given), roofs the camera cut away.
- A pack with the lighting inputs: `--pt-reference` takes the sun from the game's transmittance LUT and
  its two cloud-shadow layers (`PT_SUN_LUT=1`, `bg3_sun_colour`, `bg3_cloud_shadow` at every vertex).
  `PT_ENV=1` (set by `--native-ambient`) adds mode 7: the environment radiance a miss sees (the global
  specular cubes b35 / b37 at mip 0, x `_m27` / `_m28`, blended by `_m29`, x b13[0] 100), carried by the
  path throughput, in game units, kept out of modes 0-6 so the traced split is unchanged. Indoors a miss
  through a cut roof sees this sky too (the native lighting would use a local probe there): declared.
- 7 modes per iteration (Push::debug; same seed via Push::ls_kmode, Push::frame stays the hit-material
  epoch), so every split is over the same paths: total, 1-vertex, screen radiance at visible hits, the live
  domain (first hit within gi_rt_radius, its fade), diagnostics, and the energy by first hit (visible /
  hidden / off screen, near / far), by light (sun / local). Outputs: `pt_report.txt` (the numbers),
  `pt_tiles.csv` (16 x 9 tiles), `pt_total.pfm`, `pt_live_domain.pfm`, `live_giRaw.pfm`, `pt_compare.bmp`
  (live | PT live domain | PT total, one exposure).
- Calibration k = game screen radiance / PT radiance on the visible first hits of the same paths: it
  absorbs the fallback albedo, the estimated sun transmittance and what the PT lacks (ambient, emission).
  Shares (%) are scale-free; absolute comparisons use PT x k.
- Cut-away roofs (`--define PT_ROOFS=1`): `py -3 tools/pt_roofs.py --pack <dir> --level <name>` first writes the
  pack level's asset-DB roof volumes to `captures/pt_roofs.hlsli` (camera-relative, from the pack's cam2). Sun rays
  through a padded volume (3 m around, `PT_ROOF_SUN_UP` 50 m up: the production rule) are occluded; path rays that
  cross a volume's roof plane (`PT_ROOF_PLANE_UP` 3 m above the trigger top) end there with no radiance, and first
  rays that do so are reported in the `aborted` fraction (0 without PT_ROOFS on a pack without aborts).
- Cut-away roofs as triangles (`PT_CASTERS=1`, set by `--pt-reference` / `--native-ambient` for a pack with the
  `casters` directive unless `--no-casters` or a `PT_CASTERS` / `PT_ROOFS` define is given): shadow rays (sun and
  local lights) trace 0xFF and take a cascade triangle beyond `RT_CASCADE_TMIN` / `RT_CASCADE_TMIN_REL` x |origin|
  (0.5 m / 2%: the live sun variant's rule, the shadow-LOD copy of the surface a ray leaves is no occluder); path
  rays take the nearest cascade triangle beyond the same distance and before the scene hit (`pt_caster_hit`) as a
  vertex with the fallback material and the triangle's normal. The near rule lets paths leaving a wall within
  0.5 m of a roof pass it (-0.6% in the test box). The parameters line says `PT_CASTERS=1 (the pack's cascade
  casters: <n> triangles)`.
- gi_far offline: `--define RT_GI_FAR=1 --define GI_FAR_M=6` runs the pass with the far rule (the pack's push holds
  the base pass's radius; GI_FAR_M stands in for gi_far_m). The live path instead: `--define RT_GI_FAR=1 --push radius=6`.
- `--push FIELD=VALUE` (repeatable) overwrites one field of the pack's push before the dispatch: `radius`,
  `rays`, `gi_rt_radius`, `rt_stats`. S8 pack 142819-1, hit_sun + RT_GI_FAR, push radius 6: giRaw.a 0.8320 with rt_stats 0
  and 1 (key off 0.8790), the same as the GI_FAR_M define (2026-11-02).
- gi_hit_sun / gi_far with the sun tier: `--hlsl shaders/gi/gi_trace_rt_hit_sun.hlsl --sun-trust <file>`. The sun
  variant also reads the D-LS sun set's b8 (trust header + roof volumes, core/sun_trust.h), which the pack lacks:
  `py -3 tools/pt_roofs.py --pack <dir> --level <name> --trust <file>` writes it (padded as core/sun_trust.cpp:
  3 m around, 50 m up), the replay binds it at set 3 b8 and sets Push::ls_flags bit 25. Without it every far
  qualified hit takes the old rule (roof volumes unknown). The console then prints `GI hit sun` and `GI far` with
  the live log's wording. A pack with the `casters` directive needs no trust file for the roofs' geometry: its
  sun ray (RT_CASCADE_SUN_HIT) sees the cascade instance as live (the trust volumes still gate the replace).
- Cost kernels (`pt cost` lines): 1 spp with 1 and 2 vertices, rays up to 300 m and 8 m, median of 9
  dispatches, scaled per Mpx to the DLSS 4K render resolutions.
- CTest `pt_reference_test`: a closed box (synthetic pack) where every vertex emits 1: Lambert rho 0.5 with
  1 and 3 vertices gives exactly 1 and 1.75; rho 0.8 with Russian roulette gives 5 within 4 standard errors;
  the FresnelBlend white furnace with 4 vertices stays in (2.5, 4] (no energy gain); closure (miss 0); the
  slab pack's native material row qualifies and its light lights the PT, the dark pack gives exactly 0;
  two runs bit-identical; the casters box (its floor only in a cascade instance, moved 1 m by its transform):
  PT_CASTERS closes it (B=1: exactly 1, miss 0; B=3 with the near rule off: 1.75), without it the pack's
  RT_CASCADE_MASKS rays see the floor open (mean = 1 - miss); no validation errors.

## Native ambient: `--native-ambient` (tools/native_ambient.hlsl)

The far field the 1 m GI rays do not reach is, in game, the native probe ambient. This compares it with
the PT reference over the same GI pixels, on a pack that has the lighting inputs (else it stops with
"the pack has no lighting inputs").
- `tools/native_ambient.hlsl` (not a game shader) replays the tiled lighting's diffuse ambient at each GI
  pixel's G-buffer point from the pack's set 2, with the in-game replicas' functions: the sky irradiance
  cubes b34 / b36 at N (x `_m27` / `_m28`, blend `_m29`) x DFG.z, the local diffuse probes listed in the
  pixel's froxel lerped over it (`native_hit_diffuse_probes`), x b13[0] 100. E_native (diffuse colour 1)
  is the giRaw.rgb quantity, so it compares with the PT directly; the lighting multiplies both by the
  diffuse colour. Also: the game AO (b6 .x), the as-lit term (x diffuse colour x AO), the specular
  ambient (`bg3_spec_ambient` x SO), and the sun transmittance read from the LUT (`bg3_sun_colour` /
  colour x intensity) with the cloud shadow. Declared: roughness without the quad specular AA; skin as
  albedo x (1 - metalness); no aerial perspective (1 indoors).
- The PT runs as `--pt-reference` with `PT_SUN_LUT=1` (LUT in the pack) and `PT_ENV=1` (sky cube in the
  pack), `--pt-iters` iterations (default 32). Far field = PT total - PT live domain (first hits within
  gi_rt_radius x the live fade), x k (game / PT on visible first hits, as pt_report); reference =
  k x PT far + PT environment (game units).
- Pixels: those the live pass shaded (live giRawDepth > 0). Luminance (Rec. 709). Output
  `native_ambient_report.txt`, `native_ambient_tiles.csv` (16 x 9), `native_ambient.pfm` (E_native x AO),
  `pt_far_field.pfm` (k x PT far + env), `native_compare.bmp` (native x AO | reference | native). The
  deciding lines:
  - `native ambient means (...): E_native <v> (sky irradiance only <v>), x game AO <v> (AO <a>), x game AO x live a <v> ...`
  - `PT far field (...): <v> PT units (...); k = <k>; k x PT far <v>; PT environment through misses <v> (...)`
  - `native vs PT far field: E_native x AO / (k x PT far) <r>, E_native x AO / (k x PT far + PT env) <r>, ...`
    (above 1: the native term is brighter than the traced far field; below 1: darker)
  - `per tile (16 x 9, ...): E_native x AO / (k x PT far + PT env) p10 <> median <> p90 <>, ...`
  - `PT split (compared pixels, % of PT total): far field on screen beyond gi_rt_radius <f>% (visible <v>, hidden <h>),
    off screen <o>%, live domain <l>%; sun <s>%, local point/spot <q>%` (pt_report's definitions over these pixels)
- Per-scene table: `py -3 tools/ambient_table.py <pack> [<pack> ...]` (or `--scene NAME <pack>`) prints one markdown row
  per pack from its `native_ambient_report.txt`: E x AO, k x PT far, env, the ratio, the tiles p10 / median / p90, the
  far-field share (on screen + off screen), sun / local, k and the roof model (casters / volumes / none). CTest
  `ambient_table_test`.
  - `sun transmittance: game LUT <rgb> (...), Kasten-Young estimate <rgb>; cloud shadow mean <c>`
- Per-pixel terms for offline comparisons (PFM, bottom-up rows): `native_E.pfm` (E_native), `native_lit.pfm` (as
  lit), `native_ao.pfm` (game AO), `pt_total_rgb.pfm` / `pt_live_rgb.pfm` / `pt_env_rgb.pfm` (PT units), and
  `pt_k_terms.pfm` (r: screen radiance at visible first hits, g: the PT's radiance there; k = sum r / sum g).
- Limits: k absorbs the fallback albedo where the pack has no VT cache (one global k, region numbers
  are rough until the VT-cache pack); the native AO is the game's SSAO before the RT AO fold.
- Cost: one native dispatch per mode (3) plus the PT run (S8 2026-11-01: 11.6 s GPU for 32 x 7).
- Run on S8 (142819-1, docs/PT_FEASIBILITY_OFFLINE.md); camp and inn packs with the casters: the live batch
  tools/live_batches/replay_capture/ambient (READY_FOR_LIVE).

## Lighting inputs (sets 2-3 of the pack, common/replay_pack.h `kLightingSources`)

Set 2 = the tiled lighting's own set 1 (f469db78) at its binding numbers, as D-RF binds it as set 2:
b2 RT1 albedo, b3 RT2 material, b6 AO, b16 / b32 samplers, b33 local probe records (144 B), b34 / b36
sky irradiance cubes, b35 / b37 global specular cubes (the sky/environment radiance), b38 DFG LUT,
b45 the probe cubes (bindless, `kSampledCube`, 4096 elements). Set 3 = the sun set in the
`shaders/rt/bg3_sun.hlsli` layout: b0 the atmosphere constants (game set 5 b0), b1 the transmittance
LUT (set 5 b1), b2 / b3 cloud layer A (set 5 b4 / b5), b4 / b5 cloud layer B (set 1 b42 / b43), b6 a
declared linear clamp sampler. Cubes are `image` lines with 6 layers (the faces in Vulkan order);
the replay makes them cube-compatible and binds cube views. Older packs have none of it: the replay
binds zero dummies and notes "N lighting inputs (sets 2-3) not in the pack".
- Live writer: the core records the pack inside its pre-lighting hook (`run_pre`), where the game's
  lighting sets are bound on the compute bind point and the core's own binds are not tracked, so the
  layer reads them with `query_binding` / `query_set` (no ABI change, no core change). The AO is the
  game's SSAO before the RT AO fold (it runs later in `run_pre`). Every input is droppable and
  optional (an absent one is a note). b45: the cube elements the frame's probe records name
  (`rp::probe_cube_elements`: diffuse pair m5 bits 3-16 / 17-30 when bit 1, specular pair m6 when bit
  2; elements 2 i, 2 i + 1) when the records are host-mapped, else every cube element bound; 2D
  cookie elements of the same binding are skipped. Samplers: the game's when recorded, else declared
  trilinear clamp.
- Budget priority (`rp::texture_priority`): lighting inputs 4 (first), hit material 3, probe cubes 2,
  alpha caches 1: the S8 pack keeps its 2054 MB hit cache under `replay_pack_cap_mb=3300`, the probe
  cubes take what is left, smallest first.
- Log and note: `[replay] pack <n> lighting inputs: <b> of 19 slots bound; probe cubes <c> named by <r>
  records (records read | records not host-readable: every cube element), <k> of <e> bound cube
  elements captured, <x> beyond the slot, kept <c>; images kept <k>, dropped <d> (budget); samplers game
  <g>, declared <d>; <MB> MB[; absent: ...]`. The `textures` line lists them per binding (`s2b34 1/1`).
- Size estimate (2560 x 1067): RT1 + RT2 22 MB, AO 11-22 MB, global cubes and LUTs a few MB, clouds 1-4
  MB, probe records < 1 MB; probe cubes unknown (the first pack's line), capped by what the budget leaves.

## Pack format (common/replay_pack.h)

A folder with `manifest.txt` (grammar in the header) and raw files: `push`, `buffer`/`range` (sparse
buffer slices at their real offsets, so record offsets stay valid), `image` (VkFormat, mips, layers;
an optional last field names the game format of an image stored decoded),
`omitted`, `sampler`, `instances` (VkAccelerationStructureInstanceKHR as read at submit, the BLAS
address kept only as a key), `output` (+ the live result). The geometry is not duplicated: it is the
soup / pool slices the InstInfo entries name, plus `casters` (rt_cascade_casters: `casters.bin`, the converted
cascade soup, 52 MB at camp and 84 MB at the inn on 2026-10-31 logs). `defines` holds the live variant's
defines the contract carries (`rp::contract_defines`: RT_CASCADE_MASKS); a note names the others it left out.

## Synthetic pack and CTest `frame_replay_test`

`--synthesize`: a 128 x 128 frame (GI 64 x 64, 8 rays) of a back plane, a slab whose front face is in
the depth buffer and whose back face (20 cm behind it) carries an opaque hit-material row (real 8995
recipe, all-zero page table, 1 x 1 tile caches: albedo 0.8/0.6/0.4, roughness 0.5) and one point light
between slab and plane. GI rays from the plane hit the back face on screen but hidden, so the real
`rt_hit_material` and `native_hit_direct` run. The test writes a lit and a dark (light colour 0) pack,
replays each twice from disk and checks: rays and hits; candidates = qualified (356, all reason 0);
supported = nonzero (356); two dispatches bit-identical (images and counters); outputs finite; dark:
bounce exactly 0, same candidates; giRaw.a bit-equal lit vs dark (occlusion does not depend on the
light); no validation errors. Skips (77) without a ray-query device or the validation layer.
Checks 9-11 run the writer's image read (`shaders/replay_image.hlsl`, the same piece plan) on 13 x 9
x 3-layer, 3-mip images of all 37 mapped formats: uncompressed texels keep their bits (BGRA8 as
RGBA8), every stored texel is within half a step of the game format's decoded value, and the piece
split does not change the bytes. RTX 5090, 2026-11-01: BC1/2/3/7 (UNORM and sRGB) and BC6H come back
bit-exact (0.00 steps); BC4/BC5 0.50 / 0.49 steps (the decoder keeps more than 8 bits); A2B10G10R10
0.50 steps of 16 bits. Check 12 uploads BC3, R8, RGBA16F and BC7 arrays through a tiny staging
(block rows, layer groups, whole mips; 62 pieces), from memory and streamed from a file, and reads
every mip and layer back bit-equal.
Checks 13-16 load a third synthetic pack with lighting inputs in closed form (kSynthAmbient: sky
irradiance cube (0.25, 0.5, 1), specular cube 2, DFG (1, 0, 1), AO 0.5, one sphere probe with a diffuse
cube 3 listed in the froxel, a transmittance LUT (0.7, 0.6, 0.5) on the atmosphere sun path) and run
`tools/native_ambient.hlsl` and the PT with `PT_ENV=1 PT_SUN_LUT=1`: E_native = 300 (the probe over the
sky, x b13 100) with the sky part 48.295 and AO 0.5; as lit = E x albedo x AO; the transmittance read
back within 4e-4 (half floats); a first-ray miss sees exactly 200 (specular cube x b13 100) and the
traced total stays 0. RTX 5090 2026-11-01: all pass, errors at float rounding.
Pack 3.5 MB (the hit-record table is 3.1 MB of it). Wall about 3.3 s; dispatch 0.004 ms (RTX 5090).

## Size (at most `replay_pack_cap_mb` per pack: default 2048 MB, `bg3::replay::kPackHardCapBytes`)

Ini key `replay_pack_cap_mb` (diagnostics, not a GUI knob; `rp::pack_cap_bytes`: <= 0 is the default,
clamped to 256..16384 MB) sets the cap; the rules below are relative to it. The S8 replay job sets
3300 (one-off, user-approved) so the 2054 MB hit cache fits. The writer stages in 256 MB host-visible
chunks; the replay uploads every image and buffer in pieces of at most 256 MB (`fill_image_pieces`,
the writer's `rp::transfer_pieces` plan; image files streamed, never one 2 GB host allocation), checks
the device's maximum image size and layers, and prints `[replay] loaded <n> images (largest <MB> MB),
device-local <MB> MB of a <MB> MB heap`. `frame_replay --load-test` (manual, not a CTest) creates
and fills that S8 cache shape: RTX 5090, 2026-11-01, 10 pieces in 0.9 s, mip 1 read back bit-equal,
2218 MB device-local of a 32187 MB heap, no validation errors. A 3 GB pack fits a 32 GB GPU easily.

The geometry is taken whole while the never-dropped part stays under 1 GB (`kPackBudgetBytes`); the
textures fill the rest up to the cap (`rp::fill_budget`): highest `rp::texture_priority`
first (4: the lighting inputs of sets 2-3; 3: gHitCache b14 and the table gAlphaTex b2, whose VT
page tables and 2D views the hit material samples too; 2: the local probe cubes set 2 b45; 1: the
alpha tile caches b3), within a priority smallest first, each image
that still fits. An image larger than what is left is dropped and the smaller ones still go in.

S8 packs (2026-11-01), what they measured:
- All real textures have TRANSFER_SRC and are copied raw (BCn kept): the first pack copied all 32
  table elements (20 images, 67 MB); the 4064 elements without it were the layer's 4 x 4 dummies.
  So `read by shader` counts the two dummies (2D and array), not game textures.
- The VT caches: the first pack omitted 3 cache uses as larger than a 256 MB staging chunk. The
  second (055812, 906 MB) kept no texture: `images omitted 4099 (... layout 1, budget 4098); images
  copied 7, read by shader 1`. The old order (alpha-only first, largest first) dropped every table
  element and the alpha cache, then still had to drop the hit cache b14[1]: so b14[1] alone is
  larger than the 1142 MB left (2048 - 906). b14[0] was omitted as "two views of one image": the
  same VkImage as the alpha cache b3[1] (at least 256 MB) through another view.
- So the two caches together exceed the cap even with nothing else in the pack. With the fill the
  pack keeps the tables, the dummies and every cache that fits: expected 906 + 67 MB + b3[1]
  (if under 1075 MB); b14[1] stays dropped. Carrying both needs a cap of more than 2.4 GB (906 + 67
  + 256 + 1142 MB, all lower bounds; the textures line gives the real sizes) or a tile capture of
  only the cache pages the hits sample (the pass's own feedback: core and hit-material shader,
  outside the replay files). Tiles named by the hit tables' page tables would likely not shrink it:
  the 16 hit tables are the scene's tile sets, whose page tables point at nearly every resident tile.
- Two views of one image now share one capture when their formats store the same bytes (transfer
  copy, `rp::raw_compatible`, e.g. BC7 UNORM / SRGB: each `image` line keeps its view's format) or
  are shader reads of the same range (another format: its own read). Other differences stay
  omitted; the note `two views of one image (omitted): ...` names the mips, layers and formats.
- Measured per pack: the note and log line `[replay] pack <n> textures: <MB> MB free under the <cap> MB cap
  for <n> droppable images of <MB> MB; kept ..., dropped ...; by binding (kept/images, MB kept/all):
  ...; largest: <bindings>[<element>] <VkFormat> <w>x<h> x<layers> m<mips> copy|read <MB> MB
  p<priority> kept|dropped; ...` (the 10 largest).

Estimate for a 2560 x 1440 render frame, to be replaced by the first live pack's own line:
- set 0 images: depth 4 + normal 4 + velocity 4 + prevRadiance 8 + rtOrigin 8 B/px = 103 MB;
  live giRaw / giRawDepth reference (half resolution) 11 MB; light lists, UBOs, hdSlots < 3 MB.
- set 1: hit records 3.1 MB + pool rows 1 MB; InstInfo + instances 80 B per instance (5 MB at 60k);
  soup and pool slices only where an instance points (3 x stride x tris each: about 200-400 MB at
  2-4 M unique triangles; static meshes counted once per BLAS key, not per instance); pool basis ranges.
- textures: the elements the material set binds (gAlphaTex, gAlphaCache, gHitCache; unused slots
  hold the layer's 4 x 4 dummies) with their mip chains. Only they are dropped (`omitted`), as above;
  geometry and set 0 never (a pack whose never-dropped part passes the cap (`replay_pack_cap_mb`)
  is "over budget" and not written; its log line lists the cap and the MB per slot).
- Only each descriptor's bound bytes are captured (`rp::bound_bytes`). A set-1 buffer bound inside
  the soup or pool VkBuffer reuses the geometry's slices (`rp::alias_ranges`, `range` lines into the
  soup's file): with the split BVH gAlphaSoup b1 binds the whole soup like b6. First live pack (S8,
  2026-11-01): 1874 MB without textures, of which b1 whole was 1408 MB; set 0 + other buffers about
  157 MB, pool spans 73 MB, soup spans 237 MB. Expected now: about 160 MB + pool whole 512 MB (it
  fits the budget) + soup spans 237 MB = about 0.9 GB. The record line and a manifest note give the
  MB per slot (`by slot: ...`).

## Live writer (layer/replay.cpp, core/gi.cpp `replay_after_hit_gi`; ABI 25)

Key `replay_pack` (bg3rtx.ini, 0 off; not in the GUI presets): a positive serial takes one pack at the
next base-variant `gi_hit_direct` dispatch, once per serial (kept across core reloads); -1 one pack with
every trace (hotkey, `trace.request`, auto). Diagnostics only: the layer built with BG3RTX_DIAGNOSTICS=OFF
leaves `Bg3HostApi::replay_pack` null and the core does nothing. Capture only, no rendering change.
1. Core (hot-reloadable): right after the pass's `vkCmdDispatch` it calls the one new host call
   `int replay_pack(cmd, const Bg3ReplayDispatch*)` with the pushed constants, the push descriptor set 0
   as pushed, the output bindings (b8 giRaw, b13 giRawDepth), the material set 1 and the TLAS. A
   gi_hit_sun or SHaRC variant is refused (`[replay] pack N failed: ... variant`).
2. Layer (stable): it wraps a few `dd->vk` entries (image, view, sampler, buffer creation; the material
   sets' allocation and the layer's own writes and copies into them: tracker.cpp marks the game's
   vkUpdateDescriptorSets so those are skipped). From those records it plans and records, in the same
   command buffer, copies into host-visible staging chunks (256 MB each):
   - set 0: UBOs from the game's host mapping at record (or a transfer copy), SSBOs and images by GPU
     copies (depth as R32F), outputs after the dispatch as the live reference (`output ... s0b8.live`);
     rtStats (b20) is left out (the replay counts into its own buffer);
   - set 1: whole buffers (records, alpha records/soup, stamp, InstInfo, hit records, basis); the soup
     b6 and pool b8 whole when they fit the budget (pool first: its basis rows lie outside the
     InstInfo spans), else the InstInfo spans merged (4 KB gaps) as `range` slices of one file
     (`range S B OFFSET FILE FOFF BYTES`); every image element the set binds (gAlphaTex, gAlphaCache,
     gHitCache: what the alpha test and the hit material can index) with all mips of its view (both
     sample with a computed LOD, so any mip can be read), each VkImage once; the samplers (b4/b5
     immutable, b15 as written);
   - the TLAS instance array of the scene ring region whose InstInfo set 1 binds (rt_replay_region);
   - rt_cascade_casters: the cascade instance's triangles, from the converted cascade soup its BLAS was built
     from (bvhsplit.cpp records the instance, buffer, triangles, stride, draw list and soup frame per ring slot)
     while `geo_cascade_soup` of that frame still returns the same buffer and draw list, as `casters.bin`
     (shader copy). Log and note `[replay] pack N casters: instance <i>, <n> triangles of stride <s>, <MB> MB
     (soup of frame <f>)`, or `... left out: the cascade soup of frame <f> was replaced (<why>)`.
   The core passes the running variant's defines (core/gi.cpp `pipe_defines`); the pack keeps those of
   `rp::contract_defines` (RT_CASCADE_MASKS) and notes the rest.
   Buffers without TRANSFER_SRC are copied by `shaders/replay_copy.hlsl`. Images without TRANSFER_SRC
   (most game textures, the VT caches, the layer's dummies), or whose view swizzles, are read by
   `shaders/replay_image.hlsl` through a new SAMPLED-only 2D-array view of the game view's format and
   mapping, in their descriptor layout (no transition), and stored decoded (`rp::shader_read_format`):
   BC1/2/3/7 as RGBA8 (sRGB kept: the replay's sampler linearizes and filters as the game's), BC4/BC5
   as R8/RG8 (UNORM or SNORM), BC6H as RGBA16F, BGRA8 as RGBA8, B10G11R11 / E5B9G9R9 as RGBA16F; the
   manifest's `image` line names the game format. Why 8 bits: on the RTX 5090 BC1/2/3/7 decode exactly
   to 8-bit values and BC6H to halves (self-test 10), so those replays sample the same texels; BC4/BC5
   are within 0.5/255 (R16 would be exact-er at twice the size, which the 2 GB cap cannot afford).
   Images and copies larger than a staging chunk are split (`rp::shader_read_pieces`,
   `rp::transfer_pieces`: mips, layer groups, block rows). Still `omitted`: integer formats and 24-bit
   depth without TRANSFER_SRC, non-2D images, views outside a shader-readable layout, two views of one
   image, and the budget; the counts per reason and the omitted VkFormats are the manifest's first notes.
3. When the GPU has run that frame (life_gpu_done, no wait) a worker thread checks the staging marker,
   writes `captures/replay-<stamp>-<serial>/` and logs
   `[replay] pack <serial> written: <MB> MB (<n> images omitted, <r> read by shader), frame <f>, pass gi_hit_direct, instances <n> -> <dir>`
   (or `failed: <why>` / `over budget: <MB> MB without textures`). At record it logs
   `[replay] pack <serial> recorded at frame <f>: <n> image copies, <r> image reads (<b> BCn decoded, <MB> MB in <p> pieces), ...`
   with the buffer copy counts and the geometry mode.
Cost: one frame records the copies and reads (a few ms of CPU, GPU copies of up to 2 GB) and allocates
the staging; the file writing is off the render thread. Not measured live yet (`READY_FOR_LIVE`).
CTests: `replay_pack_test` (manifest writer round trip with the SOURCE field, InstInfo spans, the
budget fill with priorities and the S8 case with and without lighting inputs, raw-compatible formats,
shader-read format mapping and pieces, transfer pieces, the lighting contract and sources, probe
record elements, `lighting_inputs`); `frame_replay_test` writes its synthetic pack with the same writer,
replays it, and runs the image read on every mapped format.
Open: the exact VT cache sizes, formats, layers and mips at S8 (the next pack's `textures` line),
then the choice between a larger cap for this one job and a hit-sampled tile capture.


## Production Ultra local shadows

New `pass local_shadows` packs dispatch `gi/rt_local_shadows_box_k.hlsl` with the
actual captured `RT_SPEC_NATIVE`, `RT_SPEC_HAIR_ORIGIN`, `RT_SPEC_CASCADE` request
and all actual specialization values (`spec ID VALUE`). `--spec` overrides a captured
value; `--ab-spec ID=A,B` varies it in paired ABBA/BAAB dispatches. `--pipeline-stats`
works with this pass. Stock visible hair and non-OMM Ultra are the capture contract.
Old GI packs cannot be converted: they lack LS-time inputs and references.

The pack includes LS-time shadow records/atlas, projector records/2D cookies,
AO/sun mask/auxiliary state, material/class/frame data, joined light slots and the
same alpha geometry/TLAS inputs. Shadow traversal does not read hit-material caches
(set 1 b12-15); these large unused inputs are excluded. Sun set 2 holds b0-6;
box set 3 holds b16/b29/b45. Counter buffers remain device-local during LS replay.

`output S B FORMAT W H REFERENCE INITIAL` adds an exact initial storage image;
`-` means no live reference. The initial native R11G11B10 diffuse/specular and
RGBA16F auxiliary bytes are restored before **every** dispatch, outside timestamps.
`counter_initial FILE` restores the primary raw counter bins; `counters FILE`
provides the live post-dispatch reference. Extra counter buffers at set 4 are
restored from their captured buffer bytes and included in raw paired comparisons.
Every output is compared byte for byte in the paired run. Live packed colour is
compared as bytes; reconstructed AS traversal can differ from the game's AS.

`BG3RPKZ1` files use lossless, checksummed 4 MiB XPRESS_HUFF blocks. The reader
also accepts legacy raw files. Image uploads index compressed blocks and decode
one block at a time, retaining the existing bounded staging budget. The manifest's
sizes and sparse offsets always describe decoded bytes.

```powershell
bin/frame_replay --pack captures/replay-NEW --out .scratch/ls-ab `
  --ab-spec ID=0,1 --repeat 40 --ab-require-equal --pipeline-stats
```

`frame_replay_ls_test` (`--ls-self-test DIR`) writes a synthetic LS pack with a
shadowed slab, nonzero native packed colours, a populated shadow view/atlas and
projector cookie element 7. It requires colour modification, changed output for
black-cookie/dark-atlas controls, repeat identity, captured specs/counter round trip,
and exact paired restoration under Vulkan validation. `replay_pack_test` also checks
legacy manifest round trips and malformed new directives. `replay_codec_test` checks
lossless framing, random reads, corruption and legacy raw input without a GPU.

Raw compressed LS images (ls-pack2): diagnostic image creation adds TRANSFER_SRC
for sampled BC1-BC7 images, including BC4/BC5 UNORM and SNORM. Capture uses
`vkCmdCopyImageToBuffer` on the descriptor's base mip/layer and bound range;
rows are tightly packed block rows, with partial blocks only at mip edges.
The manifest keeps the view VkFormat and optional `SOURCE R G B A` component
mapping (SOURCE 0 for raw data). Replay recreates that format and view mapping,
and uploads the lossless raw blocks. LS rejects any decoded BCn fallback;
other passes retain the decoded compatibility path. A new diagnostic host build
must create the images before capture; no additional ABI change is needed.
The compressed LS disk cap remains 2048 MiB, including manifest and codec
headers. Each codec block is checked before writing, so even a failed pack
cannot overrun the cap. Synthetic tests cover raw BC4/BC5 atlas/cookie controls,
odd block extents, nonzero mip/layer bases, multiple mips/layers, codec byte
identity and sampled-value identity; no real inn size is inferred from fixtures.


## Default Ultra reflections replay

For qualification, add `--require-live-equal` to the ordinary pack replay before
profiling. It requires live references for every output and the raw counters,
and returns failure for any changed byte or missing reference. Ordinary diagnostic
replay still prints comparisons without enforcing this gate. Paired A/B equality
is a separate requirement and cannot replace agreement with the captured live frame.

Sampler lines with `STATE1` preserve LOD bias/minimum, comparison, border,
unnormalized-coordinate, flags and anisotropy-enable state in addition to the
legacy fields. Legacy sampler lines load with an incomplete-state warning;
their missing values cannot be recovered from the image bytes. The writer refuses
unsupported sampler/view extension state rather than silently dropping it.

The real inn pack `replay-20261106-084948-1` fails live specular agreement
(1,270,225 differing bytes); diffuse, hit distance and counters match exactly.
The module matches the runtime cache and repeat output is deterministic.
Sampler-state loss is a known contract gap, not an established explanation of
that difference. Do not use this pack to qualify an optimization yet.
Follow-up checked distinct raw pre/post specular snapshots, their restoration,
and the consecutive RF capture/dispatch/capture bracket. No other pass intervenes;
2,234,064 depth-valid no-hit pixels round-trip exactly. Prepared replacement
capture: `tools/live_batches/rf_pack_state1` (not staged or authorized), with a
required fixed-writer marker and separate statistics OFF/ON packs. See its README
for the strict post-capture commands and the remaining record-time UBO limitation.

`pass reflections` runs `gi/rt_reflections.hlsl`, with the captured request defines,
all specialization values and 256 push bytes. It supports ordinary replay,
`--ab-spec`, `--ab-warmup`, `--ab-require-equal` and `--pipeline-stats`.
The contract is default Ultra: `rf_hit_shade=0`, `sharc=0`, stock hair,
non-OMM. It excludes the unused hit-material caches and hit-lighting sets.

Arm `replay_pack_pass=1,replay_pack=N` (N positive) for one RF dispatch per
serial; selector 0 keeps legacy GI/LS behavior. RF capture uses ABI 27's existing
two-phase API: snapshot after LS and before RF, rebind the production pipeline
and descriptors, dispatch, then snapshot references. Both diagnostic DLLs must
be rebuilt; sampled compressed images need the existing raw-BCn host build.
No ABI bump or shader request was added.

Inputs include the G-buffer, velocity, previous radiance, geometric/physical
origin images, light lists/constants, shadow records/atlas, alpha geometry/TLAS,
AO, DFG, global specular cubes, local probe records and every bound cube element
(the mixed native b45's 2D cookies are not read by default RF). The game's
unrelated dynamic descriptors are ignored; a captured lighting buffer with a
dynamic descriptor is refused rather than copied from the wrong offset.

Initial native R11G11B10 diffuse/specular, R16 hit distance and primary raw counter
bins are restored before every dispatch, outside timestamps. Live output/counter
differences are reported separately from strict paired offline equality.
The shared LS lossless codec and 2048 MiB compressed disk cap apply; missing
required singleton lighting resources, omitted images and decoded BCn fail capture.
The cascade exclusion proof also applies to default RF: when both effective
pipelines trace mask 0x01, preserve the captured soup range instead of appending
unused cascade triangles across the driver's 2 GiB GetDimensions limit.

`frame_replay --rf-self-test DIR` (CTest `frame_replay_rf_test`) builds a tilted
slab reflecting the visible back plane, nonzero packed colours/history, a
specular local probe pair and raw BC1 cubes. It requires eligible screen hits,
zero range/ray guards, finite outputs, colour and hit-distance writes, changed
results for dark-history/black-probe controls, repeat identity and exact paired
output/counter restoration through compressed files under Vulkan validation.

Prepared capture only: `tools/live_batches/rf_pack/jobs/rf-pack-inn.json`.
The coordinator must capture the real pack before any savings investigation;
synthetic results do not establish real inn pack size, live-reference identity,
production GPU cost or an optimisation result.

## Numeric paired replay against a control

`--ab-numeric` writes `ab_numeric.csv`: one phase 0/1 cross-state comparison
per ABBA/BAAB quartet. The balanced order reverses subtraction in BAAB so
signed means always mean B minus A. The native R11G11B10/R16 values are decoded
before subtraction. Every output/channel reports differing pixel count, maximum
absolute difference, signed mean difference, mean absolute difference and
nonfinite count. Means include unchanged pixels. GPU timing still uses all four
dispatches, with restoration/readback/numeric analysis outside timestamps.

Run matching `--repeat 40 --ab-warmup 8 --spv MODULE --ab-numeric` experiments
with `--ab-spec 1031=0,0` (control) and `--ab-spec 1031=0,1` (candidate), without
`--ab-require-equal`. Then run:

```powershell
python tools/replay_numeric.py --control CONTROL_DIR --candidate CANDIDATE_DIR --out summary.json
```

The gate requires every candidate pair's differing-pixel count, maximum absolute
difference, absolute signed mean and mean absolute difference to fit the control's
empirical nearest-rank 99th percentile for each output/channel. With 40 pairs this
is the largest observed control value; there is no fitted tolerance. Nonfinite
values, missing/duplicate pairs or output/channel mismatch fail. The summary also
reports the existing distribution-free 95% median timing interval. This is a
noise-relative offline gate; live confirmation remains separate.
