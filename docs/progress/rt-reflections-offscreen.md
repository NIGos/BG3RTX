## Water world-query candidate (branch rt-reflections-offscreen, READY_FOR_LIVE (bounded provider), 2026-11-03)
- Git metadata read only; no commit, game launch or real job queued. ABI unchanged.
- rt_water_hit_shade=2 extends SpecId 1016; Experimental GUI choice; presets still 0.
- Off-grid hits use a once-per-frame GPU point/spot membership/type union, then
  world bounding-sphere/channel culling and stable nearest selection, K=32.
- K bounds local visibility rays to one subgroup's lane count (1024 / 32 = 32x below
  the existing froxel cap). More than K retains probe, never a partial energy sum.
- Truncation: reflection word 31 / compose word 55, wave/group-reduced device-local
  counters, existing delayed readback, both logs report world truncated.
- Source correction: raw b25 is not a full active frame list. ASSET_DB.md measured
  1,182 valid-looking vs 39 listed slots; unlisted slots hold other frames/places.
  Use every current froxel's active point/spot union at arbitrary world positions.
  Never-listed lights, world boxes and local probes remain provider gaps.
- Sun/roof, native light BRDF/visibility/policy, sky cube direction/mip/blend/scale
  and AO reused. Receiver C + A L - B is unchanged; unknowns retain native probe.
- Existing same-frame/TLAS and forward/opaque buffer equality guards stay in force.
- Shared CPU/HLSL sphere and nearest selector: boundaries, invalid/channel inputs,
  shuffled/tied populations through 4096, empty sets and truncation tested.
- shader_variant_test --write: 156 -> 157 requests (+1 preparation, no cross product).
  Fixed its plain-request parser; refreshed GUI schema for the new mode.
- pp-diff HEAD: 244 common requests; 210 input-identical + 25 pp-identical,
  only 9 hit-layout requests changed. Two-shader probe compiled/spirv-val passed.
- Final build succeeded; all 126 CTests passed (new CPU selector included).
  Initial generator/schema failures corrected; host_atomics_lint passed.
- waterhit: 9/9 layouts, 96/96 comparisons (modes 0/1/2 x both compose shifts);
  key-off matches HEAD in all 9; control differs. All 10 requests validate.
  Added shared-helper progress reporting to the checker's baseline comparisons.
- water_offscreen draft toggles 0/2/0/2; synthetic dryrun passed all checks.
  Every ON requires offscreen AND hidden shaded > 0 in both consumer stats.
- predicted.json: static float mean unchanged within OFF noise/encoding band;
  added reflection + compose median/p90 GPU budget 1.0 ms. Cost unmeasured.
- After separate live authorization: S8 Water Queens House, Ultra, static repeats
  then matched turns in one visible_unfocused_fg launch; inspect pool reflections.
- No quality pass, RF bias relaxation or preset promotion. Full active-list coverage
  needs a verified provider, not stale slots or story-unsafe database lights.
