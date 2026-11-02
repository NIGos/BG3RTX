## S2b far-only guard fix (branch variant-migration, sampled offline checks passed, 2026-11-02)
- Guard the far radius/base radius, per-ray weights and local classification with constant 1007; far-on math unchanged.
- Before: gifar 0/20 (value 0 body differences). After: gifar 20/20, hitinst 31/31, cascade 52/52 with --sample 16;
  controls differ by 58,222 / 42,254 / 1 lines respectively. Checker unchanged. Intermediate fix reversed far-on
  radius loads (4 lines per module); moving the non-far initialization into else restored exact equivalence.
- --pp-diff main: 274 requests, 86 identical inputs + 188 pp-same; 0 compiles needed with preprocessed keys.
- No build needed (shader-only), no game launched, no commit. Variants request delta for this fix: 0.

## Variant migration G, S2a-S2c, S3a host (branch variant-migration, verification pending, 2026-11-02)
- Verified (sampled, before PAUSE): G 87df2ce (guards as constants 1001/1002: --key guards 34/34, S8 replay
  bit-identical), S2a a332afc (wind RGB / IP grass 1004/1005: --key hitfam 27/27). 1591 -> 895 variants.
- UNVERIFIED (written with PAUSE set, nothing compiled, built or run): S2b ed115dd (RT_GI_FAR -> 1007),
  S2c f9b8c58 (RT_HIT_INSTANCED -> 1003, with 1004/1005 inside), S3a cb79a4a (layer: set 1 b19 tileset samplers
  always bound, g_wrap when off). variants.txt is stale after S2b/S2c until regenerated.
- S3 hair-origin host and shader work is implemented below. The separate occ statistics set is excluded.

### S3 implementation (uncommitted; final offline checks running)
- Constants 1008 tileset, 1009 physical hair origins, 1010 scalp/hair replace the request cross product.
- Hair origin b50 is always bound in non-mask-1 layouts; startup-off uses a 1x1 RGBA32F dummy.
  Reflection b12 is always omitted to retain the 32-descriptor budget. GI b50 is always enabled.
- RT_OCC_STATS is excluded (separate branch). No ABI change, game launch, commit or Git metadata write.
- Regenerated variants: HEAD 632 -> 170 requests, delta -462; existing water routing retained.
- Build: 101/101 CTests passed. pp-diff: 140 requests, 37 identical inputs + 103 pp-same, 0 compiles (main 23c6057).
- Shader fixes: gate the query-direction getter, initialize native alpha coarser explicitly, avoid redundant
  private camera/direction state merges and redundant physical-origin guards in the LS reference.
- Focused GI 6/6, direct reference 4/4 and LS check 6/6 exact. Full sampled checks remain pending.
- Canonicalization only permutes parallel phi declarations and intact value/predecessor pairs; block order
  ignores phi input references. Every instruction, type, operand and edge remains compared.
- Five CPU tests cover allowed permutations, changed values/edges/types/IDs/branches and process workers.
  Equivalence workers use processes to avoid the Python GIL; DXC still uses the shared machine-wide slots.
- Final commands: native --sample 16; cascade/guards/hitfam/gifar/hitinst --sample 8; compiled descriptor checks.
  Every build/shader command waits for no bg3.exe, polling every 60 s.
- Integration later: S4/S5, final --all and merge-green, then explicitly authorized scc1. Not a live quality pass.
