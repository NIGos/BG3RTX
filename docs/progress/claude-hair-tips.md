## Dithered strand tips (branch claude/hair-tips, offline, 2026-11-04)
- Added rt_hair_groom_tips, live Developer switch, off in every preset; header word 15 bit 0.
- G-buffer and velocity evaluate the native mask alpha at the cached strand UV and share the
  native temporal dither. No card grazing factor; the pixel record remains unchanged.
- Dither twin: 1600 samples per alpha, shares 0/0.25/0.5/0.75/1, all exact (tolerance 0.05).
- Required build.ps1 -Test passed: 147/147 CTests (381.45 s), with worktree-local shader cache.
- Both requested shaders compile and pass spirv-val. Variant request delta: 0.
- Identity: 256/262 closures unchanged; explicit six-request HEAD comparison 3/6 identical.
  Combined: 259/262 requests byte-identical; only rt/hair_primary_* differ (including count).
- Initial CTest found stale settings schema (regenerated) and a read-only shared shader cache;
  rerun uses BG3RTX_SHADER_CACHE=build/shader_cache within this worktree.
- No full identity run, commit or game launch. Live quality and GPU ms remain unmeasured.
- Check Test Dialogue1 strand tips/temporal edges after hair reload safety is qualified.
