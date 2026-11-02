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
- Not written yet (design): S3 hair origin host (core/gi.cpp make_layout:604 and the push-descriptor writes :1172
  use g_hair_origin_cap; bg3::hair_origin::binding_enabled and native_hit_gi::enabled(cap) also drop other bindings;
  always-bound means b50 always in the non-mask-1 layouts with a 1x1 rgba32f dummy storage image, the native_hit_gi
  slots enabled regardless of cap, and native_hit_gi_bindings_test updated), the occ stats set (branch
  worktree-agent-a0c9be1ab336eae1a, not merged here), and the S3 shader side (RT_NATIVE_TILESET, RT_HAIR_PHYSICAL_ORIGIN,
  RT_NATIVE_SCALP/HAIR as constants 1008-1010; RayQuery<RT_SKIP_PROCEDURAL> template flag -> TraceRayInline flags).

### Verification, once the PC is free (in this order, from the worktree; stop at the first failure)
1. Build, regenerate the request list, unit tests:
   `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1`
   `.\bin\shader_variant_test.exe (Get-Location).Path --write` (expect about 1590 -> fewer variants after S2b/S2c;
   commit the new shaders/variants.txt as "variant-migration: variants.txt after S2b/S2c")
   `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Test` (100 CTests; variant_matrix_test may
   need the gi_far / instanced expectations updated: the request strings no longer carry RT_GI_FAR / RT_HIT_INSTANCED).
2. Default path unchanged for every request that keeps its spelling: `py -3 tools/shader_identity.py --pp-diff main`
   (only requests of the converted files may differ; their equivalence is step 3).
3. Equivalence per step (sampled; each prints "N/N ... the same program"; a control line must be > 0):
   `py -3 tools/spec_cascade_check.py --key gifar --sample 16 --replay <S8 pack>`  (S2b; the S8 pack's pass is the
   non-far gi_hit_direct, so the replay only checks value 0 there; the static check covers value 1)
   `py -3 tools/spec_cascade_check.py --key hitinst --sample 16`  (S2c: five combinations per request)
   `py -3 tools/spec_cascade_check.py --key guards --sample 16 --replay <S8 pack>` and `--key cascade --sample 16`
   again (S2b/S2c touched the files they cover).
   <S8 pack> = `C:\Users\quali\Documents\Code Projects\BG3RTX\.claude\worktrees\test-main\captures\replay-20261101-142819-1`.
   If S2b reports DIFFERENT only in globals: an unused Workgroup variable (gs_far_stat) left with the constant 0;
   compare with `spirv-opt --eliminate-dead-variables` (if the SDK has it) or accept after reading the diff: it is a
   declaration, the body must have 0 differing lines.
4. Descriptor interfaces (S3a changes set 1): `py -3 tools/compile_shaders.py --pp-verify 4` with the bin/ checkers
   built (native_hit_gi_bindings_test, native_hit_rf_bindings_test); a game restart is needed for the layer change.
5. Before the single merge-green: `py -3 tools/spec_cascade_check.py --key <each> --all` on the final set (compiles
   every new module once, which merge-green then takes from the cache), then the scc1 live job extended per key.
