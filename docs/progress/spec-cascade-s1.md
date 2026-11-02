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
  Result: 60/60 (each of the 36 files once + 24 random, seed 1): value 0 = the plain program, value 1 = the
  RT_CASCADE_MASKS one; control differs. S8 replay (gi_hit_direct): giRaw, depth, rtStats bit-identical for both.
- Next (S1 done): once qualified, make the spec build the only cascade path and drop the 1348 old cascade requests
  plus their 1348 plain twins: 4225 -> ~1530 variants (2962 today -> ~1620 modules).
