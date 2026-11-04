## Strand indirect volume scattering (branch claude/hair-fibre-gi, offline candidate, 2026-11-04)

- Added live Developer key rt_hair_fibre_gi, off in every preset. No ABI change. No game or commits.
- GI composite replaces its Lambert response only on current sealed class 5 strand pixels at matching depth.
  It already has the gathered irradiance; changing the earlier D-LS fibre pass would need another GI binding.
- A = S + k_volume M: integrate dual forward/backward scattering with a_f, a_b, d_f=d_b=0.7 and existing k,
  then calibrate the multiple term against diffuse-incidence BSDF slabs. The directional k alone is insufficient.
  White endpoint is 1 by conservation. Colour below 0.02 and beta outside 0.35-0.7 remain clamped.
- Independent coloured slab max channel errors: CHAR_Hair 1.3882%; mid colour 0.7563%; light colour 0.8466%.
  White furnace 1.0; independent finite 20,000-bounce white walk 0.9842. GPU/CPU max relative error 1.46e-7.
- Key-off identity: 7/7 affected ordinary requests byte-identical to 97e2487b4a26. Explicit compile/spirv-val 9/9.
  Two extra composite requests only, RT_HAIR_FIBRE_GI_REFERENCE=1 (extra buffer-address capabilities).
- Build succeeds; final full CTest: 145/146, including both hair CPU and GPU tests. Defaults/schema regenerated.
  The inherited water_surface_shader_test fails 13/16 OFF comparisons against merge-base 009f60f; its inputs
  are unchanged from starting HEAD 97e2487. The same unmodified gate with --baseline HEAD passes 16/16 OFF,
  2/2 raw ordinary and 16/16 production validation. Full-suite acceptance is not claimed; water is untouched.
- One earlier suite was invalidated by the final current-frame header check changing sources during a frozen
  water gate; the final run used frozen sources. Shader cache is local to this worktree; shared artifacts read only.
- Live and cost unmeasured, per task's no-game instruction. Later check: Test Dialogue1, indirect-lit strands,
  same-run rt_hair_fibre_gi A/B with primary strands enabled; inspect hue/energy against the slab expectation.
- Full derivation, paper source and calibrated domain: RTX_HAIR_PRIMARY.md section 3, M4.1.
