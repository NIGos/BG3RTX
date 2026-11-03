## rt_spec_cascade: cascade masks as specialization constant, step S1 (branch spec-cascade-s1, off, offline, 2026-11-02)
- Key `rt_spec_cascade` (bg3rtx.ini, off; core only, hot reload, no ABI change): the 36 cascade passes request
  `<file>?...;RT_SPEC_CASCADE=1` (rt_shadow also RT_SPEC_CASCADE_SUN=1) for both rt_cascade_casters values;
  core/gi.cpp refresh() sets specialization constant 1000 = rt_cascade_casters at pipeline creation (each value
  keeps its slot and pipeline; log "gi: <request> generation N spec cascade=0/1"). Toggling rt_cascade_casters then
  needs no DXC compile. Not covered: the current-caster sun pipe and the water passes (they keep their defines).
- Shaders: rt_common.hlsli masks / cascade candidate test and native_hit_sun.hlsli roof rule / sun-hit ray take
  RT_CASCADE_ON = (constant != 0) under RT_SPEC_CASCADE. Key off: all 2962 existing requests preprocess to main's
  text (pp-diff: 2860 compared, 0 differ), so their SPIR-V is byte-identical.
- common/shader_variant.h kSpecCascade + spec_cascade_value; variants.txt +1348 requests (4225); shader_variant_test
  covers them. Interim cost: the first merge-green compiles the 1348 new modules once.
- Gate `py -3 tools/spec_cascade_check.py [--all] [--replay <pack>]`: each spec module specialized to 0 / 1, frozen,
  folded and -O must be the same program as the old plain / RT_CASCADE_MASKS module (canonical compare; a control
  pair must differ; blocks compared in reference order, DXC laid out one rt_shadow if/else the other way round).
  Result: 60/60 (each of the 36 files once + 24 random, seed 1) and 76/76 (+ 40 random, seed 2): value 0 = the plain program, value 1 = the
  RT_CASCADE_MASKS one; control differs. S8 replay (gi_hit_direct): giRaw, depth, rtStats bit-identical for both.
- Live job (READY_FOR_LIVE, not authorized): tools/live_batches/spec_cascade (scc1, S8, one launch, 17 phases, ~5.7 min,
  370 MB; dryrun passes). Gates: no gi/ DXC compile when rt_cascade_casters toggles with the key on, pipeline
  creation logged ("gi: ... pipeline N ms", new in refresh), energy and gi/rf/ls/rt GPU ms of key-on vs off on both
  sides for casters on and off; predicted.json: every delta 0 within the off-a/off-b spread.
- Follow-up branch spec-cascade-s1-default (apply only after scc1 qualifies): the spec build is the only cascade
  path (key rt_spec_cascade removed, refresh always specializes); the old cascade requests and their plain twins
  leave variants.txt: 4249 -> 1589 variants (2986 -> 1674 requests with the 85 base files; the 28 RT_CASCADE_MASKS
  left are the current-caster sun and the water passes). 100 CTests pass; spec_cascade_check.py compiles the old
  reference requests on demand.
- Then the migration
  plan (docs/BUILD_AND_PACKAGE_EFFICIENCY.md "Migration plan"): G (gi-reproj guards as constants 1001/1002),
  S2-S5 to ~100-180 requests, one merge-green at the end. Constant-id registry SpecId in common/shader_variant.h;
  kSpecCascade moved to bit 16 (gi-reproj-guards uses 4096/8192).
- Branch variant-migration (from spec-cascade-s1-default): step G, gi_temporal_mv_sample / gi_prev_radiance_guard
  as constants 1001 / 1002 (GI_SPEC_GUARDS on their 18 files; 1591 variants instead of +2192 as defines); check
  --key guards 34/34, S8 replay bit-identical for both values. Step S2a: RT_HIT_WIND_RGB / RT_HIT_IP_GRASS as
  constants 1004 / 1005 (RT_SPEC_HIT_FAMILY on the instanced requests): 1591 -> 895 variants.
